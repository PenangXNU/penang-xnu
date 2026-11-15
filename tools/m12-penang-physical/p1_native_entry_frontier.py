#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only XNU 11417 physical entry source preflight.

This validates a *source* chain, NOT a linkable or bootable XNU image.
It refuses any B4.2 source tree where the physical PMAP guards vanished.
It does not create a diagnostic kernel, infer an EL2 provider or run QEMU.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

FILES = {
    "start": "osfmk/arm64/start.s",
    "arm_init": "osfmk/arm/arm_init.c",
    "arm_vm_init": "osfmk/arm64/arm_vm_init.c",
    "pmap": "osfmk/arm/pmap/pmap.c",
}
# Anchor to actual XNU 11417 instructions/entry path; reject mere docs.
RESET_ENTRY = "LEXT(_start)"
RESET_TO_FIRST_CPU = "EXT(start_first_cpu)"
START_ENTRY = "LEXT(start_first_cpu)"
START_TO_INIT = "EXT(arm_init)@page"
INIT_BODY = "arm_init("
INIT_TO_VM = "arm_vm_init("
PHYSICAL_GUARD = ("#if defined(PENANG_PHYSICAL) || "
                  "defined(ARM64_BOARD_CONFIG_PENANG_PHYSICAL)")
BLOCKER = "B4.2 physical PMAP/Stage-2/SMMU lockdown backend is BLOCKED"


def _read(path: Path) -> tuple[str, str]:
    data = path.read_bytes()
    if not data or len(data) > 12_000_000 or b"\0" in data:
        raise ValueError("INVALID_SOURCE_FILE")
    return data.decode("utf-8"), hashlib.sha256(data).hexdigest()


def inspect(original: Path, guarded: Path) -> dict:
    """Fail closed when the original entry chain or B4.2 guards are absent."""
    result = {
        "schema": "penang-p1-native-entry-frontier-v1",
        "provenance": "HOST_ONLY",
        "stage0_g53": "HISTORICAL_DEVICE_VERIFIED_SEPARATE",
        "userland_penang_init": "NOT_RUN",
        "native_xnu_full_link": "NOT_RUN",
        "xnu_device_entry": "NOT_RUN",
        "pmap_physical": "B42_BLOCKED_BACKEND_UNVERIFIED",
        "status": "REJECTED",
        "sources": {},
        "checks": {},
    }
    paths = {
        "start": original / FILES["start"],
        "arm_init": original / FILES["arm_init"],
        "arm_vm_init": guarded / FILES["arm_vm_init"],
        "pmap": guarded / FILES["pmap"],
    }
    texts = {}
    for name, path in paths.items():
        try:
            source, digest = _read(path)
        except (OSError, UnicodeError, ValueError) as err:
            result["sources"][name] = {"available": False, "error": type(err).__name__}
            result["checks"]["required_sources_present"] = False
            return result
        texts[name] = source
        result["sources"][name] = {"sha256": digest, "bytes": path.stat().st_size}
    result["checks"]["required_sources_present"] = True
    start = texts["start"]
    arm = texts["arm_init"]
    # XNU's global Mach-O kernel entry label uses EXT(_start) / LEXT(_start).
    # Verify its unconditional branch into start_first_cpu, not just a comment.
    reset_pos = start.find(RESET_ENTRY)
    reset_tail = start[reset_pos:reset_pos + 240] if reset_pos >= 0 else ""
    result["checks"]["reset_trampoline_to_first_cpu"] = (
        reset_pos >= 0 and
        bool(re.search(r"(?m)^\s*b\s+EXT\(start_first_cpu\)\s*$", reset_tail))
    )
    # Require the cold-CPU entry label followed by its original arm_init target.
    entry_pos = start.find(START_ENTRY)
    init_pos = start.find(START_TO_INIT, entry_pos + len(START_ENTRY))
    result["checks"]["start_first_cpu_to_arm_init"] = (
        entry_pos >= 0 and init_pos > entry_pos and init_pos - entry_pos < 10000
    )
    func = arm.find(INIT_BODY)
    vm = arm.find(INIT_TO_VM, func + len(INIT_BODY))
    result["checks"]["arm_init_to_arm_vm_init"] = (
        func >= 0 and vm > func and vm - func < 40000
    )
    for name in ("arm_vm_init", "pmap"):
        source = texts[name]
        at = source.find(PHYSICAL_GUARD)
        # Check exact preprocessor-bound #error shortly after physical target gate.
        error = source.find("#error", at + len(PHYSICAL_GUARD))
        result["checks"][f"{name}_b42_guard"] = (
            at >= 0 and error > at and error - at < 250
            and BLOCKER in source[error:error + 400]
        )
    result["checks"]["kernel_entry_is_not_userland_fixture"] = (
        "penang-init" not in start and "penang-init" not in arm
    )
    if all(result["checks"].values()):
        result["status"] = "P1_SOURCE_ENTRY_CHAIN_VERIFIED_B42_COMPILE_HOLD"
    return result


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--xnu-original-root", type=Path, required=True)
    p.add_argument("--b42-guarded-root", type=Path, required=True)
    p.add_argument("--out", type=Path, help="Exclusive result file; never overwrite")
    args = p.parse_args()
    outcome = inspect(args.xnu_original_root, args.b42_guarded_root)
    data = json.dumps(outcome, indent=2, ensure_ascii=False) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open("x", encoding="utf-8") as output:
            output.write(data)
    print(data, end="")
    return 0 if outcome["status"] == "P1_SOURCE_ENTRY_CHAIN_VERIFIED_B42_COMPILE_HOLD" else 1


if __name__ == "__main__":
    raise SystemExit(main())
