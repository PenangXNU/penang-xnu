#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Public-only validation. Dependency acquisition is explicit and never performed here."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path(".build/w4/public-validation"))
    args = parser.parse_args()
    os.chdir(ROOT)
    out = args.out.resolve()
    if not out.is_relative_to((ROOT / ".build").resolve()):
        raise SystemExit("output must remain under this candidate's .build")
    out.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, "CARGO_TARGET_DIR": str(ROOT / ".build/w2w3/cargo"),
           "PYTHONDONTWRITEBYTECODE": "1", "GIT_OPTIONAL_LOCKS": "0"}
    commands = [
        ("documentary", [sys.executable, "-B", "scripts/current_status.py"]),
        ("build", ["cargo", "build", "--offline", "--locked", "--manifest-path", "tools/penang-validate/Cargo.toml"]),
        ("rust", ["cargo", "test", "--offline", "--locked", "--manifest-path", "tools/penang-validate/Cargo.toml"]),
        ("synthetic", [sys.executable, "-B", "-m", "unittest", "discover", "-s", "tests/platform", "-v"]),
        ("public-build-and-boundary", [sys.executable, "-B", "-m", "unittest", "discover", "-s", "tests/public", "-v"]),
        ("clippy", ["cargo", "clippy", "--offline", "--locked", "--manifest-path", "tools/penang-validate/Cargo.toml", "--all-targets", "--", "-D", "warnings"]),
        ("scan", [sys.executable, "-B", "scripts/publication_scan.py"]),
    ]
    rows = []
    for name, command in commands:
        try:
            p = subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True, timeout=360)
            text = p.stdout + p.stderr
            code = p.returncode
        except (OSError, subprocess.TimeoutExpired) as exc:
            text, code = str(exc), 3
        log = out / (name + ".log")
        log.write_text(text, encoding="utf-8")
        row = {"group": name, "status": "PASS" if code == 0 else "NOT_TESTED" if code == 3 else "FAIL",
               "returncode": code, "command": [Path(x).name if x == sys.executable else x for x in command],
               "log": log.relative_to(ROOT).as_posix(),
               "sha256": hashlib.sha256(log.read_bytes()).hexdigest()}
        if name in {"synthetic", "public-build-and-boundary"}:
            match = re.search(r"Ran (\d+) tests?", text)
            row["tests"] = int(match[1]) if match else 0
            row["cases"] = [{"case": n, "status": "PASS" if status == "ok" else "NOT_TESTED" if status.startswith("skipped") else "FAIL"}
                            for n, status in re.findall(r"^(test_[^\n]+?) \.\.\. (ok|FAIL|ERROR|skipped[^\n]*)$", text, re.M)]
        if name == "rust":
            row["cases"] = [{"case": n, "status": "PASS" if status == "ok" else "FAIL"}
                            for n, status in re.findall(r"^test (\S+) \.\.\. (ok|FAILED)$", text, re.M)]
        if name == "scan" and code == 0:
            row["scan"] = json.loads(text)
        rows.append(row)
        print(json.dumps({k:v for k,v in row.items() if k not in {"cases", "scan", "command"}}), flush=True)
        if name == "build" and code != 0:
            break
    result = {"status": "PASS" if len(rows) == len(commands) and all(r["status"] == "PASS" for r in rows) else "FAIL",
              "scope": "HOST_TESTS_AND_ARM64_COMPILATION_ONLY", "runtime": "NOT_TESTED",
              "groups": rows, "dependency_acquisition": False}
    (out / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"status": result["status"], "results": (out / "results.json").relative_to(ROOT).as_posix()}))
    return 0 if result["status"] == "PASS" else 1

if __name__ == "__main__":
    raise SystemExit(main())
