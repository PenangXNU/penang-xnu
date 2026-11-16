# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/m12-penang-physical"))
try:
    import p1_compiler_contract as M
finally:
    sys.path.pop(0)


class CompilerContract(unittest.TestCase):
    def test_ir_mutations_cannot_pass_on_presence_alone(self):
        ir = "\n".join(f'@s_{name} = private constant [3 x i8] c"{value}\\00"\n'
                       f'@p1_signature_{name} = global ptr @s_{name}, align 8\n'
                       f'@p1_summary_{name} = global i64 {summary}, align 8'
                       for name, value, summary in (("mixed", "12", 6), ("data", "22", 4), ("ptrs", "11", 2)))
        M.audit_ir(ir)
        for bad in (ir.replace('c"12', 'c"22'), ir.replace("i64 6,", "i64 4,"),
                    ir.replace("ptr @s_mixed,", "ptr @s_data,"), ir.replace("[3 x i8]", "[2 x i8]"),
                    ir.replace("@p1_summary_ptrs", "@absent")):
            with self.subTest(bad=bad), self.assertRaises(ValueError): M.audit_ir(bad)

    def test_binary_hash_and_public_output_are_refused_before_execution(self):
        cc = Path(shutil.which("clang") or "").resolve()
        self.assertTrue(cc.is_file(), "Clang required")
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "out"
            with self.assertRaises(ValueError): M.check(cc, "0"*64, out)
            self.assertFalse(out.exists())
        with self.assertRaises(ValueError): M.check(cc, M.sha(cc), ROOT / ".build/refused-compiler-contract")

    def test_real_compiler_requires_both_semantics_and_negative_diagnostics(self):
        cc = Path(shutil.which("clang") or "").resolve()
        query = subprocess.run([str(cc), "-E", "-P", "-x", "c", "-"],
                               input="__has_builtin(__builtin_xnu_type_signature)\n", text=True, capture_output=True)
        self.assertEqual(query.returncode, 0)
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder) / "contract"
            if query.stdout.strip() == "1":
                self.assertEqual(M.check(cc, M.sha(cc), out)["status"], "PASS_BOUNDED_TYPE_TRAITS_ONLY")
            else:
                with self.assertRaises(ValueError): M.check(cc, M.sha(cc), out)
                self.assertTrue((out / "c-contract.log").stat().st_size > 0)
                self.assertFalse((out / "results.json").exists())
