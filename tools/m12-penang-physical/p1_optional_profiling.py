#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Emit pinned fixes for XNU's existing optional profiling-off configuration.

No PMU constants are invented: enabling counters still requires its real
machine definitions. Existing disabled APIs, enabled profiling paths and all
PMAP/integrity policy are preserved. Applies only to an external source copy.
"""
import argparse
import difflib
import json
from pathlib import Path

from p1_t1_entry import read, sha, write_exclusive

PINS = {
    "osfmk/kern/kpc.h": "1a983980b1c971f3783fd8bb4af5f278ab751546145c60e0535f72ce1f5aa367",
    "osfmk/kern/ast.c": "9fa1f3081183a1536c5289a748630a7fada1626ad0f8df3c55aa8b666f1c53ef",
    "osfmk/kern/processor.c": "3f3d4af61aeed6610920666dd8adbedd83bd704e2c41f6f4a06709cfdd22db12",
    "osfmk/arm64/pcb.c": "df95730f0bdde1805579cb6d7471d1bf11bb8d0cc483df528a50cfece477fef7",
}
ANCHORS = {
    "osfmk/kern/kpc.h": ("extern uint32_t kpc_actionid[KPC_MAX_COUNTERS];\n", "CONFIG_CPU_COUNTERS"),
    "osfmk/kern/ast.c": ("\t\tkperf_thread_ast_handler(thread);\n\t\tthread->kperf_ast = 0;\n", "KPERF"),
    "osfmk/kern/processor.c": ("\t[RUNNING_TIMER_KPERF] = kperf_timer_expire,\n", "KPERF"),
    "osfmk/arm64/pcb.c": ("#include <pexpert/arm64/apple_arm64_cpu.h>\n", None),
}


def rewrite(name, source):
    if name not in ANCHORS:
        raise ValueError("unadmitted optional profiling source")
    anchor, option = ANCHORS[name]
    if source.count(anchor) != 1:
        raise ValueError("optional profiling anchor drift")
    replacement = "#if " + option + "\n" + anchor + "#endif\n" if option else ""
    return source.replace(anchor, replacement, 1)


def render(root):
    root = root.resolve(strict=True)
    patches, sources = [], {}
    for name, pin in PINS.items():
        path = root / name
        if path.resolve(strict=True) != path:
            raise ValueError("optional profiling source is symlinked")
        original = read(path)
        if sha(original) != pin:
            raise ValueError("pinned profiling source differs")
        changed = rewrite(name, original.decode())
        sources[name] = {"before": pin, "after": sha(changed.encode())}
        patches += difflib.unified_diff(original.decode().splitlines(True), changed.splitlines(True),
                                       fromfile="a/" + name, tofile="b/" + name)
    return "".join(patches).encode(), dict(scope="HOST_ONLY_OPTIONAL_PROFILING_CONFIGURATION",
                                          sources=sources, security_contract_changes=False,
                                          device_entry="NOT_RUN")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        patch, report = render(args.source_root)
        write_exclusive(args.out, patch, (args.source_root,))
    except (OSError, ValueError) as error:
        parser.exit(1, "optional profiling patch refused: " + str(error) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
