#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Emit the type-correct consumer delta for the existing private P1 proposal.

No source is modified, no storage is fabricated and no security guard changes.
This tool cannot enable the separate P1 profile or register a runtime provider.
"""
import argparse
import difflib
import json
from pathlib import Path

from p1_t1_entry import read, sha, write_exclusive

SOURCE = "osfmk/arm64/arm_vm_init.c"
SOURCE_SHA = "049a354fed3ec0c5827f6747d6e7bc293782c6ea7f6007bbf6938acbe340b06e"
OLD = ("#include <penang_p1/p1_memory_admission.h>\n"
       "/* Deliberately requires the real runtime handoff provider at link time. */\n"
       "extern const struct p1_runtime_fdt penang_p1_runtime_fdt;\n")
NEW = "#include <penang_p1/p1_runtime_fdt_provider.h>\n"
CALL = "p1_memory_admit(&penang_p1_runtime_fdt,"


def rewrite(source):
    if source.count(OLD) != 1 or source.count(CALL) != 1:
        raise ValueError("runtime FDT consumer anchor drift")
    return source.replace(OLD, NEW, 1).replace(CALL, "p1_memory_admit(p1_runtime_fdt_view(),", 1)


def render(root):
    root = root.resolve(strict=True)
    path = root / SOURCE
    for part in [path, *path.parents]:
        if part == root:
            break
        if part.is_symlink():
            raise ValueError("symlinked consumer source")
    data = read(path)
    if sha(data) != SOURCE_SHA:
        raise ValueError("pinned P1 consumer SHA mismatch")
    source = data.decode()
    updated = rewrite(source)
    patch = "".join(difflib.unified_diff(source.splitlines(True), updated.splitlines(True),
        fromfile="a/"+SOURCE, tofile="b/"+SOURCE)).encode()
    return patch, {"scope":"COMPILE_ONLY_EXISTING_P1_CONSUMER", "source":SOURCE,
        "original_sha256":sha(data), "result_sha256":sha(updated.encode()),
        "patch_sha256":sha(patch), "security_guard_changes":0,
        "provider_storage":"REQUIRED_MUTABLE_OBJECT_CONST_BORROW", "device_entry":"NOT_RUN"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        patch, result = render(args.source_root)
        write_exclusive(args.out, patch, (args.source_root,))
    except (OSError, ValueError, UnicodeError) as error:
        parser.exit(1, "runtime FDT consumer delta refused: " + str(error) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
