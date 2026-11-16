# SPDX-License-Identifier: Apache-2.0
"""Reproduce private-server contamination with real C/C++ ARM64 consumers."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/m12-penang-physical"))
try:
    import p1_mig_layout as M
finally:
    sys.path.pop(0)


class MigLayout(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix="p1-mig-layout-")
        self.addCleanup(self.folder.cleanup)
        self.base = Path(self.folder.name)
        self.roots = {}
        for role, (subsystem, modes) in M.ROLES.items():
            root = self.base / role
            (root / subsystem).mkdir(parents=True)
            self.roots[role] = root
            name = "exc_server" if role in ("kernel", "exported") else role
            header = root / subsystem / (name + ".h")
            # Private server requires an OSFMK-private type; IOKit has no such
            # declaration. The exported server has a different, public ABI.
            header.write_text("typedef ipc_entry_bits_t payload;\n" if role == "kernel"
                              else "typedef unsigned int payload;\n")
            receipt = dict(scope="HOST_ONLY_REAL_MIG_GENERATION",
                           status="GENERATED_HEADERS_ONLY_NOT_IPC_RUNTIME",
                           xnu_commit=M.XNU_COMMIT, generator_source_commit=M.MIG_COMMIT,
                           native_deployment_target=M.NATIVE_DEPLOYMENT_TARGET,
                           generator_sha256="1" * 64, osfmk_make_sha256="2" * 64,
                           config_defines=["-DCONFIG_KERNEL_INTEGRITY"], subsystem_directory=subsystem,
                           interfaces=[dict(interface=name, mode="kernel-server" if role != "exported"
                                            else "exported-server",
                                            header_sha256=hashlib.sha256(header.read_bytes()).hexdigest())])
            (root / "results.json").write_text(json.dumps(receipt))

    def mutate(self, role, change):
        path = self.roots[role] / "results.json"
        data = json.loads(path.read_text())
        change(data)
        path.write_text(json.dumps(data))

    def test_real_arm64_c_and_cpp_select_distinct_server_abis(self):
        clang = shutil.which("clang")
        self.assertIsNotNone(clang, "Clang required")
        roots, receipts = M.load_layout(self.roots)
        self.assertEqual(len(receipts), 4)
        for language in ("c", "c++"):
            for component in ("osfmk", "iokit"):
                private = component == "osfmk"
                source = ("typedef unsigned long long ipc_entry_bits_t;\n" if private else "")
                source += '#include <mach/exc_server.h>\n'
                source += ("static_assert" if language == "c++" else "_Static_assert")
                source += f'(sizeof(payload) == {8 if private else 4}, "wrong ABI");\nint fixture;\n'
                command = [clang, "--target=arm64-apple-darwin", "-nostdinc", "-x", language,
                           "-c", "-", "-o", str(self.base / (component + language + ".o"))]
                for root in M.component_paths(component, roots):
                    command += ["-I", str(root)]
                result = subprocess.run(command, input=source, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                if not private:
                    broken = subprocess.run(command + ["-I", str(roots["kernel"])], input=source,
                                            capture_output=True, text=True)
                    # Placing the private root first reproduces the old failure.
                    wrong = command[:1] + ["-I", str(roots["kernel"])] + command[1:]
                    failed = subprocess.run(wrong, input=source, capture_output=True, text=True)
                    self.assertNotEqual(failed.returncode, 0)
                    self.assertIn("ipc_entry_bits_t", failed.stderr)
                    self.assertEqual(broken.returncode, 0, broken.stderr)

    def test_bad_provenance_config_mode_inventory_and_paths_are_refused(self):
        original = (self.roots["kernel"] / "results.json").read_bytes()
        changes = [lambda d: d.update(xnu_commit="0" * 40),
                   lambda d: d.update(generator_source_commit="0" * 40),
                   lambda d: d.update(generator_sha256="0" * 64),
                   lambda d: d.update(config_defines=["-DDIFFERENT"]),
                   lambda d: d.update(native_deployment_target="10.4"),
                   lambda d: d.update(subsystem_directory="device"),
                   lambda d: d.update(interfaces=[]),
                   lambda d: d.update(interfaces=["invalid"]),
                   lambda d: d.update(generator_source_commit="c71d2d72f48995baaea76148f61002e5299841de"),
                   lambda d: d["interfaces"][0].update(mode="exported-server"),
                   lambda d: d["interfaces"][0].update(interface="../escape"),
                   lambda d: d["interfaces"].append(d["interfaces"][0].copy())]
        for change in changes:
            with self.subTest(change=change):
                self.mutate("kernel", change)
                with self.assertRaises(ValueError): M.load_layout(self.roots)
                (self.roots["kernel"] / "results.json").write_bytes(original)
        with self.assertRaises(ValueError): M.load_layout({})
        with self.assertRaises(ValueError): M.load_layout(dict(self.roots, kernel=None))
        for component in ("bsd", "libkern", "pexpert", "security", "libsa"):
            self.assertEqual(M.component_paths(component, self.roots), M.component_paths("iokit", self.roots))
        with self.assertRaises(ValueError): M.component_paths("unknown", self.roots)

    def test_changed_extra_and_symlinked_headers_are_refused(self):
        header = self.roots["kernel"] / "mach/exc_server.h"
        original = header.read_bytes()
        header.write_bytes(original + b"\n")
        with self.assertRaises(ValueError): M.load_layout(self.roots)
        header.write_bytes(original)
        extra = header.with_name("unlisted.h")
        extra.write_bytes(original)
        with self.assertRaises(ValueError): M.load_layout(self.roots)
        extra.unlink()
        outside = self.base / "outside.h"
        outside.write_bytes(original)
        header.unlink()
        header.symlink_to(outside)
        with self.assertRaises(ValueError): M.load_layout(self.roots)

    def test_generated_implementation_bytes_and_inventory_are_checked(self):
        path = self.roots["kernel"] / "mach/exc_server.c"
        path.write_text("int real_generated_fixture;\n")
        with self.assertRaises(ValueError): M.load_layout(self.roots)
        info = dict(path="mach/exc_server.c", size=path.stat().st_size,
                    sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        self.mutate("kernel", lambda d: d["interfaces"][0].update(implementation=info))
        M.load_layout(self.roots)
        path.write_text("int changed;\n")
        with self.assertRaises(ValueError): M.load_layout(self.roots)
        path.unlink()
        outside = self.base / "outside.c"
        outside.write_text("int real_generated_fixture;\n")
        path.symlink_to(outside)
        with self.assertRaises(ValueError): M.load_layout(self.roots)
