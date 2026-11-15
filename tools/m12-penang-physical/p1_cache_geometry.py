#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Emit, never apply, the pinned public XNU Penang cache-line selector delta.

The historical D3A1 CTR observation reported 64-byte I/D cache lines. This
adds only that compile-time geometry, not cache coherency or PMAP enforcement.
The generated source modification retains XNU's upstream license and notices.
"""
import argparse
import difflib
import hashlib
from pathlib import Path

SOURCE = "osfmk/arm64/proc_reg.h"
UPSTREAM_SHA256 = "43e30e1a0e387840d1a39422d9bb8db739da321c0e4b49b98f5ca5b98e726500"
ANCHOR = "#elif defined (VMAPPLE)\n"
BRANCH = """#elif defined (ARM64_BOARD_CONFIG_PENANG_PHYSICAL)

/* D3A1 CTR observation: 64-byte I/D lines; no monitor/coherency claim. */
#define MMU_I_CLINE 6
#define MMU_CLINE   6

"""


def insert_branch(source):
    if source.count(ANCHOR) != 1 or BRANCH in source:
        raise ValueError("cache selector anchor drift")
    return source.replace(ANCHOR, BRANCH + ANCHOR, 1)


def render(root):
    path = (root / SOURCE).resolve(strict=True)
    if not path.is_relative_to(root.resolve()):
        raise ValueError("source escapes upstream root")
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != UPSTREAM_SHA256:
        raise ValueError("pinned upstream proc_reg.h SHA mismatch")
    old = raw.decode("utf-8")
    return "".join(difflib.unified_diff(old.splitlines(keepends=True),
                                      insert_branch(old).splitlines(keepends=True),
                                      fromfile="a/" + SOURCE, tofile="b/" + SOURCE))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xnu-original-root", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    try:
        patch = render(args.xnu_original_root)
        with args.out.open("x", encoding="utf-8") as output:
            output.write(patch)
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, type(error).__name__ + ": cache patch refused\n")
    print("HOST_ONLY_CACHE_SELECTOR_PATCH_NOT_APPLIED sha256=" + hashlib.sha256(patch.encode()).hexdigest())


if __name__ == "__main__":
    main()
