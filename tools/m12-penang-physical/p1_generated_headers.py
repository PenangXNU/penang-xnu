#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate real version/syscall headers from pinned XNU, outside the checkout.

The Darwin version is explicit build metadata, never inferred from the XNU tag.
No SDK declarations, Make execution, target execution or security fallback.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

XNU_COMMIT = "43a90889846e00bfb5cf1d255cdc0a701a1e05a4"
REPO = Path(__file__).resolve().parents[2]
INPUTS = ("config/newvers.pl", "libkern/libkern/version.h.template",
          "bsd/kern/makesyscalls.sh", "bsd/kern/syscalls.master",
          "makedefs/MakeInc.kernel", "makedefs/MakeInc.rule", "bsd/sys/Makefile",
          "libkern/libkern/Makefile", "bsd/conf/Makefile.template", "config/version.c.template")


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def version_valid(value):
    return re.fullmatch(r"[1-9][0-9]{0,3}\.[0-9]{1,3}\.[0-9]{1,3}(?:[dabr][0-9]{1,3})?", value) is not None


def portable_syscall_generator(data):
    # These are printf literals, not regexps. POSIX leaves unknown awk escapes
    # undefined: BSD awk drops them, while mawk preserves invalid C backslashes.
    anchor = rb"\(sy_call_t *\)"
    require(data.count(anchor) == 2, "pinned syscall printf anchors changed")
    return data.replace(anchor, b"(sy_call_t *)")


def generate(root, out, version, unifdef, admitted_sha, emit_sources=False):
    root = root.resolve(strict=True)
    target = out.resolve()
    require(version_valid(version), "explicit Darwin build version required")
    require(not target.exists() and not target.is_relative_to(root)
            and not target.is_relative_to(REPO), "fresh external output required")
    tool = unifdef.resolve(strict=True)
    require(re.fullmatch(r"[0-9a-f]{64}", admitted_sha) is not None and sha(tool) == admitted_sha,
            "unifdef binary must be explicitly hash-admitted")
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    require(revision == XNU_COMMIT, "wrong XNU revision")
    inputs = {}
    for name in INPUTS:
        path = (root / name).resolve(strict=True)
        require(path.is_relative_to(root) and path.is_file(), "source escapes root")
        original = subprocess.check_output(["git", "-C", str(root), "show", "HEAD:" + name])
        require(path.read_bytes() == original, "generator input differs from pinned commit")
        inputs[name] = sha(path)
    target.mkdir(parents=True)
    (target / "libkern").mkdir()
    (target / "sys").mkdir()
    (target / "xnuVersion").write_text(version + "\n")
    header = target / "libkern/version.h"
    header.write_bytes((root / INPUTS[1]).read_bytes())
    env = os.environ.copy()
    # Avoid Perl/shell configuration or release-system environment injection.
    for key in list(env):
        if key.startswith(("PERL", "RC_", "BASH_FUNC_")) or key in ("ENV", "BASH_ENV", "SHELLOPTS"):
            env.pop(key, None)
    env.update(SRCROOT=str(root), OBJROOT=str(target), TARGET=str(target),
               CURRENT_KERNEL_CONFIG_LC="development", KERNEL_BUILD_OBJROOT="P1_DIAGNOSTIC",
               KERNEL_BUILD_DATE="explicit diagnostic build metadata", LC_ALL="C")
    commands = []

    def run(command, cwd=target):
        result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, timeout=60)
        index = len(commands)
        (target / (str(index) + ".log")).write_bytes(result.stdout + result.stderr)
        commands.append({"argv": list(map(str, command)), "returncode": result.returncode})
        return result

    result = run(["perl", str(root / "config/newvers.pl"), str(header)])
    require(result.returncode == 0 and "###KERNEL_" not in header.read_text(), "version generation failed")
    # Empty SEED_DEFINES is explicit. Unresolved platform #ifs stay in the header.
    result = run([str(tool), str(root / "bsd/kern/syscalls.master")])
    require(result.returncode in (0, 1), "syscall unifdef failed")
    master = target / "bsd.syscalls.master"
    master.write_bytes(result.stdout)
    result = run(["sh", str(root / "bsd/kern/makesyscalls.sh"), str(master), "header"], target / "sys")
    require(result.returncode == 0, "upstream syscall generator failed")
    syscall = target / "sys/syscall.h"
    require(syscall.is_file() and "#define\tSYS_exit" in syscall.read_text(), "generated syscall interface missing")
    sources = []
    headers = [header, syscall]
    if emit_sources:
        version_source = target / "version.c"
        version_source.write_bytes((root / "config/version.c.template").read_bytes())
        result = run(["perl", str(root / "config/newvers.pl"), str(version_source)])
        require(result.returncode == 0 and "###KERNEL_" not in version_source.read_text(),
                "upstream kernel version source generation failed")
        sources.append(version_source)
        generator = target / "makesyscalls.portable.sh"
        generator.write_bytes(portable_syscall_generator((root / "bsd/kern/makesyscalls.sh").read_bytes()))
        result = run(["sh", str(generator), str(master), "proto"], target / "sys")
        proto = target / "sys/sysproto.h"
        require(result.returncode == 0 and proto.is_file() and proto.stat().st_size > 0,
                "upstream syscall argument header generation failed")
        headers.append(proto)
        kernel = target / "bsd/kern"
        kernel.mkdir(parents=True)
        for mode, name in (("table", "init_sysent.c"), ("names", "syscalls.c"),
                           ("audit", "audit_kevents.c"), ("systrace", "systrace_args.c")):
            result = run(["sh", str(generator), str(master), mode], kernel)
            path = kernel / name
            require(result.returncode == 0 and path.is_file() and path.stat().st_size > 0,
                    "upstream syscall implementation generation failed")
            sources.append(path)
    require(sha(tool) == admitted_sha, "unifdef changed during generation")
    report = {"scope": "HOST_ONLY_REAL_UPSTREAM_GENERATION", "xnu_commit": revision,
              "darwin_build_version": version, "version_authority": "EXPLICIT_CALLER_BUILD_METADATA",
              "seed_defines": [], "inputs": inputs, "unifdef_sha256": admitted_sha,
              "commands": commands, "headers": {p.relative_to(target).as_posix(): sha(p) for p in headers},
              "sources": {p.relative_to(target).as_posix(): sha(p) for p in sources},
              "portable_syscall_generator_sha256": sha(generator) if emit_sources else None,
              "native_xnu_full_link": "NOT_RUN", "device_entry": "NOT_RUN"}
    (target / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("xnu-root", "out", "unifdef"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--darwin-version", required=True)
    parser.add_argument("--unifdef-sha256", required=True)
    parser.add_argument("--emit-sources", action="store_true", help="also emit kernel version and BSD table/names/audit/systrace sources")
    args = parser.parse_args()
    try:
        report = generate(args.xnu_root, args.out, args.darwin_version, args.unifdef, args.unifdef_sha256, args.emit_sources)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, type(error).__name__ + ": header generation refused; inspect private diagnostics\n")
    print(report["scope"], "headers=" + str(len(report["headers"])))


if __name__ == "__main__":
    main()
