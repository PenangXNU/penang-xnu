#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A branch's public file inventory cannot be invalidated by parallel PRs.

Crucially, forbidden paths, symlinks and private Git object contents remain
scanned across all local refs.
"""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("penang_publication_scan",
                                              REPO / "scripts" / "publication_scan.py")
SCAN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCAN)


def git(path, *args):
    proc = subprocess.run(["git", "-C", str(path), *args],
                          capture_output=True, text=True, timeout=12,
                          check=False)
    if proc.returncode:
        raise AssertionError(proc.stderr)
    return proc.stdout


class CrossBranchHistory(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="penang-public-ref-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        git(self.root, "init", "-b", "main")
        git(self.root, "config", "user.name", "Penang Test")
        git(self.root, "config", "user.email", "test@example.invalid")
        (self.root / "audit").mkdir()
        (self.root / "README.md").write_text("safe source\n", encoding="utf-8")
        self.inventory(["README.md", "audit/public-files.json"])
        self.commit("base")
        git(self.root, "switch", "-c", "parallel")
        (self.root / "parallel.md").write_text("parallel source\n", encoding="utf-8")
        self.inventory(["README.md", "audit/public-files.json", "parallel.md"])
        self.commit("parallel")
        git(self.root, "switch", "main")
        git(self.root, "switch", "-c", "feature")
        (self.root / "feature.md").write_text("feature source\n", encoding="utf-8")
        self.inventory(["README.md", "audit/public-files.json", "feature.md"])
        self.commit("feature")

    def inventory(self, files):
        (self.root / "audit" / "public-files.json").write_text(
            json.dumps({"files": sorted(files), "content_pins": {}}, indent=2) + "\n",
            encoding="utf-8",
        )

    def commit(self, message):
        git(self.root, "add", "-A")
        git(self.root, "commit", "-m", message)

    def test_parallel_branch_does_not_require_foreign_inventory(self):
        result = SCAN.scan(self.root)
        self.assertEqual(result["status"], "PASS", result["issues"])
        self.assertEqual(result["history_status"], "PASS", result["issues"])
        self.assertGreaterEqual(result["commits_checked"], 3)

    def test_current_branch_historical_unlisted_remains_rejected(self):
        (self.root / "transient.md").write_text("unlisted source\n", encoding="utf-8")
        self.commit("unlisted-on-current-branch")
        git(self.root, "rm", "transient.md")
        git(self.root, "commit", "-m", "drop-unlisted")
        result = SCAN.scan(self.root)
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(any(issue["code"] == "HISTORICAL_UNLISTED_FILE"
                            and issue["path"] == "transient.md"
                            for issue in result["issues"]), result["issues"])

    def test_other_branch_forbidden_path_still_rejected(self):
        git(self.root, "switch", "parallel")
        (self.root / "rom-work").mkdir()
        (self.root / "rom-work" / "forbidden.md").write_text("bad path\n", encoding="utf-8")
        self.commit("forbidden-path-on-foreign-branch")
        git(self.root, "switch", "feature")
        result = SCAN.scan(self.root)
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(any(issue["code"] == "FORBIDDEN_PATH"
                            and issue["path"] == "rom-work/forbidden.md"
                            for issue in result["issues"]), result["issues"])


if __name__ == "__main__":
    unittest.main()
