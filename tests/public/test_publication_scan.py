# SPDX-License-Identifier: Apache-2.0
"""Synthetic publication negatives, including a deleted secret in local Git history."""
from pathlib import Path
import importlib.util
import json
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("publication_scan", ROOT / "scripts/publication_scan.py")
scanner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scanner)

class PublicationScanTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / ".build/w4/scanner-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "audit").mkdir()
        self.put("safe.txt", "public fixture\n")

    def put(self, name, text):
        (self.root / name).write_text(text)
        files = [p.relative_to(self.root).as_posix() for p in self.root.rglob("*") if p.is_file()]
        files = sorted(set(files) | {"audit/public-files.json"})
        (self.root / "audit/public-files.json").write_text(json.dumps({"files": files}))

    def codes(self):
        return {x["code"] for x in scanner.scan(self.root)["issues"]}

    def test_safe_fixture_passes(self):
        self.assertEqual(scanner.scan(self.root)["status"], "PASS")

    def test_credential_pattern_is_rejected(self):
        self.put("token.txt", "gh" + "p_" + "A" * 36)
        self.assertIn("GITHUB_TOKEN", self.codes())

    def test_private_absolute_path_is_rejected(self):
        self.put("location.txt", "/" + "home/" + "privateuser/" + "artifact")
        self.assertIn("PRIVATE_PATH", self.codes())

    def test_archive_is_rejected(self):
        self.put("payload.zip", "not even a real archive")
        self.assertIn("FORBIDDEN_FILE_TYPE", self.codes())

    def test_private_seed_extension_is_rejected(self):
        self.put("private.seed", "A" * 32)
        self.assertIn("FORBIDDEN_FILE_TYPE", self.codes())

    def test_symlink_is_rejected(self):
        (self.root / "alias").symlink_to(self.root / "safe.txt")
        self.assertIn("SYMLINK", self.codes())

    def test_deleted_secret_is_still_rejected_in_history(self):
        def git(*args):
            subprocess.run(["git", "-C", str(self.root), *args], check=True, capture_output=True)
        git("init", "-b", "fixture")
        git("config", "user.name", "Synthetic fixture")
        git("config", "user.email", "fixture@localhost")
        git("add", "safe.txt", "audit/public-files.json")
        git("-c", "commit.gpgsign=false", "commit", "-m", "safe")
        (self.root / "deleted.txt").write_text("gh" + "p_" + "B" * 36)
        git("add", "deleted.txt")
        git("-c", "commit.gpgsign=false", "commit", "-m", "synthetic negative")
        (self.root / "deleted.txt").unlink()
        git("add", "-u")
        git("-c", "commit.gpgsign=false", "commit", "-m", "delete fixture")
        self.assertIn("GITHUB_TOKEN", self.codes())
