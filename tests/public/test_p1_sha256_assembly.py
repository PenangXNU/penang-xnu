# SPDX-License-Identifier: Apache-2.0
"""Independent A64 word checks; never execute ARM instructions or run a VM."""
import importlib.util
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
PROGRAM = ROOT / "tools/m12-penang-physical/p1_sha256_assembly.py"
SPEC = importlib.util.spec_from_file_location("p1_sha256_assembly", PROGRAM)
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)


def macro_fixture():
    # Self-authored instruction-syntax fixture, not a vendor source excerpt.
    result = []
    for modern in (True, False):
        result.append("#if ADAPTER_STYLE\n" if modern else "#else\n")
        for operation, count in (("SHA256SU0", 2), ("SHA256SU1", 3),
                                 ("SHA256H", 3), ("SHA256H2", 3)):
            if modern:
                args = ["v$" + str(i) + ".4s" for i in range(count)]
                if operation in ("SHA256H", "SHA256H2"):
                    args[:2] = ["q$0", "q$1"]
            else:
                args = ["q$" + str(i) for i in range(count)]
            result.append(".macro " + operation + "\n" + operation.lower()
                          + " " + ", ".join(args) + "\n.endm\n")
    return "".join(result) + "#endif\n"


def compression_calls():
    # Independent, self-authored static schedule corresponding to the pinned
    # routine: four schedule+hash groups and two final hash-only groups.
    groups = []
    for step in range(4):
        groups.extend((("SHA256SU0", (step, (step + 1) % 4)),
                       ("SHA256H", (18, 19, step + 4)),
                       ("SHA256SU1", (step, (step + 2) % 4, (step + 3) % 4)),
                       ("SHA256H2", (19, 20, step + 4))))
    tail = [(operation, (dest, src, message)) for message in (4, 5, 6, 7)
            for operation, dest, src in (("SHA256H", 18, 19), ("SHA256H2", 19, 20))]
    return "".join(operation + " " + ", ".join(map(str, args)) + "\n"
                   for operation, args in groups + tail + groups * 3 + tail)


