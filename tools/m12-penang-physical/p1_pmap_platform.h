/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PENANG_P1_PMAP_PLATFORM_H
#define PENANG_P1_PMAP_PLATFORM_H

/* Explicit code-generation experiment only. No implementation or successful
 * return is supplied for Apple's SART/UAT hardware bootstrap. Default and
 * Apple-platform compilation still require the original headers/providers.
 * This selector neither qualifies DMA nor admits a physical kernel image. */
#if defined(P1_PMAP_APPLE_IO_BOOTSTRAP)
#error PMAP platform result must not be overridden
#endif

#if defined(PENANG_P1_DIAGNOSTIC)
#if PENANG_P1_DIAGNOSTIC != 1 || !defined(ARM64_BOARD_CONFIG_PENANG_P1_DIAGNOSTIC) || \
    ARM64_BOARD_CONFIG_PENANG_P1_DIAGNOSTIC != 1 || \
    !defined(PENANG_P1_SANDBOX_COMPILE) || PENANG_P1_SANDBOX_COMPILE != 1 || \
    !defined(PENANG_P1_IOMMU_CODEGEN_ONLY) || PENANG_P1_IOMMU_CODEGEN_ONLY != 1 || \
    !defined(NO_MONITOR) || NO_MONITOR != 1
#error P1 PMAP requires explicit diagnostic code-generation selection
#endif
/* Presence alone activates MONITOR/WT and protected-board code in XNU.
 * The value-based feature selectors below may be explicitly zero, as in
 * generated configuration; none may be active in the P1 experiment. */
#if defined(PENANG_PHYSICAL) || defined(ARM64_BOARD_CONFIG_PENANG_PHYSICAL) || \
    defined(PENANG_P2_PROTECTED) || defined(MONITOR) || \
    defined(QEMU_VIRT) || defined(VMAPPLE) || defined(APPLEVIRTUALPLATFORM) || \
    defined(ARM64_BOARD_CONFIG_QEMU_VIRT) || \
    defined(__arm64e__) || defined(__ARM_FEATURE_PAUTH) || \
    defined(CPU_HAS_APPLE_PAC) || defined(HAS_APPLE_PAC) || defined(HAS_PARAVIRTUALIZED_PAC) || \
    defined(APPLE_ARM64_ARCH_FAMILY) || defined(PENANG_B43_VERIFIED_PHYSICAL_LOCKDOWN) || \
    (defined(CONFIG_PMAP_PPL) && CONFIG_PMAP_PPL) || \
    (defined(CONFIG_SPTM) && CONFIG_SPTM) || (defined(XNU_MONITOR) && XNU_MONITOR) || \
    (defined(PMAP_CS) && PMAP_CS) || (defined(PMAP_CS_ENABLE) && PMAP_CS_ENABLE) || \
    (defined(HAS_GUARDED_IO_FILTER) && HAS_GUARDED_IO_FILTER) || \
    defined(KERNEL_INTEGRITY_KTRR) || defined(KERNEL_INTEGRITY_CTRR) || defined(KERNEL_INTEGRITY_WT)
#error P1 PMAP cannot replace a protected or Apple-platform bootstrap
#endif
#define P1_PMAP_APPLE_IO_BOOTSTRAP 0
#else
#if defined(ARM64_BOARD_CONFIG_PENANG_P1_DIAGNOSTIC) || \
    defined(PENANG_P1_IOMMU_CODEGEN_ONLY) || defined(PENANG_P1_SANDBOX_COMPILE)
#error Partial P1 PMAP selection is forbidden
#endif
#define P1_PMAP_APPLE_IO_BOOTSTRAP 1
#endif
#endif
