#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pure-C SCM MP_ASSIGN candidate tests; no secure call or physical grants."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2] / "tools" / "m12-penang-physical"


def compiler():
    path = shutil.which("clang-18") or shutil.which("clang")
    if not path:
        raise unittest.SkipTest("Clang not available")
    return path


def run(cmd):
    return subprocess.run([str(x) for x in cmd],
                          cwd=SOURCE, text=True, capture_output=True,
                          timeout=45, check=False)


class SCMMPHostCandidate(unittest.TestCase):
    def test_host_wire_shape_and_adversarial_inputs(self):
        with tempfile.TemporaryDirectory(prefix="penang-scm-host-") as folder:
            exe = Path(folder) / "candidate-test"
            cmd = [compiler(), "-std=c11", "-O2", "-Wall", "-Wextra",
                   "-Werror", "-I", SOURCE,
                   SOURCE / "penang_scm_mp_candidate.c",
                   SOURCE / "penang_pmap_lockdown_contract.c",
                   SOURCE / "host_test_scm_mp_candidate.c",
                   "-o", exe]
            cp = run(cmd)
            self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            cp = run([exe])
            self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertIn("PENANG_SCM_MP_HOST_CANDIDATE_PASS", cp.stdout)
            self.assertIn("negatives=13", cp.stdout)
            self.assertIn("physical_authority=BLOCKED no_smc=1", cp.stdout)

    def test_freestanding_aarch64_object_has_no_transport_imports(self):
        with tempfile.TemporaryDirectory(prefix="penang-scm-aarch64-") as folder:
            obj = Path(folder) / "candidate.o"
            cp = run([
                compiler(), "--target=aarch64-none-elf",
                "-std=c11", "-ffreestanding", "-fno-builtin",
                "-fno-stack-protector", "-O2", "-Wall", "-Wextra",
                "-Werror", "-I", SOURCE, "-c",
                SOURCE / "penang_scm_mp_candidate.c", "-o", obj,
            ])
            self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertTrue(obj.is_file())
            nm = shutil.which("llvm-nm-18") or shutil.which("llvm-nm")
            if nm:
                symbols = run([nm, "--undefined-only", obj])
                self.assertEqual(symbols.returncode, 0, symbols.stderr)
                self.assertFalse(symbols.stdout.strip(), symbols.stdout)

    def test_cpp_platformexpert_header_compatibility(self):
        cxx = shutil.which("clang++-18") or shutil.which("clang++")
        if not cxx:
            self.skipTest("Clang C++ not available")
        source = (
            '#include "penang_scm_mp_candidate.h"\n'
            'static_assert(alignof(penang_scm_mp_descriptor) >= 64, "SCM buffer");\n'
            'static_assert(PENANG_SCM_MP_PERM_READ == 4, "SCM R bit");\n'
            'static_assert(PENANG_SCM_MP_PERM_EXEC == 1, "SCM X bit");\n'
            'int (*decoder)(const penang_scm_mp_request *, '
            'penang_scm_mp_descriptor *) = &penang_scm_mp_encode_candidate;\n'
        )
        for target in ([], ["--target=aarch64-none-elf"]):
            with self.subTest(target=target):
                cp = subprocess.run(
                    [cxx, *target, "-std=c++17", "-ffreestanding",
                     "-fno-builtin", "-Werror", "-fsyntax-only", "-x", "c++",
                     "-I", str(SOURCE), "-"],
                    input=source, text=True, capture_output=True,
                    timeout=30, check=False,
                )
                self.assertEqual(cp.returncode, 0, cp.stderr)

    def test_attempted_physical_backend_opt_in_fails_compile(self):
        with tempfile.TemporaryDirectory(prefix="penang-scm-no-dispatch-") as folder:
            for target in ([], ["--target=aarch64-none-elf"]):
                with self.subTest(target=target):
                    obj = Path(folder) / ("target.o" if target else "host.o")
                    cp = run([
                        compiler(), *target, "-std=c11",
                        "-DPENANG_SCM_MP_ENABLE_PHYSICAL_BACKEND=1",
                        "-ffreestanding", "-fno-builtin", "-I", SOURCE,
                        "-c", SOURCE / "penang_scm_mp_candidate.c", "-o", obj
                    ])
                    self.assertNotEqual(cp.returncode, 0)
                    self.assertIn("Physical SCM dispatch is not qualified",
                                  cp.stderr)
                    self.assertFalse(obj.exists())


if __name__ == "__main__":
    unittest.main()
