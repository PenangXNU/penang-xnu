# SPDX-License-Identifier: Apache-2.0
"""P1 physical platform services against self-authored device trees and a
two-security-state GICv3 register model. HOST_ONLY: no device, no QEMU, and
no claim that the model equals SM4350 silicon."""
import ctypes
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/p1-penang-platform"
SOURCES = ["ppp_resources.c", "ppp_gicv3.c", "ppp_timer_uart.c", "ppp_runtime_fdt.c"]
HEADERS = ["ppp_platform.h", "ppp_gicv3_regs.h", "ppp_runtime_fdt.h"]
ADMISSION = ROOT / "tools/m12-penang-physical"  # owner of struct p1_runtime_fdt and p1_memory_admit
RAM = 0x80000000

GICD, GICR, STRIDE, UART = 0x0F200000, 0x0F240000, 0x20000, 0x04C8C000
HZ = 19200000
(SR_MPIDR, SR_SRE, SR_PMR, SR_BPR1, SR_CTLR, SR_IGRPEN1, SR_IAR1, SR_EOIR1, SR_SGI1R,
 SR_CNTFRQ) = range(10)


def u32(x): return struct.pack(">I", x)
def u64(x): return struct.pack(">Q", x)
def pad(x): return x + bytes((-len(x)) % 4)


class Tree:
    """Minimal FDT v17 writer for adversarial fixtures."""
    def __init__(self):
        self.strings = bytearray()
        self.offsets = {}

    def prop(self, name, data=b""):
        if name not in self.offsets:
            self.offsets[name] = len(self.strings)
            self.strings.extend(name.encode() + b"\0")
        return u32(3) + u32(len(data)) + u32(self.offsets[name]) + pad(data)

    @staticmethod
    def node(name, body): return u32(1) + pad(name.encode() + b"\0") + body + u32(2)

    def blob(self, root_body):
        structure = self.node("", root_body) + u32(9)
        a = 40 + 16
        b = a + len(structure)
        return struct.pack(">10I", 0xd00dfeed, b + len(self.strings), a, b, 40, 17, 16, 0,
                           len(self.strings), len(structure)) + bytes(16) + structure + bytes(self.strings)


def holi_shaped(**o):
    """Same *shape* as the target tree: 2/2 root cells, a 1/1 /soc bus with a
    translating ranges, eight CPUs, timer PPIs 1,2,3,0, GENI console SPI."""
    t = Tree()
    p, n = t.prop, t.node
    cpus = p("#address-cells", u32(2)) + p("#size-cells", u32(0))
    for i in range(o.get("cpus", 8)):
        cpus += n("cpu@%x" % (i << 8), p("device_type", b"cpu\0") + p("reg", u64(i << 8)))
    cpus += n("cpu-map", b"")
    gic_reg = o.get("gic_reg", u32(GICD) + u32(0x10000) + u32(GICR) + u32(0x100000))
    gic = p("compatible", o.get("gic_compatible", b"arm,gic-v3\0"))
    gic += p("#interrupt-cells", u32(o.get("intr_cells", 3))) + p("interrupt-controller")
    if o.get("regions", 1) is not None:
        gic += p("#redistributor-regions", u32(o.get("regions", 1)))
    if o.get("stride", STRIDE) is not None:
        gic += p("redistributor-stride", u64(o.get("stride", STRIDE)))
    gic += p("reg", gic_reg) * (2 if o.get("duplicate_reg") else 1)
    ppis = o.get("ppis", [(1, 1), (1, 2), (1, 3), (1, 0)])
    timer = p(o.get("timer_prop", "interrupts"), b"".join(u32(a) + u32(b) + u32(o.get("flags", {}).get(b, 0xff08)) for a, b in ppis))
    timer += p("compatible", b"arm,armv8-timer\0")  # compatible after interrupts on purpose
    if o.get("hz", HZ) is not None:
        timer += p("clock-frequency", u32(o.get("hz", HZ)))
    uart = (p("compatible", b"qcom,msm-geni-console\0") + p("reg", u32(UART) + u32(0x4000)) +
            p("interrupts", u32(0) + u32(0x1fe) + u32(4)) + p("status", o.get("uart_status", b"ok\0")))
    soc = p("#address-cells", u32(1)) + p("#size-cells", u32(1)) + p("compatible", b"simple-bus\0")
    if o.get("soc_ranges", b"x") is not None:
        soc += p("ranges", o.get("soc_ranges", u32(0) + u64(o.get("soc_parent", 0)) + u32(0xffffffff)))
    soc += n("psci", p("compatible", b"arm,psci-1.0\0") + p("method", b"smc\0"))
    soc += n("interrupt-controller@f200000", gic)
    if o.get("second_gic"):
        soc += n("interrupt-controller@1", gic)
    if o.get("timer", True):
        soc += n("timer", timer)
    soc += n("timer@f420000", p("compatible", b"arm,armv7-timer-mem\0") + p("reg", u32(0xf420000) + u32(0x1000)))
    soc += n("qcom,qup_uart@4c8c000", uart)
    for _ in range(o.get("extra_uarts", 0)):
        soc += n("serial@1", uart)
    soc += n("disabled-gic", p("compatible", b"arm,gic-v3\0") + p("status", b"disabled\0"))
    body = p("#address-cells", u32(2)) + p("#size-cells", u32(2)) + n("cpus", cpus) + n("soc", soc)
    if o.get("memory"):
        body += n("memory@80000000", p("device_type", b"memory\0") + p("reg", u64(RAM) + u64(0x8000000)))
        res = p("#address-cells", u32(2)) + p("#size-cells", u32(2)) + p("ranges")
        res += n("fw@87000000", p("reg", u64(RAM + 0x7000000) + u64(0x100000)) + p("no-map"))
        if o.get("dynamic"):
            res += n("pool", p("size", u64(0x400000)))
        body += n("reserved-memory", res)
    return t.blob(body)


class Ppi(ctypes.Structure):
    _fields_ = [("intid", ctypes.c_uint32), ("dt_flags", ctypes.c_uint32)]


