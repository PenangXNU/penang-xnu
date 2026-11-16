#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""LOCAL_ONLY adapter for the owner-held Apple KDK private headers.

The Kernel Development Kit is proprietary. This tool never copies, prints or
stores its content: it verifies a header directory against published SHA-256
pins, locates named headers, and runs caller-supplied compile commands with
that directory added to the include path. Its report holds paths relative to
the KDK directory, hashes, exit codes and symbol counts only.

Run it on a machine where the KDK may lawfully be used. It executes the
compiler commands found in the plan; it performs no network access.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

SCHEMA = "penang-kdk-probe-report-1"
REQUIRED = ("TrustCache/API.h", "CodeSignature/Entitlements.h")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inside(child, parent):
    try:
        Path(child).resolve().relative_to(Path(parent).resolve())
        return True
    except ValueError:
        return False


def verify(headers, pins):
    """Every pinned header present and byte-exact; no symlinks; nothing unpinned in pinned folders."""
    headers = Path(headers)
    if not headers.is_dir():
        raise SystemExit("KDK_HEADERS_NOT_A_DIRECTORY")
    table = pins["headers"]
    if any(name not in table for name in REQUIRED):
        raise SystemExit("PINS_INCOMPLETE")
    rows = []
    for name, want in sorted(table.items()):
        path = headers / name
        if ".." in Path(name).parts or Path(name).is_absolute() or path.is_symlink() or not path.is_file():
            raise SystemExit("KDK_HEADER_MISSING_OR_UNSAFE " + name)
        data = path.read_bytes()
        if len(data) != want["bytes"] or hashlib.sha256(data).hexdigest() != want["sha256"]:
            raise SystemExit("KDK_HEADER_HASH_MISMATCH " + name)
        rows.append(name)
    for folder in sorted({Path(name).parts[0] for name in table}):
        for path in (headers / folder).rglob("*"):
            if path.is_file() and path.relative_to(headers).as_posix() not in table:
                raise SystemExit("KDK_UNPINNED_FILE " + path.relative_to(headers).as_posix())
    return rows


def find(root, names):
    root = Path(root)
    found = []
    for path in sorted(root.rglob("*")):
        if path.is_file() and not path.is_symlink() and path.name in names:
            found.append({"path": path.relative_to(root).as_posix(), "bytes": path.stat().st_size, "sha256": digest(path)})
    return found


def scrub(line, headers, extra):
    """One diagnostic line with local paths removed. Source excerpts are never kept."""
    line = line.replace(str(Path(headers).resolve()), "<KDK>").replace(str(headers), "<KDK>")
    for label, path in extra.items():
        line = line.replace(str(Path(path).resolve()), label).replace(str(path), label)
    return re.sub(r"(?:/(?:home|Users|mnt|root|tmp|workspace)/)[^\s:'\"]*/", "<LOCAL>/", line)[:300]


def symbols(nm, obj, flag):
    out = subprocess.run([nm, flag, "--extern-only", str(obj)], capture_output=True, text=True)
    if out.returncode:
        return None
    return sorted({line.split()[-1] for line in out.stdout.splitlines() if line.strip()})


