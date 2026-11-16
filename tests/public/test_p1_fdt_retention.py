# SPDX-License-Identifier: Apache-2.0
"""P0 hardening of the retained run-time FDT: provider bind (ABI 2), the
boot bridge's retained copy and seed transport, and their composition with
the memory-admission gate. HOST_ONLY: self-authored trees, a modelled page
table, no device. Optional sanitizers: PPP_TEST_HOST_CC=gcc
PPP_TEST_CFLAGS="-fsanitize=address,undefined" and LD_PRELOAD of libasan."""
import ctypes
import hashlib
import json
import mmap
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PLATFORM = ROOT / "tools/p1-penang-platform"
ADMISSION = ROOT / "tools/m12-penang-physical"
BRIDGE = ROOT / "tools/m12-boot-bridge"
PAGE = 0x4000
VA_KERNEL_MIN = 0xFFFF800000000000


def u32(x): return struct.pack(">I", x & 0xffffffff)
def u64(x): return struct.pack(">Q", x)
def pad(b): return b + bytes((-len(b)) % 4)


class Tree:
    """FDT v17 writer that can also emit deliberately malformed trees."""
    def __init__(self):
        self.strings = bytearray()
        self.offsets = {}

    def name(self, n):
        if n not in self.offsets:
            self.offsets[n] = len(self.strings)
            self.strings += n.encode() + b"\0"
        return self.offsets[n]

    def prop(self, n, data=b""):
        return u32(3) + u32(len(data)) + u32(self.name(n)) + pad(data)

    @staticmethod
    def node(n, body): return u32(1) + pad(n.encode() + b"\0") + body + u32(2)

    def blob(self, root, rsv=(), tail=u32(9), last_comp=16, structure=None):
        s = structure if structure is not None else self.node("", root) + tail
        rmap = b"".join(u64(a) + u64(b) for a, b in rsv) + bytes(16)
        a = 40 + len(rmap)
        b = a + len(s)
        return struct.pack(">10I", 0xd00dfeed, b + len(self.strings), a, b, 40, 17, last_comp, 0,
                           len(self.strings), len(s)) + rmap + s + bytes(self.strings)


def penang_tree(ram=(0xa0000000, 0x20000000), seed=bytes(range(1, 65)), seeds=1, dynamic=False,
                profile="qcom,holi", gic=(0x0f200000, 0x10000, 0x0f240000, 0x100000), extra_root=b"",
                kaslr=True):
    t = Tree()
    p, n = t.prop, t.node
    chosen = b"".join(p("rng-seed", seed) for _ in range(seeds)) if seed is not None else b""
    if kaslr:
        chosen += p("kaslr-seed", u64(0x1122334455667788))
    chosen += p("bootargs", b"console=null\0")
    cpus = p("#address-cells", u32(2)) + p("#size-cells", u32(0)) + \
        n("cpu@0", p("device_type", b"cpu\0") + p("reg", u64(0)))
    res = p("#address-cells", u32(2)) + p("#size-cells", u32(2)) + p("ranges")
    res += n("fw@%x" % ram[0], p("reg", u64(ram[0] + ram[1] - 0x100000) + u64(0x100000)) + p("no-map"))
    if dynamic:
        res += n("pool", p("size", u64(0x400000)) + p("reusable"))
    root = p("#address-cells", u32(2)) + p("#size-cells", u32(2)) + p("compatible", profile.encode() + b"\0")
    root += n("chosen", chosen) + n("cpus", cpus)
    root += n("memory@%x" % ram[0], p("device_type", b"memory\0") + p("reg", u64(ram[0]) + u64(ram[1])))
    root += n("reserved-memory", res)
    root += n("intc", p("compatible", b"arm,gic-v3\0") + p("#interrupt-cells", u32(3)) +
              p("reg", b"".join(u64(x) for x in gic)))
    root += n("timer", p("compatible", b"arm,armv8-timer\0") +
              p("interrupts", b"".join(u32(1) + u32(i) + u32(8) for i in (1, 2, 3, 0))))
    root += n("psci", p("compatible", b"arm,psci-1.0\0") + p("method", b"smc\0"))
    root += extra_root
    return t.blob(root)


def qemu_tree(**kw):
    kw.setdefault("ram", (0x40000000, 0x40000000))
    kw.setdefault("gic", (0x08000000, 0x10000, 0x080a0000, 0xf60000))
    return penang_tree(profile="linux,dummy-virt", **kw)


class RuntimeFdt(ctypes.Structure):
    _fields_ = [("bytes", ctypes.c_void_p), ("length", ctypes.c_size_t), ("physical_address", ctypes.c_uint64)]