class Resources(ctypes.Structure):
    _fields_ = [("abi_version", ctypes.c_uint32), ("cpu_count", ctypes.c_uint32),
                ("gicd_pa", ctypes.c_uint64), ("gicd_size", ctypes.c_uint64),
                ("gicr_pa", ctypes.c_uint64), ("gicr_size", ctypes.c_uint64),
                ("gicr_stride", ctypes.c_uint64), ("timer_count", ctypes.c_uint32),
                ("timer_dt_hz", ctypes.c_uint32), ("timer", Ppi * 4),
                ("psci_method", ctypes.c_uint32), ("console_candidates", ctypes.c_uint32),
                ("console_pa", ctypes.c_uint64), ("console_size", ctypes.c_uint64),
                ("console_intid", ctypes.c_uint32), ("reserved", ctypes.c_uint32)]


R32 = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint64)
W32 = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint32)
R64 = ctypes.CFUNCTYPE(ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint64)
W64 = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64)
SRD = ctypes.CFUNCTYPE(ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint32)
SWR = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64)


class Io(ctypes.Structure):
    _fields_ = [("ctx", ctypes.c_void_p), ("read32", R32), ("write32", W32), ("read64", R64),
                ("write64", W64), ("sysreg_read", SRD), ("sysreg_write", SWR)]


class Gic(ctypes.Structure):
    _fields_ = [("io", Io), ("gicd", ctypes.c_uint64), ("gicr", ctypes.c_uint64),
                ("boot_mpidr", ctypes.c_uint64), ("intid_count", ctypes.c_uint32),
                ("single_security", ctypes.c_uint32), ("poll_limit", ctypes.c_uint32),
                ("ready", ctypes.c_uint32), ("ack_count", ctypes.c_uint64),
                ("eoi_count", ctypes.c_uint64), ("spurious_count", ctypes.c_uint64)]


class RuntimeFdt(ctypes.Structure):
    _fields_ = [("bytes", ctypes.c_void_p), ("length", ctypes.c_size_t), ("physical_address", ctypes.c_uint64)]


class Timer(ctypes.Structure):
    _fields_ = [("frequency_hz", ctypes.c_uint64), ("virtual_intid", ctypes.c_uint32),
                ("edge", ctypes.c_uint32), ("polarity_low", ctypes.c_uint32),
                ("trigger_defaulted", ctypes.c_uint32), ("cpu_mask", ctypes.c_uint32)]


class Uart(ctypes.Structure):
    _fields_ = [("io", Io), ("base", ctypes.c_uint64), ("poll_limit", ctypes.c_uint32),
                ("ready", ctypes.c_uint32), ("sent", ctypes.c_uint64), ("dropped", ctypes.c_uint64)]


