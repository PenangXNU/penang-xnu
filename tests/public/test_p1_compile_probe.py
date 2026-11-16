# SPDX-License-Identifier: Apache-2.0
"""Real LLVM selected-TU probes plus refusal and artifact-validation negatives."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/m12-penang-physical"
sys.path.insert(0, str(TOOLS))
try:
    import p1_compile_probe as MOD
finally:
    sys.path.pop(0)


class CompileProbe(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix="penang-probe-")
        self.addCleanup(folder.cleanup)
        self.temp = Path(folder.name)
        self.tree = self.temp / "source"
        self.tree.mkdir()
        self.board = TOOLS / "PENANG_PHYSICAL.h"
        self.osfmk = self.temp / "osfmk.make"
        self.iokit = self.temp / "iokit.make"
        self.source = "osfmk/fixture.c"
        self.cpp = "iokit/fixture.cpp"
        self.asm = "osfmk/fixture.s"
        self.write(self.tree / self.source, "int fixture(void) { return 7; }\n")
        self.write(self.tree / self.cpp, 'extern "C" int fixture_cpp(void) { return 9; }\n')
        self.write(self.tree / self.asm, ".text\n.globl _fixture_asm\n_fixture_asm:\n ret\n")
        self.guard_text = ("#if defined(PENANG_PHYSICAL) || defined(ARM64_BOARD_CONFIG_PENANG_PHYSICAL)\n"
                           '#error "B4.2 physical PMAP/Stage-2/SMMU lockdown backend is BLOCKED"\n#endif\n')
        for name in MOD.GUARDS:
            self.write(self.tree / name, self.guard_text)
        self.flags = "export CONFIG_DEFINES = -DCONFIG_PMAP_PPL -DCONFIG_KERNEL_INTEGRITY\n"
        self.write(self.osfmk, self.flags + self.refs([self.source, self.asm, *MOD.GUARDS]))
        self.write(self.iokit, self.flags + self.refs([self.cpp]))
        self.clang = shutil.which("clang-18") or shutil.which("clang")
        self.clangxx = shutil.which("clang++-18") or shutil.which("clang++")
        self.nm = shutil.which("llvm-nm-18") or shutil.which("llvm-nm")

    @staticmethod
    def write(path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    @staticmethod
    def refs(names):
        return "".join("$(SOURCE_DIR)/" + name + "\n" for name in names)

    def probe(self, sources=None, out=None, **kwargs):
        return MOD.probe(self.tree, self.osfmk, self.iokit, self.board,
                         sources or [self.source], out or self.temp / "output",
                         clang=self.clang or "clang", clangxx=self.clangxx or "clang++",
                         llvm_nm=self.nm or "llvm-nm", **kwargs)

    def require_llvm(self):
        if not all((self.clang, self.clangxx, self.nm)):
            self.skipTest("Clang C/C++ and llvm-nm required for actual ARM64 object checks")

    def test_vm_semantic_types_are_enforced_only_for_upstream_vm_units(self):
        self.require_llvm()
        for name in ("bsd/vm/vm_unix.c", "bsd/kern/kern_mman.c"):
            self.assertEqual(MOD.source_flags(name), ["-DVM_UNSAFE_TYPES=1"])
        for name in ("bsd/vm/nested/file.c", "bsd/kern/kern_mman_other.c", "bsd/kern/other.c"):
            self.assertEqual(MOD.source_flags(name), [])
        source = "osfmk/vm/vm_sanitize.c"
        self.write(self.tree / source, "#if VM_UNSAFE_TYPES != 1\n#error semantic wrappers required\n#endif\nint vm_semantic_fixture;\n")
        self.write(self.osfmk, self.osfmk.read_text() + self.refs([source]))
        self.assertEqual(self.probe([source])["compiled_objects"], 1)
        generated = self.temp / "headers"
        self.write(generated / "meta_features.h", "#undef VM_UNSAFE_TYPES\n")
        result = self.probe([source], out=self.temp / "erased-vm", include_dirs=[generated])
        self.assertEqual(result["compiled_objects"], 0)
        self.assertIn("VM_UNSAFE_TYPES", result["units"][0]["invalid_macros"])
        with self.assertRaises(ValueError):
            MOD.config_flags(self.flags.rstrip() + " -DVM_UNSAFE_TYPES=0")

    def test_actual_c_cpp_and_assembly_objects_remain_host_only(self):
        self.require_llvm()
        asm = self.tree / self.asm
        self.write(asm, "#if !ARM_LARGE_MEMORY || VM_KERNEL_LINK_ADDRESS != 0xfffffe0007004000 || !XNU_TARGET_OS_OSX || !XNU_PLATFORM_MacOSX || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ != 150600\n"
                   '#error "assembly does not match the native helper address-space layout"\n#endif\n'
                   + asm.read_text())
        before = {p: MOD.digest(p) for p in self.tree.rglob("*") if p.is_file()}
        r = self.probe([self.source, self.cpp, self.asm])
        self.assertEqual(r["status"], "SELECTED_OBJECTS_ONLY_B42_COMPILE_HOLD", r)
        self.assertEqual(r["compiled_objects"], 3)
        self.assertEqual(r["blocked_units"], 0)
        self.assertEqual(r["scope"], "HOST_ONLY")
        for field in ("native_xnu_full_link", "xnu_device_entry", "userland_penang_init"):
            self.assertEqual(r[field], "NOT_RUN")
        for row in r["units"]:
            self.assertEqual(row["object"]["format"], "ARM64_MH_OBJECT")
            self.assertTrue(row["object"]["defined_symbols"])
            self.assertIn("--target=arm64-apple-darwin", row["command"])
            self.assertIn("-DCONFIG_PMAP_PPL", row["command"])
        self.assertIn("-DMACH_KERNEL_PRIVATE", r["units"][0]["command"])
        cpp_command = r["units"][1]["command"]
        self.assertNotIn("-DMACH_KERNEL_PRIVATE", cpp_command)
        self.assertNotIn("-DMACH_KERNEL", cpp_command)
        for flag in ("-DIOKIT_KERNEL_PRIVATE", "-DDRIVER_PRIVATE", "-std=gnu++2b", "-fapple-kext"):
            self.assertIn(flag, cpp_command)
        self.assertEqual(before, {p: MOD.digest(p) for p in before})
        self.assertTrue((self.temp / "output/results.json").is_file())

    def test_component_flags_are_visible_to_actual_preprocessor(self):
        self.require_llvm()
        self.write(self.tree / self.source,
                   "#if !defined(MACH_KERNEL_PRIVATE) || defined(IOKIT_KERNEL_PRIVATE)\n"
                   '#error "wrong OSFMK component flags"\n#endif\nint fixture;\n')
        self.write(self.tree / self.cpp,
                   "#if defined(MACH_KERNEL_PRIVATE) || defined(MACH_KERNEL) || !defined(IOKIT_KERNEL_PRIVATE)\n"
                   '#error "wrong IOKit component flags"\n#endif\nextern "C" { int fixture_cpp; }\n')
        r = self.probe([self.source, self.cpp])
        self.assertEqual(r["compiled_objects"], 2, r)

    def test_native_cpp_does_not_require_userspace_static_destructor_registration(self):
        self.require_llvm()
        self.write(self.tree / self.cpp, 'extern "C" void consume();\nstruct D { ~D() { consume(); } };\nD object;\n')
        r = self.probe([self.cpp])
        self.assertEqual(r["compiled_objects"], 1)
        command = r["units"][0]["command"]
        self.assertIn("-fno-c++-static-destructors", command)
        obj = command[-1]
        undefined = subprocess.check_output([self.nm, "--undefined-only", obj], text=True)
        self.assertNotIn("atexit", undefined)

    def test_kernel_sha_abi_mode_is_effective_and_cannot_be_erased(self):
        self.require_llvm()
        source = "osfmk/arm64/corecrypto/sha256_compress_arm64.s"
        self.write(self.tree / source, "#if BUILDKERNEL != 1\n#error kernel vector-preservation mode required\n#endif\n"
                   ".text\n.globl _crypto_fixture\n_crypto_fixture:\n ret\n")
        self.write(self.osfmk, self.osfmk.read_text() + self.refs([source]))
        first = self.probe([source], out=self.temp / "kernel-mode")
        self.assertEqual(first["compiled_objects"], 1, first)
        generated = self.temp / "generated"
        generated.mkdir()
        self.write(generated / "meta_features.h", "#undef BUILDKERNEL\n")
        second = self.probe([source], out=self.temp / "erased-mode", include_dirs=[generated])
        self.assertEqual(second["compiled_objects"], 0)
        self.assertIn("BUILDKERNEL", second["units"][0]["invalid_macros"])

    def test_generated_feature_header_is_consumed_by_component(self):
        self.require_llvm()
        generated = self.temp / "generated"
        self.write(generated / "osfmk/meta_features.h", "#define PROBE_FEATURE 41\n")
        self.write(generated / "iokit/meta_features.h", "#define PROBE_FEATURE 42\n")
        self.write(self.tree / self.source, "_Static_assert(PROBE_FEATURE == 41, \"wrong component\");\nint fixture;\n")
        self.write(self.tree / self.cpp, 'static_assert(PROBE_FEATURE == 42, "wrong component");\nextern "C" { int fixture_cpp; }\n')
        r = self.probe([self.source, self.cpp], include_dirs=[generated])
        self.assertEqual(r["compiled_objects"], 2, r)

    def test_source_root_cannot_shadow_the_recorded_feature_header(self):
        self.require_llvm()
        generated = self.temp / "generated"
        self.write(generated / "osfmk/meta_features.h", "#define SELECTED_FEATURE 41\n")
        self.write(self.tree / "meta_features.h", "#define SELECTED_FEATURE 99\n")
        self.write(self.tree / self.source, '_Static_assert(SELECTED_FEATURE == 41, "shadowed");\nint fixture;\n')
        r = self.probe(include_dirs=[generated])
        self.assertEqual(r["compiled_objects"], 1, r)
        self.assertEqual(r["units"][0]["feature_header"]["sha256"], MOD.digest(generated / "osfmk/meta_features.h"))

    def test_feature_header_cannot_disable_physical_identity_or_security(self):
        self.require_llvm()
        generated = self.temp / "generated"
        for macro in ("PENANG_PHYSICAL", "ARM64_BOARD_CONFIG_PENANG_PHYSICAL", "CONFIG_PMAP_PPL",
                      "CONFIG_KERNEL_INTEGRITY", "__ARM_16K_PG__", "__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__"):
            with self.subTest(macro=macro):
                self.write(generated / "osfmk/meta_features.h", "#undef " + macro + "\n")
                r = self.probe([MOD.GUARDS[0]], out=self.temp / macro,
                               include_dirs=[generated])
                self.assertEqual(r["compiled_objects"], 0)
                self.assertEqual(r["units"][0]["blocker"], "PREINCLUDE_PROFILE_REJECTED", r)
                self.assertIn(macro, r["units"][0]["invalid_macros"])

    def test_successful_guard_compilation_is_never_counted(self):
        self.require_llvm()
        self.write(self.tree / MOD.GUARDS[0],
                   "#undef PENANG_PHYSICAL\n#undef ARM64_BOARD_CONFIG_PENANG_PHYSICAL\n"
                   + self.guard_text + "int unexpectedly_compiled_guard;\n")
        r = self.probe([MOD.GUARDS[0]])
        self.assertEqual(r["units"][0]["returncode"], 0, r)
        self.assertEqual(r["units"][0]["blocker"], "B42_GUARD_COMPILED_UNEXPECTEDLY")
        self.assertEqual(r["compiled_objects"], 0)

    def test_driver_argument_environment_is_scrubbed(self):
        self.require_llvm()
        self.write(self.tree / self.source, "#ifdef INJECTED_ENV_FLAG\n"
                   '#error "environment injection"\n#endif\nint fixture;\n')
        with mock.patch.dict(os.environ, {"CCC_OVERRIDE_OPTIONS": "+-DINJECTED_ENV_FLAG=1",
                                          "CL": "-DINJECTED_ENV_FLAG=1", "_CL_": "-DINJECTED_ENV_FLAG=1"}):
            r = self.probe()
        self.assertEqual(r["compiled_objects"], 1, r)
        for key in ("CCC_OVERRIDE_OPTIONS", "CL", "_CL_"):
            self.assertIn(key, r["ignored_environment_variables"])

    def test_board_mutation_during_tool_setup_cannot_change_forced_bytes(self):
        self.require_llvm()
        original = self.board.read_bytes()
        self.board = self.temp / "approved-board.h"
        self.board.write_bytes(original)
        original_tool = MOD.tool
        def mutate(name, env=None):
            result = original_tool(name, env)
            self.board.write_bytes(original.replace(b"#define MAX_CPUS 1", b"#define MAX_CPUS 2"))
            return result
        with mock.patch.object(MOD, "tool", side_effect=mutate):
            r = self.probe()
        self.assertEqual(r["compiled_objects"], 0, r)
        self.assertEqual(r["units"][0]["blocker"], "BOARD_HEADER_CHANGED_DURING_PROBE")
        self.assertEqual(Path(r["board_header_snapshot"]).read_bytes(), original)

    def test_b42_and_missing_headers_are_real_compiler_blockers(self):
        self.require_llvm()
        self.write(self.tree / self.source, '#include "missing_required_header.h"\nint fixture;\n')
        r = self.probe([*MOD.GUARDS, self.source])
        self.assertEqual(r["status"], "NATIVE_BUILD_BLOCKED")
        self.assertEqual(r["compiled_objects"], 0)
        self.assertEqual([x["blocker"] for x in r["units"]],
                         ["B42_COMPILE_REFUSAL", "B42_COMPILE_REFUSAL", "COMPILER_FAILURE"])
        self.assertTrue(all(x["returncode"] != 0 for x in r["units"]))

    def test_empty_or_missing_object_does_not_count_as_compiled(self):
        self.require_llvm()
        self.write(self.tree / self.source, "/* empty translation unit */\n")
        r = self.probe()
        self.assertEqual(r["units"][0]["returncode"], 0)
        self.assertEqual(r["status"], "NATIVE_BUILD_BLOCKED")
        self.assertEqual(r["compiled_objects"], 0)
        with self.assertRaises(ValueError):
            MOD.inspect_object(self.temp / "missing.o", self.nm)

    def test_non_arm64_and_truncated_macho_objects_are_rejected(self):
        self.require_llvm()
        obj = self.temp / "host.o"
        subprocess.run([self.clang, "--target=x86_64-apple-darwin", "-c",
                        str(self.tree / self.source), "-o", str(obj)], check=True,
                       text=True, capture_output=True, timeout=30)
        with self.assertRaisesRegex(ValueError, "ARM64"):
            MOD.inspect_object(obj, self.nm)
        obj.write_bytes(b"not-a-Mach-O")
        with self.assertRaisesRegex(ValueError, "truncated"):
            MOD.inspect_object(obj, self.nm)

    def test_unsupported_flags_and_security_overrides_are_rejected(self):
        good = self.flags
        for flag in ("-UCONFIG_PMAP_PPL", "-include secret.h", "@response",
                     "-DNO_MONITOR", "-DQEMU_VIRT", "-DCONFIG_SPTM", "-D__arm64e__",
                     "-DCONFIG_PMAP_PPL=0", "-DCONFIG_KERNEL_INTEGRITY=1",
                     "-DXNU_PLATFORM_MACOSX", "-DXNU_TARGET_OS_IPHONE", "-DPENANG_PHYSICAL=0", "-DMAX_CPUS=8", "-DCONFIG_PMAP_PPL", "-DBUILDKERNEL=0"):
            with self.subTest(flag=flag), self.assertRaises(ValueError):
                MOD.config_flags(good.rstrip() + " " + flag + "\n")
        with self.assertRaises(ValueError):
            MOD.config_flags(good.replace("-DCONFIG_PMAP_PPL", ""))
        self.assertEqual(MOD.config_flags(good), ["-DCONFIG_PMAP_PPL", "-DCONFIG_KERNEL_INTEGRITY"])

    def test_source_selection_traversal_and_symlink_escape_are_rejected(self):
        for source in ("osfmk/unlisted.c", "../outside.c", "/outside.c", "osfmk/./fixture.c"):
            with self.subTest(source=source), self.assertRaises(ValueError):
                self.probe([source])
        path = self.tree / self.source
        path.unlink()
        outside = self.temp / "private.c"
        self.write(outside, "int outside;\n")
        path.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "escapes"):
            self.probe()
        self.assertFalse((self.temp / "output").exists())

    def test_guard_and_board_changes_are_rejected_before_compiling(self):
        path = self.tree / MOD.GUARDS[0]
        path.write_text("/* refusal removed */\n")
        with self.assertRaisesRegex(ValueError, "refusal"):
            self.probe()
        path.write_text(self.guard_text)
        self.board = self.temp / "unsafe-board.h"
        self.board.write_text("#define NO_MONITOR 1\n")
        with self.assertRaisesRegex(ValueError, "board header"):
            self.probe()
        self.assertFalse((self.temp / "output").exists())

    def test_output_refuses_sources_repository_and_existing_directory(self):
        for output in (self.tree / "output", ROOT / ".build/refused-probe", self.temp):
            with self.subTest(output=output), self.assertRaises(ValueError):
                self.probe(out=output)
        self.assertFalse((self.tree / "output").exists())

    def test_cli_reports_blocked_compilation_and_refuses_output_reuse(self):
        self.require_llvm()
        out = self.temp / "cli-output"
        command = [sys.executable, "-B", str(TOOLS / "p1_compile_probe.py"),
                   "--source-root", str(self.tree), "--osfmk-make", str(self.osfmk),
                   "--iokit-make", str(self.iokit), "--board-header", str(self.board),
                   "--source", MOD.GUARDS[0], "--out", str(out),
                   "--clang", self.clang, "--clangxx", self.clangxx, "--llvm-nm", self.nm]
        first = subprocess.run(command, text=True, capture_output=True, timeout=30)
        self.assertEqual(first.returncode, 2, first.stderr)
        self.assertEqual(json.loads(first.stdout)["status"], "NATIVE_BUILD_BLOCKED")
        receipt = (out / "results.json").read_bytes()
        second = subprocess.run(command, text=True, capture_output=True, timeout=30)
        self.assertEqual(second.returncode, 1)
        self.assertEqual((out / "results.json").read_bytes(), receipt)


if __name__ == "__main__":
    unittest.main()
