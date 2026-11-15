#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Public-only regression tests for the B4.2 native source-selection guard.

Tests cannot establish Qualcomm SCM, Stage-2, SMMU, PMAP or device readiness.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[2] / "tools" / "m12-penang-physical"


class B42NativeSelectionTests(unittest.TestCase):
    def test_original_b42_adversarial_matrix(self):
        cp = subprocess.run(
            [sys.executable, "-B", str(TOOLS / "test_verify_b42_pmap_lockdown.py")],
            cwd=TOOLS, capture_output=True, text=True, timeout=40, check=False,
        )
        self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
        self.assertIn(
            "B42_PMAP_LOCKDOWN_VERIFIER_TESTS_PASS total=12 negatives=10 device_gate=BLOCKED",
            cp.stdout,
        )

    def test_board_header_preprocessor_rejects_unsafe_profiles(self):
        compiler = shutil.which("clang")
        if compiler is None:
            self.skipTest("clang is not installed in this host environment")
        source = '#include "PENANG_PHYSICAL.h"\nint main(void) { return 0; }\n'
        base = [compiler, "--target=aarch64-linux-gnu", "-x", "c", "-std=c11",
                "-fsyntax-only", "-I", str(TOOLS)]
        ok = subprocess.run(base + ["-"], input=source, text=True,
                            capture_output=True, timeout=30, check=False)
        self.assertEqual(ok.returncode, 0, ok.stderr)
        for macro in ("NO_MONITOR", "XNU_MONITOR", "CONFIG_SPTM",
                      "QEMU_VIRT", "PENANG_B43_VERIFIED_PHYSICAL_LOCKDOWN",
                      "__arm64e__"):
            with self.subTest(forbidden_macro=macro):
                cp = subprocess.run(base + ["-D" + macro + "=1", "-"],
                                    input=source, text=True, capture_output=True,
                                    timeout=30, check=False)
                self.assertNotEqual(cp.returncode, 0, macro + ": not rejected")
                self.assertIn("error:", cp.stderr.lower())


if __name__ == "__main__":
    unittest.main()