class Model:
    """Non-secure view of a GICv3 with security extensions, plus a GENI UART.
    `secure` INTIDs are RAZ/WI, as Group 0 / Secure Group 1 state is for a
    non-secure access when GICD_CTLR.DS == 0 (Arm IHI 0069)."""
    def __init__(self, cpu=3, cpus=8, ds=0, secure=(), **quirk):
        self.q = quirk
        self.ds, self.secure = ds, set(secure)
        self.aff3 = quirk.get("aff3", 0)
        self.mpidr = (1 << 31) | (cpu << 8) | (self.aff3 << 32)
        self.cpu_index, self.cpus = cpu, cpus
        self.lines = 640
        self.ctlr = (1 << 4 if quirk.get("are", True) else 0) | (ds << 6) | quirk.get("ctlr_pre", 0)
        self.enabled = set(quirk.get("stale_enabled", ()))
        self.pending, self.active = set(), set()
        self.group, self.prio, self.edge, self.route = set(), {}, set(), {}
        self.asleep = True
        self.sre = 1 if quirk.get("sre_preset") else 0
        self.pmr = self.igrpen1 = 0
        self.icc_ctlr = 2 if quirk.get("eoimode_preset") else 0
        self.faults, self.writes, self.sys_writes, self.tx = [], [], [], []
        self.uart = {"status": 0, "irq": 0, "len": 0}
        self.io = Io(None, R32(self.r32), W32(self.w32), R64(self.r64), W64(self.w64),
                     SRD(self.srd), SWR(self.swr))

    # -- register decode -------------------------------------------------
    def owned(self, i): return i < self.lines and i not in self.secure

    def bits(self, base, values, read=True):
        return sum(1 << (i - base) for i in values if base <= i < base + 32 and self.owned(i))

    def frame(self, a):
        if GICD <= a < GICD + 0x10000:
            return "d", a - GICD, 32
        if GICR <= a < GICR + self.cpus * STRIDE:
            index, off = divmod(a - GICR, STRIDE)
            return ("r", index), off, 0
        if UART <= a < UART + 0x4000:
            return "u", a - UART, 0
        self.faults.append(a)
        return None, 0, 0

    def bank_read(self, off, base):
        word = (off & 0x7f) // 4 * 32 + base
        kind = off & ~0x7f
        if kind == 0x080: return self.bits(word, self.group if self.ds else range(1024))
        if kind in (0x100, 0x180): return self.bits(word, self.enabled)
        if kind in (0x200, 0x280): return self.bits(word, self.pending)
        if kind in (0x300, 0x380): return self.bits(word, self.active)
        return None

    def bank_write(self, off, base, v):
        word = (off & 0x7f) // 4 * 32 + base
        chosen = {word + b for b in range(32) if v >> b & 1 and self.owned(word + b)}
        kind = off & ~0x7f
        if kind == 0x080:
            if self.ds: self.group = {i for i in self.group if not word <= i < word + 32} | chosen
        elif kind == 0x100:
            if not self.q.get("enable_stuck_off"): self.enabled |= chosen
        elif kind == 0x180: self.enabled -= chosen
        elif kind == 0x200: self.pending |= chosen
        elif kind == 0x280: self.pending -= chosen
        elif kind == 0x380: self.active -= chosen
        else: return False
        return True

    def prio_cfg(self, off, base, v=None):
        if 0x400 <= off < 0x800:
            first = off - 0x400 + (0 if base == 0 else 0)
            if v is None:
                return sum(self.prio.get(first + i, 0) << 8 * i for i in range(4) if self.owned(first + i))
            for i in range(4):
                if self.owned(first + i): self.prio[first + i] = v >> 8 * i & 0xff
            return True
        if 0xc00 <= off < 0xd00:
            first = (off - 0xc00) // 4 * 16
            if v is None:
                return sum(2 << 2 * i for i in range(16) if first + i in self.edge and self.owned(first + i))
            for i in range(16):
                if self.owned(first + i) and not (first + i < 32 and self.q.get("ppi_cfg_fixed")):
                    (self.edge.add if v >> (2 * i + 1) & 1 else self.edge.discard)(first + i)
            return True
        return None

    def r32(self, _, a):
        kind, off, base = self.frame(a)
        if kind == "d":
            if off == 0: return self.ctlr | (0x80000000 if self.q.get("rwp_stuck") else 0)
            if off == 4: return self.lines // 32 - 1
            if off == 0xffe8: return self.q.get("pidr2", 0x3b)
            value = self.bank_read(off, 0)
            if value is None: value = self.prio_cfg(off, 32)
            if value is not None: return value
        elif kind == "u":
            if off == 0x68: return self.q.get("geni_fw", 0x0205)
            if off == 0x48: return self.q.get("geni_clk", 1)
            if off == 0x40: return self.uart["status"]
            if off == 0x610: return self.uart["irq"]
        elif kind:
            mine = kind[1] == self.cpu_index
            if off == 0: return 0
            if off == 0x14:
                stuck = self.q.get("wake_stuck")
                return (2 if self.asleep else 0) | (4 if self.asleep or stuck else 0)
            if off == 0xffe8: return 0x3b
            if off >= 0x10000 and mine:
                value = self.bank_read(off - 0x10000, 0)
                if value is None: value = self.prio_cfg(off - 0x10000, 0)
                if value is not None: return value
        if kind: self.faults.append(a)
        return 0

    def w32(self, _, a, v):
        kind, off, base = self.frame(a)
        self.writes.append((kind, off, v))
        if kind == "d":
            if off == 0:
                keep = 1 << 6
                allowed = (1 << 1) | (0 if self.q.get("are_fixed_off") else 1 << 4) | (1 if self.ds else 0)
                if self.q.get("grp1_stuck_off"): allowed &= ~2
                self.ctlr = (self.ctlr & keep) | (v & allowed)
                return
            if self.bank_write(off, 0, v) or self.prio_cfg(off, 32, v): return
        elif kind == "u":
            if off == 0x270: self.uart["len"] = v
            elif off == 0x600 and v == 1 << 27 and not self.q.get("uart_dead"): self.uart["irq"] |= 1 << 30
            elif off == 0x700:
                self.tx.append(v & 0xff)
                self.uart["irq"] |= 1
            elif off == 0x618: self.uart["irq"] &= ~v
            elif off != 0x80c: self.faults.append(a)
            return
        elif kind:
            if kind[1] != self.cpu_index: self.faults.append(a)
            elif off == 0x14: self.asleep = bool(v & 2)
            elif off >= 0x10000 and (self.bank_write(off - 0x10000, 0, v) or self.prio_cfg(off - 0x10000, 0, v)): pass
            else: self.faults.append(a)
            return
        if kind: self.faults.append(a)

    def r64(self, _, a):
        kind, off, _b = self.frame(a)
        if kind == "d" and 0x6000 + 8 * 32 <= off < 0x8000:
            i = (off - 0x6000) // 8
            return self.route.get(i, 0) if self.owned(i) else 0
        if kind and kind != "u" and kind != "d" and off == 8:
            index = kind[1]
            last = 1 << 4 if index == self.cpus - 1 and not self.q.get("no_last") else 0
            return (self.aff3 << 24 | index << 8) << 32 | last | (2 if self.q.get("vlpis") else 0)
        if kind: self.faults.append(a)
        return 0

    def w64(self, _, a, v):
        kind, off, _b = self.frame(a)
        self.writes.append((kind, off, v))
        if kind == "d" and 0x6000 + 8 * 32 <= off < 0x8000:
            i = (off - 0x6000) // 8
            if self.owned(i): self.route[i] = v
        elif kind: self.faults.append(a)

    # -- CPU interface ---------------------------------------------------
    def deliverable(self):
        if not (self.igrpen1 and self.ctlr & 2 and self.sre): return None
        for i in sorted(self.pending & self.enabled):
            if self.ds and i not in self.group: continue
            if self.prio.get(i, 0) < self.pmr and (i < 32 or self.route.get(i) == self.mpidr & 0xff00ffffff):
                return i
        return None

    def srd(self, _, reg):
        if reg == SR_MPIDR: return self.mpidr
        if reg == SR_SRE: return self.sre
        if reg == SR_PMR: return self.pmr
        if reg == SR_CTLR: return self.icc_ctlr
        if reg == SR_IGRPEN1: return self.igrpen1
        if reg == SR_CNTFRQ: return self.q.get("cntfrq", HZ)
        if reg == SR_IAR1:
            i = self.deliverable()
            if i is None: return 1023
            self.pending.discard(i)
            self.active.add(i)
            return i
        self.faults.append(("sysreg", reg))
        return 0

    def swr(self, _, reg, v):
        self.sys_writes.append((reg, v))
        if reg == SR_SRE: self.sre = 0 if self.q.get("sre_denied") else v & 1
        elif reg == SR_PMR: self.pmr = self.q.get("pmr_readback", v & 0xff)
        elif reg == SR_BPR1: pass
        elif reg == SR_CTLR:
            if not self.q.get("eoimode_fixed"): self.icc_ctlr = v & 2
        elif reg == SR_IGRPEN1: self.igrpen1 = 0 if self.q.get("igrpen_denied") else v & 1
        elif reg == SR_EOIR1: self.active.discard(v)
        elif reg == SR_SGI1R:
            target_ok = (v >> 16 & 0xff) == (self.mpidr >> 8 & 0xff) and v >> (self.mpidr & 0xf) & 1 and not v >> 40 & 1
            if target_ok: self.pending.add(v >> 24 & 0xf)
        else: self.faults.append(("sysreg", reg))


