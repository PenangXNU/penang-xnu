# SPDX-License-Identifier: Apache-2.0
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/m12-penang-physical"))
spec = importlib.util.spec_from_file_location("optional", ROOT / "tools/m12-penang-physical/p1_optional_headers.py")
MOD = importlib.util.module_from_spec(spec)
spec.loader.exec_module(MOD)


class OptionalHeaders(unittest.TestCase):
    def test_optional_pmu_off_compiles_but_enabled_still_requires_real_header(self):
        cc = shutil.which("clang")
        if not cc: self.skipTest("Clang required")
        src = "#include <kern/kpc.h>\nint bootstrap_fixture;\n"
        for flag, expected in (("0", 0), ("1", 1)):
            for name in MOD.KPC_FILES:
                result = subprocess.run([cc, "-x", "c", "-fsyntax-only", "-nostdinc",
                                         "-DCONFIG_CPU_COUNTERS=" + flag, "-"],
                                        input=MOD.rewrite(name, src), text=True, capture_output=True)
                self.assertEqual(result.returncode != 0, expected != 0)

    def test_only_include_lines_change_and_drift_is_rejected(self):
        for name in set(MOD.PINS) - MOD.KPC_FILES:
            source = "#include <pexpert/arm64/apple_arm64_cpu.h>\nint existing_function(void) { return 7; }\n"
            self.assertEqual(MOD.rewrite(name, source), "int existing_function(void) { return 7; }\n")
            for bad in ("", source + source):
                with self.assertRaises(ValueError): MOD.rewrite(name, bad)

    def test_pins_and_escaping_sources_are_rejected_and_no_guard_is_emitted(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            pins = {}
            for name in MOD.PINS:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                include = "#include <kern/kpc.h>\n" if name in MOD.KPC_FILES else "#include <pexpert/arm64/apple_arm64_cpu.h>\n"
                path.write_text(include + "void preserved(void) {}\n")
                pins[name] = MOD.sha(path.read_bytes())
            with self.assertRaises(ValueError): MOD.render(root)
            with mock.patch.dict(MOD.PINS, pins):
                patch, report = MOD.render(root)
                self.assertFalse(report["security_contract_changes"])
                for word in (b"pmap.c", b"arm_vm_init.c", b"NO_MONITOR", b"trust_cache_runtime_init"):
                    self.assertNotIn(word, patch)
                path = root / next(iter(MOD.PINS))
                path.unlink();path.symlink_to(ROOT / "AGENTS.md")
                with self.assertRaises(ValueError): MOD.render(root)
