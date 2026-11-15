# SPDX-License-Identifier: Apache-2.0
"""Native cache selector tests; no PMAP/provider qualification."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("p1_cache_geometry", ROOT / "tools/m12-penang-physical/p1_cache_geometry.py")
MOD = importlib.util.module_from_spec(spec)
spec.loader.exec_module(MOD)


class CacheGeometry(unittest.TestCase):
    def test_native_branch_preserves_other_targets_and_unknown_rejection(self):
        cc = shutil.which("clang")
        if not cc:
            self.skipTest("clang unavailable")
        source = ("#if defined(OTHER_TARGET)\n#define MMU_I_CLINE 7\n#define MMU_CLINE 7\n"
                  + MOD.ANCHOR + "#define MMU_I_CLINE 8\n#define MMU_CLINE 8\n"
                  + "#else\n#error processor not supported\n#endif\n")
        native = MOD.insert_branch(source)
        for target, expected in (("ARM64_BOARD_CONFIG_PENANG_PHYSICAL", "6"),
                                 ("OTHER_TARGET", "7"), ("VMAPPLE", "8")):
            with self.subTest(target=target):
                result = subprocess.run([cc, "--target=arm64-apple-darwin", "-E", "-dM", "-x", "c",
                                         "-D" + target, "-"], input=native, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("#define MMU_CLINE " + expected, result.stdout)
                self.assertIn("#define MMU_I_CLINE " + expected, result.stdout)
                if target.startswith("ARM64_BOARD"):
                    self.assertNotIn("#define VMAPPLE", result.stdout)
                    self.assertNotIn("#define NO_MONITOR", result.stdout)
        result = subprocess.run([cc, "-E", "-x", "c", "-"], input=native, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("processor not supported", result.stderr)

    def test_anchor_drift_and_repeat_patch_are_refused(self):
        for source in ("", MOD.ANCHOR * 2, MOD.BRANCH + MOD.ANCHOR):
            with self.subTest(source=source), self.assertRaises(ValueError):
                MOD.insert_branch(source)

    def test_wrong_upstream_bytes_and_escaping_source_are_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / MOD.SOURCE
            source.parent.mkdir(parents=True)
            source.write_text(MOD.ANCHOR)
            with self.assertRaisesRegex(ValueError, "SHA mismatch"):
                MOD.render(root)
            source.unlink()
            with tempfile.NamedTemporaryFile() as external:
                source.symlink_to(external.name)
                with self.assertRaisesRegex(ValueError, "escapes"):
                    MOD.render(root)