def compile_units(args, pins):
    headers = Path(args.kdk_headers)
    verified = verify(headers, pins)
    out = Path(args.out)
    if out.exists():
        raise SystemExit("OUTPUT_EXISTS")
    if inside(out, headers) or inside(headers, out):
        raise SystemExit("OUTPUT_AND_KDK_MUST_BE_DISJOINT")
    plan = json.loads(Path(args.plan).read_text(encoding="utf-8"))
    units = plan.get("units")
    if not isinstance(units, list) or not units:
        raise SystemExit("PLAN_HAS_NO_UNITS")
    compiler = Path(args.compiler).resolve()
    nm = args.nm or shutil.which("llvm-nm")
    out.mkdir(parents=True)
    labels = {"<XNU>": plan.get("xnu_root", "/nonexistent-xnu-root"), "<OUT>": out}
    rows = []
    for index, unit in enumerate(units):
        argv = unit.get("argv")
        if (not isinstance(argv, list) or not all(isinstance(x, str) for x in argv) or "-o" in argv or
                not isinstance(unit.get("source"), str)):
            raise SystemExit("PLAN_UNIT_INVALID %d" % index)
        # Never let a plan smuggle the proprietary directory elsewhere or preprocess it to a file.
        if any(x in ("-E", "-M", "-MD", "-MMD", "-MF", "-dM", "-dD", "-H", "-save-temps") or x.startswith("-frewrite")
               for x in argv):
            raise SystemExit("PLAN_UNIT_FORBIDDEN_FLAG %d" % index)
        obj = out / ("%03d.o" % index)
        cmd = [str(compiler), *argv, "-I", str(headers), "-c", unit["source"], "-o", str(obj)]
        run = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout)
        errors = [line for line in run.stderr.splitlines() if " error:" in line]
        row = {"source": unit.get("name", unit["source"]), "returncode": run.returncode, "error_count": len(errors),
               "first_error": scrub(errors[0], headers, labels) if errors else None,
               "kdk_headers_named_in_diagnostics": sorted({m for m in re.findall(
                   re.escape(str(headers)) + r"/([A-Za-z0-9_./-]+\.h)", run.stderr)}),
               "status": "BLOCKED"}
        if run.returncode == 0 and obj.is_file():
            row.update(status="COMPILED_OBJECT_ONLY", object_sha256=digest(obj), object_bytes=obj.stat().st_size)
            if nm:
                defined, undefined = symbols(nm, obj, "--defined-only"), symbols(nm, obj, "--undefined-only")
                row.update(defined_external=len(defined or []), undefined=undefined)
                if not defined:
                    row["status"] = "COMPILED_EMPTY_NONPROVIDER"
        elif obj.exists():
            obj.unlink()
        rows.append(row)
    report = {
        "schema": SCHEMA, "scope": "LOCAL_ONLY_PRIVATE_BUILD_INPUT", "kdk": pins["kdk"],
        "kdk_headers_verified": len(verified), "compiler_sha256": digest(compiler),
        "compiler_version": subprocess.run([str(compiler), "--version"], capture_output=True, text=True).stdout.splitlines()[:1],
        "units": rows, "compiled": sum(r["status"] != "BLOCKED" for r in rows), "blocked": sum(r["status"] == "BLOCKED" for r in rows),
        "contains_apple_header_content": False, "link": "NOT_RUN", "runtime": "NOT_RUN",
        "limits": "Object compilation only. Not an interface qualification, a link, a trust policy or a device result.",
    }
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pins", default=str(Path(__file__).with_name("kdk-15.6-24G84-header-pins.json")))
    sub = parser.add_subparsers(dest="command", required=True)
    v = sub.add_parser("verify", help="check a header directory against the pins")
    v.add_argument("--kdk-headers", required=True)
    f = sub.add_parser("find", help="locate headers by file name inside a KDK tree; prints paths and hashes only")
    f.add_argument("--kdk-root", required=True)
    f.add_argument("names", nargs="+")
    c = sub.add_parser("compile", help="run a compile plan with the verified headers on the include path")
    for name in ("--kdk-headers", "--plan", "--compiler", "--out"):
        c.add_argument(name, required=True)
    c.add_argument("--nm")
    c.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args(argv)
    pins = json.loads(Path(args.pins).read_text(encoding="utf-8"))
    if args.command == "verify":
        print(json.dumps({"status": "KDK_HEADERS_MATCH_PINS", "kdk": pins["kdk"], "headers": len(verify(args.kdk_headers, pins))}))
    elif args.command == "find":
        print(json.dumps({"kdk_root_relative": find(args.kdk_root, set(args.names))}, indent=2))
    else:
        report = compile_units(args, pins)
        print(json.dumps({"compiled": report["compiled"], "blocked": report["blocked"], "report": "report.json"}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
