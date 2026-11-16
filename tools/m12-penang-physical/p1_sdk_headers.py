#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the public availability/early-BSD header subset needed by IIG.

Uses pinned upstream generators, not an Apple SDK/KDK or handwritten substitutes.
This is a header subset: it supplies no library, TrustCache API or IIG provider.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

from p1_generated_headers import XNU_COMMIT, REPO, require, sha

AV_COMMIT = "80d3cf03b6e9a9eebe90be15bea964dfa10c476e"
AV_INPUTS = ("availability", "availability.pl", "availability.dsl", "CMakeLists.txt")
TEMPLATES = ("Availability.h", "AvailabilityInternal.h", "AvailabilityInternalLegacy.h",
             "AvailabilityInternalPrivate.h", "AvailabilityMacros.h",
             "AvailabilityProhibitedInternal.h", "AvailabilityVersions.h", "os_availability.h")
BSD_INPUTS = ("bsd/sys/Makefile", "bsd/sys/make_symbol_aliasing.sh", "bsd/sys/make_posix_availability.sh")
CF_COMMIT = "fdf352a2f1000c86b6b686fc36dea5d858f341f3"
CF_SHA256 = "d761e71289c31895a66704804b6fc034a6a4cae48775c2ce164e55993a4c44e5"


def portable_conditionals(source):
    """Select Swift CoreFoundation's real portable branch under its new name.

    The original prefers an installed SDK TargetConditionals.h. Renaming that
    file would otherwise include itself and silently leave every macro unset.
    Compiler-provided TARGET_* macros take precedence over the portable
    fallback. No CPU or OS constants are substituted.
    """
    require(source.is_file() and not source.is_symlink() and sha(source) == CF_SHA256,
            "wrong public CoreFoundation portability header")
    data = source.read_bytes()
    anchor = b"#if __has_include(<TargetConditionals.h>)"
    require(data.count(anchor) == 1, "CoreFoundation portability selector drift")
    data = data.replace(anchor, b"#if 0 /* Penang: use this pinned source's portable branch; no SDK recursion */", 1)
    # Newer Clang exposes TargetConditionals macros directly. Preserve their
    # target-specific meaning instead of redefining them to fallback aliases.
    import re
    return re.sub(rb"(?m)^(#define[ \t]+)(TARGET_[A-Z0-9_]+)([ \t]+[^\n]*)$",
                  lambda m: b"#ifndef " + m[2] + b"\n" + m[0] + b"\n#endif", data)


def pinned_inputs(root, commit, names):
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    require(revision == commit, "wrong upstream source revision")
    result = {}
    for name in names:
        path = root / name
        require(path.is_file() and path.resolve(strict=True) == path, "upstream input is not a regular contained file")
        original = subprocess.check_output(["git", "-C", str(root), "show", "HEAD:" + name])
        require(path.read_bytes() == original, "upstream generator input changed")
        result[name] = sha(path)
    return result


