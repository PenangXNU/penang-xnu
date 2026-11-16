#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Emit a pinned SART/UAT platform-selection proposal; never apply it.

Only four original sites change: two hardware includes and two bootstrap
calls. Protected guards, trust consumers and Stage-1 algorithms are untouched.
The separately selected P1 code-generation experiment has no Apple IOMMU
provider and offers no DMA guarantee. Physical/runtime admission is separate.
"""
import argparse
import difflib
import json
from pathlib import Path

from p1_t1_entry import read, sha, write_exclusive

SOURCE = "osfmk/arm/pmap/pmap.c"
SOURCE_PINS = {
    "342cb5c640922bb5990c0605a05f16653b8201ee63105f9c1e9c2d818314ab45": "PINNED_PUBLIC_XNU_11417",
    "a35cf812d0b768e21879ba0ae54c2a13362bc160e8542d8b000d518f042716e8": "GUARDED_B42_SOURCE",
    "9a73b233ad26bde07aa9ea8b070f0a685cb40ef24f6219053484cf9d435b9751": "PRIVATE_P1_SANDBOX_PROPOSAL",
}
INCLUDES = "#include <arm64/ppl/sart.h>\n#include <arm64/ppl/uat.h>\n"


def select(source):
    if source.count(INCLUDES) != 1:
        raise ValueError("SART/UAT include anchor drift")
    updated = source.replace(INCLUDES,
        "#include <penang_p1/p1_pmap_platform.h>\n#if P1_PMAP_APPLE_IO_BOOTSTRAP\n" + INCLUDES + "#endif\n", 1)
    for name in ("uat", "sart"):
        call = "\t" + name + "_bootstrap();\n"
        if source.count(call) != 1:
            raise ValueError("SART/UAT call anchor drift")
        updated = updated.replace(call, "#if P1_PMAP_APPLE_IO_BOOTSTRAP\n" + call + "#endif\n", 1)
    return updated


def render(root):
    root = root.resolve(strict=True)
    path = root / SOURCE
    # Reject a symlink in any component, including links that stay in-tree.
    for part in [path, *path.parents]:
        if part == root:
            break
        if part.is_symlink():
            raise ValueError("symlinked source")
    data = read(path)
    source_hash = sha(data)
    if source_hash not in SOURCE_PINS:
        raise ValueError("pinned pmap.c SHA mismatch")
    text = data.decode()
    changed = select(text)
    patch = "".join(difflib.unified_diff(text.splitlines(True), changed.splitlines(True),
        fromfile="a/"+SOURCE, tofile="b/"+SOURCE)).encode()
    return patch, {"scope":"COMPILE_ONLY_PLATFORM_SELECTION_PROPOSAL", "source":SOURCE,
        "source_class":SOURCE_PINS[source_hash], "original_sha256":source_hash,
        "result_sha256":sha(changed.encode()), "patch_sha256":sha(patch),
        "changed_sites":4, "security_guard_changes":0, "security_provider":"NOT_SUPPLIED",
        "dma_enforcement":"NOT_QUALIFIED", "device_entry":"NOT_RUN"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        patch, result = render(args.source_root)
        write_exclusive(args.out, patch, (args.source_root,))
    except (OSError, ValueError, UnicodeError) as error:
        parser.exit(1, "PMAP platform proposal refused: " + str(error) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
