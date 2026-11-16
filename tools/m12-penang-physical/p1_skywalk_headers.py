#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Materialize pinned Skywalk exports, without executing Make expressions.

Bounded to the literal export grammar in XNU 11417. Source bytes and notices
are copied unchanged into a fresh private include root.
"""
import argparse
import json
from pathlib import Path
import re

from p1_generated_headers import XNU_COMMIT, REPO, require, sha
from p1_native_inputs import make_text
from p1_sdk_headers import pinned_inputs

DIRECTORIES = ("", "core", "channel", "mem", "nexus", "packet", "namespace")
EXPECTED = {
    "INSTALL_MI_LIST": "${DATAFILES}",
    "INSTALL_SF_MI_LCL_LIST": "$(sort ${DATAFILES} ${PRIVATE_DATAFILES})",
    "EXPORT_MI_LIST": "$(sort ${INSTALL_MI_LIST} ${INSTALL_SF_MI_LCL_LIST})",
    "EXPORT_MI_DIR": "${INSTALL_MI_DIR}",
}


def exports(text):
    flat = make_text(text)
    def value(name):
        values = re.findall(r"(?m)^" + re.escape(name) + r"[ \t]*=[ \t]*(.*)$", flat)
        require(len(values) == 1, "ambiguous or missing Skywalk export assignment")
        return " ".join(values[0].split())
    for name, expected in EXPECTED.items():
        require(value(name) == expected, "unsupported Skywalk export expression")
    directory = value("INSTALL_MI_DIR")
    require(directory in ("skywalk", "skywalk/netns"), "unsupported export namespace")
    names = value("DATAFILES").split() + value("PRIVATE_DATAFILES").split()
    require(all(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*\.h", n) for n in names),
            "literal Skywalk header names required")
    return directory, sorted(set(names))


def generate(root, out):
    root, target = root.resolve(strict=True), out.resolve()
    require(not target.exists() and not target.is_relative_to(root) and not target.is_relative_to(REPO),
            "fresh private header output required")
    plan, inputs = {}, {}
    for directory in DIRECTORIES:
        source = Path("bsd/skywalk") / directory
        make = (source / "Makefile").as_posix()
        inputs.update(pinned_inputs(root, XNU_COMMIT, (make,)))
        namespace, names = exports((root / make).read_text())
        files = [(source / n).as_posix() for n in names]
        inputs.update(pinned_inputs(root, XNU_COMMIT, files))
        for name, source_name in zip(names, files):
            dest = namespace + "/" + name
            require(dest not in plan, "duplicate Skywalk export destination")
            plan[dest] = source_name
    target.mkdir(parents=True)
    for dest, name in plan.items():
        require(sha(root / name) == inputs[name], "Skywalk source changed during export")
        path = target / dest
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes((root / name).read_bytes())
    report = dict(scope="HOST_ONLY_PINNED_SKYWALK_HEADER_EXPORTS", xnu_commit=XNU_COMMIT,
                  inputs=inputs, headers={dest: dict(source=name, sha256=sha(target / dest)) for dest, name in plan.items()},
                  native_xnu_full_link="NOT_RUN", device_entry="NOT_RUN")
    (target / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xnu-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = generate(args.xnu_root, args.out)
    except (OSError, ValueError) as error:
        parser.exit(1, type(error).__name__ + ": Skywalk header export refused\n")
    print(report["scope"], "headers=" + str(len(report["headers"])))


if __name__ == "__main__":
    main()
