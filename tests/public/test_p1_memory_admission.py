# SPDX-License-Identifier: Apache-2.0
"""Real C admission checks with self-authored FDTs; no PTE or device execution."""
import ctypes
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/m12-penang-physical"
BASE = 0x40000000


def u32(x): return struct.pack(">I", x)
def u64(x): return struct.pack(">Q", x)
def pad(x): return x + bytes((-len(x)) % 4)


def fdt(*, reserved=(), dynamic=False, duplicate=False, cells=2, ram=None, memreserve=()):
    strings = bytearray()
    offsets = {}
    def prop(name, data):
        if name not in offsets:
            offsets[name] = len(strings)
            strings.extend(name.encode() + b"\0")
        return u32(3) + u32(len(data)) + u32(offsets[name]) + pad(data)
    def node(name, body): return u32(1) + pad(name.encode() + b"\0") + body + u32(2)
    bank = ram or [(BASE, 0x1000000)]
    body = prop("#address-cells", u32(cells)) + prop("#size-cells", u32(2))
    body += node("memory@40000000", prop("device_type", b"memory\0") +
                 prop("reg", b"".join(u64(a)+u64(b) for a,b in bank)))
    res = prop("#address-cells", u32(2)) + prop("#size-cells", u32(2)) + prop("ranges", b"")
    for i,(a,b) in enumerate(reserved):
        value = prop("reg", u64(a)+u64(b))
        if duplicate: value += prop("reg", u64(a)+u64(b))
        res += node("reservation@" + str(i), value)
    if dynamic: res += node("pool", prop("size", u64(0x4000)))
    body += node("reserved-memory", res)
    structure = node("", body) + u32(9)
    reserve = b"".join(u64(a)+u64(b) for a,b in memreserve) + bytes(16)
    a = 40 + len(reserve)
    b = a + len(structure)
    return struct.pack(">10I", 0xd00dfeed, b+len(strings), a, b, 40, 17, 16, 0,
                       len(strings), len(structure)) + reserve + structure + strings


class RuntimeFdt(ctypes.Structure):
    _fields_ = [("bytes", ctypes.c_void_p), ("length", ctypes.c_size_t),
                ("physical_address", ctypes.c_uint64)]


class MemoryAdmission(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang")
        if not cls.cc: raise unittest.SkipTest("Clang required")
        cls.folder = tempfile.TemporaryDirectory(prefix="p1-memory-")
        cls.root = Path(cls.folder.name)
        lib = cls.root / "admission.so"
        subprocess.run([cls.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared",
                        str(TOOLS / "p1_memory_admission.c"), "-o", str(lib)], check=True, capture_output=True)
        cls.lib = ctypes.CDLL(str(lib))
        cls.lib.p1_memory_admit.argtypes = [ctypes.POINTER(RuntimeFdt)] + [ctypes.c_uint64]*3
        cls.lib.p1_memory_admit.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()

    def admit(self, blob, base=BASE, size=0x100000, top=BASE+0x40000, pa=BASE+0x8000):
        buffer = ctypes.create_string_buffer(blob)
        original = bytes(buffer)
        obj = RuntimeFdt(ctypes.cast(buffer, ctypes.c_void_p), len(blob), pa)
        result = self.lib.p1_memory_admit(ctypes.byref(obj), base, size, top)
        self.assertEqual(bytes(buffer), original, "admission must not mutate the runtime FDT")
        return result

    def test_contiguous_materialized_window_is_admitted_without_p2_claim(self):
        self.assertEqual(self.admit(fdt(reserved=[(BASE+0x200000,0x4000)])), 0)

    def test_unmaterialized_dynamic_request_never_gets_a_dry_run_address(self):
        self.assertEqual(self.admit(fdt(dynamic=True)), 2)

    def test_reserved_property_memreserve_and_outward_16k_rounding(self):
        for kwargs in ({"reserved": [(BASE+0x80000,1)]}, {"memreserve": [(BASE+0x80000,1)]},
                       {"reserved": [(BASE+0xfffff,1)]}):
            with self.subTest(kwargs=kwargs): self.assertEqual(self.admit(fdt(**kwargs)), 4)

    def test_holes_overlapping_banks_overflow_and_misaligned_windows(self):
        for blob in (fdt(ram=[(BASE,0x80000),(BASE+0xc0000,0x40000)]),
                     fdt(ram=[(BASE,0x200000),(BASE+0x80000,0x200000)]),
                     fdt(ram=[(BASE,2**64-1)])):
            self.assertNotEqual(self.admit(blob), 0)
        for base,size,top in ((BASE+1,0x100000,BASE+0x40000), (BASE,0,BASE+0x40000),
                              (BASE,0x100000,BASE+0x100000), (2**64-0x4000,0x8000,BASE)):
            self.assertEqual(self.admit(fdt(),base,size,top), 3)

    def test_fdt_must_remain_in_the_reserved_bootstrap_prefix(self):
        for pa in (BASE-1,BASE+0x3fff0,BASE+0x80000,2**64-1):
            self.assertEqual(self.admit(fdt(),pa=pa), 5)

    def test_duplicates_cells_and_malformed_fdt_are_rejected(self):
        self.assertNotEqual(self.admit(fdt(reserved=[(BASE+0x200000,0x4000)],duplicate=True)),0)
        self.assertNotEqual(self.admit(fdt(cells=1)),0)
        blob=fdt()
        for offset,value in ((0,0),(8,2**32-1),(12,0),(16,41),(20,16),(32,2**32-1),(36,2**32-1)):
            data=bytearray(blob);struct.pack_into(">I",data,offset,value)
            with self.subTest(offset=offset): self.assertNotEqual(self.admit(bytes(data)),0)
        for length in (0,3,39,len(blob)-1): self.assertNotEqual(self.admit(blob[:length]),0)

    def test_bounded_header_and_token_mutations_cannot_crash_or_write(self):
        rng=random.Random(11417)
        original=fdt(dynamic=True)
        for _ in range(600):
            blob=bytearray(original)
            at=rng.randrange(len(blob));blob[at]^=rng.randrange(1,256)
            self.assertIn(self.admit(bytes(blob)),range(6))

    def test_native_object_and_protected_profile_refusals(self):
        obj=self.root/"admission.o"
        base=[self.cc,"--target=arm64-apple-darwin","-mcpu=cortex-a76","-ffreestanding",
              "-c",str(TOOLS/"p1_memory_admission.c"),"-o",str(obj)]
        good=subprocess.run(base+["-DPENANG_P1_DIAGNOSTIC=1","-DPENANG_P1_SANDBOX_COMPILE=1"],capture_output=True)
        self.assertEqual(good.returncode,0,good.stderr)
        self.assertEqual(struct.unpack_from("<4I",obj.read_bytes()),(0xfeedfacf,0x100000c,0,1))
        for flags in (["-DPENANG_PHYSICAL=1"],["-DPENANG_P1_DIAGNOSTIC=1"],
                      ["-DPENANG_P1_DIAGNOSTIC=1", "-DPENANG_P1_SANDBOX_COMPILE=1",
                       "-DARM64_BOARD_CONFIG_PENANG_PHYSICAL=1"],
                      ["-DPENANG_P1_DIAGNOSTIC=1", "-DPENANG_P1_SANDBOX_COMPILE=1",
                       "-DPENANG_P2_PROTECTED=1"]):
            self.assertNotEqual(subprocess.run(base+flags,capture_output=True).returncode,0)
