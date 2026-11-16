# SPDX-License-Identifier: Apache-2.0
"""Generator admission and real Perl/shell replay using owned synthetic inputs."""
import importlib.util
import os
from pathlib import Path
import subprocess
import shutil
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
                                kwargs.get("pin", MOD.sha(self.tool)), kwargs.get("emit_sources", False))

    def test_actual_shell_modes_emit_selected_bsd_sources_and_argument_header(self):
        script = self.root / MOD.INPUTS[2]
        script.write_text('''# Owned fixtures include the two pinned printf anchors: \\(sy_call_t *\\) \\(sy_call_t *\\)
case "$2" in
header) printf '#define\\tSYS_exit 1\\n' > syscall.h ;;
proto) printf 'struct argument { int value; };\\n' > sysproto.h ;;
table) printf 'int table_provider;\\n' > init_sysent.c ;;
names) printf 'int names_provider;\\n' > syscalls.c ;;
audit) printf 'int audit_provider;\\n' > audit_kevents.c ;;
systrace) printf 'int trace_provider;\\n' > systrace_args.c ;;
*) exit 2 ;;
esac
''')
        subprocess.run(["git", "-C", str(self.root), "add", "."], check=True)
        subprocess.run(["git", "-C", str(self.root), "commit", "-qm", "owned syscall modes"], check=True)
        self.commit = subprocess.check_output(["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True).strip()
        r = self.generate(emit_sources=True)
        self.assertEqual(set(r["sources"]), {"version.c", "bsd/kern/init_sysent.c", "bsd/kern/syscalls.c", "bsd/kern/audit_kevents.c", "bsd/kern/systrace_args.c"})
        self.assertIn("sys/sysproto.h", r["headers"])

    def test_portable_awk_printf_casts_compile_as_c_without_undefined_escapes(self):
        original = rb'BEGIN { printf("\\(sy_call_t *\\)(void (*)(void))nosys"); }' + b"\n" + rb'# second anchor: \(sy_call_t *\)'
        # Exercise the exact original script anchor shape and real host awk.
        original = original.replace(b"\\\\(", b"\\(").replace(b"\\\\)", b"\\)")
        changed = MOD.portable_syscall_generator(original)
        run = subprocess.run(["awk", changed.decode()], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout, "(sy_call_t *)(void (*)(void))nosys")
        consumer = "typedef int sy_call_t(void); int nosys(void); sy_call_t *entry = " + run.stdout + ";\n"
        compiled = subprocess.run([shutil.which("clang"), "--target=arm64-apple-darwin", "-nostdinc", "-x", "c",
                                   "-fsyntax-only", "-"], input=consumer, capture_output=True, text=True)
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        self.assertNotIn(rb"\(sy_call_t", changed)
        with self.assertRaises(ValueError): MOD.portable_syscall_generator(b"anchor drift")

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
