#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""T73 getpid Mach-O and ROMFS HOST_ONLY checks; NO guest/kernel execution."""
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "t73-bsd-userland"
spec = importlib.util.spec_from_file_location("t73_getpid_package", TOOLS / "package_getpid.py")
PKG = importlib.util.module_from_spec(spec)
spec.loader.exec_module(PKG)


def run(args):
    result = subprocess.run([str(x) for x in args], text=True,
                            capture_output=True, timeout=35, check=False)
    if result.returncode:
        raise AssertionError(f"command failed: {args}: {result.stderr}")
    return result.stdout


def compiler(name, fallback=None):
    found = shutil.which(name) or (shutil.which(fallback) if fallback else None)
    if not found:
        raise unittest.SkipTest(name + " unavailable")
    return found


def synthetic_dyld_for_romfs_test_only():
    # Structurally valid Q5 profile, with a NOP payload, not an executable dyld.
    image = bytearray(0x4100)
    state = [0] * 33
    state[32] = 0x1000
    commands = b"".join((
        PKG.segment("__TEXT", 0, 0x4000, 0, 0x4000, 5),
        PKG.segment("__LINKEDIT", 0x4000, 0x4000, 0x4000, 0x230, 1),
        struct.pack("<III20s", 0xf, 32, 12, b"/usr/lib/dyld\0"),
        struct.pack("<4I33Q2I", 5, 288, 6, 68, *state, 0, 0),
        struct.pack("<6I", 0x32, 24, 1, 0xf0500, 0xf0500, 0),
        struct.pack("<4I", 0x1d, 16, 0x4100, 0x130),
    ))
    assert len(commands) == 0x1f8
    image[:32] = struct.pack("<8I", 0xfeedfacf, 0x100000c, 0, 7,
                             6, len(commands), 1, 0)
    image[32:32 + len(commands)] = commands
    image[0x1000:0x1004] = b"\x1f\x20\x03\xd5"
    output = bytes(image) + PKG.code_signature(bytes(image), PKG.DYLD_IDENT, "dyld")
    PKG.verify_q5_dyld(output)
    return output


def built_carrier():
    clang = compiler("clang-18", "clang")
    lld = compiler("ld.lld-18", "ld.lld")
    objcopy = compiler("llvm-objcopy-18", "llvm-objcopy")
    with tempfile.TemporaryDirectory(prefix="penang-t73-carrier-") as tmp:
        root = Path(tmp)
        obj, elf = root / "fixture.o", root / "fixture.elf"
        run([clang, "--target=aarch64-none-elf", "-mcpu=cortex-a76",
             "-mstrict-align", "-mgeneral-regs-only", "-ffixed-x18",
             "-fno-addrsig", "-c", TOOLS / "fixture_getpid.S", "-o", obj])
        run([lld, "-m", "aarch64elf", "--build-id=none", "--no-undefined",
             "--orphan-handling=error", "-T", TOOLS / "fixture_getpid.lds",
             "-o", elf, obj])
        parts = []
        for section in (".text", ".data"):
            path = root / (section[1:] + ".raw")
            run([objcopy, "--only-section=" + section, "-O", "binary", elf, path])
            parts.append(path.read_bytes())
        yield parts[0], parts[1]


