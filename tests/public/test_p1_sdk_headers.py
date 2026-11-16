# SPDX-License-Identifier: Apache-2.0
"""Real isolated generator processes with owned inputs and admission negatives."""
import json
import os
import hashlib
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/m12-penang-physical"))
try:
    import p1_sdk_headers as M
finally:
    sys.path.pop(0)


class SdkHeaders(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix="public-sdk-subset-")
        self.addCleanup(self.folder.cleanup)
        self.base = Path(self.folder.name)
        self.xnu, self.av = self.base / "xnu", self.base / "availability"
        for root, names in ((self.xnu, M.BSD_INPUTS), (self.av, (*M.AV_INPUTS, *("templates/"+n for n in M.TEMPLATES)))):
            root.mkdir()
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("owned fixture\n")
        (self.av / "availability").write_text('''import pathlib,sys,os
assert 'DRIVERKITROOT' not in os.environ and 'PYTHONPATH' not in os.environ
if '--preprocess' in sys.argv:
    i=sys.argv.index('--preprocess');pathlib.Path(sys.argv[i+2]).write_bytes(pathlib.Path(sys.argv[i+1]).read_bytes())
else: print('10.15')
''')
        (self.av / "availability.pl").write_text('#!/bin/sh\nexec "$(dirname "$0")/availability" "$@"\n')
        (self.xnu / M.BSD_INPUTS[1]).write_text('printf "__DARWIN_ALIAS_STARTING_MAC___MAC_10_15(x) x\\n" > "$2"\n')
        (self.xnu / M.BSD_INPUTS[2]).write_text('printf "___POSIX_C_DEPRECATED_STARTING_200809L\\n" > "$1"\n')
        self.pins = []
        for root in (self.xnu, self.av):
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            subprocess.run(["git", "-C", str(root), "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                            "commit", "-qm", "owned fixture"], check=True)
            self.pins.append(subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip())

    def generate(self, out=None):
        with mock.patch.object(M, "XNU_COMMIT", self.pins[0]), mock.patch.object(M, "AV_COMMIT", self.pins[1]):
            return M.generate(self.xnu, self.av, out or self.base / "out")

    def test_isolated_processes_generate_ten_headers_and_never_claim_sdk_or_kernel(self):
        with mock.patch.dict(os.environ, {"DRIVERKITROOT": "injected", "PYTHONPATH": "injected", "BASH_ENV": "injected"}):
            r = self.generate()
        self.assertEqual(len(r["headers"]), 10)
        self.assertEqual(r["full_sdk"], "NOT_PROVIDED")
        self.assertEqual(r["native_xnu_full_link"], "NOT_RUN")
        self.assertTrue(all(c["returncode"] == 0 for c in r["commands"]))
        with self.assertRaises(ValueError): self.generate()

    def test_wrong_revision_and_changed_inputs_are_refused_before_output(self):
        with self.assertRaises(ValueError): M.generate(self.xnu, self.av, self.base / "out")
        p = self.av / "availability.dsl"
        p.write_text("changed")
        with self.assertRaises(ValueError): self.generate()
        self.assertFalse((self.base / "out").exists())

    def test_symlink_input_and_overlapping_output_are_refused(self):
        for root in (self.xnu, self.av, ROOT):
            with self.assertRaises(ValueError): self.generate(root / "refused-header-output")
        path = self.av / "availability.dsl"
        outside = self.base / "outside"
        outside.write_bytes(path.read_bytes())
        path.unlink()
        path.symlink_to(outside)
        with self.assertRaises(ValueError): self.generate()
        self.assertFalse((self.base / "out").exists())

    def test_failed_generator_preserves_log_without_pass_receipt(self):
        original = subprocess.run
        def run(command, **kwargs):
            if command[0] == "python3":
                return subprocess.CompletedProcess(command, 1, b"", b"failed")
            return original(command, **kwargs)
        with mock.patch.object(M.subprocess, "run", side_effect=run):
            with self.assertRaises(ValueError): self.generate()
        self.assertEqual((self.base / "out/0.log").read_bytes(), b"failed")
        self.assertFalse((self.base / "out/results.json").exists())

    def test_portable_conditionals_keep_compiler_detection_and_avoid_sdk_self_recursion(self):
        source = self.base / "owned-conditionals.h"
        source.write_text('''#if __has_include(<TargetConditionals.h>)
#include <TargetConditionals.h>
#else
#if defined(__aarch64__)
#define TARGET_CPU_ARM64 1
#else
#define TARGET_CPU_ARM64 0
#endif
#if defined(__APPLE__)
#define TARGET_OS_DARWIN 1
#else
#define TARGET_OS_DARWIN 0
#endif
#define TARGET_OS_OSX TARGET_OS_DARWIN
#endif
''')
        with self.assertRaises(ValueError): M.portable_conditionals(source)
        pin = hashlib.sha256(source.read_bytes()).hexdigest()
        include = self.base / "include"
        include.mkdir()
        with mock.patch.object(M, "CF_SHA256", pin):
            (include / "TargetConditionals.h").write_bytes(M.portable_conditionals(source))
        for target, expected in (("arm64-apple-darwin", 1), ("x86_64-unknown-linux-gnu", 0)):
            r = subprocess.run([shutil.which("clang"), "--target="+target, "-nostdinc", "-I", str(include),
                                "-DTARGET_OS_OSX="+str(expected), "-Werror=macro-redefined",
                                "-x", "c", "-fsyntax-only", "-"], input='#include <TargetConditionals.h>\n'
                               f'_Static_assert(TARGET_CPU_ARM64 == {expected}, "CPU");\n'
                               f'_Static_assert(TARGET_OS_OSX == {expected}, "OS");\n', capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
        with mock.patch.object(M, "CF_SHA256", pin):
            link = self.base / "symlink.h"
            link.symlink_to(source)
            with self.assertRaises(ValueError): M.portable_conditionals(link)
