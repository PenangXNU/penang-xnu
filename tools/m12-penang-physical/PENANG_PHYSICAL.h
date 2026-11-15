/* SPDX-License-Identifier: MIT
 * PENANG_PHYSICAL: stage-one compile configuration (NOT a verified device BSP).
 * Qualcomm SM4350-AC / ARM64 boot CPU only, 16KiB exploratory XNU target.
 *
 * No Apple PAC, KTRR/CTRR, PPL, SPTM, or fabricated hardware trust root.
 * This file does not qualify CPU TGran16, runtime MMIO, interconnect coherency
 * or memory ownership. Those require M11-D3 and platform-provider proof.
 */
#ifndef _PEXPERT_ARM64_PENANG_PHYSICAL_H
#define _PEXPERT_ARM64_PENANG_PHYSICAL_H

#if defined(__arm64e__) || defined(__ARM_FEATURE_PAUTH) || \
    defined(CPU_HAS_APPLE_PAC) || defined(HAS_APPLE_PAC) || defined(HAS_PARAVIRTUALIZED_PAC)
#error PENANG_PHYSICAL requires the non-arm64e ABI without Apple PAC
#endif
#if defined(CONFIG_SPTM) || defined(KERNEL_INTEGRITY_KTRR) || \
    defined(KERNEL_INTEGRITY_CTRR) || defined(KERNEL_INTEGRITY_WT) || \
    defined(MONITOR) || defined(XNU_MONITOR) || defined(PMAP_CS) || \
    defined(PMAP_CS_ENABLE) || defined(HAS_GUARDED_IO_FILTER) || \
    defined(APPLE_ARM64_ARCH_FAMILY)
#error PENANG_PHYSICAL cannot claim unimplemented Apple monitor or AMCC/GXF hardware
#endif
#if defined(NO_MONITOR)
#error PENANG_PHYSICAL forbids silent NO_MONITOR Stage-1-only fallback while B4.2 lockdown is BLOCKED
#endif
#if defined(__ARM_RANGE_TLBI__) && __ARM_RANGE_TLBI__
#error PENANG_PHYSICAL Kryo 460 (ARMv8.2-A) does not implement FEAT_TLBIRANGE
#endif
#if defined(QEMU_VIRT) || defined(ARM64_BOARD_CONFIG_QEMU_VIRT) || defined(APPLEVIRTUALPLATFORM)
#error PENANG_PHYSICAL forbids QEMU_VIRT or virtual platform cross-contamination
#endif
#if defined(PENANG_B43_VERIFIED_PHYSICAL_LOCKDOWN)
#error PENANG_PHYSICAL forbids asserting unverified physical lockdown gate override
#endif
/* No physical PMAP lockdown/Stage-2 ownership implementation has been
 * admitted. This is a compile contract, not authority to boot. */
#define PENANG_PHYSICAL 1
#define MAX_CPUS 1
#define MAX_CPU_CLUSTERS 1
#define MAX_L2_CLINE 6
#define NO_XNU_PLATFORM_ERROR_HANDLER 1

#define __ARM_ARCH__ 8
#define __ARM_VMSA__ 8
#define __ARM_VFP__ 4
#define __ARM_16K_PG__ 1
#define ARM_PARAMETERIZED_PMAP 1
#define __ARM_MIXED_PAGE_SIZE__ 1
#define ARM_ARCH_TIMER 1
#define HAS_GIC_V3 1
#define GIC_SPURIOUS_IRQ 1023
#define GICR_PE_SIZE 0x20000

/* Deliberately no PL011_UART, APPLEARMSMP, NO_MONITOR, weak PMAP,
 * PMIC, PSCI or KDK KernelSupport compatibility statements. */
#endif
