# SPDX-License-Identifier: Apache-2.0
"""Generator admission and real Perl/shell replay using owned synthetic inputs."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("generated", ROOT / "tools/m12-penang-physical/p1_generated_headers.py")
MOD = importlib.util.module_from_spec(spec)
spec.loader.exec_module(MOD)


class GeneratedHeaders(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix="p1-upstream-generation-")
        self.addCleanup(self.folder.cleanup)
        self.base = Path(self.folder.name)
        self.root = self.base / "source"
        self.root.mkdir()
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        subprocess.run(["git", "-C", str(self.root), "config", "user.name", "Synthetic test"], check=True)
        subprocess.run(["git", "-C", str(self.root), "config", "user.email", "test@example.invalid"], check=True)
        for name in MOD.INPUTS:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("owned synthetic generator input\n")
        (self.root / MOD.INPUTS[1]).write_text("#define VERSION ###KERNEL_VERSION_LONG###\n")
        (self.root / MOD.INPUTS[0]).write_text('''use strict;
die "injected" if exists $ENV{"PERL5OPT"};
open(my $v,"<","$ENV{OBJROOT}/xnuVersion") or die;
my $version=<$v>;chomp($version);
open(my $f,">",$ARGV[0]) or die; print $f "#define VERSION $version\\n";
''')
        (self.root / MOD.INPUTS[2]).write_text("printf '#define\\tSYS_exit 1\\n' > syscall.h\n")
        subprocess.run(["git", "-C", str(self.root), "add", "."], check=True)
        subprocess.run(["git", "-C", str(self.root), "commit", "-qm", "owned fixture"], check=True)
        self.commit = subprocess.check_output(["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True).strip()
        self.tool = self.base / "synthetic-unifdef"
        self.tool.write_text('#!/bin/sh\ncat "$1"\n')
        self.tool.chmod(0o700)

    def generate(self, **kwargs):
        with mock.patch.object(MOD, "XNU_COMMIT", self.commit):
            return MOD.generate(self.root, kwargs.get("out", self.base / "output"),
                                kwargs.get("version", "24.6.0d1"), self.tool,
                                kwargs.get("pin", MOD.sha(self.tool)))

    def test_real_processes_preserve_explicit_metadata_and_do_not_execute_make(self):
        with mock.patch.dict(os.environ, {"PERL5OPT": "-e injected"}):
            result = self.generate()
        self.assertEqual(result["darwin_build_version"], "24.6.0d1")
        self.assertEqual(result["seed_defines"], [])
        self.assertEqual(len(result["headers"]), 2)
        self.assertEqual(result["device_entry"], "NOT_RUN")
        self.assertEqual((self.base / "output/libkern/version.h").read_text(), "#define VERSION 24.6.0d1\n")
        with self.assertRaises(ValueError): self.generate()

    def test_version_injection_binary_pin_and_wrong_tree_are_refused_before_output(self):
        for version in ("", "24.6.0;touch x", "$(id)", "24.6.0\n", "0.0.0", "24.6.0d1/../"):
            with self.subTest(version=version), self.assertRaises(ValueError): self.generate(version=version)
        with self.assertRaises(ValueError): self.generate(pin="0"*64)
        with self.assertRaises(ValueError): MOD.generate(self.root,self.base/"output","24.6.0",self.tool,MOD.sha(self.tool))
        self.assertFalse((self.base / "output").exists())

    def test_drift_and_symlink_escape_do_not_execute_a_modified_generator(self):
        p = self.root / MOD.INPUTS[0]
        p.write_text('die "changed";')
        with self.assertRaises(ValueError): self.generate()
        p.unlink();p.symlink_to(self.tool)
        with self.assertRaises(ValueError): self.generate()
        self.assertFalse((self.base / "output").exists())

    def test_source_and_public_output_directories_are_refused(self):
        for out in (self.root / "new", ROOT / ".build/refused-generated-headers"):
            with self.subTest(out=out), self.assertRaises(ValueError): self.generate(out=out)
            self.assertFalse(out.exists())

    def test_failing_generator_keeps_diagnostics_but_never_emits_a_pass_receipt(self):
        self.tool.write_text("#!/bin/sh\nexit 2\n")
        with self.assertRaises(ValueError): self.generate()
        self.assertTrue((self.base / "output/1.log").is_file())
        self.assertFalse((self.base / "output/results.json").exists())
