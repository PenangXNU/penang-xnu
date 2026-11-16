# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/m12-penang-physical"))
try:
    import p1_firehose_headers as M
finally:
    sys.path.pop(0)


class FirehoseHeaders(unittest.TestCase):
    def test_export_uses_upstream_flags_and_missing_admission_or_drift_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            root = base / "source"
            root.mkdir()
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            for name in (M.CONFIG, M.HEADER):
                (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / M.CONFIG).write_text("COPY_HEADERS_UNIFDEF_FLAGS = " + " ".join(M.FLAGS) + "\n")
            (root / M.HEADER).write_text("#if KERNEL && OS_FIREHOSE_SPI\nint kernel_provider;\n#else\n#error missing real kernel export flags\n#endif\n")
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            subprocess.run(["git", "-C", str(root), "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                            "commit", "-qm", "owned fixture"], check=True)
            commit = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
            # Owned process adapter; the private upstream replay uses real unifdef.
            tool = base / "synthetic-unifdef"
            tool.write_text('#!/bin/sh\nexec "' + shutil.which("clang") + '" -E -P -x c "$@"\n')
            tool.chmod(0o700)
            with mock.patch.object(M, "COMMIT", commit):
                with self.assertRaises(ValueError): M.generate(root, tool, "0"*64, base / "out")
                self.assertFalse((base / "out").exists())
                r = M.generate(root, tool, M.sha(tool), base / "out")
                self.assertEqual((base / "out" / M.HEADER).read_text().strip(), "int kernel_provider;")
                self.assertEqual(r["native_xnu_full_link"], "NOT_RUN")
                with self.assertRaises(ValueError): M.generate(root, tool, M.sha(tool), base / "out")
                with self.assertRaises(ValueError): M.generate(root, tool, M.sha(tool), root / "out")
                (root / M.CONFIG).write_text("COPY_HEADERS_UNIFDEF_FLAGS = -DKERNEL=0\n")
                with self.assertRaises(ValueError): M.generate(root, tool, M.sha(tool), base / "changed")
