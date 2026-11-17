# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/m12-penang-physical"
sys.path.insert(0, str(TOOLS))
import p1_pmap_platform as platform

P1 = ["PENANG_P1_DIAGNOSTIC=1", "ARM64_BOARD_CONFIG_PENANG_P1_DIAGNOSTIC=1",
      "PENANG_P1_SANDBOX_COMPILE=1", "PENANG_P1_IOMMU_CODEGEN_ONLY=1", "NO_MONITOR=1"]
# Self-authored compile oracle. No replacement function definitions exist.
FIXTURE = platform.INCLUDES + "void entry(void) {\n\tuat_bootstrap();\n\tsart_bootstrap();\n}\n"


class PmapPlatform(unittest.TestCase):
    def setUp(self):
        self.cc = shutil.which("clang")
        self.nm = shutil.which("llvm-nm") or shutil.which("nm")
        if not self.cc or not self.nm:
            self.skipTest("Clang and nm required")
        self.temp = tempfile.TemporaryDirectory(prefix="p1-pmap-platform-")
        self.root = Path(self.temp.name)
        (self.root/"penang_p1").mkdir()
        shutil.copy(TOOLS/"p1_pmap_platform.h",self.root/"penang_p1")
        self.source = self.root/"consumer.c"
        self.source.write_text(platform.select(FIXTURE))

    def tearDown(self):
        if hasattr(self, "temp"):
            self.temp.cleanup()

    def compile(self, macros):
        (self.root/"consumer.o").unlink(missing_ok=True)
        return subprocess.run([self.cc,"-Werror=implicit-function-declaration","-I",str(self.root),
            *["-D"+m for m in macros],"-c",str(self.source),"-o",str(self.root/"consumer.o")],
            capture_output=True,text=True)

    def test_default_requires_original_headers_and_hardware_providers(self):
        self.assertIn("arm64/ppl/sart.h",self.compile([]).stderr)
        (self.root/"arm64/ppl").mkdir(parents=True)
        (self.root/"arm64/ppl/sart.h").write_text("void sart_bootstrap(void);\n")
        (self.root/"arm64/ppl/uat.h").write_text("void uat_bootstrap(void);\n")
        for flags in [[],["NO_MONITOR=1"],["VMAPPLE=1","NO_MONITOR=1"],
                      ["PMAP_CS=1","PMAP_CS_ENABLE=1","XNU_MONITOR=1"]]:
            with self.subTest(default_flags=flags):
                self.assertEqual(self.compile(flags).returncode,0)
                nm = subprocess.check_output([self.nm,"-u",str(self.root/"consumer.o")],text=True)
                self.assertIn("sart_bootstrap",nm)
                self.assertIn("uat_bootstrap",nm)

    def test_complete_codegen_selection_has_no_fake_provider_definition(self):
        result = self.compile(P1)
        self.assertEqual(result.returncode,0,result.stderr)
        nm = subprocess.check_output([self.nm,str(self.root/"consumer.o")],text=True)
        self.assertIn("entry",nm)
        self.assertNotIn("sart_bootstrap",nm)
        self.assertNotIn("uat_bootstrap",nm)
        self.assertEqual(self.compile(P1+["CONFIG_PMAP_PPL=0","CONFIG_SPTM=0","XNU_MONITOR=0",
                                         "PMAP_CS=0","PMAP_CS_ENABLE=0","HAS_GUARDED_IO_FILTER=0"]).returncode,0)

    def test_partial_or_disabled_optins_are_refused(self):
        for macro in P1:
            with self.subTest(missing=macro):
                self.assertNotEqual(self.compile([m for m in P1 if m!=macro]).returncode,0)
        for name in [m.split("=")[0] for m in P1]:
            for value in ["0","2","-1", "", "invalid"]:
                with self.subTest(optin=name,value=value):
                    flags=[m for m in P1 if not m.startswith(name+"=")]+[name+"="+value]
                    self.assertNotEqual(self.compile(flags).returncode,0)
        for macro in P1[:-1]:
            self.assertNotEqual(self.compile([macro]).returncode,0)

    def test_protected_or_other_platform_cannot_select_p1(self):
        for macro in ["ARM64_BOARD_CONFIG_PENANG_PHYSICAL", "PENANG_P2_PROTECTED", "QEMU_VIRT",
                      "VMAPPLE", "APPLEVIRTUALPLATFORM", "__arm64e__", "HAS_APPLE_PAC",
                      "CONFIG_PMAP_PPL", "CONFIG_SPTM", "XNU_MONITOR",
                      "KERNEL_INTEGRITY_KTRR", "KERNEL_INTEGRITY_CTRR", "P1_PMAP_APPLE_IO_BOOTSTRAP=0",
                      "PENANG_PHYSICAL", "MONITOR", "PMAP_CS", "PMAP_CS_ENABLE", "KERNEL_INTEGRITY_WT",
                      "HAS_GUARDED_IO_FILTER", "ARM64_BOARD_CONFIG_QEMU_VIRT", "__ARM_FEATURE_PAUTH",
                      "CPU_HAS_APPLE_PAC", "HAS_PARAVIRTUALIZED_PAC", "APPLE_ARM64_ARCH_FAMILY",
                      "PENANG_B43_VERIFIED_PHYSICAL_LOCKDOWN"]:
            with self.subTest(macro=macro):
                self.assertNotEqual(self.compile(P1+[macro if "=" in macro else macro+"=1"]).returncode,0)

    def test_presence_based_markers_remain_forbidden_when_zero(self):
        for name in ["PENANG_PHYSICAL","ARM64_BOARD_CONFIG_PENANG_PHYSICAL","PENANG_P2_PROTECTED",
                     "MONITOR","KERNEL_INTEGRITY_WT"]:
            with self.subTest(marker=name):
                self.assertNotEqual(self.compile(P1+[name+"=0"]).returncode,0)

    def test_actual_protected_header_still_refuses_no_monitor_in_both_orders(self):
        shutil.copy(TOOLS/"PENANG_PHYSICAL.h",self.root/"PENANG_PHYSICAL.h")
        for first,second in [("PENANG_PHYSICAL.h","penang_p1/p1_pmap_platform.h"),
                             ("penang_p1/p1_pmap_platform.h","PENANG_PHYSICAL.h")]:
            self.source.write_text('#include "'+first+'"\n#include "'+second+'"\nint selection=P1_PMAP_APPLE_IO_BOOTSTRAP;\n')
            self.assertEqual(self.compile([]).returncode,0)
            for flags in [P1,["NO_MONITOR=0"],["NO_MONITOR=1"]]:
                result=self.compile(flags)
                self.assertNotEqual(result.returncode,0)
                self.assertIn("PENANG_PHYSICAL forbids silent NO_MONITOR",result.stderr)

    def test_guard_text_is_preserved_and_source_drift_rejected(self):
        guard='#if defined(PENANG_PHYSICAL)\n#error B42_PROTECTED\n#endif\n'
        result=platform.select(guard+FIXTURE)
        self.assertTrue(result.startswith(guard))
        for source in [FIXTURE+platform.INCLUDES, FIXTURE.replace("\tuat_bootstrap();\n", ""),
                       FIXTURE+"\tsart_bootstrap();\n"]:
            with self.subTest(source=source):
                self.assertRaises(ValueError,platform.select,source)
        path=self.root/platform.SOURCE
        path.parent.mkdir(parents=True)
        path.write_text(FIXTURE)
        self.assertRaisesRegex(ValueError,"SHA mismatch",platform.render,self.root)
        path.unlink()
        path.symlink_to(self.source)
        self.assertRaisesRegex(ValueError,"symlink",platform.render,self.root)


if __name__ == "__main__":
    unittest.main()