def generate(xnu, availability, out, conditionals=None):
    xnu, availability = xnu.resolve(strict=True), availability.resolve(strict=True)
    target = out.resolve()
    require(not target.exists() and all(not target.is_relative_to(p) for p in (xnu, availability, REPO)),
            "fresh external output required")
    xnu_inputs = pinned_inputs(xnu, XNU_COMMIT, BSD_INPUTS)
    av_inputs = pinned_inputs(availability, AV_COMMIT, (*AV_INPUTS, *("templates/" + n for n in TEMPLATES)))
    portable = portable_conditionals(conditionals) if conditionals is not None else None
    target.mkdir(parents=True)
    sdk = target / "sdk"
    libexec, include = sdk / "usr/local/libexec", sdk / "usr/include"
    libexec.mkdir(parents=True)
    (include / "os").mkdir(parents=True)
    (include / "sys").mkdir()
    # Fixed process environment prevents shell startup hooks, Python imports,
    # DRIVERKITROOT redirection and SDK overrides from changing the generator.
    env = {k: os.environ[k] for k in ("PATH", "LD_LIBRARY_PATH") if k in os.environ}
    env.update(LC_ALL="C", PYTHONNOUSERSITE="1", PYTHONSAFEPATH="1")
    commands = []

    def run(command):
        result = subprocess.run(list(map(str, command)), env=env, cwd=target,
                                capture_output=True, timeout=60)
        (target / (str(len(commands)) + ".log")).write_bytes(result.stdout + result.stderr)
        commands.append({"argv": list(map(str, command)), "returncode": result.returncode})
        require(result.returncode == 0, "upstream header generator failed; inspect private log")
        return result

    embedded = libexec / "availability"
    run(["python3", "-I", availability / "availability", "--av_version", "151", "--preprocess",
         availability / "availability", embedded])
    embedded.chmod(0o700)
    # Preserve the original compatibility launcher required by XNU's script.
    shutil.copyfile(availability / "availability.pl", libexec / "availability.pl")
    (libexec / "availability.pl").chmod(0o700)
    for platform in ("--ios", "--macosx"):
        require(run(["python3", "-I", embedded, platform]).stdout.strip(), "empty upstream version catalogue")
    for name in TEMPLATES:
        output = include / ("os/availability.h" if name == "os_availability.h" else name)
        run(["python3", "-I", embedded, "--preprocess", availability / "templates" / name, output])
        require(output.stat().st_size > 0, "empty generated availability header")
    run(["bash", xnu / "bsd/sys/make_symbol_aliasing.sh", sdk, include / "sys/_symbol_aliasing.h"])
    run(["sh", xnu / "bsd/sys/make_posix_availability.sh", include / "sys/_posix_availability.h"])
    if portable is not None:
        (include / "TargetConditionals.h").write_bytes(portable)
    require("__DARWIN_ALIAS_STARTING_MAC___MAC_" in (include / "sys/_symbol_aliasing.h").read_text(),
            "upstream symbol catalogue missing")
    require("___POSIX_C_DEPRECATED_STARTING_200809L" in (include / "sys/_posix_availability.h").read_text(),
            "upstream POSIX catalogue missing")
    # Catch source drift during execution before admitting any output.
    require(pinned_inputs(xnu, XNU_COMMIT, BSD_INPUTS) == xnu_inputs, "XNU changed during generation")
    require(pinned_inputs(availability, AV_COMMIT, tuple(av_inputs)) == av_inputs, "availability source changed")
    if conditionals is not None:
        require(sha(conditionals) == CF_SHA256, "portability header changed during generation")
    report = dict(scope="HOST_ONLY_PUBLIC_HEADER_SUBSET", xnu_commit=XNU_COMMIT,
                  availability_commit=AV_COMMIT, xnu_inputs=xnu_inputs, availability_inputs=av_inputs,
                  portable_conditionals=(dict(source="swiftlang/swift-corelibs-foundation/Sources/CoreFoundation/include/CFTargetConditionals.h",
                                              commit=CF_COMMIT, input_sha256=CF_SHA256,
                                              qualification="PORTABLE_COMPILER_DETECTION_NOT_APPLE_SDK_EQUIVALENCE") if portable is not None else None),
                  commands=commands, headers={p.relative_to(include).as_posix(): sha(p) for p in sorted(include.rglob("*.h"))},
                  full_sdk="NOT_PROVIDED", native_xnu_full_link="NOT_RUN", device_entry="NOT_RUN")
    (target / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("xnu-root", "availability-root", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--portable-conditionals-source", type=Path,
                        help="opt into the pinned Swift CoreFoundation compiler-detection adapter")
    args = parser.parse_args()
    try:
        report = generate(args.xnu_root, args.availability_root, args.out, args.portable_conditionals_source)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, type(error).__name__ + ": public header generation refused; inspect private diagnostics\n")
    print(report["scope"], "headers=" + str(len(report["headers"])))


if __name__ == "__main__":
    main()