class Handoff(ctypes.Structure):
    _fields_ = [("abi_version", ctypes.c_uint32), ("reserved0", ctypes.c_uint32)] + \
        [(n, ctypes.c_uint64) for n in ("fdt_pa", "fdt_length", "phys_base", "virt_base", "mem_size",
                                        "kernel_end_pa", "top_of_kernel_data", "adt_pa", "adt_length",
                                        "boot_args_pa")]


TRANSLATE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64))


class Mapping(ctypes.Structure):
    _fields_ = [("ctx", ctypes.c_void_p), ("translate", TRANSLATE), ("va_min", ctypes.c_uint64),
                ("va_max", ctypes.c_uint64)]


def cc():
    return shutil.which("clang-18") or shutil.which("clang")


class Base(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not cc():
            raise unittest.SkipTest("Clang required")
        cls.folder = tempfile.TemporaryDirectory(prefix="p1-fdt-retention-")
        cls.dir = Path(cls.folder.name)
        host = os.environ.get("PPP_TEST_HOST_CC") or cc()
        flags = os.environ.get("PPP_TEST_CFLAGS", "").split()
        cls.lib_path = cls.dir / "provider.so"
        subprocess.run([host, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared", *flags,
                        "-I", str(ADMISSION), "-I", str(PLATFORM), str(PLATFORM / "ppp_runtime_fdt.c"),
                        str(PLATFORM / "ppp_resources.c"), str(ADMISSION / "p1_memory_admission.c"),
                        "-o", str(cls.lib_path)], check=True, capture_output=True)
        cls.bridge_path = cls.dir / "bridge.so"
        subprocess.run([host, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared", *flags,
                        "-DM12_HOST_SYNTHETIC_ENTROPY", "-I", str(BRIDGE), str(BRIDGE / "bridge_core.c"),
                        str(BRIDGE / "m12_retained_fdt.c"), "-o", str(cls.bridge_path)],
                       check=True, capture_output=True)
        cls.bridge = ctypes.CDLL(str(cls.bridge_path))
        cls.bridge.m12_sha512.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
        cls.bridge.m12_condition_seed.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_char_p]
        cls.bridge.m12_retain_fdt.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32,
                                              ctypes.POINTER(ctypes.c_uint32)]

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()

    def provider(self):
        """Fresh copy: the descriptor is single-assignment per process image."""
        self.copies = getattr(self, "copies", 0) + 1
        path = self.dir / ("%s-%d.so" % (self.id().rsplit(".", 1)[-1], self.copies))
        shutil.copyfile(self.lib_path, path)
        lib = ctypes.CDLL(str(path))
        lib.ppp_runtime_fdt_bind.argtypes = [ctypes.POINTER(Handoff), ctypes.POINTER(Mapping)]
        lib.ppp_fdt_check_structure.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        lib.ppp_fdt_status_name.restype = ctypes.c_char_p
        lib.p1_memory_admit.argtypes = [ctypes.POINTER(RuntimeFdt)] + [ctypes.c_uint64] * 3
        lib.ppp_resolve.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
        return lib, RuntimeFdt.in_dll(lib, "penang_p1_runtime_fdt")


