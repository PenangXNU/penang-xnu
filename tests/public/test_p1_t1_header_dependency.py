# SPDX-License-Identifier: Apache-2.0
"""TrustCache dependency remains mandatory in the active real monitor branch."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/m12-penang-physical"
sys.path.insert(0, str(TOOLS))
try:
    import p1_t1_header_dependency as MOD
finally:
    sys.path.pop(0)

# Public selector/include context only. No TrustCache API or substitute exists.
FIXTURE = ("/* synthetic dependency context */\n" + MOD.API + "\n" + MOD.SELECTOR + "\n"
           + MOD.BLOCK + "\n/* dependent declarations retained here */\n#endif\n")


class T1HeaderDependency(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix="penang-t1-header-")
        self.addCleanup(folder.cleanup)
        self.temp = Path(folder.name)
        self.root = self.temp / "source"
        self.path = self.root / MOD.SOURCE
        self.path.parent.mkdir(parents=True)
        self.path.write_text(FIXTURE)

    def preprocess(self, text, monitor=0, sptm=0):
        cc = shutil.which("clang-18") or shutil.which("clang")
        if not cc:
            self.skipTest("Clang required for actual preprocessor dependency checks")
        return subprocess.run([cc, "--target=arm64-apple-darwin", "-E", "-x", "c", "-nostdinc",
                               "-DXNU_MONITOR=" + str(monitor), "-DCONFIG_SPTM=" + str(sptm), "-"],
                              input=text, capture_output=True, text=True, timeout=30)

    def test_inactive_monitor_drops_only_its_unused_dependency(self):
        result = self.preprocess(MOD.relocate(FIXTURE))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotEqual(self.preprocess(FIXTURE).returncode, 0)

    def test_active_ppl_monitor_still_requires_real_api(self):
        result = self.preprocess(MOD.relocate(FIXTURE), monitor=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("TrustCache/API.h", result.stderr)
        self.assertIn("file not found", result.stderr)

    def test_existing_sptm_selector_retains_priority(self):
        result = self.preprocess(MOD.relocate(FIXTURE), monitor=1, sptm=1)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_mutant_unconditional_and_disabled_includes_break_expected_behavior(self):
        updated = MOD.relocate(FIXTURE)
        unconditional = MOD.API + updated.replace(MOD.API, "", 1)
        self.assertNotEqual(self.preprocess(unconditional).returncode, 0)
        disabled = updated.replace(MOD.BLOCK, "#if 0\n", 1)
        self.assertEqual(self.preprocess(disabled, monitor=1).returncode, 0)
        self.assertNotEqual(self.preprocess(updated, monitor=1).returncode, 0)

    def test_exact_delta_changes_one_include_scope_and_preserves_contents(self):
        updated = MOD.relocate(FIXTURE)
        self.assertEqual(updated.count(MOD.API), 1)
        self.assertEqual(updated.count(MOD.SELECTOR), 1)
        self.assertEqual(updated.replace(MOD.BLOCK + "\n" + MOD.API, MOD.BLOCK).replace(
            "/* synthetic dependency context */\n", "/* synthetic dependency context */\n" + MOD.API, 1), FIXTURE)
        with mock.patch.object(MOD, "SOURCE_SHA", MOD.sha(FIXTURE.encode())):
            patch, report = MOD.render(self.root)
        self.assertEqual(patch.count(b"--- a/"), 1)
        self.assertEqual(report["changed_files"], [MOD.SOURCE])
        self.assertEqual(report["trustcache_provider"], "NOT_PROVIDED")
        for forbidden in (b"startup.c", b"arm_vm_init.c", b"arm/pmap/pmap.c", b"NO_MONITOR"):
            self.assertNotIn(forbidden, patch)
        self.assertEqual(self.path.read_text(), FIXTURE)

    def test_source_pin_drift_and_anchor_changes_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "SHA mismatch"):
            MOD.render(self.root)
        for source in (FIXTURE.replace(MOD.API, ""), FIXTURE + MOD.API,
                       FIXTURE.replace(MOD.BLOCK, "#if OTHER_MONITOR\n")):
            with self.subTest(source=source[-40:]), self.assertRaises(ValueError):
                MOD.relocate(source)

    def test_source_traversal_and_output_overwrite_are_refused(self):
        self.path.unlink()
        outside = self.temp / "outside.h"
        outside.write_text(FIXTURE)
        self.path.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "escapes"):
            MOD.render(self.root)
        output = self.temp / "patch.diff"
        MOD.write_exclusive(output, b"original", (self.root,))
        with self.assertRaises(FileExistsError):
            MOD.write_exclusive(output, b"overwritten", (self.root,))
        self.assertEqual(output.read_bytes(), b"original")


if __name__ == "__main__":
    unittest.main()
