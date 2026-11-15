#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Synthetic negative tests for the physical XNU source entry frontier.

Proves no guard can be lost without failing this preflight; no guest/device run.
"""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
P = ROOT / "tools" / "m12-penang-physical" / "p1_native_entry_frontier.py"
SPEC = importlib.util.spec_from_file_location("p1_native_entry_frontier", P)
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)


class NativeEntryFrontier(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix="penang-entry-")
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.original = self.root / "original"
        self.guarded = self.root / "guarded"
        start = """/* XNU source fixture */
LEXT(start_first_cpu)
    mov x0, x20
    adrp lr, EXT(arm_init)@page
    add lr, lr, EXT(arm_init)@pageoff
    br lr
"""
        init = """void arm_init(boot_args *args) {
    do_something();
    arm_vm_init(xmaxmem, args);
}
"""
        blocker = """#if defined(PENANG_PHYSICAL) || defined(ARM64_BOARD_CONFIG_PENANG_PHYSICAL)
#error "PENANG_PHYSICAL: B4.2 physical PMAP/Stage-2/SMMU lockdown backend is BLOCKED; no provider"
#endif
"""
        self._write(self.original / MOD.FILES["start"], start)
        self._write(self.original / MOD.FILES["arm_init"], init)
        for name in ("arm_vm_init", "pmap"):
            self._write(self.guarded / MOD.FILES[name], blocker)

    @staticmethod
    def _write(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data, encoding="utf-8")

    def inspect(self):
        return MOD.inspect(self.original, self.guarded)

    def test_exact_source_entry_chain_remains_security_blocked(self):
        outcome = self.inspect()
        self.assertEqual(outcome["status"], "P1_SOURCE_ENTRY_CHAIN_VERIFIED_B42_COMPILE_HOLD")
        self.assertEqual(outcome["pmap_physical"], "B42_BLOCKED_BACKEND_UNVERIFIED")
        self.assertEqual(outcome["native_xnu_full_link"], "NOT_RUN")
        self.assertEqual(outcome["xnu_device_entry"], "NOT_RUN")
        self.assertEqual(outcome["userland_penang_init"], "NOT_RUN")
        self.assertTrue(all(outcome["checks"].values()))
        self.assertEqual(len(outcome["sources"]), 4)
        self.assertTrue(all(len(x["sha256"]) == 64 for x in outcome["sources"].values()))

    def test_silent_removal_of_either_b42_guard_is_rejected(self):
        for name in ("arm_vm_init", "pmap"):
            with self.subTest(name=name):
                file = self.guarded / MOD.FILES[name]
                old = file.read_text(encoding="utf-8")
                try:
                    file.write_text("/* guard removed */", encoding="utf-8")
                    self.assertEqual(self.inspect()["status"], "REJECTED")
                    self.assertFalse(self.inspect()["checks"][f"{name}_b42_guard"])
                finally:
                    file.write_text(old, encoding="utf-8")

    def test_false_physical_entry_and_missing_vm_link_are_rejected(self):
        for name, content in (
            ("start", "LEXT(start_first_cpu)\n bl fake_boot\n"),
            ("arm_init", "void arm_init(boot_args *args) { /* no VM */ }\n"),
        ):
            with self.subTest(name=name):
                file = self.original / MOD.FILES[name]
                old = file.read_text(encoding="utf-8")
                try:
                    file.write_text(content, encoding="utf-8")
                    self.assertEqual(self.inspect()["status"], "REJECTED")
                finally:
                    file.write_text(old, encoding="utf-8")

    def test_missing_sources_and_userland_name_confusion(self):
        file = self.original / MOD.FILES["start"]
        original = file.read_text(encoding="utf-8")
        file.unlink()
        outcome = self.inspect()
        self.assertEqual(outcome["status"], "REJECTED")
        self.assertFalse(outcome["checks"]["required_sources_present"])
        self._write(file, original + "\npenang-init: .ascii \"NOT_KERNEL_ENTRY\"\n")
        outcome = self.inspect()
        self.assertEqual(outcome["status"], "REJECTED")
        self.assertFalse(outcome["checks"]["kernel_entry_is_not_userland_fixture"])

    def test_cli_exclusive_receipt_and_refusal_on_second_write(self):
        with tempfile.TemporaryDirectory(prefix="penang-p1-receipt-") as temp:
            receipt = Path(temp) / "audit.json"
            cmd = [sys.executable, "-B", str(P), "--xnu-original-root",
                   str(self.original), "--b42-guarded-root", str(self.guarded),
                   "--out", str(receipt)]
            first = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
            self.assertEqual(first.returncode, 0, first.stderr)
            report = json.loads(receipt.read_text(encoding="utf-8"))
            self.assertEqual(report["provenance"], "HOST_ONLY")
            again = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
            self.assertNotEqual(again.returncode, 0)
            self.assertEqual(json.loads(receipt.read_text(encoding="utf-8")), report)


if __name__ == "__main__":
    unittest.main()