class Window:
    """A kernel window whose only backed pages are the retained-FDT slot.

    phys_base 0xa0000000, 256 MiB. The slot is a real 16-KiB-aligned host
    buffer; virt_base is chosen so that the slot's VA is that buffer. The
    page-table model vouches only for pages listed in `mapped`."""
    PHYS = 0xa0000000
    SIZE = 0x10000000
    KERNEL_END = PHYS + 0x03100000
    ADT = KERNEL_END
    ARGS = ADT + 0x10000
    SLOT = ARGS + PAGE

    def __init__(self, blob, slot_offset=0, slot=None, top=None):
        self.slot = self.SLOT if slot is None else slot
        self.length = len(blob)
        span = (slot_offset + len(blob) + PAGE - 1) // PAGE * PAGE + PAGE
        self.map = mmap.mmap(-1, span + PAGE)
        base = ctypes.addressof(ctypes.c_char.from_buffer(self.map))
        self.host = (base + PAGE - 1) // PAGE * PAGE
        ctypes.memmove(self.host + slot_offset, blob, len(blob))
        self.fdt_pa = self.slot + slot_offset
        self.virt = (self.host - (self.slot - self.PHYS)) % 2**64
        self.top = top if top is not None else self.slot + (slot_offset + len(blob) + PAGE - 1) // PAGE * PAGE
        self.mapped = {(self.host + i) // 0x1000 for i in range(0, span, 0x1000)}
        self.alias = {}
        self.calls = []
        self.hook = TRANSLATE(self.translate)

    def translate(self, _ctx, va, out):
        self.calls.append(va)
        page = va // 0x1000
        if page not in self.mapped:
            return 1
        out[0] = self.alias.get(page, self.PHYS + ((va - self.virt) % 2**64))
        return 0

    def handoff(self, **kw):
        h = Handoff(2, 0, self.fdt_pa, self.length, self.PHYS, self.virt, self.SIZE, self.KERNEL_END,
                    self.top, self.ADT, 0x10000, self.ARGS)
        for k, v in kw.items():
            setattr(h, k, v % 2**64 if isinstance(v, int) else v)
        return h

    def mapping(self, va_min=None, va_max=None):
        lo = self.virt if va_min is None else va_min
        return Mapping(None, self.hook, lo, (self.virt + self.SIZE) if va_max is None else va_max)

    def snapshot(self): return bytes(self.map)


class ProviderBind(Base):
    def bind(self, lib, w, h=None, m=None):
        return lib.ppp_fdt_status_name(lib.ppp_runtime_fdt_bind(
            ctypes.byref(h or w.handoff()), ctypes.byref(m or w.mapping()))).decode()

    def assert_untouched(self, desc):
        self.assertEqual(bytes(desc), bytes(ctypes.sizeof(desc)), "a refusal must not modify the descriptor")

    def test_valid_tree_binds_once_and_feeds_admission_and_resolution(self):
        lib, desc = self.provider()
        blob = penang_tree()
        w = Window(blob, slot_offset=8)
        before = w.snapshot()
        self.assertEqual(self.bind(lib, w), "OK")
        self.assertEqual((desc.bytes, desc.length, desc.physical_address), (w.host + 8, len(blob), w.fdt_pa))
        self.assertEqual(w.snapshot(), before, "the retained tree is never written")
        # Mapping proven for every 4-KiB page of the tree, including the last byte.
        self.assertIn(w.host + 8, w.calls)
        self.assertIn(w.host + 8 + len(blob) - 1, w.calls)
        self.assertEqual(lib.p1_memory_admit(ctypes.byref(desc), w.PHYS, w.SIZE, w.top), 0)
        res = ctypes.create_string_buffer(256)
        self.assertEqual(lib.ppp_resolve(desc.bytes, desc.length, res), 0)
        self.assertEqual(self.bind(lib, Window(penang_tree())), "ALREADY_BOUND")
        self.assertEqual((desc.bytes, desc.length), (w.host + 8, len(blob)))

    def test_no_byte_is_read_before_the_mapping_is_proven(self):
        # The tree's VA is unbacked host memory (the null page region): any
        # read would crash the test process. The page-table model refuses.
        blob = penang_tree(extra_root=Tree.node("filler", Tree().prop("data", bytes(3 * PAGE))))
        w = Window(blob)
        self.assertGreater(len(w.mapped), 8)
        for page in sorted(w.mapped)[5:6]:
            w.mapped.discard(page)  # one hole in the middle of a multi-page tree
        lib, desc = self.provider()
        self.assertEqual(self.bind(lib, w), "UNMAPPED")
        self.assert_untouched(desc)
        lib, desc = self.provider()
        w = Window(blob)
        null_virt = 0x4000 - (w.slot - w.PHYS)
        h = w.handoff(virt_base=null_virt)
        self.assertEqual(self.bind(lib, w, h=h, m=w.mapping(null_virt % 2**64, (null_virt + w.SIZE) % 2**64)),
                         "WINDOW")  # virt window would wrap below zero
        h = w.handoff(virt_base=0x10000000)
        self.assertEqual(self.bind(lib, w, h=h, m=w.mapping(0x10000000, 0x20000000)), "UNMAPPED")
        self.assert_untouched(desc)
        # A page mapped at the wrong physical address (an alias) is refused.
        w = Window(blob)
        w.alias[sorted(w.mapped)[0]] = 0xdead0000
        self.assertEqual(self.bind(lib, w), "UNMAPPED")
        self.assert_untouched(desc)
        self.assertEqual(self.bind(lib, Window(blob)), "OK", "refusals never consumed the assignment")

    def test_virtual_window_contract(self):
        blob = penang_tree()
        w = Window(blob)
        lib, desc = self.provider()
        cases = {
            "WINDOW": [dict(virt_base=0), dict(virt_base=w.virt + 0x1000), dict(phys_base=w.PHYS + 0x1000),
                       dict(mem_size=0), dict(mem_size=0x1234), dict(virt_base=2**64 - PAGE),
                       dict(kernel_end_pa=w.PHYS), dict(kernel_end_pa=w.top),
                       dict(top_of_kernel_data=w.PHYS + w.SIZE + PAGE)],
            "ARGS": [dict(abi_version=1), dict(reserved0=1), dict(boot_args_pa=0)],
        }
        for expected, variants in cases.items():
            for kw in variants:
                with self.subTest(kw=kw):
                    self.assertEqual(self.bind(lib, w, h=w.handoff(**kw)), expected)
                    self.assert_untouched(desc)
        # The adapter's vouched VA range must contain the whole kernel window.
        for lo, hi in ((w.virt + PAGE, None), (None, w.virt + w.SIZE - PAGE), (w.virt + 1, w.virt)):
            self.assertEqual(self.bind(lib, w, m=w.mapping(lo, hi)), "WINDOW")
        self.assertEqual(lib.ppp_runtime_fdt_bind(None, ctypes.byref(w.mapping())), 1)
        self.assertEqual(lib.ppp_runtime_fdt_bind(ctypes.byref(w.handoff()), None), 1)
        self.assertEqual(lib.ppp_runtime_fdt_bind(ctypes.byref(w.handoff()),
                                                  ctypes.byref(Mapping(None, TRANSLATE(0), 0, 1))), 1)
        self.assert_untouched(desc)
        self.assertEqual(self.bind(lib, w), "OK")

    def test_placement_must_survive_vm_bring_up(self):
        blob = penang_tree()
        lib, desc = self.provider()
        # In the prefix [phys_base, segLOWEST): freed by arm_vm_prot_finalize.
        w = Window(blob, slot=Window.PHYS + 0x2000000, top=Window.SLOT + 0x100000)
        self.assertEqual(self.bind(lib, w), "NOT_RETAINED")
        # Straddling topOfKernelData, or above it (general allocator memory).
        w = Window(blob)
        self.assertEqual(self.bind(lib, w, h=w.handoff(top_of_kernel_data=w.top - PAGE)), "NOT_RETAINED")
        w = Window(blob, top=Window.SLOT)
        self.assertEqual(self.bind(lib, w, h=w.handoff(top_of_kernel_data=Window.ARGS)), "NOT_RETAINED")
        # Sharing pages with the Apple device tree or boot_args.
        w = Window(blob)
        self.assertEqual(self.bind(lib, w, h=w.handoff(adt_length=0x20000)), "OVERLAP")
        self.assertEqual(self.bind(lib, w, h=w.handoff(boot_args_pa=w.fdt_pa + 64)), "OVERLAP")
        self.assertEqual(self.bind(lib, w, h=w.handoff(adt_pa=2**64 - 8, adt_length=16)), "ARGS")
        # Alignment and length.
        w = Window(blob, slot_offset=4)
        self.assertEqual(self.bind(lib, w), "ALIGNMENT")
        w = Window(blob)
        for kw, expected in ((dict(fdt_length=39), "NOT_RETAINED"), (dict(fdt_length=len(blob) - 8), "LENGTH"),
                             (dict(fdt_length=len(blob) + 8), "LENGTH"),
                             (dict(fdt_length=w.top - w.fdt_pa + 8), "NOT_RETAINED"),
                             (dict(fdt_length=3 * 1024 * 1024), "NOT_RETAINED")):
            self.assertEqual(self.bind(lib, w, h=w.handoff(**kw)), expected, kw)
        self.assert_untouched(desc)
        self.assertEqual(self.bind(lib, Window(blob)), "OK")

    def test_whole_structure_is_validated_before_publication(self):
        t = Tree()
        p, n = t.prop, t.node
        good_root = p("#address-cells", u32(2)) + p("#size-cells", u32(2)) + n("chosen", p("a", b"x"))
        variants = {
            "property after child": p("#address-cells", u32(2)) + n("chosen", b"") + p("late", b"x"),
            "duplicate property": p("a", b"1") + p("a", b"2"),
            "duplicate root child": n("chosen", b"") + n("chosen", b""),
            "empty child name": n("", b""),
            "slash in node name": n("a/b", b""),
            "control byte in property name": p("bad\x01", b""),
            "empty property name": p("", b""),
        }
        blobs = {k: Tree.blob(t, v) for k, v in variants.items()}
        root = Tree.node("", good_root)
        blobs["two roots"] = t.blob(None, structure=root + root + u32(9))
        blobs["data after FDT_END"] = t.blob(None, structure=root + u32(9) + u32(4))
        blobs["missing FDT_END"] = t.blob(None, structure=root + u32(4))
        blobs["unbalanced"] = t.blob(None, structure=root[:-4] + u32(9))
        blobs["unknown token"] = t.blob(None, structure=root[:-4] + u32(7) + u32(2) + u32(9))
        deep = b""
        for _ in range(33):
            deep = Tree.node("n", deep)
        blobs["depth 34"] = t.blob(None, structure=Tree.node("", deep) + u32(9))
        blobs["reservation wraps"] = t.blob(good_root, rsv=[(2**64 - 0x1000, 0x2000)])
        blobs["zero-size reservation"] = t.blob(good_root, rsv=[(0x80000000, 0)])
        ok = t.blob(good_root)
        def patch(blob, offset, value):
            d = bytearray(blob)
            struct.pack_into(">I", d, offset, value)
            return bytes(d)
        o_struct, o_strings = struct.unpack_from(">II", ok, 8)
        blobs["strings overlap structure"] = patch(ok, 12, o_struct + 4)
        blobs["reservation map overlaps structure"] = patch(ok, 16, o_struct - 8)
        blobs["reservation map unterminated"] = patch(ok, 16, len(ok) - 8)
        blobs["structure inside header"] = patch(ok, 8, 32)
        blobs["name offset past strings"] = patch(ok, o_struct + 8 + 8, 0xffff)
        blobs["version 16"] = patch(ok, 20, 16)
        blobs["last compatible 15"] = patch(ok, 24, 15)
        lib, desc = self.provider()
        self.assertEqual(lib.ppp_fdt_check_structure(ok, len(ok)), 0)
        for label, blob in blobs.items():
            with self.subTest(label=label):
                self.assertNotEqual(lib.ppp_fdt_check_structure(blob, len(blob)), 0)
                self.assertIn(self.bind(lib, Window(blob)), ("STRUCTURE", "HEADER", "LENGTH"))
                self.assert_untouched(desc)
        self.assertEqual(self.bind(lib, Window(ok)), "OK")

    def test_reentrant_bind_from_the_translation_hook_is_refused(self):
        lib, desc = self.provider()
        blob = penang_tree()
        w = Window(blob)
        inner = Window(blob)
        seen = []
        def nested(ctx, va, out):
            if not seen:
                seen.append(lib.ppp_runtime_fdt_bind(ctypes.byref(inner.handoff()), ctypes.byref(inner.mapping())))
            return w.translate(ctx, va, out)
        hook = TRANSLATE(nested)
        m = Mapping(None, hook, w.virt, w.virt + w.SIZE)
        self.assertEqual(self.bind(lib, w, m=m), "OK")
        self.assertEqual(lib.ppp_fdt_status_name(seen[0]), b"BUSY")
        self.assertEqual(desc.bytes, w.host)

    def test_storage_written_outside_bind_is_never_overwritten(self):
        lib, desc = self.provider()
        desc.length = 4
        self.assertEqual(self.bind(lib, Window(penang_tree())), "ALREADY_BOUND")
        self.assertEqual((desc.bytes, desc.length, desc.physical_address), (None, 4, 0))

    def test_mutation_sweep_never_publishes_a_bad_tree(self):
        """Random corruption of a valid tree. Every accepted variant must also
        be digestible by the resolver and the admission gate without fault."""
        rng = random.Random(20261116)
        base = penang_tree()
        lib, desc = self.provider()
        accepted = rejected = 0
        res = ctypes.create_string_buffer(256)
        for _ in range(3000):
            d = bytearray(base)
            for _ in range(rng.randint(1, 4)):
                at = rng.randrange(len(d))
                d[at] = rng.choice((0, 0xff, rng.randrange(256), d[at] ^ (1 << rng.randrange(8))))
            blob = bytes(d)
            if lib.ppp_fdt_check_structure(blob, len(blob)) == 0:
                accepted += 1
                lib.ppp_resolve(blob, len(blob), res)
                fdt = RuntimeFdt(ctypes.cast(ctypes.c_char_p(blob), ctypes.c_void_p), len(blob), Window.SLOT)
                lib.p1_memory_admit(ctypes.byref(fdt), Window.PHYS, Window.SIZE, Window.SLOT + 0x100000)
            else:
                rejected += 1
        self.assertGreater(rejected, 1000)
        self.assertGreater(accepted, 100)  # e.g. changes inside property values
        for _ in range(200):
            d = bytearray(base)
            d[rng.randrange(40, len(d))] ^= 0xff
            w = Window(bytes(d))
            status = self.bind(lib, w)
            if status != "OK":
                self.assert_untouched(desc)
            else:
                self.assertEqual(lib.ppp_fdt_check_structure(desc.bytes, desc.length), 0)
                break


class ParserDifferential(Base):
    """Rejections that come from the specification versus policy decisions."""
    def test_property_after_child_is_rejected_by_every_consumer(self):
        t = Tree()
        p, n = t.prop, t.node
        root = p("#address-cells", u32(2)) + p("#size-cells", u32(2))
        root += n("memory@80000000", p("device_type", b"memory\0") + p("reg", u64(0x80000000) + u64(0x4000000)))
        root += p("late", b"x")
        blob = t.blob(root)
        lib, _ = self.provider()
        res = ctypes.create_string_buffer(256)
        fdt = RuntimeFdt(ctypes.cast(ctypes.c_char_p(blob), ctypes.c_void_p), len(blob), 0x80000000 + 0x8000)
        verdicts = {"structure": lib.ppp_fdt_check_structure(blob, len(blob)),
                    "resolver": lib.ppp_resolve(blob, len(blob), res),
                    "admission": lib.p1_memory_admit(ctypes.byref(fdt), 0x80000000, 0x4000000, 0x80400000)}
        self.assertNotEqual(verdicts["structure"], 0)
        self.assertNotEqual(verdicts["resolver"], 0)
        # Recorded, not asserted: the admission gate is owned by the native
        # build chain. Its verdict is reported in the research record.
        print("\nPARSER_DIFFERENTIAL property-after-child", json.dumps(verdicts))


class BridgeSeedAndCopy(Base):
    def test_sha512_matches_fips_180_4(self):
        for n in list(range(0, 260)) + [511, 512, 513, 1000, 4096]:
            data = bytes((i * 7 + n) & 0xff for i in range(n))
            out = ctypes.create_string_buffer(64)
            self.bridge.m12_sha512(data, n, out)
            self.assertEqual(out.raw, hashlib.sha512(data).digest(), n)

    def test_seed_transport_policy(self):
        out = ctypes.create_string_buffer(64)
        field = bytes(range(1, 65))
        self.assertEqual(self.bridge.m12_condition_seed(field, 64, out), 1)
        self.assertEqual(out.raw, field)
        for n in (65, 128, 512):
            field = bytes((i * 13 + 1) & 0xff for i in range(n))
            self.assertEqual(self.bridge.m12_condition_seed(field, n, out), 2)
            self.assertEqual(out.raw, hashlib.sha512(field).digest())
        for field in (bytes(range(1, 64)), bytes(64), bytes(512), bytes(range(256)) * 2 + b"\x01", b""):
            out.raw = b"\xaa" * 64
            self.assertEqual(self.bridge.m12_condition_seed(field, len(field), out), 0, len(field))
            self.assertEqual(out.raw, bytes(64), "output is wiped on refusal")
        self.assertEqual(self.bridge.m12_condition_seed(None, 64, out), 0)

    def test_retained_copy_is_byte_exact_except_zeroed_seed_values(self):
        seed = bytes((i * 5 + 3) & 0xff or 1 for i in range(512))
        src_blob = penang_tree(seed=seed)
        src = ctypes.create_string_buffer(src_blob, len(src_blob))
        cap = (len(src_blob) + PAGE - 1) // PAGE * PAGE
        dst = ctypes.create_string_buffer(b"\x5a" * cap, cap)
        scrubbed = ctypes.c_uint32(99)
        self.assertEqual(self.bridge.m12_retain_fdt(src, len(src_blob), dst, cap, ctypes.byref(scrubbed)), 0)
        self.assertEqual(scrubbed.value, 2)  # rng-seed and kaslr-seed
        copy = dst.raw[:len(src_blob)]
        self.assertEqual(dst.raw[len(src_blob):], bytes(cap - len(src_blob)))
        differ = [i for i in range(len(src_blob)) if copy[i] != src_blob[i]]
        start = src_blob.index(seed)
        kaslr = src_blob.index(u64(0x1122334455667788))
        self.assertEqual(differ, [i for i in range(start, start + 512) if seed[i - start]] +
                         [i for i in range(kaslr, kaslr + 8) if src_blob[i]])
        self.assertEqual(bytes(copy[i] for i in differ), bytes(len(differ)))
        lib, _ = self.provider()
        self.assertEqual(lib.ppp_fdt_check_structure(copy, len(copy)), 0, "structure is unchanged")
        self.assertEqual(src.raw[:len(src_blob)], src_blob, "the source is not modified by the copy")

    def test_only_the_root_chosen_node_is_scrubbed(self):
        t = Tree()
        decoy = Tree.node("chosen", t.prop("rng-seed", b"\x77" * 64))
        blob = penang_tree(extra_root=b"", kaslr=False)
        # Insert a nested node also called "chosen" below /cpus/cpu@0.
        t2 = Tree()
        p, n = t2.prop, t2.node
        root = p("#address-cells", u32(2)) + n("chosen", p("rng-seed", b"\x11" * 64)) + \
            n("soc", n("chosen", p("rng-seed", b"\x22" * 64)))
        blob = t2.blob(root)
        del decoy, t
        src = ctypes.create_string_buffer(blob, len(blob))
        dst = ctypes.create_string_buffer(len(blob) + 64)
        count = ctypes.c_uint32()
        self.assertEqual(self.bridge.m12_retain_fdt(src, len(blob), dst, len(blob) + 64, ctypes.byref(count)), 0)
        self.assertEqual(count.value, 1)
        self.assertNotIn(b"\x11" * 64, dst.raw)
        self.assertIn(b"\x22" * 64, dst.raw)

    def test_retained_copy_refusals(self):
        blob = penang_tree()
        src = ctypes.create_string_buffer(blob, len(blob))
        dst = ctypes.create_string_buffer(len(blob))
        self.assertNotEqual(self.bridge.m12_retain_fdt(src, len(blob), dst, len(blob) - 1, None), 0)
        self.assertNotEqual(self.bridge.m12_retain_fdt(src, len(blob) - 1, dst, len(blob), None), 0)
        self.assertNotEqual(self.bridge.m12_retain_fdt(src, len(blob), ctypes.byref(src, 8), len(blob), None), 0)
        bad = bytearray(blob)
        o_struct = struct.unpack_from(">I", bad, 8)[0]
        struct.pack_into(">I", bad, o_struct, 7)  # unknown token
        bsrc = ctypes.create_string_buffer(bytes(bad), len(bad))
        dst = ctypes.create_string_buffer(b"\x33" * len(bad), len(bad))
        self.assertNotEqual(self.bridge.m12_retain_fdt(bsrc, len(bad), dst, len(bad), None), 0)
        self.assertEqual(dst.raw, bytes(len(bad)), "a refused copy is wiped")


DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bridge_contract.h"
static void *load(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); rewind(f);
    void *b = aligned_alloc(16384, (*n + 16383) & ~(size_t)16383); if (fread(b, 1, *n, f) != *n) abort(); fclose(f); return b;
}
int main(int argc, char **argv) {
    size_t fl, ml; (void)argc;
    void *fdt = load(argv[1], &fl), *macho = load(argv[2], &ml);
    struct m12_cpu_context c; memset(&c, 0, sizeof(c));
    c.x0 = (uint64_t)(uintptr_t)fdt; c.active_el_raw = 4; c.current_el_raw = 4;
    c.id_aa64mmfr0_el1 = 1u << 20; c.cntfrq_el0 = 19200000; c.mpidr_el1 = 0x80000000u;
    c.payload_start_pa = (uint64_t)(uintptr_t)macho; c.payload_end_pa = c.payload_start_pa + ml;
    c.force_profile = (uint32_t)atoi(argv[3]);
    static struct m12_handoff_plan p;
    int s = m12_bridge_execute(&c, &p, 0);
    printf("{\"status\":\"%s\",\"phys_base\":%llu,\"mem_size\":%llu,\"kernel_lo\":%llu,\"kernel_hi\":%llu,"
           "\"dt_pa\":%llu,\"args_pa\":%llu,\"retained_pa\":%llu,\"retained_len\":%llu,\"top\":%llu,"
           "\"seed_kind\":%u,\"fdt_totalsize\":%u,\"boot_args_size\":%zu,\"ba_top\":%llu,\"ba_dt\":%llu,"
           "\"ba_physbase\":%llu,\"ba_virtbase\":%llu,\"ba_memsize\":%llu,\"ba_dtlen\":%u}\n",
           m12_status_name((uint32_t)s), (unsigned long long)p.phys_base, (unsigned long long)p.mem_size,
           (unsigned long long)p.kernel_lo_pa, (unsigned long long)p.kernel_hi_pa,
           (unsigned long long)p.dt_pa, (unsigned long long)p.args_pa,
           (unsigned long long)p.retained_fdt_pa, (unsigned long long)p.retained_fdt_length,
           (unsigned long long)p.top_of_kernel_data, p.seed_kind, p.fdt_totalsize,
           sizeof(struct m12_boot_args), (unsigned long long)p.boot_args_copy.topOfKernelData,
           (unsigned long long)p.boot_args_copy.deviceTreeP, (unsigned long long)p.boot_args_copy.physBase,
           (unsigned long long)p.boot_args_copy.virtBase, (unsigned long long)p.boot_args_copy.memSize,
           p.boot_args_copy.deviceTreeLength);
    return 0;
}
'''


def macho(vmaddr=0xfffffe0007004000, size=0x8000):
    entry = vmaddr + 0x100
    seg = struct.pack("<II16sQQQQIIII", 0x19, 72, b"__TEXT", vmaddr, size, 0, size, 5, 5, 0, 0)
    regs = [0] * 32 + [entry, 0]
    thread = struct.pack("<IIII", 5, 16 + 272, 6, 68) + struct.pack("<34Q", *regs)
    cmds = seg + thread
    head = struct.pack("<IIIIIIII", 0xfeedfacf, 0x0100000c, 0, 2, 2, len(cmds), 1, 0)
    body = head + cmds
    return body + bytes(size - len(body))


class BridgeLayout(Base):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        src = cls.dir / "driver.c"
        src.write_text(DRIVER)
        cls.driver = cls.dir / "driver"
        host = os.environ.get("PPP_TEST_HOST_CC") or cc()
        subprocess.run([host, "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-DM12_HOST_SYNTHETIC_ENTROPY",
                        *os.environ.get("PPP_TEST_CFLAGS", "").split(), "-I", str(BRIDGE), str(src),
                        str(BRIDGE / "bridge_core.c"), str(BRIDGE / "m12_retained_fdt.c"), "-o", str(cls.driver)],
                       check=True, capture_output=True)
        (cls.dir / "kernel.macho").write_bytes(macho())

    def run_bridge(self, blob, profile):
        path = self.dir / "in.dtb"
        path.write_bytes(blob)
        out = subprocess.run([str(self.driver), str(path), str(self.dir / "kernel.macho"), str(profile)],
                             capture_output=True, text=True, check=True)
        return json.loads(out.stdout)

    def test_retained_slot_lies_in_the_region_xnu_never_frees(self):
        for profile, blob in ((2, qemu_tree()), (1, penang_tree(seed=bytes(range(1, 256)) + bytes(range(1, 256)) + b"\x01\x02"))):
            with self.subTest(profile=profile):
                r = self.run_bridge(blob, profile)
                self.assertEqual(r["status"], "BRIDGE_OK")
                self.assertEqual(r["boot_args_size"], 1152, "boot_args ABI unchanged")
                self.assertEqual(r["retained_pa"], r["args_pa"] + PAGE)
                self.assertEqual(r["retained_len"], len(blob))
                self.assertEqual(r["top"], r["retained_pa"] + (len(blob) + PAGE - 1) // PAGE * PAGE)
                self.assertEqual(r["ba_top"], r["top"])
                self.assertEqual(r["retained_pa"] % PAGE, 0)
                # [kernel_hi, top) is XNU's [end_kern, topOfKernelData): mapped RWNX, never freed.
                self.assertGreaterEqual(r["retained_pa"], r["kernel_hi"])
                self.assertLessEqual(r["top"], r["phys_base"] + r["mem_size"])
                self.assertGreater(r["kernel_lo"], r["phys_base"], "the freed prefix is below the kernel")
                self.assertEqual(r["seed_kind"], 1 if profile == 2 else 2)

    def test_bridge_output_composes_with_provider_and_admission(self):
        """End to end on the host: bridge plan -> retained copy -> bind ->
        p1_memory_admit, all with the bridge's own numbers."""
        blob = qemu_tree()
        r = self.run_bridge(blob, 2)
        cap = r["top"] - r["retained_pa"]
        src = ctypes.create_string_buffer(blob, len(blob))
        slot = mmap.mmap(-1, cap + PAGE)
        host = (ctypes.addressof(ctypes.c_char.from_buffer(slot)) + PAGE - 1) // PAGE * PAGE
        self.assertEqual(self.bridge.m12_retain_fdt(src, len(blob), host, cap, None), 0)
        lib, desc = self.provider()
        virt = (host - (r["retained_pa"] - r["phys_base"])) % 2**64
        h = Handoff(2, 0, r["retained_pa"], r["retained_len"], r["phys_base"], virt, r["mem_size"], r["kernel_hi"],
                    r["top"], r["dt_pa"], 0x10000, r["args_pa"])
        backed = range(host, host + cap)
        def translate(_c, va, out):
            if va not in backed: return 1
            out[0] = r["phys_base"] + ((va - virt) % 2**64)
            return 0
        hook = TRANSLATE(translate)
        m = Mapping(None, hook, virt, (virt + r["mem_size"]) % 2**64)
        self.assertEqual(lib.ppp_fdt_status_name(lib.ppp_runtime_fdt_bind(ctypes.byref(h), ctypes.byref(m))), b"OK")
        self.assertEqual(lib.p1_memory_admit(ctypes.byref(desc), r["phys_base"], r["mem_size"], r["top"]), 0)
        retained = ctypes.string_at(desc.bytes, desc.length)
        self.assertNotIn(bytes(range(1, 65)), retained, "the seed does not survive in the retained tree")

    def test_seed_and_reservation_refusals_at_the_bridge(self):
        cases = [(qemu_tree(seed=bytes(range(1, 49))), "ERR_NO_ENTROPY"), (qemu_tree(seed=bytes(range(1, 65)), seeds=2), "ERR_NO_ENTROPY"),
                 (qemu_tree(seed=None), "ERR_NO_ENTROPY"), (qemu_tree(seed=bytes(range(256)) * 2 + b"\x01"), "ERR_NO_ENTROPY"),
                 (penang_tree(dynamic=True), "ERR_RESV_UNRES")]
        for blob, prefix in cases:
            with self.subTest(prefix=prefix):
                status = self.run_bridge(blob, 1 if b"qcom,holi" in blob else 2)["status"]
                self.assertTrue(status.startswith(prefix), status)


if __name__ == "__main__":
    unittest.main()
