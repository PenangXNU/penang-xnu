# SPDX-License-Identifier: Apache-2.0
"""Profiling-off compiles; profiling-on still demands the real dependencies."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/m12-penang-physical"))
try:
    import p1_optional_profiling as M
finally:
    sys.path.pop(0)


class OptionalProfiling(unittest.TestCase):
    def test_off_configuration_and_enabled_missing_dependencies_with_real_clang(self):
        cc = shutil.which("clang")
        self.assertIsNotNone(cc, "Clang required")
        fixtures = {
            "osfmk/kern/kpc.h": "typedef unsigned int uint32_t;\n#define KPC_MAX_COUNTERS CORE_NCTRS\n" + M.ANCHORS["osfmk/kern/kpc.h"][0],
            "osfmk/kern/ast.c": "struct thread { int base; };\nvoid f(struct thread *thread) {\n" + M.ANCHORS["osfmk/kern/ast.c"][0] + "}\n",
            "osfmk/kern/processor.c": "void quantum(void);\nenum { RUNNING_TIMER_QUANTUM };\nvoid (*timers[])(void) = { [RUNNING_TIMER_QUANTUM] = quantum,\n" + M.ANCHORS["osfmk/kern/processor.c"][0] + "};\n",
        }
        for name, source in fixtures.items():
            for flag in (0, 1):
                r = subprocess.run([cc, "--target=arm64-apple-darwin", "-x", "c", "-fsyntax-only", "-nostdinc",
                                    "-Werror=implicit-function-declaration", "-DCONFIG_CPU_COUNTERS="+str(flag),
                                    "-DKPERF="+str(flag), "-"], input=M.rewrite(name, source), text=True, capture_output=True)
                self.assertEqual(r.returncode == 0, flag == 0, (name, flag, r.stderr))
        name = "osfmk/arm64/pcb.c"
        self.assertEqual(M.rewrite(name, M.ANCHORS[name][0]+"int kept;\n"), "int kept;\n")

    def test_pins_drift_symlinks_and_security_scope(self):
        with tempfile.TemporaryDirectory() as folder:
            root, pins = Path(folder), {}
            for name in M.PINS:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(M.ANCHORS[name][0])
                pins[name] = M.sha(path.read_bytes())
                for bad in ("", path.read_text()*2):
                    with self.assertRaises(ValueError): M.rewrite(name, bad)
            with self.assertRaises(ValueError): M.render(root)
            with mock.patch.dict(M.PINS, pins):
                patch, report = M.render(root)
                self.assertFalse(report["security_contract_changes"])
                for forbidden in (b"pmap.c", b"arm_vm_init.c", b"NO_MONITOR", b"CONFIG_KERNEL_INTEGRITY", b"CORE_NCTRS="):
                    self.assertNotIn(forbidden, patch)
                path = root / next(iter(M.PINS))
                path.unlink()
                path.symlink_to(ROOT / "AGENTS.md")
                with self.assertRaises(ValueError): M.render(root)
