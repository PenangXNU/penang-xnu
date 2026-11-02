"""Own-source host ABI and ARM64 compilation. No Apple SDK or privileged execution."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class PublicBuildTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / ".build/w4/compile-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.tmp.cleanup)
        self.out = Path(self.tmp.name)
        self.cc = os.environ.get("CC", "clang-18")
        self.cxx = os.environ.get("CXX", "clang++-18")

    def run_command(self, args):
        result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_host_cpp_calls_real_c_abi(self):
        objects = []
        for i, name in enumerate(["contracts/platform/src/arm64.c", "platforms/qemu-virt/backend.c"]):
            output = self.out / (str(i) + ".o")
            self.run_command([self.cc, "-std=c11", "-Werror", "-Icontracts/platform/include",
                              "-c", name, "-o", str(output)])
            objects.append(str(output))
        exe = self.out / "abi"
        self.run_command([self.cxx, "-std=c++17", "-Werror", "-Icontracts/platform/include",
                          "tests/platform/abi_consumer.cpp", *objects, "-o", str(exe)])
        self.run_command([str(exe)])

    def test_own_sources_and_guard_cross_compile_arm64(self):
        include = self.out / "include/arm64"
        include.mkdir(parents=True)
        shutil.copyfile(ROOT / "contracts/platform/include/penang_translation_v1.h",
                        include / "penang_translation_v1.h")
        guard = self.out / "guard.s"
        guard.write_text('#include "' + str(ROOT / "platforms/qemu-virt/xnu/penang_pre_mmu.h") +
                         '"\n.text\n.globl _preview_guard\n_preview_guard:\nPV_PRE_MMU_GUARD\nret\n')
        sources = [(str(guard), self.cc, ["-x", "assembler-with-cpp"]),
                   ("contracts/platform/src/arm64.c", self.cc, ["-std=c11"]),
                   ("platforms/qemu-virt/backend.c", self.cc, ["-std=c11"]),
                   ("tests/platform/abi_consumer.cpp", self.cxx, ["-std=c++17"])]
        for i, (source, compiler, flags) in enumerate(sources):
            obj = self.out / ("arm64-" + str(i) + ".o")
            self.run_command([compiler, "--target=arm64-apple-macos15.5", "-mcpu=cortex-a76",
                              "-ffreestanding", "-Werror", *flags, "-Icontracts/platform/include",
                              "-I" + str(include.parent), "-c", source, "-o", str(obj)])
            header = obj.read_bytes()[:12]
            self.assertEqual(header[:4], bytes.fromhex("cffaedfe"))
            self.assertEqual(int.from_bytes(header[4:8], "little"), 0x0100000c)
