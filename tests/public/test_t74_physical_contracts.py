#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original T74 C contract tests, HOST_ONLY; never a device PMAP provider.

Physical reservations, Stage-2 grants, DMA/SMMU, SCM and actual hardware
lockdown remain UNVERIFIED. Synthetic host mode must not authorize a device.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tools" / "m12-penang-physical"


def clang():
    compiler = shutil.which("clang-18") or shutil.which("clang")
    if compiler is None:
        raise unittest.SkipTest("Clang missing on host")
    return compiler


def invoke(args, *, timeout=40):
    return subprocess.run(
        [str(x) for x in args], cwd=SOURCE, text=True,
        capture_output=True, timeout=timeout, check=False
    )


class T74PhysicalContracts(unittest.TestCase):
    def test_b42_lockdown_default_and_synthetic_host_model(self):
        with tempfile.TemporaryDirectory(prefix="penang-t74-b42-") as tmp:
            for simulated in (False, True):
                with self.subTest(synthetic_host=simulated):
                    executable = Path(tmp) / ("model" if simulated else "blocked")
                    command = [
                        clang(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I", SOURCE, SOURCE / "penang_pmap_lockdown_contract.c",
                        SOURCE / "host_test_pmap_lockdown_contract.c",
                        "-o", executable,
                    ]
                    if simulated:
                        command.insert(1, "-DB42_HOST_MODEL_SYNTHETIC_BACKEND=1")
                    cp = invoke(command)
                    self.assertEqual(cp.returncode, 0, cp.stderr)
                    completed = invoke([executable])
                    self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
                    self.assertRegex(
                        completed.stdout,
                        r"B42_PMAP_LOCKDOWN_CONTRACT_TESTS_PASS total=\d+ negatives=\d+ backend_verified=" +
                        ("1" if simulated else "0"),
                    )
                    numbers = re.search(r"total=(\d+) negatives=(\d+)", completed.stdout)
                    self.assertIsNotNone(numbers)
                    self.assertGreaterEqual(int(numbers.group(1)), 33)
                    self.assertGreaterEqual(int(numbers.group(2)), 30)

    def test_stage1_host_transition_invariants(self):
        with tempfile.TemporaryDirectory(prefix="penang-t74-stage1-") as tmp:
            executable = Path(tmp) / "t2"
            cp = invoke([
                clang(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", SOURCE, SOURCE / "t2_stage1.c",
                SOURCE / "test_t2_stage1.c", "-o", executable
            ])
            self.assertEqual(cp.returncode, 0, cp.stderr)
            result = invoke([executable])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("T2_STAGE1_HOST_PASS", result.stdout)

    def test_original_platform_expert_contract_host_only(self):
        with tempfile.TemporaryDirectory(prefix="penang-t74-platform-") as tmp:
            executable = Path(tmp) / "platform"
            cp = invoke([
                clang(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", SOURCE, SOURCE / "penang_physical_contract.c",
                SOURCE / "host_test_penang_physical.c", "-o", executable
            ])
            self.assertEqual(cp.returncode, 0, cp.stderr)
            result = invoke([executable])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertRegex(
                result.stdout,
                r"M12_2_PHYSICAL_PLATFORM_CONTRACT_HOST_PASS total=\d+ negatives=\d+ no_MMIO=1",
            )

    def test_host_synthetic_backend_must_not_compile_on_aarch64(self):
        # AArch64 compile rejection is a safety gate, not an execution test.
        with tempfile.TemporaryDirectory(prefix="penang-t74-cross-") as tmp:
            obj = Path(tmp) / "cannot-promote.o"
            cp = invoke([
                clang(), "--target=aarch64-none-elf", "-std=c11",
                "-DB42_HOST_MODEL_SYNTHETIC_BACKEND=1", "-I", SOURCE,
                "-c", SOURCE / "penang_pmap_lockdown_contract.c", "-o", obj
            ])
            self.assertNotEqual(cp.returncode, 0, "Synthetic host backend cross-compiled on ARM64")
            self.assertIn("error:", cp.stderr.lower())
            self.assertFalse(obj.exists())


if __name__ == "__main__":
    unittest.main()
