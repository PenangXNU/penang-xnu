#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Apply libdispatch's genuine kernel header export flags with real unifdef."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from p1_generated_headers import REPO, require, sha
from p1_sdk_headers import pinned_inputs

COMMIT = "934ff27e48ab7a6ee799442c5f19ebbb990567e8"
CONFIG = "xcodeconfig/libfirehose_kernel.xcconfig"
HEADER = "os/firehose_buffer_private.h"
FLAGS = ["-DKERNEL=1", "-DOS_FIREHOSE_SPI=1", "-DOS_VOUCHER_ACTIVITY_SPI_TYPES=1", "-UOS_VOUCHER_ACTIVITY_SPI"]


def generate(root, tool, admitted_sha, out):
    root, tool, target = root.resolve(strict=True), tool.resolve(strict=True), out.resolve()
    require(not target.exists() and not target.is_relative_to(root) and not target.is_relative_to(REPO),
            "fresh external firehose export required")
    require(re.fullmatch(r"[0-9a-f]{64}", admitted_sha) is not None and sha(tool) == admitted_sha,
            "unifdef must be explicitly hash-admitted")
    inputs = pinned_inputs(root, COMMIT, (CONFIG, HEADER))
    lines = re.findall(r"(?m)^COPY_HEADERS_UNIFDEF_FLAGS\s*=\s*(.*)$", (root / CONFIG).read_text())
    require(len(lines) == 1 and lines[0].split() == FLAGS, "kernel header export flags changed")
    target.mkdir(parents=True)
    command = [str(tool), *FLAGS, str(root / HEADER)]
    result = subprocess.run(command, capture_output=True, timeout=60,
                            env={k: os.environ[k] for k in ("PATH", "LD_LIBRARY_PATH") if k in os.environ})
    (target / "generation.log").write_bytes(result.stderr)
    require(result.returncode in (0, 1) and result.stdout.strip(), "libdispatch kernel header export failed")
    require(sha(tool) == admitted_sha and pinned_inputs(root, COMMIT, (CONFIG, HEADER)) == inputs,
            "header export inputs changed during generation")
    header = target / HEADER
    header.parent.mkdir()
    header.write_bytes(result.stdout)
    report = dict(scope="HOST_ONLY_GENUINE_LIBDISPATCH_KERNEL_HEADER_EXPORT", source_commit=COMMIT,
                  inputs=inputs, unifdef_sha256=admitted_sha, command=command, returncode=result.returncode,
                  headers={HEADER: sha(header)}, native_xnu_full_link="NOT_RUN", device_entry="NOT_RUN")
    (target / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("libdispatch-root", "unifdef", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--unifdef-sha256", required=True)
    a = parser.parse_args()
    try:
        r = generate(a.libdispatch_root, a.unifdef, a.unifdef_sha256, a.out)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, type(error).__name__ + ": firehose header export refused\n")
    print(r["scope"])


if __name__ == "__main__":
    main()
