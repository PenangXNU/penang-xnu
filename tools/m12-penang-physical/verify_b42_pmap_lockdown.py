#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pure PMAP source-selection gate from the 2026-11-07 B4.2 verifier.

Original research branch: research/m12-b42-pmap-lockdown-argon-20261107
Original verifier is retained unchanged in the private local worktree.
These source-selection functions are extracted byte-for-byte at AST nodes.
This gate is not a Qualcomm Stage-2/SCM/DMA provider or a device authorization.
"""
from pathlib import Path
import re

APPLE_AMCC_FILES = (
    "osfmk/arm64/amcc_rorgn_ppl.c",
    "osfmk/arm64/amcc_rorgn_ppl_amcc.c",
    "osfmk/arm64/amcc_rorgn_ppl_ctrr3.c",
    "osfmk/arm64/amcc_rorgn_common.c",
)


APPLE_SPTM_FILES = (
    "osfmk/arm64/amcc_rorgn_sptm.c",
    "osfmk/arm64/sptm/pmap/pmap.c",
    "osfmk/arm64/sptm/start_sptm.s",
    "osfmk/arm64/sptm/arm_init_sptm.c",
)


CORE_BOOTSTRAP_FILES = (
    "osfmk/arm/pmap/pmap.c",
    "osfmk/arm/arm_init.c",
    "osfmk/arm64/arm_vm_init.c",
    "osfmk/arm64/locore.s",
    "osfmk/arm64/start.s",
)


FORBIDDEN_STUB_PATTERNS = (
    re.compile(r"(?m)^\s*(?:void|int|bool|kern_return_t)\s+pmap_ppl_(?:un)?lockdown_pages\s*\("),
    re.compile(r"(?m)^\s*#\s*define\s+pmap_ppl_(?:un)?lockdown_pages\b"),
    re.compile(r"(?m)^\s*#\s*define\s+pmap_cs_(?:un)?lockdown_pages\b"),
    re.compile(r"(?m)^\s*void\s+rorgn_(?:lockdown|stash_range|validate_core)\s*\("),
    re.compile(r"(?m)^\s*void\s+ml_enable_monitor\s*\("),
    re.compile(r"(?m)^\s*void\s+(?:uat|sart)_bootstrap\s*\("),
    re.compile(r"(?m)^\s*#\s*define\s+NO_MONITOR\b"),
)


def extract_makefile_sources(makefile_text: str):
    return sorted(set(re.findall(
        r"\$\(SOURCE_DIR\)/([A-Za-z0-9_./+-]+\.(?:cpp|c|s|S))(?=\s|\\|$)",
        makefile_text,
    )))


def assess_b42(
    iokit_make: str,
    osfmk_make: str,
    osfmk_config: str,
    board_header_text: str = "",
    patch_text: str = "",
    missing_public_sources=(),
    stderr_iokit: str = "",
    stderr_osfmk: str = "",
):
    added_patch_lines = "\n".join(
        line[1:]
        for line in patch_text.splitlines()
        if line.startswith("+") and not line.startswith("+++")
    )
    osfmk_sources = set(extract_makefile_sources(osfmk_make))
    iokit_sources = set(extract_makefile_sources(iokit_make))

    iokit_ok = (
        "iokit/penang_physical/PenangPhysicalPlatformExpert.cpp" in iokit_sources
        and "iokit/penang_physical/penang_physical_contract.c" in iokit_sources
    )
    bootstrap_ok = all(req in osfmk_sources for req in CORE_BOOTSTRAP_FILES)
    config_ok = "PENANG_PHYSICAL_SOURCES" in osfmk_config
    stderr_ok = not stderr_iokit.strip() and not stderr_osfmk.strip()
    doconf_valid = iokit_ok and bootstrap_ok and config_ok and stderr_ok

    amcc_found = sorted(f for f in APPLE_AMCC_FILES if f in osfmk_sources or Path(f).name in osfmk_make)
    sptm_found = sorted(f for f in APPLE_SPTM_FILES if f in osfmk_sources or f in osfmk_make)
    ppl_selector = "-DCONFIG_PMAP_PPL" in osfmk_make or "CONFIG_PMAP_PPL" in osfmk_config
    kernel_integrity = "-DCONFIG_KERNEL_INTEGRITY" in osfmk_make or "CONFIG_KERNEL_INTEGRITY" in osfmk_config

    qemu_leak = (
        "PenangQemuVirtPlatformExpert.cpp" in iokit_make
        or "qemu_virt_sources" in osfmk_config
        or "-DQEMU_VIRT" in osfmk_make
        or "-DARM64_BOARD_CONFIG_QEMU_VIRT" in osfmk_make
    )
    no_monitor_header_define = bool(
        re.search(r"(?m)^\s*#\s*define\s+NO_MONITOR\b", board_header_text)
    )
    silent_fallback = (
        "-DNO_MONITOR" in osfmk_make
        or "-DXNU_NO_TYPE_BUILTINS" in osfmk_make
        or no_monitor_header_define
    )
    stub_violation = any(
        pat.search(added_patch_lines) or pat.search(board_header_text)
        for pat in FORBIDDEN_STUB_PATTERNS
    )
    missing_list = sorted(set(missing_public_sources))

    diag_fail_closed = False
    if patch_text:
        has_pmap_guard = (
            "--- a/osfmk/arm/pmap/pmap.c" in patch_text
            and '#error "PENANG_PHYSICAL: B4.2 physical PMAP/Stage-2/SMMU lockdown backend is BLOCKED' in patch_text
        )
        has_vm_guard = "--- a/osfmk/arm64/arm_vm_init.c" in patch_text
        diag_fail_closed = (
            has_pmap_guard
            and has_vm_guard
            and not amcc_found
            and not sptm_found
            and not missing_list
            and not stub_violation
            and not silent_fallback
            and not qemu_leak
            and bootstrap_ok
        )

    return {
        "DOCONF_NATIVE_VALID": "PASS" if doconf_valid else "FAIL",
        "BOOTSTRAP_CORE_SOURCES": "PASS" if bootstrap_ok else "FAIL",
        "CONFIG_PMAP_PPL_SELECTOR": "PRESENT_NON_SPTM_FAMILY" if ppl_selector else "ABSENT",
        "CONFIG_KERNEL_INTEGRITY_OPTION": "PRESENT" if kernel_integrity else "ABSENT",
        "APPLE_AMCC_LEAK": f"FOUND_{len(amcc_found)}_FILES" if amcc_found else "NONE",
        "APPLE_AMCC_FILES": amcc_found,
        "APPLE_SPTM_LEAK": "FOUND" if sptm_found else "NONE",
        "PUBLIC_DROP_SOURCE_COMPLETENESS": "PASS" if not missing_list else "FAIL",
        "MISSING_PUBLIC_SOURCES": missing_list,
        "NO_QEMU_CONTAMINATION": "PASS" if not qemu_leak else "FAIL",
        "NO_SILENT_FALLBACK": "PASS" if not silent_fallback else "FAIL",
        "NO_FAKE_LOCKDOWN_STUBS": "PASS" if not stub_violation else "FAIL",
        "DIAGNOSTIC_CLEAN_PUBLIC_SELECTION": (
            "PASS" if (doconf_valid and not amcc_found and not sptm_found and not missing_list) else "FAIL"
        ),
        "DIAGNOSTIC_FAIL_CLOSED_GUARD": (
            "VERIFIED" if diag_fail_closed else ("NOT_APPLICABLE" if not patch_text else "FAIL")
        ),
        "B4_PHYSICAL_PMAP_LOCKDOWN": "BLOCKED",
        "B4_NATIVE_XNU_FULL_LINK": "NOT_RUN",
        "XNU_DEVICE_ENTRY": "NOT_RUN",
        "SECURITY_GATE": "BLOCKED",
        "can_promote_to_device": False,
    }
