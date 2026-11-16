# SPDX-License-Identifier: Apache-2.0
"""LOCAL_ONLY KDK adapter, exercised with self-authored stand-in headers.
No Apple file is read or needed by these tests."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/p1-penang-platform/kdk/p1_kdk_probe.py"
PINS = ROOT / "tools/p1-penang-platform/kdk/kdk-15.6-24G84-header-pins.json"
SECRET = "STAND_IN_PRIVATE_LINE_7f3a"  # must never reach a report


class KdkProbe(unittest.TestCase):
    def setUp(self):
        self.cc = shutil.which("clang-18") or shutil.which("clang")
        if not self.cc: self.skipTest("Clang required")
        self.folder = tempfile.TemporaryDirectory(prefix="p1-kdk-")
        self.root = Path(self.folder.name)
        self.kdk = self.root / "kdk/PrivateHeaders/platform"
        files = {"TrustCache/API.h": "/* %s */\ntypedef struct { int error; } TCReturn_t;\n#define kUUIDSize 16\n" % SECRET,
                 "CodeSignature/Entitlements.h": '#define kCSWebBrowserHostEntitlement "stand.in.host"\n'}
        for name, text in files.items():
            (self.kdk / name).parent.mkdir(parents=True, exist_ok=True)
            (self.kdk / name).write_text(text)
        self.pins = self.root / "pins.json"
        self.pins.write_text(json.dumps({"kdk": "STAND_IN", "headers": {
            n: {"bytes": len(t.encode()), "sha256": hashlib.sha256(t.encode()).hexdigest()} for n, t in files.items()}}))
        self.src = self.root / "src"
        self.src.mkdir()
        (self.src / "good.c").write_text("#include <TrustCache/API.h>\nint provider(TCReturn_t r) { return r.error + kUUIDSize; }\n")
        (self.src / "empty.c").write_text("#include <CodeSignature/Entitlements.h>\nstatic int unused;\n")
        (self.src / "bad.c").write_text("#include <TrustCache/API.h>\n#include <arm64/ppl/sart.h>\nint x;\n")
        (self.src / "misuse.c").write_text("#include <TrustCache/API.h>\nint f(TCReturn_t r) { return r.component; }\n")

    def tearDown(self): self.folder.cleanup()

    def tool(self, *args, pins=None):
        return subprocess.run([sys.executable, "-B", str(TOOL), "--pins", str(pins or self.pins), *args],
                              capture_output=True, text=True)

    def plan(self, *names, extra=()):
        path = self.root / "plan.json"
        path.write_text(json.dumps({"xnu_root": str(self.src), "units": [
            {"name": n, "source": str(self.src / n), "argv": ["-std=c11", "-Wno-unused", *extra]} for n in names]}))
        return path

    def compile(self, plan, out="out"):
        return self.tool("compile", "--kdk-headers", str(self.kdk), "--plan", str(plan), "--compiler", self.cc,
                         "--out", str(self.root / out))

    def test_published_pins_hold_hashes_only(self):
        pins = json.loads(PINS.read_text(encoding="utf-8"))
        self.assertEqual((pins["kdk"], len(pins["headers"])), ("KDK_15.6_24G84", 21))
        for name in ("TrustCache/API.h", "CodeSignature/Entitlements.h"):
            self.assertIn(name, pins["headers"])
        for name, row in pins["headers"].items():
            self.assertEqual(set(row), {"bytes", "sha256"})
            self.assertRegex(row["sha256"], r"^[0-9a-f]{64}$")
        self.assertNotIn("#define", PINS.read_text(encoding="utf-8"))

    def test_verify_accepts_exact_headers_and_refuses_every_deviation(self):
        self.assertIn("KDK_HEADERS_MATCH_PINS", self.tool("verify", "--kdk-headers", str(self.kdk)).stdout)
        api = self.kdk / "TrustCache/API.h"
        original = api.read_text()
        api.write_text(original + " ")
        self.assertIn("KDK_HEADER_HASH_MISMATCH", self.tool("verify", "--kdk-headers", str(self.kdk)).stderr)
        api.unlink()
        self.assertIn("KDK_HEADER_MISSING_OR_UNSAFE", self.tool("verify", "--kdk-headers", str(self.kdk)).stderr)
        (self.root / "elsewhere.h").write_text(original)
        api.symlink_to(self.root / "elsewhere.h")
        self.assertIn("KDK_HEADER_MISSING_OR_UNSAFE", self.tool("verify", "--kdk-headers", str(self.kdk)).stderr)
        api.unlink()
        api.write_text(original)
        (self.kdk / "TrustCache/Injected.h").write_text("#define kUUIDSize 1\n")
        self.assertIn("KDK_UNPINNED_FILE", self.tool("verify", "--kdk-headers", str(self.kdk)).stderr)
        (self.kdk / "TrustCache/Injected.h").unlink()
        self.assertIn("KDK_HEADERS_NOT_A_DIRECTORY", self.tool("verify", "--kdk-headers", str(self.root / "none")).stderr)
        # The real pins never match stand-in content.
        self.assertNotEqual(self.tool("verify", "--kdk-headers", str(self.kdk), pins=PINS).returncode, 0)

    def test_compile_reports_states_without_header_content_or_local_paths(self):
        result = self.compile(self.plan("good.c", "empty.c", "bad.c", "misuse.c"))
        self.assertEqual(json.loads(result.stdout)["compiled"], 2, result.stderr)
        text = (self.root / "out/report.json").read_text()
        report = json.loads(text)
        states = {u["source"]: u["status"] for u in report["units"]}
        self.assertEqual(states["good.c"], "COMPILED_OBJECT_ONLY")
        self.assertIn(states["empty.c"], ("COMPILED_EMPTY_NONPROVIDER", "COMPILED_OBJECT_ONLY"))
        self.assertEqual((states["bad.c"], states["misuse.c"]), ("BLOCKED", "BLOCKED"))
        bad = next(u for u in report["units"] if u["source"] == "bad.c")
        self.assertIn("'arm64/ppl/sart.h' file not found", bad["first_error"])
        self.assertTrue(bad["first_error"].startswith("<XNU>/bad.c"))
        self.assertNotIn(SECRET, text)
        self.assertNotIn(str(self.root), text)
        self.assertEqual((report["contains_apple_header_content"], report["link"], report["runtime"]), (False, "NOT_RUN", "NOT_RUN"))
        self.assertFalse((self.root / "out/002.o").exists(), "a failed unit leaves no object")
        self.assertEqual([p.name for p in (self.root / "out").iterdir() if p.suffix == ".h"], [])

    def test_compile_refuses_unverified_headers_reused_output_and_exfiltrating_plans(self):
        self.assertIn("OUTPUT_AND_KDK_MUST_BE_DISJOINT", self.compile(self.plan("good.c"), out="kdk/PrivateHeaders/platform/out").stderr)
        (self.root / "taken").mkdir()
        self.assertIn("OUTPUT_EXISTS", self.compile(self.plan("good.c"), out="taken").stderr)
        for index, flag in enumerate(("-E", "-MD", "-dM", "-H", "-frewrite-includes", "-o")):
            with self.subTest(flag=flag):
                result = self.compile(self.plan("good.c", extra=[flag]), out="x%d" % index)
                self.assertRegex(result.stderr, "PLAN_UNIT_(FORBIDDEN_FLAG|INVALID)")
        empty = self.root / "empty-plan.json"
        empty.write_text('{"units": []}')
        self.assertIn("PLAN_HAS_NO_UNITS", self.compile(empty, out="y").stderr)
        (self.kdk / "CodeSignature/Entitlements.h").write_text("#define tampered 1\n")
        self.assertIn("KDK_HEADER_HASH_MISMATCH", self.compile(self.plan("good.c"), out="z").stderr)
        self.assertFalse((self.root / "z").exists())

    def test_find_prints_paths_and_hashes_only(self):
        (self.root / "kdk/deep").mkdir()
        (self.root / "kdk/deep/sart.h").write_text("/* %s */\n" % SECRET)
        result = self.tool("find", "--kdk-root", str(self.root / "kdk"), "sart.h", "uat.h")
        rows = json.loads(result.stdout)["kdk_root_relative"]
        self.assertEqual([(r["path"], set(r)) for r in rows], [("deep/sart.h", {"path", "bytes", "sha256"})])
        self.assertNotIn(SECRET, result.stdout)


if __name__ == "__main__":
    unittest.main()
