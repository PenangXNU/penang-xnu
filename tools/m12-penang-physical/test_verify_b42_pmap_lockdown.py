#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""M12-B4.2 synthetic & adversarial negative tests for verify_b42_pmap_lockdown."""
from pathlib import Path
from verify_b42_pmap_lockdown import assess_b42

HERE = Path(__file__).resolve().parent
BOARD_HDR = (HERE / "PENANG_PHYSICAL.h").read_text()

IOKIT_OK = (
    "$(SOURCE_DIR)/iokit/penang_physical/PenangPhysicalPlatformExpert.cpp \\\n"
    "$(SOURCE_DIR)/iokit/penang_physical/penang_physical_contract.c\n"
)
OSFMK_CORE = (
    "export CONFIG_DEFINES = -DCONFIG_PMAP_PPL -DCONFIG_KERNEL_INTEGRITY\n"
    "$(SOURCE_DIR)/osfmk/arm/pmap/pmap.c \\\n"
    "$(SOURCE_DIR)/osfmk/arm/arm_init.c \\\n"
    "$(SOURCE_DIR)/osfmk/arm64/arm_vm_init.c \\\n"
    "$(SOURCE_DIR)/osfmk/arm64/locore.s \\\n"
    "$(SOURCE_DIR)/osfmk/arm64/start.s\n"
)
OSFMK_B41 = (
    OSFMK_CORE
    + "$(SOURCE_DIR)/osfmk/arm64/amcc_rorgn_ppl.c \\\n"
    + "$(SOURCE_DIR)/osfmk/arm64/amcc_rorgn_ppl_amcc.c \\\n"
    + "$(SOURCE_DIR)/osfmk/arm64/amcc_rorgn_ppl_ctrr3.c \\\n"
    + "$(SOURCE_DIR)/osfmk/arm64/amcc_rorgn_common.c\n"
)
CFG_OK = "options CONFIG_PMAP_PPL\noptions PENANG_PHYSICAL_SOURCES\noptions CONFIG_KERNEL_INTEGRITY\n"
DIAG_PATCH_OK = (
    "--- a/osfmk/arm/pmap/pmap.c\n+++ b/osfmk/arm/pmap/pmap.c\n"
    '+#error "PENANG_PHYSICAL: B4.2 physical PMAP/Stage-2/SMMU lockdown backend is BLOCKED; "\n'
    "--- a/osfmk/arm64/arm_vm_init.c\n+++ b/osfmk/arm64/arm_vm_init.c\n"
    '+#error "PENANG_PHYSICAL: B4.2 physical PMAP/Stage-2/SMMU lockdown backend is BLOCKED; "\n'
)


def check_common_blocked(res, label):
    assert res["B4_PHYSICAL_PMAP_LOCKDOWN"] == "BLOCKED", label
    assert res["B4_NATIVE_XNU_FULL_LINK"] == "NOT_RUN", label
    assert res["XNU_DEVICE_ENTRY"] == "NOT_RUN", label
    assert res["SECURITY_GATE"] == "BLOCKED", label
    assert res["can_promote_to_device"] is False, label


# 1. B4.1 replay baseline: catches all 4 AMCC files
r1 = assess_b42(
    IOKIT_OK,
    OSFMK_B41,
    CFG_OK,
    board_header_text=BOARD_HDR,
    missing_public_sources=[
        "osfmk/arm64/amcc_rorgn_ppl.c",
        "osfmk/arm64/amcc_rorgn_ppl_amcc.c",
        "osfmk/arm64/amcc_rorgn_ppl_ctrr3.c",
        "osfmk/arm64/amcc_rorgn_common.c",
    ],
)
assert r1["DOCONF_NATIVE_VALID"] == "PASS"
assert r1["APPLE_AMCC_LEAK"] == "FOUND_4_FILES"
assert r1["PUBLIC_DROP_SOURCE_COMPLETENESS"] == "FAIL"
assert r1["DIAGNOSTIC_CLEAN_PUBLIC_SELECTION"] == "FAIL"
check_common_blocked(r1, "b41_baseline")

# 2. B4.2 diagnostic fail-closed profile: 0 AMCC, 0 missing, #error guard verified, still BLOCKED
r2 = assess_b42(IOKIT_OK, OSFMK_CORE, CFG_OK, board_header_text=BOARD_HDR, patch_text=DIAG_PATCH_OK)
assert r2["DOCONF_NATIVE_VALID"] == "PASS"
assert r2["APPLE_AMCC_LEAK"] == "NONE"
assert r2["PUBLIC_DROP_SOURCE_COMPLETENESS"] == "PASS"
assert r2["DIAGNOSTIC_CLEAN_PUBLIC_SELECTION"] == "PASS"
assert r2["DIAGNOSTIC_FAIL_CLOSED_GUARD"] == "VERIFIED"
check_common_blocked(r2, "b42_diagnostic_clean")

# 3. Hidden 4th file amcc_rorgn_common.c leak (the exact gap from B4.1!)
r3 = assess_b42(
    IOKIT_OK,
    OSFMK_CORE + "$(SOURCE_DIR)/osfmk/arm64/amcc_rorgn_common.c\n",
    CFG_OK,
    board_header_text=BOARD_HDR,
    patch_text=DIAG_PATCH_OK,
    missing_public_sources=["osfmk/arm64/amcc_rorgn_common.c"],
)
assert r3["APPLE_AMCC_LEAK"] == "FOUND_1_FILES"
assert r3["DIAGNOSTIC_CLEAN_PUBLIC_SELECTION"] == "FAIL"
assert r3["DIAGNOSTIC_FAIL_CLOSED_GUARD"] == "FAIL"
check_common_blocked(r3, "hidden_amcc_common_leak")

