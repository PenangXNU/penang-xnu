#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Emit only pinned optional/unused include fixes; no flags or guards change."""
import argparse
import difflib
import json
from pathlib import Path

from p1_t1_entry import read, sha, write_exclusive

PINS = {
    "osfmk/kern/startup.c": "79b1adfe16012c0cbf4f0fa72b705444ef46f6e9abd178fe8970e0d32c17b48f",
    "osfmk/arm64/cpu.c": "fc45579fed1b54ae5f5477514ed4c77125ebb260342e52975384b071424810ce",
    "osfmk/arm64/sleh.c": "b7659a1b4fba99c4b610017860b982634489bda7a44dc06fb607a90357af1490",
    "osfmk/arm/machine_routines_common.c": "021fa4096909c3c51e37cba6124d1de9f412f84f76d8ecd48696f56430bb78c7",
}
KPC_FILES = {"osfmk/kern/startup.c", "osfmk/arm64/cpu.c"}


def rewrite(name, source):
    include = ("#include <kern/kpc.h>\n" if name in KPC_FILES else
               "#include <pexpert/arm64/apple_arm64_cpu.h>\n")
    if name not in PINS or source.count(include) != 1:
        raise ValueError("pinned include anchor drift")
    # Neither of the latter two pinned TUs uses this header's Apple core helpers.
    replacement = "#if CONFIG_CPU_COUNTERS\n" + include + "#endif\n" if name in KPC_FILES else ""
    return source.replace(include, replacement, 1)


def render(root):
    root = root.resolve(strict=True)
    patches, sources = [], {}
    for name, pin in PINS.items():
        path = (root / name).resolve(strict=True)
        if not path.is_relative_to(root):
            raise ValueError("source escapes original root")
        data = read(path)
        if sha(data) != pin:
            raise ValueError("pinned source differs")
        source = data.decode()
        changed = rewrite(name, source)
        sources[name] = {"before": pin, "after": sha(changed.encode())}
        patches += difflib.unified_diff(source.splitlines(True), changed.splitlines(True),
                                       fromfile="a/" + name, tofile="b/" + name)
    return "".join(patches).encode(), {"scope": "HOST_ONLY_INCLUDE_HYGIENE", "sources": sources,
                                     "security_contract_changes": False, "device_entry": "NOT_RUN"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        patch, report = render(args.source_root)
        write_exclusive(args.out, patch, (args.source_root,))
    except (OSError, ValueError) as error:
        parser.exit(1, "optional include patch refused: " + str(error) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
