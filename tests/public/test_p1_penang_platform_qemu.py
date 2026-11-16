# SPDX-License-Identifier: Apache-2.0
"""Opt-in QEMU `virt` run of the unmodified P1 platform services at EL1.

Never runs by default: starting an emulator needs the explicit permission
this project requires, expressed as PENANG_ALLOW_QEMU=1. It proves delivery
on QEMU's GICv3 model (both security configurations), not on SM4350 silicon,
and it is not an XNU run."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/p1-penang-platform"
CFLAGS = ["--target=aarch64-none-elf", "-mcpu=cortex-a76", "-mgeneral-regs-only", "-ffreestanding",
          "-fno-builtin", "-fno-stack-protector", "-fno-pic", "-O2", "-Wall", "-Wextra", "-Werror", "-std=gnu11"]


def tool(*names):
    for name in names:
        path = shutil.which(name)
        if path: return path
    return None


@unittest.skipUnless(os.environ.get("PENANG_ALLOW_QEMU") == "1", "QEMU runs are opt-in: set PENANG_ALLOW_QEMU=1")
class PenangPlatformQemu(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc, cls.ld = tool("clang-18", "clang"), tool("ld.lld-18", "ld.lld")
        cls.objcopy, cls.qemu = tool("llvm-objcopy-18", "llvm-objcopy"), tool("qemu-system-aarch64")
        if not all((cls.cc, cls.ld, cls.objcopy, cls.qemu)):
            raise unittest.SkipTest("clang, ld.lld, llvm-objcopy and qemu-system-aarch64 required")
        cls.folder = tempfile.TemporaryDirectory(prefix="p1-platform-qemu-")
        out = Path(cls.folder.name)
        objects = []
        for source in ("qemu/head.S", "qemu/harness.c", "ppp_resources.c", "ppp_gicv3.c", "ppp_timer_uart.c"):
            obj = out / (Path(source).stem + ".o")
            subprocess.run([cls.cc, *CFLAGS, "-c", str(TOOLS / source), "-o", str(obj)], check=True, capture_output=True)
            objects.append(str(obj))
        subprocess.run([cls.ld, "-T", str(TOOLS / "qemu/link.lds"), "-nostdlib", *objects, "-o", str(out / "harness.elf")],
                       check=True, capture_output=True)
        cls.image = out / "harness.img"
        subprocess.run([cls.objcopy, "-O", "binary", str(out / "harness.elf"), str(cls.image)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()

    def run_guest(self, machine, cpus):
        result = subprocess.run(
            [self.qemu, "-machine", machine, "-cpu", "cortex-a76", "-smp", str(cpus), "-m", "256", "-nic", "none",
             "-display", "none", "-serial", "none", "-monitor", "none",
             "-semihosting-config", "enable=on,target=native", "-kernel", str(self.image)],
            capture_output=True, text=True, timeout=120)
        values = dict(line.split()[1].split("=", 1) for line in (result.stdout + result.stderr).splitlines() if line.startswith("PPP_QEMU "))
        return result.returncode, {k: (int(v, 16) if v.startswith("0x") else v) for k, v in values.items()}

    def test_group1_irq_delivery_in_both_security_configurations(self):
        # secure=on gives non-secure EL1 the two-security-state view (GICD_CTLR.DS == 0).
        for machine, ds in (("virt,gic-version=3", 1), ("virt,gic-version=3,secure=on", 0)):
            for cpus in (1, 4):
                with self.subTest(machine=machine, cpus=cpus):
                    code, v = self.run_guest(machine, cpus)
                    self.assertEqual((code, v.get("result")), (0, "PASS"), v)
                    self.assertEqual((v["current_el"], v["gicd_ctlr_ds"], v["cpus"]), (1, ds, cpus))
                    # The timer interrupt number comes from the emulator's tree (27 there, 19 on holi).
                    self.assertEqual(v["virtual_timer_intid"], 27)
                    self.assertEqual((v["irqs_before_enable"], v["sgi_delivered"], v["timer_delivered"]), (0, 1, 1))
                    # Disabled at the controller: the source asserts, nothing is delivered.
                    self.assertEqual((v["timer_after_disable"], v["istatus_while_disabled"]), (1, 1))
                    self.assertEqual((v["acks"], v["eois"], v["unexpected_intids"]), (2, 2, 0))
                    self.assertEqual(v["timer_hz"], 62500000)

    def test_gicv2_machine_is_refused_before_any_controller_write(self):
        code, v = self.run_guest("virt,gic-version=2", 1)
        self.assertEqual(code, 10, v)  # no arm,gic-v3 node: resolution fails, GIC never touched
        self.assertNotIn("gic_init_status", v)


if __name__ == "__main__":
    unittest.main()


@unittest.skipUnless(os.environ.get("PENANG_ALLOW_QEMU") == "1", "QEMU runs are opt-in: set PENANG_ALLOW_QEMU=1")
class RetainedFdtQemu(unittest.TestCase):
    """The retained-FDT bind with the MMU on: 16-KiB granule, TTBR1 kernel
    window at XNU's virtual base, translation proven with AT S1E1R."""
    SLOT = 0x48000000 + 0x03100000 + 0x10000 + 0x4000

    @classmethod
    def setUpClass(cls):
        cls.cc, cls.ld = tool("clang-18", "clang"), tool("ld.lld-18", "ld.lld")
        cls.objcopy, cls.qemu = tool("llvm-objcopy-18", "llvm-objcopy"), tool("qemu-system-aarch64")
        if not all((cls.cc, cls.ld, cls.objcopy, cls.qemu)):
            raise unittest.SkipTest("clang, ld.lld, llvm-objcopy and qemu-system-aarch64 required")
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import test_p1_fdt_retention as fixtures
        cls.folder = tempfile.TemporaryDirectory(prefix="p1-fdt-qemu-")
        out = Path(cls.folder.name)
        objects = []
        admission = ROOT / "tools/m12-penang-physical"
        for source in (TOOLS / "qemu/head.S", TOOLS / "qemu/fdt_harness.c", TOOLS / "ppp_runtime_fdt.c",
                       TOOLS / "ppp_resources.c", admission / "p1_memory_admission.c"):
            obj = out / (source.stem + ".o")
            subprocess.run([cls.cc, *CFLAGS, "-I", str(admission), "-I", str(TOOLS), "-c", str(source), "-o", str(obj)],
                           check=True, capture_output=True)
            objects.append(str(obj))
        subprocess.run([cls.ld, "-T", str(TOOLS / "qemu/link.lds"), "-nostdlib", *objects, "-o", str(out / "fdt.elf")],
                       check=True, capture_output=True)
        cls.image = out / "fdt.img"
        subprocess.run([cls.objcopy, "-O", "binary", str(out / "fdt.elf"), str(cls.image)], check=True, capture_output=True)
        blob = fixtures.penang_tree(ram=(0x40000000, 0x40000000), extra_root=fixtures.Tree.node(
            "filler", fixtures.Tree().prop("data", bytes(3 * 0x4000))))
        cls.tree = out / "tree.dtb"
        cls.tree.write_bytes(blob)
        cls.length = len(blob)

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()

    def run_guest(self, scenario):
        result = subprocess.run(
            [self.qemu, "-machine", "virt,gic-version=3", "-cpu", "cortex-a76", "-m", "1024", "-nic", "none",
             "-display", "none", "-serial", "none", "-monitor", "none",
             "-semihosting-config", "enable=on,target=native", "-kernel", str(self.image),
             "-device", "loader,file=%s,addr=0x%x,force-raw=on" % (self.tree, self.SLOT),
             "-device", "loader,addr=0x4fff0000,data=%d,data-len=8" % scenario,
             "-device", "loader,addr=0x4fff0008,data=%d,data-len=8" % self.length],
            capture_output=True, text=True, timeout=120)
        values = dict(line.split()[1].split("=", 1) for line in (result.stdout + result.stderr).splitlines()
                      if line.startswith("PPP_FDT "))
        return result.returncode, {k: (int(v, 16) if v.startswith("0x") else v) for k, v in values.items()}

    def test_bind_through_a_real_mmu(self):
        code, v = self.run_guest(0)
        self.assertEqual((code, v.get("result")), (0, "PASS"), v)
        self.assertEqual((v["mmu"], v["bind_status"], v["descriptor_pa"]), (1, 0, self.SLOT))
        self.assertEqual(v["descriptor_va"], 0xfffffe0004000000 + (self.SLOT - 0x48000000))
        self.assertEqual((v["admit_status"], v["resolve_status"], v["virtual_timer_intid"], v["gicd"]),
                         (0, 0, 19, 0x0f200000))

    def test_unproven_mappings_are_refused_without_a_fault(self):
        # 9 = UNMAPPED, 3 = WINDOW. A load from an unproven address would have
        # taken a synchronous abort (exit 3 with unexpected_vector).
        for scenario, expected in ((1, 9), (2, 9), (3, 9), (4, 3)):
            with self.subTest(scenario=scenario):
                code, v = self.run_guest(scenario)
                self.assertNotIn("unexpected_vector", v, v)
                self.assertEqual((code, v["bind_status"], v["descriptor_untouched"]), (0, expected, 1), v)
                self.assertEqual((v["retry_status"], v["second_bind"]), (0, 2), "refusal did not consume the bind")