# 4. Naive CONFIG_PMAP_PPL removal dropping arm_init.c / arm_vm_init.c / start.s
r4 = assess_b42(
    IOKIT_OK,
    "$(SOURCE_DIR)/osfmk/arm/pmap/pmap.c \\\n$(SOURCE_DIR)/osfmk/arm64/locore.s\n",
    "options PENANG_PHYSICAL_SOURCES\n",
    board_header_text=BOARD_HDR,
)
assert r4["BOOTSTRAP_CORE_SOURCES"] == "FAIL"
assert r4["DOCONF_NATIVE_VALID"] == "FAIL"
check_common_blocked(r4, "naive_ppl_removal_drops_bootstrap")

# 5. SPTM leak
r5 = assess_b42(
    IOKIT_OK,
    OSFMK_CORE + "$(SOURCE_DIR)/osfmk/arm64/amcc_rorgn_sptm.c\n",
    CFG_OK,
    board_header_text=BOARD_HDR,
    patch_text=DIAG_PATCH_OK,
)
assert r5["APPLE_SPTM_LEAK"] == "FOUND"
assert r5["DIAGNOSTIC_CLEAN_PUBLIC_SELECTION"] == "FAIL"
check_common_blocked(r5, "sptm_leak")

# 6. QEMU_VIRT platform contamination
r6 = assess_b42(
    IOKIT_OK + "$(SOURCE_DIR)/iokit/penang_qemu_virt/PenangQemuVirtPlatformExpert.cpp\n",
    OSFMK_CORE,
    CFG_OK,
    board_header_text=BOARD_HDR,
    patch_text=DIAG_PATCH_OK,
)
assert r6["NO_QEMU_CONTAMINATION"] == "FAIL"
assert r6["DIAGNOSTIC_FAIL_CLOSED_GUARD"] == "FAIL"
check_common_blocked(r6, "qemu_leak")

# 7. Silent NO_MONITOR fallback in board header
r7 = assess_b42(
    IOKIT_OK,
    OSFMK_CORE,
    CFG_OK,
    board_header_text=BOARD_HDR + "\n#define NO_MONITOR 1\n",
    patch_text=DIAG_PATCH_OK,
)
assert r7["NO_SILENT_FALLBACK"] == "FAIL"
assert r7["DIAGNOSTIC_FAIL_CLOSED_GUARD"] == "FAIL"
check_common_blocked(r7, "silent_no_monitor_header")

# 8. Silent -DXNU_NO_TYPE_BUILTINS compiler flag fallback
r8 = assess_b42(
    IOKIT_OK,
    OSFMK_CORE + " -DXNU_NO_TYPE_BUILTINS ",
    CFG_OK,
    board_header_text=BOARD_HDR,
    patch_text=DIAG_PATCH_OK,
)
assert r8["NO_SILENT_FALLBACK"] == "FAIL"
check_common_blocked(r8, "no_type_builtins_fallback")

# 9. Fake pmap_ppl_lockdown_pages no-op stub in patch
r9 = assess_b42(
    IOKIT_OK,
    OSFMK_CORE,
    CFG_OK,
    board_header_text=BOARD_HDR,
    patch_text=DIAG_PATCH_OK + "+void pmap_ppl_lockdown_pages(uintptr_t a, size_t b, uint64_t c, bool d) {}\n",
)
assert r9["NO_FAKE_LOCKDOWN_STUBS"] == "FAIL"
assert r9["DIAGNOSTIC_FAIL_CLOSED_GUARD"] == "FAIL"
check_common_blocked(r9, "fake_lockdown_stub")

# 10. Fake ml_enable_monitor stub in patch
r10 = assess_b42(
    IOKIT_OK,
    OSFMK_CORE,
    CFG_OK,
    board_header_text=BOARD_HDR,
    patch_text=DIAG_PATCH_OK + "+void ml_enable_monitor(void) {}\n",
)
assert r10["NO_FAKE_LOCKDOWN_STUBS"] == "FAIL"
check_common_blocked(r10, "fake_ml_enable_monitor_stub")

# 11. Diagnostic patch missing fail-closed #error guard
r11 = assess_b42(
    IOKIT_OK,
    OSFMK_CORE,
    CFG_OK,
    board_header_text=BOARD_HDR,
    patch_text="--- a/osfmk/conf/files.arm64\n+++ b/osfmk/conf/files.arm64\n",
)
assert r11["DIAGNOSTIC_FAIL_CLOSED_GUARD"] == "FAIL"
check_common_blocked(r11, "missing_fail_closed_error_guard")

# 12. Non-empty doconf stderr
r12 = assess_b42(
    IOKIT_OK,
    OSFMK_CORE,
    CFG_OK,
    board_header_text=BOARD_HDR,
    stderr_osfmk="config: syntax error",
)
assert r12["DOCONF_NATIVE_VALID"] == "FAIL"
check_common_blocked(r12, "doconf_stderr_error")

print("B42_PMAP_LOCKDOWN_VERIFIER_TESTS_PASS total=12 negatives=10 device_gate=BLOCKED")
