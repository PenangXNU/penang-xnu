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
#if !PENANG_P1_DIAGNOSTIC || !defined(ARM64_BOARD_CONFIG_PENANG_P1_DIAGNOSTIC) || \
    !ARM64_BOARD_CONFIG_PENANG_P1_DIAGNOSTIC || \
    !defined(PENANG_P1_SANDBOX_COMPILE) || !PENANG_P1_SANDBOX_COMPILE || \
    !defined(PENANG_P1_IOMMU_CODEGEN_ONLY) || !PENANG_P1_IOMMU_CODEGEN_ONLY || \
    !defined(NO_MONITOR) || !NO_MONITOR
#error P1 PMAP requires explicit diagnostic code-generation selection
#endif
#if defined(ARM64_BOARD_CONFIG_PENANG_PHYSICAL) || defined(PENANG_P2_PROTECTED) || \
    defined(QEMU_VIRT) || defined(VMAPPLE) || defined(APPLEVIRTUALPLATFORM) || \
    defined(__arm64e__) || defined(HAS_APPLE_PAC) || \
    (defined(CONFIG_PMAP_PPL) && CONFIG_PMAP_PPL) || \
    (defined(CONFIG_SPTM) && CONFIG_SPTM) || (defined(XNU_MONITOR) && XNU_MONITOR) || \
    defined(KERNEL_INTEGRITY_KTRR) || defined(KERNEL_INTEGRITY_CTRR)
#error P1 PMAP cannot replace a protected or Apple-platform bootstrap
#endif
#define P1_PMAP_APPLE_IO_BOOTSTRAP 0
#else
#if defined(ARM64_BOARD_CONFIG_PENANG_P1_DIAGNOSTIC) || defined(PENANG_P1_IOMMU_CODEGEN_ONLY)
#error Partial P1 PMAP selection is forbidden
#endif
#define P1_PMAP_APPLE_IO_BOOTSTRAP 1
#endif
#endif