class T73HostPackaging(unittest.TestCase):
    def test_emit_real_getpid_carrier_and_romfs(self):
        for text, data in built_carrier():
            main = PKG.emit_main(text, data)
            evidence = PKG.verify_main(main)
            self.assertEqual(evidence["trust_admission"], "OPEN")
            self.assertEqual(evidence["guest_execution"], "NOT_RUN")
            self.assertIn(bytes.fromhex("11 22 00 51 00"), main)
            dyld = synthetic_dyld_for_romfs_test_only()
            romfs, receipt = PKG.build_romfs(main, dyld)
            self.assertTrue(romfs.startswith(b"-rom1fs-"))
            self.assertEqual(receipt["scope"], "HOST_ONLY")
            self.assertEqual(receipt["paths"],
                             ["/sbin/penang-dyn-init", "/sbin/launchd", "/usr/lib/dyld"])
            self.assertEqual(romfs.count(main), 1)
            self.assertEqual(romfs.count(dyld), 1)
            with tempfile.TemporaryDirectory(prefix="penang-t73-package-") as tmp:
                out = Path(tmp) / "new"
                r = PKG.write_package(text, data, dyld, out)
                self.assertEqual(r["sha256"], PKG.sha((out / "dynamic-userland.romfs").read_bytes()))
                self.assertEqual((out / "penang-dyn-init.macho").read_bytes(), main)
                with self.assertRaises(PKG.Invalid):
                    PKG.write_package(text, data, dyld, out)

    def test_mutations_rejected_before_guest_execution(self):
        for text, data in built_carrier():
            valid = PKG.emit_main(text, data)
            # Both the signature envelope and each signed code page are checked.
            for place in (0x1000, 0x8100 + 20 + 112, 0x8000,
                          0x4000, 32 + 4 * 72 + 24 + 32 + 8,
                          32 + 72 * 2 + 64):
                with self.subTest(offset=place):
                    bad = bytearray(valid)
                    bad[place] ^= 1
                    with self.assertRaises(PKG.Invalid):
                        PKG.verify_main(bytes(bad))
            invalid_data = bytearray(data)
            invalid_data[0] ^= 1
            with self.assertRaises(PKG.Invalid):
                PKG.emit_main(text, bytes(invalid_data))
            with self.assertRaises(PKG.Invalid):
                PKG.emit_main(bytes(0x3001), data)

    def test_dyld_identity_and_signature_are_not_assumed(self):
        dyld = synthetic_dyld_for_romfs_test_only()
        receipt = PKG.verify_q5_dyld(dyld)
        self.assertEqual(receipt["identity_verification"], "STRUCTURAL_PROFILE_ONLY")
        self.assertEqual(receipt["guest_execution"], "NOT_RUN")
        broken = bytearray(dyld)
        broken[0x1000] ^= 1
        with self.assertRaises(PKG.Invalid):
            PKG.verify_q5_dyld(bytes(broken))
        broken = bytearray(dyld)
        broken[0x4100 + 20 + 88] ^= 1
        with self.assertRaises(PKG.Invalid):
            PKG.verify_q5_dyld(bytes(broken))

    def test_resigned_dyld_load_command_mutations_are_rejected(self):
        dyld = synthetic_dyld_for_romfs_test_only()
        identity = 32 + 72 * 2
        thread = identity + 32
        build = thread + 288
        signature = build + 24
        # Re-sign every malformed input: these negatives must exercise the
        # load-command checks, independently of CodeDirectory integrity.
        mutations = [
            ("identity command", "<I", identity, 0xdeadbeef),
            ("identity name offset", "<I", identity + 8, 16),
            ("identity path", "<B", identity + 12, ord("!")),
            ("identity padding", "<B", identity + 31, 1),
            ("thread command", "<I", thread, 0xdeadbeef),
            ("thread flavor", "<I", thread + 8, 1),
            ("thread count", "<I", thread + 12, 67),
            ("thread PC before payload", "<Q", thread + 16 + 32 * 8, 0x218),
            ("thread PC unaligned", "<Q", thread + 16 + 32 * 8, 0x1001),
            ("thread PC in LINKEDIT", "<Q", thread + 16 + 32 * 8, 0x4000),
            ("thread PC overflow", "<Q", thread + 16 + 32 * 8, 0xffffffffffffffff),
            ("thread EL1h", "<I", thread + 16 + 33 * 8, 5),
            ("thread AArch32", "<I", thread + 16 + 33 * 8, 0x10),
            ("thread reserved flags", "<I", thread + 16 + 33 * 8 + 4, 1),
            ("build command", "<I", build, 0xdeadbeef),
            ("build platform", "<I", build + 8, 2),
            ("build minimum OS", "<I", build + 12, 0),
            ("build SDK", "<I", build + 16, 0),
            ("build tool count", "<I", build + 20, 1),
            ("signature command", "<I", signature, 0xdeadbeef),
        ]
        for label, fmt, offset, value in mutations:
            with self.subTest(label=label):
                malformed = bytearray(dyld[:0x4100])
                struct.pack_into(fmt, malformed, offset, value)
                malformed = bytes(malformed)
                signed = malformed + PKG.code_signature(
                    malformed, PKG.DYLD_IDENT, "dyld")
                PKG.verify_signature(signed, 0x4100, PKG.DYLD_IDENT, "dyld")
                with self.assertRaises(PKG.Invalid):
                    PKG.verify_q5_dyld(signed)


if __name__ == "__main__":
    unittest.main()
