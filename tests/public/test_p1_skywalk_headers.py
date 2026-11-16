# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/m12-penang-physical"))
try:
    import p1_skywalk_headers as M
finally:
    sys.path.pop(0)

MAKE = """DATAFILES =
PRIVATE_DATAFILES = test.h
INSTALL_MI_LIST = ${DATAFILES}
INSTALL_SF_MI_LCL_LIST = $(sort ${DATAFILES} ${PRIVATE_DATAFILES})
INSTALL_MI_DIR = skywalk
EXPORT_MI_LIST = $(sort ${INSTALL_MI_LIST} ${INSTALL_SF_MI_LCL_LIST})
EXPORT_MI_DIR = ${INSTALL_MI_DIR}
"""


class SkywalkExports(unittest.TestCase):
    def test_only_bounded_literal_export_grammar_is_accepted(self):
        self.assertEqual(M.exports(MAKE), ("skywalk", ["test.h"]))
        self.assertEqual(M.exports(MAKE.replace("test.h", "")), ("skywalk", []))
        for token in ("$(UNKNOWN)", "$(shell touch injected)", "../escape.h", "/escape.h", "test.h;touch", "${DATAFILES}"):
            with self.subTest(token=token), self.assertRaises(ValueError): M.exports(MAKE.replace("test.h", token))
        for bad in (MAKE + "PRIVATE_DATAFILES = duplicate.h\n", MAKE.replace("skywalk\n", "outside\n"),
                    MAKE.replace("$(sort ${DATAFILES} ${PRIVATE_DATAFILES})", "$(wildcard *)")):
            with self.assertRaises(ValueError): M.exports(bad)

    def test_original_bytes_are_copied_and_changed_or_escaping_sources_are_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            root = base / "xnu"
            root.mkdir()
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            for d in M.DIRECTORIES:
                path = root / "bsd/skywalk" / d
                path.mkdir(parents=True, exist_ok=True)
                name = (d or "root") + ".h"
                (path / "Makefile").write_text(MAKE.replace("test.h", name))
                (path / name).write_text("/* owned fixture, preserve bytes */\n")
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            subprocess.run(["git", "-C", str(root), "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                            "commit", "-qm", "owned fixture"], check=True)
            commit = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
            with mock.patch.object(M, "XNU_COMMIT", commit):
                result = M.generate(root, base / "out")
                self.assertEqual(len(result["headers"]), 7)
                self.assertEqual((base / "out/skywalk/root.h").read_bytes(), (root / "bsd/skywalk/root.h").read_bytes())
                with self.assertRaises(ValueError): M.generate(root, base / "out")
                with self.assertRaises(ValueError): M.generate(root, root / "out")
                h = root / "bsd/skywalk/root.h"
                data = h.read_bytes()
                h.write_bytes(data + b"changed")
                with self.assertRaises(ValueError): M.generate(root, base / "changed")
                h.unlink()
                outside = base / "outside.h"
                outside.write_bytes(data)
                h.symlink_to(outside)
                with self.assertRaises(ValueError): M.generate(root, base / "escaped")
                self.assertFalse((base / "escaped").exists())