class Sha256Assembly(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix="penang-sha256-encodings-")
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.compiler = shutil.which("clang-18") or shutil.which("clang")

    def compile(self, text, *, name="probe", target_flags=("-mcpu=cortex-a76",),
                target="arm64-apple-darwin"):
        if not self.compiler:
            self.skipTest("Clang AArch64 integrated assembler required")
        source = self.root / (name + ".s")
        obj = self.root / (name + ".o")
        source.write_text(text)
        run = subprocess.run([self.compiler, "--target=" + target, *target_flags,
                              "-x", "assembler-with-cpp", "-c", str(source), "-o", str(obj)],
                             text=True, capture_output=True, timeout=30)
        return run, obj

    def test_standard_a64_encodings_use_independent_architecture_words(self):
        instructions = (".text\nsha256h q18, q19, v4.4s\n"
                        "sha256h2 q19, q20, v4.4s\n"
                        "sha256su0 v0.4s, v1.4s\nsha256su1 v0.4s, v2.4s, v3.4s\n")
        run, obj = self.compile(instructions)
        self.assertEqual(run.returncode, 0, run.stderr)
        words = struct.unpack("<4I", MOD.macho_text(obj.read_bytes()))
        self.assertEqual(words, (0x5e044272, 0x5e045293, 0x5e282820, 0x5e036040))
        self.assertEqual(MOD.encode("sha256h", 18, 19, 4), 0x5e044272)
        self.assertEqual(MOD.encode("sha256h2", 19, 20, 4), 0x5e045293)
        self.assertEqual(MOD.encode("sha256su0", 0, 1), 0x5e282820)
        self.assertEqual(MOD.encode("sha256su1", 0, 2, 3), 0x5e036040)

    def test_legacy_rejection_patched_equivalence_and_object_audit(self):
        original = macro_fixture()
        body = ".text\n.globl _sha256_probe\n_sha256_probe:\n" + compression_calls()
        old, _ = self.compile(original + body, name="legacy")
        self.assertNotEqual(old.returncode, 0)
        self.assertIn("invalid operand", old.stderr)
        patched = MOD.rewrite_legacy(original)
        self.assertEqual(patched.count(".macro SHA256"), 8)
        run, obj = self.compile(patched + body)
        self.assertEqual(run.returncode, 0, run.stderr)
        report = MOD.audit_object(obj)
        self.assertEqual(report["crypto_instructions"], 80)
        self.assertEqual(report["operations"],
                         {"sha256su0": 16, "sha256su1": 16, "sha256h": 24, "sha256h2": 24})
        self.assertEqual(report["arm_execution"], "NOT_RUN")
        self.assertEqual(report["sha256_runtime_kat"], "NOT_RUN")
        standard, standard_obj = self.compile("#define ADAPTER_STYLE 1\n" + original + body,
                                              name="standard")
        self.assertEqual(standard.returncode, 0, standard.stderr)
        self.assertEqual(MOD.macho_text(obj.read_bytes()), MOD.macho_text(standard_obj.read_bytes()))
        for mutate in (lambda words: words.__setitem__(0, words[0] ^ 1),
                       lambda words: words.__setitem__(1, words[1] ^ 0x1000),
                       lambda words: words.__setitem__(2, 0xd503201f)):
            words = list(struct.unpack("<80I", MOD.macho_text(obj.read_bytes())))
            mutate(words)
            with self.assertRaisesRegex(ValueError, "encoding/order/register"):
                MOD.audit_text(struct.pack("<80I", *words))

    def test_register_domain_missing_crypto_and_macro_drift_are_rejected(self):
        for args in (("sha256su0", -1, 0), ("sha256su1", 0, 1, 32),
                     ("sha256h", 0, 1), ("sha256h", True, 1, 2)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                MOD.encode(*args)
        good = macro_fixture()
        for bad in ("", good.replace("q$2", "q$3"), good + good,
                    MOD.rewrite_legacy(good)):
            with self.subTest(bad=bad[:20]), self.assertRaises(ValueError):
                MOD.rewrite_legacy(bad)
        run, _ = self.compile(".text\nsha256h q18, q19, v4.4s\n", name="missing-crypto",
                              target_flags=("-march=armv8-a+nocrypto",), target="aarch64-none-elf")
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("sha2", run.stderr.lower())

    def test_source_pins_path_escape_and_exclusive_outputs(self):
        pins = {}
        for name, text in ((MOD.HEADER, macro_fixture()), (MOD.SOURCE, "self-authored test input\n")):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            pins[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        with self.assertRaisesRegex(ValueError, "SHA mismatch"):
            MOD.render(self.root)
        with mock.patch.dict(MOD.PINS, pins):
            patch = MOD.render(self.root)
            self.assertIn("--- a/" + MOD.HEADER, patch)
            self.assertNotIn("--- a/" + MOD.SOURCE, patch)
            source = self.root / MOD.SOURCE
            source.write_text("changed source\n")
            with self.assertRaisesRegex(ValueError, "SHA mismatch"):
                MOD.render(self.root)
            source.unlink()
            outside = self.root.parent / (self.root.name + "-outside.s")
            outside.write_text("private source must not be read\n")
            self.addCleanup(outside.unlink)
            source.symlink_to(outside)
            with self.assertRaisesRegex(ValueError, "escapes"):
                MOD.render(self.root)
        command = [sys.executable, "-B", str(PROGRAM), "audit", "--object",
                   str(self.root / "missing.o"), "--out", str(self.root / "receipt.json")]
        failed = subprocess.run(command, text=True, capture_output=True, timeout=10)
        self.assertNotEqual(failed.returncode, 0)
        self.assertFalse((self.root / "receipt.json").exists())

    def test_cli_audit_is_exclusive_and_macho_bounds_are_checked(self):
        run, obj = self.compile(MOD.rewrite_legacy(macro_fixture()) + ".text\n"
                                + compression_calls())
        self.assertEqual(run.returncode, 0, run.stderr)
        receipt = self.root / "receipt.json"
        command = [sys.executable, "-B", str(PROGRAM), "audit", "--object", str(obj),
                   "--out", str(receipt)]
        first = subprocess.run(command, text=True, capture_output=True, timeout=10)
        self.assertEqual(first.returncode, 0, first.stderr)
        prohibited = ROOT / ".build/refused-sha256-private-receipt.json"
        refused = subprocess.run(command[:-1] + [str(prohibited)], text=True, capture_output=True, timeout=10)
        self.assertNotEqual(refused.returncode, 0)
        self.assertFalse(prohibited.exists())
        old = receipt.read_bytes()
        again = subprocess.run(command, text=True, capture_output=True, timeout=10)
        self.assertNotEqual(again.returncode, 0)
        self.assertEqual(receipt.read_bytes(), old)
        raw = obj.read_bytes()
        # LLVM's object begins with its LC_SEGMENT_64 and first text section.
        self.assertEqual(struct.unpack_from("<I", raw, 32)[0], 0x19)
        for offset, fmt, value in ((4, "<I", 0x1000007), (32 + 4, "<I", 8),
                                   (32 + 72 + 40, "<Q", len(raw) * 2),
                                   (32 + 72 + 48, "<I", 0)):
            with self.subTest(offset=offset):
                bad = bytearray(raw)
                struct.pack_into(fmt, bad, offset, value)
                with self.assertRaises(ValueError):
                    MOD.macho_text(bytes(bad))

    def test_malformed_macho_sections_and_opcode_sequence_are_rejected(self):
        for raw in (b"", bytes(32), struct.pack("<8I", 0xfeedfacf, 0x100000c, 0, 1, 1, 8, 0, 0)
                    + struct.pack("<II", 0x19, 8)):
            with self.subTest(size=len(raw)), self.assertRaises(ValueError):
                MOD.macho_text(raw)
        for text in (b"", bytes(3), struct.pack("<I", 0xd503201f)):
            with self.subTest(size=len(text)), self.assertRaises(ValueError):
                MOD.audit_text(text)


if __name__ == "__main__":
    unittest.main()
