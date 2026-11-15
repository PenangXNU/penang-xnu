#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Retired workflow files may leave the tree without erasing their Git provenance."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("retired_public_scanner", ROOT / "scripts/publication_scan.py")
SCAN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(SCAN)


def git(root, *args, check=True):
    p = subprocess.run(["git", "-C", str(root), *args],
                       capture_output=True, text=True, timeout=15, check=False)
    if check and p.returncode:
        raise AssertionError(p.stderr)
    return p


class RetiredFiles(unittest.TestCase):
    def setUp(self):
        t = tempfile.TemporaryDirectory(prefix="penang-retire-")
        self.addCleanup(t.cleanup)
        self.root = Path(t.name)
        git(self.root, "init", "-b", "main")
        git(self.root, "config", "user.name", "Penang synthetic")
        git(self.root, "config", "user.email", "test@example.invalid")
        (self.root / "audit").mkdir()
        (self.root / "README.md").write_text("public source\n", encoding="utf-8")
        (self.root / "W4-HANDOFF.md").write_text("historical internal receipt\n", encoding="utf-8")
        self.sha = hashlib.sha256((self.root / "W4-HANDOFF.md").read_bytes()).hexdigest()
        self.inventory(["README.md", "W4-HANDOFF.md", "audit/public-files.json"])
        self.commit("original inventoried")
        (self.root / "W4-HANDOFF.md").unlink()
        self.inventory(["README.md", "audit/public-files.json"],
                       {"W4-HANDOFF.md": [self.sha]})
        self.commit("retired from public tree")

    def inventory(self, files, retired=None):
        d = {"files": sorted(files), "content_pins": {}}
        if retired is not None:
            d["retired_files"] = retired
        (self.root / "audit/public-files.json").write_text(
            json.dumps(d, indent=2) + "\n", encoding="utf-8"
        )

    def commit(self, message):
        git(self.root, "add", "-A")
        git(self.root, "commit", "-m", message)

    def test_retired_file_preserves_pinned_history(self):
        result = SCAN.scan(self.root)
        self.assertEqual(result["status"], "PASS", result["issues"])
        self.assertEqual(result["history_status"], "PASS")

    def test_unseen_hash_is_rejected(self):
        self.inventory(["README.md", "audit/public-files.json"],
                       {"W4-HANDOFF.md": [self.sha, "0" * 64]})
        self.commit("invalid new pin")
        result = SCAN.scan(self.root)
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(any(x["code"] == "RETIRED_PIN_UNVERIFIED"
                            for x in result["issues"]), result["issues"])

    def test_changed_historical_blob_not_silently_accepted(self):
        # A later inventoried version must also be pinned when retired.
        (self.root / "W4-HANDOFF.md").write_text("modified receipt\n", encoding="utf-8")
        self.inventory(["README.md", "W4-HANDOFF.md", "audit/public-files.json"])
        self.commit("second active version")
        (self.root / "W4-HANDOFF.md").unlink()
        self.inventory(["README.md", "audit/public-files.json"],
                       {"W4-HANDOFF.md": [self.sha]})
        self.commit("retire without second hash")
        result = SCAN.scan(self.root)
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(any(x["code"] == "RETIRED_BLOB_UNPINNED"
                            for x in result["issues"]), result["issues"])

    def test_unlisted_historical_file_still_rejected(self):
        (self.root / "transient.md").write_text("never inventoried\n", encoding="utf-8")
        self.commit("unlisted")
        (self.root / "transient.md").unlink()
        self.commit("deleted unlisted")
        result = SCAN.scan(self.root)
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(any(x["code"] == "HISTORICAL_UNLISTED_FILE"
                            and x["path"] == "transient.md" for x in result["issues"]),
                        result["issues"])


if __name__ == "__main__":
    unittest.main()
