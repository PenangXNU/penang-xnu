# SPDX-License-Identifier: Apache-2.0
"""Pinned source-delta boundaries and real assembled entry-object negatives."""
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/m12-penang-physical/p1_t1_entry.py"
SPEC = importlib.util.spec_from_file_location("p1_t1_entry", TOOL)
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)
ASM = """.text
.globl __start, _start_first_cpu
__start:
 b _start_first_cpu
_start_first_cpu:
 adrp x30, _arm_init@PAGE
 add x30, x30, _arm_init@PAGEOFF
 msr TCR_EL1, x1
 msr TTBR1_EL1, x0
 msr MAIR_EL1, x0
 msr VBAR_EL1, x0
 msr SCTLR_EL1, x0
 tlbi vmalle1
 dsb ish
 isb
 ic iallu
 ret
"""


class T1Entry(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix="penang-t1-entry-")
        self.addCleanup(folder.cleanup)
        self.temp = Path(folder.name)
        self.root = self.temp / "source"
        self.source = self.root / MOD.SOURCE
        self.source.parent.mkdir(parents=True)
        self.original = "/* synthetic assembly source, no platform implementation */\n" + "\n".join(MOD.EDITS)
        self.source.write_text(self.original)

    def pinned_fixture(self, text=None, p1_diagnostic=False):
        text = self.original if text is None else text
        updated = text
        for anchor in MOD.EDITS:
            guard = MOD.P1_TUNABLES_GUARD if p1_diagnostic else "#if !defined(PENANG_PHYSICAL)\n"
            updated = updated.replace(anchor, guard + anchor + "#endif\n", 1)
        pin = "P1_RESULT_SHA" if p1_diagnostic else "RESULT_SHA"
        return mock.patch.multiple(MOD, SOURCE_SHA=MOD.sha(text.encode()), **{pin:MOD.sha(updated.encode())})

    def assemble(self, text=ASM):
        cc = shutil.which("clang-18") or shutil.which("clang")
        if cc is None:
            self.skipTest("Clang required for actual ARM64 Mach-O assembly")
        path = self.temp / "fixture.s"
        out = self.temp / "fixture.o"
        path.write_text(text)
        run = subprocess.run([cc, "--target=arm64-apple-darwin", "-mcpu=cortex-a76", "-c",
                              str(path), "-o", str(out)], text=True, capture_output=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stderr)
        return out

    def test_default_pin_rejects_arbitrary_or_modified_upstream(self):
        with self.assertRaisesRegex(ValueError, "SHA mismatch"):
            MOD.source_delta(self.root)
        self.assertEqual(self.source.read_text(), self.original)

    def test_source_delta_only_pairs_include_and_application_for_explicit_board(self):
        with self.pinned_fixture():
            patch, report = MOD.source_delta(self.root)
        self.assertEqual(patch.count(b"--- a/"), 1)
        self.assertIn(b"--- a/osfmk/arm64/start.s", patch)
        self.assertEqual(patch.count(b"+#if !defined(PENANG_PHYSICAL)"), 2)
        self.assertNotIn(b"pmap_cs", patch)
        self.assertNotIn(b"startup.c", patch)
        self.assertEqual(report["changed_files"], [MOD.SOURCE])
        self.assertEqual(report["scope"], "HOST_ONLY")
        self.assertEqual(report["native_xnu_full_link"], "NOT_RUN")
        self.assertEqual(report["physical_cpu_errata"], "NOT_VERIFIED")
        self.assertEqual(self.source.read_text(), self.original)

    def test_anchor_removal_and_duplication_do_not_produce_a_patch(self):
        for text in (self.original.replace(MOD.EDITS[0], ""), self.original + MOD.EDITS[1]):
            with self.subTest(text=text[-35:]), self.pinned_fixture(text):
                self.source.write_text(text)
                with self.assertRaisesRegex(ValueError, "anchor"):
                    MOD.source_delta(self.root)

    def test_diagnostic_entry_pairs_tunables_selection_without_protected_alias(self):
        cc=shutil.which("clang")
        patch_tool=shutil.which("patch")
        if not cc or not patch_tool:self.skipTest("Clang preprocessor and patch required")
        with self.pinned_fixture(p1_diagnostic=True):
            patch,report=MOD.source_delta(self.root,p1_diagnostic=True)
        self.assertEqual(report["profile"],"P1_DIAGNOSTIC_CODEGEN_ONLY")
        self.assertEqual(patch.count(b"--- a/"),1)
        diff=self.temp/"p1.patch";diff.write_bytes(patch)
        subprocess.run([patch_tool,"-p1","-i",str(diff)],cwd=self.root,check=True,capture_output=True)
        header=self.root/"arm64/tunables/tunables.s";header.parent.mkdir(parents=True)
        header.write_text("APPLE_TUNABLES_HEADER_SELECTED\n")
        required=("PENANG_P1_DIAGNOSTIC","ARM64_BOARD_CONFIG_PENANG_P1_DIAGNOSTIC",
                  "PENANG_P1_SANDBOX_COMPILE","PENANG_P1_IOMMU_CODEGEN_ONLY","NO_MONITOR")
        p1=[name+"=1" for name in required]
        cases=[(p1,False),(["PENANG_PHYSICAL=1"],False),([],True),(["NO_MONITOR=1"],True)]
        for name in required:
            cases.append(([flag for flag in p1 if not flag.startswith(name+"=")],True))
            for value in ("0","2","-1"):
                cases.append(([flag for flag in p1 if not flag.startswith(name+"=")]+[name+"="+value],True))
        for flags,apple_path in cases:
            with self.subTest(flags=flags):
                run=subprocess.run([cc,"--target=arm64-apple-darwin","-x","assembler-with-cpp","-E","-P",
                    "-I",str(self.root),*["-D"+flag for flag in flags],str(self.source)],capture_output=True,text=True)
                self.assertEqual(run.returncode,0,run.stderr)
                self.assertEqual("APPLE_TUNABLES_HEADER_SELECTED" in run.stdout,apple_path)
                self.assertEqual("APPLY_TUNABLES x12" in run.stdout,apple_path)

    def test_source_symlink_escape_and_output_boundaries_are_rejected(self):
        self.source.unlink()
        outside = self.temp / "outside.s"
        outside.write_text(self.original)
        self.source.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "escapes"):
            MOD.source_delta(self.root)
        for path, protected in ((ROOT / ".build/t1-refused.patch", ()),
                                (self.root / "refused.patch", (self.root,))):
            with self.assertRaisesRegex(ValueError, "outside"):
                MOD.write_exclusive(path, b"patch", protected)

    def test_patch_and_audit_receipts_never_overwrite_existing_files(self):
        out = self.temp / "delta.patch"
        with self.pinned_fixture():
            patch, _ = MOD.source_delta(self.root)
        MOD.write_exclusive(out, patch, (self.root,))
        with self.assertRaises(FileExistsError):
            MOD.write_exclusive(out, b"changed", (self.root,))
        self.assertEqual(out.read_bytes(), patch)

    def test_actual_assembly_has_entry_relocation_and_architecture_evidence(self):
        report = MOD.audit(self.assemble().read_bytes())
        self.assertEqual(report["status"], "STRUCTURAL_ENTRY_OBJECT_ONLY")
        self.assertEqual(report["symbols"]["__start"]["value"], 0)
        self.assertEqual(report["symbols"]["_start_first_cpu"]["value"], 4)
        self.assertEqual(report["arm_init_page_pair_offset"], 4)
        self.assertEqual(len(report["relocations"]), 2)
        self.assertTrue(all(report["instructions"].values()))
        self.assertEqual(report["physical_cpu_errata"], "NOT_VERIFIED")

    def test_ret_entry_renamed_target_missing_relocations_and_missing_mmu_are_rejected(self):
        for text, reason in (
            (ASM.replace(" b _start_first_cpu", " ret"), "unconditionally"),
            (ASM.replace("_start_first_cpu", "_fake_first_cpu"), "entry symbols"),
            (ASM.replace("_arm_init", "_fake_init"), "arm_init"),
            (ASM.replace(" add x30, x30, _arm_init", " add x29, x29, _arm_init"), "arm_init"),
            (ASM.replace(" msr VBAR_EL1, x0", " nop"), "instructions"),
        ):
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, reason):
                MOD.audit(self.assemble(text).read_bytes())

    def test_malformed_wrong_arch_and_arm64e_objects_are_rejected(self):
        good = self.assemble().read_bytes()
        mutations = [b"not Mach-O", good[:31]]
        for offset, value in ((4, 0x01000007), (8, 2), (12, 2), (16, 0xFFFFFFFF), (20, 0xFFFFFFFF)):
            data = bytearray(good)
            struct.pack_into("<I", data, offset, value)
            mutations.append(bytes(data))
        for data in mutations:
            with self.subTest(header=data[:24]), self.assertRaises(ValueError):
                MOD.audit(data)

    def test_cli_can_compare_structural_objects_and_preserves_exclusive_receipt(self):
        obj = self.assemble()
        out = self.temp / "entry.json"
        cmd = [sys.executable, "-B", str(TOOL), "audit", "--object", str(obj),
               "--reference-object", str(obj), "--out", str(out)]
        run = subprocess.run(cmd, text=True, capture_output=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stderr)
        receipt = out.read_bytes()
        report = json.loads(receipt)
        self.assertTrue(report["reference"]["identical_text"])
        again = subprocess.run(cmd, text=True, capture_output=True, timeout=30)
        self.assertEqual(again.returncode, 1)
        self.assertEqual(out.read_bytes(), receipt)


if __name__ == "__main__":
    unittest.main()