class PenangPlatform(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang-18") or shutil.which("clang")
        if not cls.cc: raise unittest.SkipTest("Clang required")
        cls.folder = tempfile.TemporaryDirectory(prefix="p1-platform-")
        lib = Path(cls.folder.name) / "ppp.so"
        host_cc = os.environ.get("PPP_TEST_HOST_CC") or cls.cc  # e.g. gcc for sanitizer runs
        subprocess.run([host_cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared",
                        *os.environ.get("PPP_TEST_CFLAGS", "").split(),  # e.g. sanitizers
                        "-I", str(ADMISSION), *[str(TOOLS / s) for s in SOURCES],
                        str(ADMISSION / "p1_memory_admission.c"), "-o", str(lib)], check=True, capture_output=True)
        cls.lib_path = lib
        cls.lib = lib = ctypes.CDLL(str(lib))
        lib.ppp_resolve.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Resources)]
        lib.ppp_gic_init.argtypes = [ctypes.POINTER(Gic), ctypes.POINTER(Resources), ctypes.POINTER(Io), ctypes.c_uint32]
        for name in ("ppp_gic_configure",):
            getattr(lib, name).argtypes = [ctypes.POINTER(Gic), ctypes.c_uint32, ctypes.c_int]
        for name in ("ppp_gic_enable", "ppp_gic_disable", "ppp_gic_self_sgi", "ppp_gic_eoi"):
            getattr(lib, name).argtypes = [ctypes.POINTER(Gic), ctypes.c_uint32]
        lib.ppp_gic_eoi.restype = None
        lib.ppp_gic_ack.argtypes = [ctypes.POINTER(Gic)]
        lib.ppp_gic_ack.restype = ctypes.c_uint32
        cls.HANDLER = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint32)
        lib.ppp_gic_dispatch.argtypes = [ctypes.POINTER(Gic), cls.HANDLER, ctypes.c_void_p]
        lib.ppp_timer_admit.argtypes = [ctypes.POINTER(Resources), ctypes.POINTER(Io), ctypes.POINTER(Timer)]
        lib.ppp_uart_probe.argtypes = [ctypes.POINTER(Uart), ctypes.POINTER(Resources), ctypes.POINTER(Io), ctypes.c_uint32]
        lib.ppp_uart_putc.argtypes = [ctypes.POINTER(Uart), ctypes.c_uint8]
        for name in ("ppp_status_name", "ppp_gic_status_name"):
            getattr(lib, name).restype = ctypes.c_char_p

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()

    def resolve(self, blob, length=None):
        buffer = ctypes.create_string_buffer(blob, len(blob))
        res = Resources()
        status = self.lib.ppp_resolve(buffer, len(blob) if length is None else length, ctypes.byref(res))
        self.assertEqual(buffer.raw, blob, "resolution must not mutate the tree")
        if status: self.assertEqual(bytes(res), bytes(ctypes.sizeof(res)), "failure must not leak partial resources")
        return self.lib.ppp_status_name(status).decode(), res

    def bring_up(self, model, poll=64, **tree):
        name, res = self.resolve(holi_shaped(**tree))
        self.assertEqual(name, "OK")
        gic = Gic()
        status = self.lib.ppp_gic_init(ctypes.byref(gic), ctypes.byref(res), ctypes.byref(model.io), poll)
        return self.lib.ppp_gic_status_name(status).decode(), gic, res

    # ---- device tree ------------------------------------------------------
    def test_resources_come_from_the_tree_through_bus_translation(self):
        name, r = self.resolve(holi_shaped())
        self.assertEqual(name, "OK")
        self.assertEqual((r.gicd_pa, r.gicd_size, r.gicr_pa, r.gicr_size, r.gicr_stride),
                         (GICD, 0x10000, GICR, 0x100000, STRIDE))
        self.assertEqual([(t.intid, t.dt_flags) for t in r.timer], [(17, 0xff08), (18, 0xff08), (19, 0xff08), (16, 0xff08)])
        self.assertEqual((r.cpu_count, r.timer_dt_hz, r.psci_method), (8, HZ, 1))
        self.assertEqual((r.console_candidates, r.console_pa, r.console_intid), (1, UART, 32 + 0x1fe))
        # A relocating bus moves every resource; nothing is compiled in.
        name, r = self.resolve(holi_shaped(soc_parent=0x100000000))
        self.assertEqual((name, r.gicd_pa, r.gicr_pa, r.console_pa),
                         ("OK", 0x100000000 + GICD, 0x100000000 + GICR, 0x100000000 + UART))

    def test_untranslatable_or_out_of_contract_trees_are_refused(self):
        cases = {
            "TRANSLATE": [dict(soc_ranges=None), dict(soc_ranges=u32(0) + u64(0) + u32(0x1000)),
                          dict(soc_ranges=bytes(5)), dict(gic_reg=u32(GICD) + u32(0x10000))],
            "GIC_LAYOUT": [dict(regions=2), dict(stride=0x18000), dict(stride=0x10000), dict(intr_cells=2),
                           dict(gic_reg=u32(GICD) + u32(0x20000) + u32(GICR) + u32(0x100000)),
                           dict(gic_reg=u32(GICD) + u32(0x10000) + u32(GICD) + u32(0x100000)),
                           dict(gic_reg=u32(GICD + 0x1000) + u32(0x10000) + u32(GICR) + u32(0x100000))],
            "GIC_DUPLICATE": [dict(second_gic=True)],
            "TRANSLATE ": [  # arithmetic at the top of the address space must not wrap
                dict(soc_ranges=u32(0) + u64(2**64 - 0x1000) + u32(0xffffffff)),
                dict(soc_ranges=u32(0) + u64(2**64 - GICR - 0x100000 + 1) + u32(0xffffffff)),
                dict(soc_ranges=u32(0xf0000000) + u64(0) + u32(0x0fffffff)),
                dict(soc_ranges=u32(0xfffff000) + u64(0) + u32(0xffffffff)),
                dict(gic_reg=u32(0xffff0000) + u32(0x10000) + u32(GICR) + u32(0x100000)),
                dict(gic_reg=u32(GICD) + u32(0x10000) + u32(0xfff00000) + u32(0x100000)),
                dict(gic_reg=u32(GICD) + u32(0) + u32(GICR) + u32(0x100000))],
            "FDT": [dict(duplicate_reg=True)],
            "GIC_ABSENT": [dict(gic_compatible=b"arm,gic-v3"), dict(gic_compatible=b"arm,gic-v3x\0")],
            "TIMER_ABSENT": [dict(timer=False)],
            "TIMER_LAYOUT": [dict(ppis=[(0, 1), (1, 2), (1, 3)]), dict(ppis=[(1, 16), (1, 2), (1, 3)]),
                             dict(ppis=[(1, 1), (1, 2)]), dict(ppis=[(1, 3), (1, 2), (1, 3)]),
                             dict(timer_prop="interrupts-extended"), dict(hz=0)],
            "CPUS": [dict(cpus=0), dict(cpus=9)],
        }
        for expected, variants in cases.items():
            for variant in variants:
                with self.subTest(expected=expected, variant=variant):
                    self.assertEqual(self.resolve(holi_shaped(**variant))[0], expected.strip())

    def test_optional_console_is_never_guessed(self):
        for variant in (dict(extra_uarts=1), dict(uart_status=b"disabled\0")):
            name, r = self.resolve(holi_shaped(**variant))
            self.assertEqual((name, r.console_pa, r.console_intid), ("OK", 0, 0))
        name, r = self.resolve(holi_shaped(stride=None, regions=None, hz=None))
        self.assertEqual((name, r.gicr_stride, r.timer_dt_hz), ("OK", STRIDE, 0))

    def test_malformed_and_truncated_trees_fail_closed(self):
        blob = holi_shaped()
        for offset, value in ((0, 0), (4, 2**32 - 1), (8, 2**32 - 4), (8, 42), (12, 2**32 - 1), (20, 16),
                              (24, 18), (32, 2**32 - 1), (36, 2**32 - 4), (36, 8)):
            data = bytearray(blob)
            struct.pack_into(">I", data, offset, value)
            with self.subTest(offset=offset, value=value): self.assertEqual(self.resolve(bytes(data))[0], "FDT")
        for length in (0, 39, 40, len(blob) - 1):
            self.assertEqual(self.resolve(blob, length)[0], "FDT")
        start = struct.unpack_from(">I", blob, 8)[0]
        size = struct.unpack_from(">I", blob, 36)[0]
        refused = 0
        for at in range(start, start + size, 4):  # every structure word, three corruptions each
            for value in (0, 2**32 - 1, 3):
                data = bytearray(blob)
                struct.pack_into(">I", data, at, value)
                refused += self.resolve(bytes(data))[0] != "OK"
        self.assertGreater(refused, size // 4)
        t = Tree()
        deep = b""
        for _ in range(17): deep = Tree.node("n", deep)
        self.assertEqual(self.resolve(t.blob(deep))[0], "DEPTH")
        self.assertEqual(self.lib.ppp_resolve(None, 64, ctypes.byref(Resources())), 1)
        self.assertEqual(self.lib.ppp_resolve(ctypes.create_string_buffer(blob), len(blob), None), 1)

    # ---- GICv3 ------------------------------------------------------------
    def test_non_secure_bring_up_never_touches_group0_or_other_cpus(self):
        m = Model()
        name, gic, _ = self.bring_up(m)
        self.assertEqual((name, gic.ready, gic.single_security, gic.intid_count), ("OK", 1, 0, 640))
        self.assertEqual(gic.gicr, GICR + 3 * STRIDE)
        self.assertEqual(m.faults, [])
        self.assertFalse(m.asleep)
        self.assertEqual((m.sre, m.igrpen1, m.ctlr & 0x13), (1, 1, 0x12))
        for kind, off, value in m.writes:
            if kind == "d":
                self.assertFalse(off == 0 and value & 1, "Group 0 enable must never be written")
                self.assertFalse(0x80 <= off < 0x100 or 0xd00 <= off < 0xd80, "group registers are secure-owned")
            else:
                self.assertEqual(kind, ("r", 3))
        self.assertLessEqual({reg for reg, _ in m.sys_writes}, {SR_SRE, SR_PMR, SR_BPR1, SR_CTLR, SR_IGRPEN1})

    def test_stale_firmware_enables_are_cleared_before_delivery(self):
        m = Model(stale_enabled={27, 40, 300})
        m.pending |= {40}
        name, gic, _ = self.bring_up(m)
        self.assertEqual((name, m.enabled), ("OK", set()))
        self.assertEqual(self.lib.ppp_gic_ack(ctypes.byref(gic)), 1023)

    def test_timer_ppi_is_delivered_as_irq_and_completed_once(self):
        m = Model()
        name, gic, res = self.bring_up(m)
        timer = Timer()
        self.assertEqual(self.lib.ppp_timer_admit(ctypes.byref(res), ctypes.byref(m.io), ctypes.byref(timer)), 0)
        self.assertEqual((timer.frequency_hz, timer.virtual_intid, timer.edge), (HZ, 19, 0))
        # holi-shaped trees say 0xff08: level-low with a GICv2 mask of 0xff.
        self.assertEqual((timer.polarity_low, timer.trigger_defaulted, timer.cpu_mask), (1, 0, 0xff))
        g = ctypes.byref(gic)
        self.assertEqual(self.lib.ppp_gic_configure(g, 19, 0), 0)
        self.assertEqual(self.lib.ppp_gic_enable(g, 19), 0)
        seen = []
        handler = self.HANDLER(lambda _ctx, intid: seen.append((intid, 19 in m.active)))
        self.assertEqual(self.lib.ppp_gic_dispatch(g, handler, None), 0)  # nothing pending
        m.pending.add(19)
        self.assertEqual(self.lib.ppp_gic_dispatch(g, handler, None), 1)
        self.assertEqual((seen, m.active, gic.ack_count, gic.eoi_count, gic.spurious_count), ([(19, True)], set(), 1, 1, 1))
        self.assertEqual(self.lib.ppp_gic_configure(g, 542, 0), 0)
        group_writes = [w for w in m.writes if (w[1] & 0xffff) >> 7 in (1, 0x1a) and w[0] != "u"]
        self.assertEqual(group_writes, [], "IGROUPR/IGRPMODR belong to the secure world when DS == 0")
        self.assertNotIn((SR_EOIR1, 1023), m.sys_writes)
        self.assertEqual(self.lib.ppp_gic_disable(g, 19), 0)
        m.pending.add(19)
        self.assertEqual(self.lib.ppp_gic_dispatch(g, handler, None), 0)

    def test_secure_owned_interrupts_fail_closed(self):
        m = Model(secure={19, 29, 542})
        name, gic, _ = self.bring_up(m)
        g = ctypes.byref(gic)
        self.assertEqual(name, "OK")
        self.assertEqual(self.lib.ppp_gic_configure(g, 19, 0), 0)  # PPI trigger may legitimately be fixed
        self.assertEqual(self.lib.ppp_gic_status_name(self.lib.ppp_gic_enable(g, 19)), b"NOT_OWNED")
        self.assertEqual(self.lib.ppp_gic_status_name(self.lib.ppp_gic_configure(g, 542, 1)), b"NOT_OWNED")
        self.assertEqual(self.lib.ppp_gic_status_name(self.lib.ppp_gic_configure(g, 542, 0)), b"NOT_OWNED")
        self.assertEqual(self.lib.ppp_gic_status_name(self.lib.ppp_gic_enable(g, 542)), b"NOT_OWNED")
        m.pending |= {19, 542}
        self.assertEqual(self.lib.ppp_gic_ack(g), 1023)

    def test_spi_route_trigger_priority_and_self_sgi(self):
        m = Model()
        _, gic, res = self.bring_up(m)
        g = ctypes.byref(gic)
        m.pending.add(res.console_intid)  # stale edge from a previous owner
        self.assertEqual(self.lib.ppp_gic_configure(g, res.console_intid, 1), 0)
        self.assertEqual(self.lib.ppp_gic_enable(g, res.console_intid), 0)
        self.assertEqual((m.route[542], m.prio[542], 542 in m.edge, m.pending), (3 << 8, 0xa0, True, set()))
        self.assertEqual(self.lib.ppp_gic_configure(g, 542, 0), 0)
        self.assertEqual((542 in m.edge, 542 in m.enabled), (False, False))
        self.assertEqual(self.lib.ppp_gic_status_name(self.lib.ppp_gic_self_sgi(g, 1)), b"CONFIG")
        self.assertEqual(self.lib.ppp_gic_configure(g, 1, 0), 0)
        self.assertEqual(self.lib.ppp_gic_enable(g, 1), 0)
        self.assertEqual(self.lib.ppp_gic_self_sgi(g, 1), 0)
        self.assertEqual(self.lib.ppp_gic_ack(g), 1)
        self.lib.ppp_gic_eoi(g, 1)
        self.assertEqual(m.active, set())
        for intid, expected in ((16, b"INTID"), (640, b"INTID"), (1023, b"INTID")):
            self.assertEqual(self.lib.ppp_gic_status_name(self.lib.ppp_gic_self_sgi(g, intid)), expected)
        for call in (self.lib.ppp_gic_enable, self.lib.ppp_gic_disable):
            self.assertEqual(self.lib.ppp_gic_status_name(call(g, 640)), b"INTID")
        self.lib.ppp_gic_eoi(g, 1021)
        self.assertNotIn((SR_EOIR1, 1021), m.sys_writes)
        self.assertEqual(m.faults, [])

    def test_single_security_state_moves_only_configured_interrupts_to_group1(self):
        m = Model(ds=1)
        name, gic, _ = self.bring_up(m)
        g = ctypes.byref(gic)
        self.assertEqual((name, gic.single_security, m.ctlr & 3), ("OK", 1, 2))
        self.assertEqual(self.lib.ppp_gic_configure(g, 19, 0), 0)
        self.assertEqual(self.lib.ppp_gic_enable(g, 19), 0)
        self.assertEqual(m.group, {19})
        m.pending |= {19}
        self.assertEqual(self.lib.ppp_gic_ack(g), 19)

    def test_bring_up_refusals_leave_the_controller_not_ready(self):
        cases = {
            "ARCH": dict(pidr2=0x2b), "ROUTING": dict(are=False, are_fixed_off=True),
            "RWP_TIMEOUT": dict(rwp_stuck=True), "WAKE_TIMEOUT": dict(wake_stuck=True),
            "SRE": dict(sre_denied=True), "DIST_ENABLE": dict(grp1_stuck_off=True),
            "REDIST": dict(vlpis=True),
        }
        for expected, quirk in cases.items():
            with self.subTest(expected=expected):
                m = Model(**quirk)
                name, gic, _ = self.bring_up(m)
                self.assertEqual((name, gic.ready, m.ctlr & 2 and m.igrpen1), (expected, 0, 0))
                # Fail closed: nothing can be delivered on this CPU afterwards.
                self.assertEqual((m.igrpen1, m.pmr, gic.gicr, gic.intid_count), (0, 0, 0, 0))
                if expected == "SRE":  # interface not granted: no further ICC_* access at all
                    self.assertLessEqual({reg for reg, _ in m.sys_writes}, {SR_SRE})
                m.pending |= {19, 40}
                m.enabled |= {19, 40}
                self.assertIsNone(m.deliverable())
                self.assertEqual(self.lib.ppp_gic_ack(ctypes.byref(gic)), 1023)
                self.assertEqual(self.lib.ppp_gic_status_name(self.lib.ppp_gic_enable(ctypes.byref(gic), 19)), b"STATE")
        for quirk in (dict(eoimode_preset=True, eoimode_fixed=True), dict(igrpen_denied=True)):
            m = Model(**quirk)
            self.assertEqual((self.bring_up(m)[0], m.igrpen1, m.pmr, m.ctlr & 2), ("CPUIF", 0, 0, 0))
        self.assertEqual(self.bring_up(Model(pmr_readback=0xa0))[0], "CPUIF")
        # Forwarding that firmware had already enabled is left as found, but
        # the CPU interface is still closed after a refusal.
        m = Model(ctlr_pre=2, wake_stuck=True)
        self.assertEqual((self.bring_up(m)[0], m.ctlr & 2, m.igrpen1, m.sre), ("WAKE_TIMEOUT", 2, 0, 0))
        # Legacy routing with a group already enabled must not be switched.
        self.assertEqual(self.bring_up(Model(are=False, ctlr_pre=2))[0], "ROUTING")
        m = Model(are=False)
        self.assertEqual((self.bring_up(m)[0], m.ctlr & 0x10), ("OK", 0x10))
        # The boot CPU has no redistributor inside the described region, even
        # when firmware forgot GICR_TYPER.Last: the walk stays inside the tree's reg.
        for quirk in (dict(), dict(no_last=True)):
            m = Model(cpu=9, **quirk)
            self.assertEqual((self.bring_up(m)[0], m.faults), ("REDIST", []))
        # Aff3 takes part in the redistributor match (MPIDR[39:32] -> TYPER[63:56]).
        m = Model(aff3=1)
        name, gic, _ = self.bring_up(m)
        self.assertEqual((name, gic.gicr), ("OK", GICR + 3 * STRIDE))
        g = ctypes.byref(gic)
        self.assertEqual(self.lib.ppp_gic_configure(g, 542, 0), 0)
        self.assertEqual(m.route[542], 1 << 32 | 3 << 8)
        m = Model(eoimode_preset=True)
        self.assertEqual((self.bring_up(m)[0], m.icc_ctlr), ("OK", 0))

    def test_wrong_cpu_double_init_and_argument_checks(self):
        m = Model()
        _, gic, res = self.bring_up(m)
        g = ctypes.byref(gic)
        self.assertEqual(self.lib.ppp_gic_status_name(
            self.lib.ppp_gic_init(g, ctypes.byref(res), ctypes.byref(m.io), 8)), b"STATE")
        m.mpidr = (1 << 31) | (5 << 8)  # a different core calls in
        for call in (self.lib.ppp_gic_enable, self.lib.ppp_gic_disable, self.lib.ppp_gic_self_sgi):
            self.assertEqual(self.lib.ppp_gic_status_name(call(g, 1)), b"WRONG_CPU")
        self.assertEqual(self.lib.ppp_gic_status_name(self.lib.ppp_gic_configure(g, 40, 0)), b"WRONG_CPU")
        bad = Resources()
        fresh = Gic()
        self.assertEqual(self.lib.ppp_gic_init(ctypes.byref(fresh), ctypes.byref(bad), ctypes.byref(m.io), 8), 1)
        self.assertEqual(self.lib.ppp_gic_init(ctypes.byref(fresh), ctypes.byref(res), ctypes.byref(Io()), 8), 1)
        self.assertEqual(self.lib.ppp_gic_init(None, ctypes.byref(res), ctypes.byref(m.io), 8), 1)

    # ---- retained run-time FDT provider ------------------------------------
    # Bind behaviour (ABI 2: mapping proof, structure, placement, single
    # assignment) is covered in test_p1_fdt_retention.py.
    def test_provider_object_is_the_symbol_the_native_vm_bootstrap_links_against(self):
        nm = shutil.which("llvm-nm-18") or shutil.which("llvm-nm")
        if not nm: self.skipTest("llvm-nm required")
        obj = Path(self.folder.name) / "provider.o"
        consumer = Path(self.folder.name) / "consumer.o"
        source = Path(self.folder.name) / "consumer.c"
        # Exactly the declaration the native arm_vm_init carries.
        source.write_text('#include "p1_memory_admission.h"\nextern const struct p1_runtime_fdt penang_p1_runtime_fdt;\n'
                          'int probe(void) { return p1_memory_admit(&penang_p1_runtime_fdt, 0, 0, 0); }\n')
        flags = [self.cc, "--target=arm64-apple-darwin", "-std=c11", "-ffreestanding", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ADMISSION), "-c"]
        subprocess.run(flags + [str(TOOLS / "ppp_runtime_fdt.c"), "-o", str(obj)], check=True, capture_output=True)
        subprocess.run(flags + [str(source), "-o", str(consumer)], check=True, capture_output=True)
        symbols = lambda path, mode: subprocess.run([nm, mode, str(path)], check=True, capture_output=True, text=True).stdout
        self.assertIn("_penang_p1_runtime_fdt", symbols(consumer, "--undefined-only"))
        defined = [line.split() for line in symbols(obj, "--defined-only").splitlines() if line.endswith(" _penang_p1_runtime_fdt")]
        self.assertEqual(len(defined), 1)
        self.assertNotIn(defined[0][1], "TtRr", "single-assignment storage must not sit in text or constant data")
        self.assertEqual(symbols(obj, "--undefined-only").split(), [])

    # ---- interface headers --------------------------------------------------
    def test_register_map_matches_the_architecture_values_xnu_uses(self):
        text = (TOOLS / "ppp_gicv3_regs.h").read_text(encoding="utf-8")
        import re
        def value(name):
            shift, literal = re.search(r"#define %s\s+(?:\(1u << (\d+)\)|(0x[0-9a-f]+)u?)" % name, text).groups()
            return 1 << int(shift) if shift else int(literal, 16)
        # Offsets pexpert/pexpert/arm64/VMAPPLE.h states for the same registers (XNU 43a90889).
        expected = {"PPP_GICR_TYPER": 0x08, "PPP_GICR_WAKER": 0x14, "PPP_GICR_TYPER_LAST": 0x10,
                    "PPP_GICR_WAKER_PROCESSORSLEEP": 0x2, "PPP_GICR_WAKER_CHILDRENASLEEP": 0x4,
                    "PPP_GICD_ISENABLER": 0x100, "PPP_GICD_IGROUPR": 0x80, "PPP_GICR_SGI_BASE": 0x10000,
                    "PPP_ICC_CTLR_EOIMODE": 0x2, "PPP_ICC_CTLR_CBPR": 0x1, "PPP_GICD_CTLR_RWP": 0x80000000,
                    "PPP_GICD_IROUTER": 0x6000, "PPP_GICD_ICFGR": 0xc00, "PPP_GICD_IPRIORITYR": 0x400}
        for name, number in expected.items():
            with self.subTest(name=name): self.assertEqual(value(name), number)
        # The header selects no board and no interrupt-group policy.
        for forbidden in ("VMAPPLE", "HAS_GIC_V3", "NO_MONITOR", "APPLEVIRTUALPLATFORM", "ENABLEGRP0"):
            self.assertNotIn("#define " + forbidden, text)
        subprocess.run([self.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only", "-x", "c",
                        str(TOOLS / "ppp_gicv3_regs.h")], check=True, capture_output=True)

    def test_candidate_entitlement_names_stay_inside_the_publicly_evidenced_set(self):
        import re
        text = (TOOLS / "xnu-interfaces/CodeSignature/Entitlements.h").read_text(encoding="utf-8")
        macros = dict(re.findall(r'(?m)^#define (kCS\w+)\s+"([^"]+)"$', text))
        # Keys the public XNU 43a90889 tests grant under each mask
        # (tests/entitlements/hardened-binary-entitlements-{1,2}.plist).
        prefix = "com.apple.%s.web-browser-engine.%s"
        evidenced = {
            "kCSWebBrowserHostEntitlement": {prefix % (k, "host") for k in ("private", "developer")},
            "kCSWebBrowserGPUEntitlement": {prefix % ("private", "gpu"), prefix % ("developer", "gpu"),
                                            prefix % ("developer", "rendering"), prefix % ("developer", "renderer")},
            "kCSWebBrowserNetworkEntitlement": {prefix % (k, n) for k in ("private", "developer") for n in ("network", "networking")},
            "kCSWebBrowserWebContentEntitlement": {prefix % (k, "webcontent") for k in ("private", "developer")},
        }
        self.assertEqual(set(macros), set(evidenced), "exactly the identifiers the two public consumers use")
        for name, key in macros.items():
            self.assertIn(key, evidenced[name])
        self.assertEqual(len(set(macros.values())), 4)
        self.assertIn("#define PENANG_CODESIGNATURE_ENTITLEMENTS_CANDIDATE 1", text)
        self.assertIn("CANDIDATE interface, not Apple's header", text)
        self.assertNotIn("typedef", text)  # declares no type, function or policy result

    # ---- timer trigger flags ------------------------------------------------
    def test_timer_trigger_flags_policy(self):
        timer = Timer()
        m = Model()
        def admit(flags_for_ppi):
            name, res = self.resolve(holi_shaped(flags=flags_for_ppi))
            self.assertEqual(name, "OK")
            return self.lib.ppp_timer_admit(ctypes.byref(res), ctypes.byref(m.io), ctypes.byref(timer))
        accepted = {0x4: (0, 0, 0), 0x8: (1, 0, 0), 0x0: (0, 1, 0), 0xff04: (0, 0, 0xff), 0x0108: (1, 0, 1),
                    0xff00: (0, 1, 0xff)}
        for flags, (low, defaulted, mask) in accepted.items():
            with self.subTest(flags=hex(flags)):
                self.assertEqual(admit({3: flags}), 0)  # PPI 3 is the virtual timer
                self.assertEqual((timer.edge, timer.polarity_low, timer.trigger_defaulted, timer.cpu_mask),
                                 (0, low, defaulted, mask))
        # Edge, mixed trigger bits and reserved bits are refused, never coerced.
        for flags in (0x1, 0x2, 0x3, 0x5, 0x6, 0x9, 0xc, 0xf, 0x10, 0x80, 0x10004, 0x80000008):
            with self.subTest(flags=hex(flags)):
                self.assertEqual(admit({3: flags}), 5)
                self.assertEqual((timer.frequency_hz, timer.virtual_intid), (0, 0), "nothing admitted")
        # Every timer specifier is checked, not only the one in use.
        for ppi in (0, 1, 2):
            self.assertEqual(admit({ppi: 0x5}), 5)

    # ---- timer and console ------------------------------------------------
    def test_timer_frequency_is_adopted_never_invented(self):
        _, res = self.resolve(holi_shaped())
        timer = Timer()
        admit = lambda r, m: self.lib.ppp_timer_admit(ctypes.byref(r), ctypes.byref(m.io), ctypes.byref(timer))
        self.assertEqual((admit(res, Model(cntfrq=0)), timer.frequency_hz, timer.virtual_intid), (3, 0, 0))
        self.assertEqual(admit(res, Model(cntfrq=24000000)), 4)
        _, free = self.resolve(holi_shaped(hz=None))
        self.assertEqual((admit(free, Model(cntfrq=24000000)), timer.frequency_hz), (0, 24000000))
        _, short = self.resolve(holi_shaped())
        short.timer_count = 2
        self.assertEqual(admit(short, Model()), 2)
        self.assertEqual(admit(Resources(), Model()), 1)

    def test_geni_console_is_adopted_read_only_and_bounded(self):
        _, res = self.resolve(holi_shaped())
        for quirk, expected in ((dict(geni_fw=0x0105), 2), (dict(geni_fw=0), 2), (dict(geni_clk=0), 3)):
            m = Model(**quirk)
            uart = Uart()
            self.assertEqual(self.lib.ppp_uart_probe(ctypes.byref(uart), ctypes.byref(res), ctypes.byref(m.io), 16), expected)
            self.assertEqual((uart.ready, m.writes), (0, []), "identification must not write")
            self.assertEqual(self.lib.ppp_uart_putc(ctypes.byref(uart), 65), 5)
        m = Model()
        uart = Uart()
        self.assertEqual(self.lib.ppp_uart_probe(ctypes.byref(uart), ctypes.byref(res), ctypes.byref(m.io), 16), 0)
        self.assertEqual(m.writes, [])
        for byte in b"P1\n":
            self.assertEqual(self.lib.ppp_uart_putc(ctypes.byref(uart), byte), 0)
        self.assertEqual((bytes(m.tx), uart.sent, uart.dropped, m.uart["irq"], m.faults), (b"P1\n", 3, 0, 0, []))
        dead = Model(uart_dead=True)
        self.lib.ppp_uart_probe(ctypes.byref(uart), ctypes.byref(res), ctypes.byref(dead.io), 16)
        self.assertEqual((self.lib.ppp_uart_putc(ctypes.byref(uart), 65), uart.ready, uart.dropped), (4, 0, 1))
        self.assertEqual(self.lib.ppp_uart_putc(ctypes.byref(uart), 65), 5)  # no second stall
        _, none = self.resolve(holi_shaped(extra_uarts=1))
        self.assertEqual(self.lib.ppp_uart_probe(ctypes.byref(uart), ctypes.byref(none), ctypes.byref(m.io), 16), 5)

    # ---- target compilation ----------------------------------------------
    def test_freestanding_arm64_objects_have_no_external_dependencies(self):
        nm = shutil.which("llvm-nm-18") or shutil.which("llvm-nm")
        for target in ("--target=aarch64-none-elf", "--target=arm64-apple-darwin"):
            for source in SOURCES:
                with self.subTest(target=target, source=source):
                    obj = Path(self.folder.name) / (source + target[-6:] + ".o")
                    subprocess.run([self.cc, target, "-std=c11", "-O2", "-ffreestanding", "-fno-builtin",
                                    "-mgeneral-regs-only", "-fno-stack-protector", "-Wall", "-Wextra", "-Werror",
                                    "-I", str(ADMISSION), "-c", str(TOOLS / source), "-o", str(obj)],
                                   check=True, capture_output=True)
                    if nm:
                        undefined = subprocess.run([nm, "-u", str(obj)], check=True, capture_output=True, text=True).stdout.split()
                        self.assertEqual([s for s in undefined if s != "U"], [], "no libc, kernel or MMIO symbol may be required")

    def test_no_physical_address_is_compiled_into_the_sources(self):
        import re
        for source in SOURCES + HEADERS:
            text = (TOOLS / source).read_text(encoding="utf-8")
            masks = {0xd00dfeed, 0xffffff, 0xffffffff, 0xff00ffffff, 0xffff00f0}  # FDT magic and field masks
            text = __import__("re").sub(r"(?s)/\*.*?\*/", "", text)  # comments may cite register offsets
            for literal in re.findall(r"0x[0-9a-fA-F]+", text):
                value = int(literal, 16)
                self.assertTrue(value < 0x00100000 or value in masks, "%s embeds %s" % (source, literal))


if __name__ == "__main__":
    unittest.main()
