# SPDX-License-Identifier: Apache-2.0
"""Native-input inventory, filesystem boundaries and security HOLD regressions."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/m12-penang-physical"
sys.path.insert(0, str(TOOLS))
try:
    import p1_native_inputs as MOD
finally:
    sys.path.pop(0)


class NativeInputs(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix="penang-inputs-")
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.original = self.root / "original"
        self.guarded = self.root / "guarded"
        self.params = {name: self.root / name for name in MOD.INPUTS}
        self.core = ["osfmk/arm/pmap/pmap.c", "osfmk/arm/arm_init.c",
                     "osfmk/arm64/arm_vm_init.c", "osfmk/arm64/locore.s", "osfmk/arm64/start.s"]
        self.iokit = ["iokit/penang_physical/PenangPhysicalPlatformExpert.cpp",
                      "iokit/penang_physical/penang_physical_contract.c"]
        self.write(self.original / self.core[4],
                   "LEXT(_start)\n b EXT(start_first_cpu)\nLEXT(start_first_cpu)\n adrp lr, EXT(arm_init)@page\n")
        self.write(self.original / self.core[1], "void arm_init() { arm_vm_init(); }\n")
        guard = MOD.PHYSICAL_GUARD + '\n#error "' + MOD.BLOCKER + '"\n#endif\n'
        for name in self.core + self.iokit:
            self.write(self.guarded / name, guard if name in (self.core[0], self.core[2]) else "/* fixture */\n")
        self.write(self.params["osfmk_make"], "export CONFIG_DEFINES = -DCONFIG_PMAP_PPL -DCONFIG_KERNEL_INTEGRITY\n" + self.refs(self.core))
        self.write(self.params["iokit_make"], self.refs(self.iokit))
        self.write(self.params["osfmk_config"], "options PENANG_PHYSICAL_SOURCES\noptions CONFIG_PMAP_PPL\noptions CONFIG_KERNEL_INTEGRITY\n")
        self.write(self.params["board_header"], (TOOLS / "PENANG_PHYSICAL.h").read_text())

    @staticmethod
    def refs(paths):
        return "\n".join("$(SOURCE_DIR)/" + p for p in paths) + "\n"

    @staticmethod
    def write(path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def inspect(self):
        return MOD.inspect(self.original, self.guarded, **self.params)

    def reject(self, code=None):
        result = self.inspect()
        self.assertEqual(result["status"], "REJECTED", result)
        if code:
            self.assertIn(code, [b["code"] for b in result["blockers"]])
        return result

    def test_complete_fixture_is_only_input_audit_not_compilation(self):
        result = self.inspect()
        self.assertEqual(result["status"], "INPUT_AUDIT_COMPLETE_B42_COMPILE_HOLD", result)
        self.assertEqual(result["reference_count"], 7)
        self.assertEqual(result["scope"], "HOST_ONLY")
        for name in ("compile", "native_xnu_full_link", "xnu_device_entry", "userland_penang_init"):
            self.assertEqual(result[name], "NOT_RUN")
        self.assertEqual(result["b42"]["SECURITY_GATE"], "BLOCKED")
        self.assertEqual(result["b42"]["can_promote_to_device"], False)
        self.assertEqual(result["inputs"]["board_header"]["bytes"], self.params["board_header"].stat().st_size)
        self.assertEqual(len(result["inputs"]["board_header"]["sha256"]), 64)
        self.assertNotIn(str(self.root), json.dumps(result))

    def test_unavailable_recipes_list_all_prerequisites(self):
        result = MOD.inspect(self.original)
        self.assertEqual(result["status"], "BLOCKED")
        self.assertEqual(len(result["blockers"]), 5)
        self.assertEqual(len(result["sources"]), 2)

    def test_every_missing_or_commented_core_and_iokit_reference(self):
        for name, paths in (("osfmk_make", self.core), ("iokit_make", self.iokit)):
            path = self.params[name]
            old = path.read_text()
            for source in paths:
                with self.subTest(source=source):
                    self.write(path, old.replace("$(SOURCE_DIR)/" + source, "# $(SOURCE_DIR)/" + source))
                    self.reject()
            self.write(path, old)

    def test_missing_sources_are_derived_from_disk_not_a_supplied_empty_list(self):
        path = self.params["osfmk_make"]
        with path.open("a") as out:
            out.write(self.refs(["osfmk/arm64/amcc_rorgn_ppl.c", "osfmk/arm64/amcc_rorgn_common.c"]))
        result = self.reject("SOURCE_UNAVAILABLE_OR_ESCAPES_ROOT")
        self.assertEqual(result["b42"]["APPLE_AMCC_LEAK"], "FOUND_2_FILES")
        self.assertEqual(len(result["b42"]["MISSING_PUBLIC_SOURCES"]), 2)

    def test_unparsed_traversal_absolute_and_noncanonical_sources(self):
        path = self.params["osfmk_make"]
        old = path.read_text()
        for token in ("../outside.c", "/outside.c", "osfmk//odd.c", "osfmk/./odd.c",
                      "osfmk/$(DYNAMIC).c", "osfmk/not.c.bad", "osfmk/x.c;touch-pwned"):
            with self.subTest(token=token):
                self.write(path, old + self.refs([token]))
                self.reject("UNSUPPORTED_SOURCE_REFERENCES")
        self.write(path, old)

    def test_known_recipe_templates_are_classified_separately_from_sources(self):
        path = self.params["osfmk_make"]
        with path.open("a") as out:
            out.write("\t${C_RULE_1A} $(SOURCE_DIR)/osfmk/arm64/${C_RULE_1B}\n"
                      "\t${S_RULE_1A} $(SOURCE_DIR)/osfmk/arm64/${S_RULE_1B}\n"
                      "genassym.o: $(SOURCE_DIR)/$(COMPONENT)/$(GENASSYM_LOCATION)/genassym.c\n")
        path = self.params["iokit_make"]
        with path.open("a") as out:
            out.write("\t${P_RULE_1A} $(SOURCE_DIR)/iokit/Kernel/${P_RULE_1B}\n")
        result = self.inspect()
        self.assertEqual(result["status"], "INPUT_AUDIT_COMPLETE_B42_COMPILE_HOLD", result)
        self.assertEqual(result["reference_count"], 7)
        self.assertEqual(len(result["references"]["osfmk"]["recipe_templates"]), 2)
        self.assertEqual(result["references"]["osfmk"]["generated_dependencies"],
                         ["$(COMPONENT)/$(GENASSYM_LOCATION)/genassym.c"])
        self.assertEqual(result["references"]["iokit"]["recipe_templates"],
                         ["iokit/Kernel/${P_RULE_1B}"])

    def test_unknown_or_malformed_macro_templates_fail_closed(self):
        path = self.params["osfmk_make"]
        old = path.read_text()
        for token in ("", "osfmk/${UNKNOWN}", "osfmk/$(C_RULE_1B)",
                      "../osfmk/${C_RULE_1B}", "osfmk//${C_RULE_1B}",
                      "osfmk/${C_RULE_1B}/suffix.c", "osfmk/${C_RULE_1B};bad",
                      "$(COMPONENT)/../$(GENASSYM_LOCATION)/genassym.c",
                      "$(COMPONENT)/$(UNKNOWN)/genassym.c",
                      "osfmk/start.s\\#hidden", "osfmk/start.s\\ hidden",
                      "osfmk/start.s\\;bad", "osfmk/start.s\\other"):
            with self.subTest(token=token):
                self.write(path, old + self.refs([token]))
                self.reject("UNSUPPORTED_SOURCE_REFERENCES")
        self.write(path, old)

    def test_recipe_context_and_family_must_match_exactly(self):
        path = self.params["osfmk_make"]
        old = path.read_text()
        for line in ("\t${C_RULE_1A} $(SOURCE_DIR)/osfmk/${S_RULE_1B}",
                     "RULE=$(SOURCE_DIR)/osfmk/${C_RULE_1B}",
                     "\t${C_RULE_1A} $(SOURCE_DIR)/osfmk/${C_RULE_1B} ; extra",
                     "else.o: $(SOURCE_DIR)/$(COMPONENT)/$(GENASSYM_LOCATION)/genassym.c"):
            with self.subTest(line=line):
                self.write(path, old + line + "\n")
                self.reject("UNSUPPORTED_SOURCE_REFERENCES")
        self.write(path, old)

    def test_multiline_guard_message_preserves_the_physical_refusal(self):
        for source in (self.core[0], self.core[2]):
            path = self.guarded / source
            self.write(path, path.read_text().replace('#endif', '       "additional refusal detail"\n#endif'))
        self.assertEqual(self.inspect()["status"], "INPUT_AUDIT_COMPLETE_B42_COMPILE_HOLD")
        source = self.guarded / self.core[0]
        self.write(source, "#if 0\n" + source.read_text() + "#endif\n")
        self.reject("TOP_LEVEL_B42_REFUSAL_MISSING")

    def test_spliced_inactive_parent_guard_is_not_admitted(self):
        source = self.guarded / self.core[0]
        old = source.read_text()
        self.write(source, "#\\\nif 0\n" + old + "#endif\n")
        self.reject("TOP_LEVEL_B42_REFUSAL_MISSING")

    def test_symlink_escape_and_nonregular_input(self):
        path = self.guarded / self.core[0]
        path.unlink()
        outside = self.root / "outside.c"
        self.write(outside, "private input must not be read\n")
        path.symlink_to(outside)
        result = self.reject("SOURCE_UNAVAILABLE_OR_ESCAPES_ROOT")
        self.assertNotIn("guarded/" + self.core[0], result["sources"])
        self.assertEqual(result["entry"]["status"], "NOT_RUN")
        result = MOD.inspect(self.original, self.guarded, **{**self.params, "board_header": self.root})
        self.assertEqual(result["status"], "BLOCKED")

    def test_legacy_partial_pass_cannot_admit_no_monitor_in_either_component(self):
        for name in ("osfmk_make", "iokit_make"):
            path = self.params[name]
            old = path.read_text()
            self.write(path, old + "\nexport CONFIG_DEFINES += -DNO_MONITOR\n")
            self.reject("FORBIDDEN_PROFILE_TOKEN")
            self.write(path, old)

    def test_omitted_reference_cannot_expose_outside_bytes_through_legacy_reader(self):
        make = self.params["osfmk_make"]
        self.write(make, make.read_text().replace("$(SOURCE_DIR)/" + self.core[0], "# omitted"))
        path = self.guarded / self.core[0]
        outside = self.root / "outside.c"
        self.write(outside, path.read_text())
        path.unlink()
        path.symlink_to(outside)
        result = self.reject("TOP_LEVEL_B42_REFUSAL_MISSING")
        self.assertEqual(result["entry"], {"status": "NOT_RUN"})
        self.assertNotIn("guarded/" + self.core[0], result["sources"])

    def test_selector_removal_disable_and_undef_are_rejected(self):
        path = self.params["osfmk_config"]
        old = path.read_text()
        for macro in ("CONFIG_PMAP_PPL", "CONFIG_KERNEL_INTEGRITY"):
            for change in (old.replace("options " + macro, "# options " + macro),
                           old + "\n-D" + macro + "=0\n", old + "\n-U" + macro + "\n"):
                with self.subTest(macro=macro, change=change):
                    self.write(path, change)
                    self.reject("UNSUPPORTED_SECURITY_SELECTOR")
        self.write(path, old)

    def test_commented_and_inactive_physical_guards_are_rejected(self):
        for source in (self.core[0], self.core[2]):
            path = self.guarded / source
            old = path.read_text()
            for change in ("/*\n" + old + "*/\n", "#if 0\n" + old + "#endif\n",
                           old.replace("#error", "// #error")):
                with self.subTest(source=source, change=change):
                    self.write(path, change)
                    self.reject("TOP_LEVEL_B42_REFUSAL_MISSING")
            self.write(path, old)

    def test_missing_original_and_oversize_or_binary_inputs_fail_closed(self):
        for data in (b"\0not text", b"x" * (MOD.MAX_INPUT + 1)):
            with self.subTest(size=len(data)):
                self.params["osfmk_make"].write_bytes(data)
                result = self.inspect()
                self.assertEqual(result["status"], "BLOCKED")
                self.assertIn("MISSING_OR_INVALID_INPUT", [b["code"] for b in result["blockers"]])
        (self.original / self.core[4]).unlink()
        self.assertIn("ORIGINAL_SOURCE_UNAVAILABLE", [b["code"] for b in self.inspect()["blockers"]])

    def test_cli_exit_status_and_exclusive_receipt(self):
        receipt = self.root / "receipt.json"
        command = [sys.executable, "-B", str(TOOLS / "p1_native_inputs.py"),
                   "--xnu-original-root", str(self.original), "--b42-guarded-root", str(self.guarded),
                   "--out", str(receipt)]
        for name, path in self.params.items():
            command += ["--" + name.replace("_", "-"), str(path)]
        first = subprocess.run(command, capture_output=True, text=True, timeout=20)
        self.assertEqual(first.returncode, 0, first.stderr)
        old = receipt.read_bytes()
        second = subprocess.run(command, capture_output=True, text=True, timeout=20)
        self.assertEqual(second.returncode, 1)
        self.assertEqual(receipt.read_bytes(), old)
        blocked = subprocess.run(command[:5], capture_output=True, text=True, timeout=20)
        self.assertEqual(blocked.returncode, 2)


if __name__ == "__main__":
    unittest.main()
