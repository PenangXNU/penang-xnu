/* SPDX-License-Identifier: Apache-2.0 */
/*
 * GICv3 architectural register map, from Arm IHI 0069 (GICv3 and GICv4
 * architecture specification). Offsets and bit positions only: no base
 * address, no board selection and no interrupt-group policy.
 *
 * XNU's own GICv3 constants live in pexpert/pexpert/arm64/VMAPPLE.h, which
 * also selects the VMAPPLE board (NO_MONITOR, HAS_GIC_V3, Group 0 timer as
 * FIQ). A non-Apple board must not include that header to obtain an offset;
 * this file exists so it never has to.
 */
#ifndef PENANG_P1_GICV3_REGS_H
#define PENANG_P1_GICV3_REGS_H

/* Distributor (GICD_*), relative to the distributor frame. */
#define PPP_GICD_CTLR        0x0000u
#define PPP_GICD_TYPER       0x0004u
#define PPP_GICD_IGROUPR     0x0080u
#define PPP_GICD_ISENABLER   0x0100u
#define PPP_GICD_ICENABLER   0x0180u
#define PPP_GICD_ISPENDR     0x0200u
#define PPP_GICD_ICPENDR     0x0280u
#define PPP_GICD_ISACTIVER   0x0300u
#define PPP_GICD_ICACTIVER   0x0380u
#define PPP_GICD_IPRIORITYR  0x0400u
#define PPP_GICD_ICFGR       0x0c00u
#define PPP_GICD_IGRPMODR    0x0d00u
#define PPP_GICD_IROUTER     0x6000u
#define PPP_GIC_PIDR2        0xffe8u /* same offset in every GICv3 frame */

/* GICD_CTLR. Bit 1 enables Group 1 non-secure both in the non-secure view of
 * a two-security-state distributor and when DS == 1. Bit 0 is Group 0 when
 * DS == 1 and is never written by a non-secure Group 1 client. */
#define PPP_GICD_CTLR_ENABLE_BIT0 (1u << 0)
#define PPP_GICD_CTLR_ENABLE_G1NS (1u << 1)
#define PPP_GICD_CTLR_ARE_NS      (1u << 4)
#define PPP_GICD_CTLR_DS          (1u << 6)
#define PPP_GICD_CTLR_RWP         (1u << 31)
#define PPP_GICD_TYPER_ITLINES    0x1fu

/* Redistributor: RD_base frame, then the SGI_base frame 64 KiB above it.
 * SGI/PPI registers reuse the distributor offsets inside SGI_base. */
#define PPP_GICR_CTLR        0x0000u
#define PPP_GICR_TYPER       0x0008u /* 64-bit */
#define PPP_GICR_WAKER       0x0014u
#define PPP_GICR_SGI_BASE    0x10000u
#define PPP_GICR_FRAME_SIZE  0x10000u
#define PPP_GICR_CTLR_RWP    (1u << 3)
#define PPP_GICR_TYPER_VLPIS (1u << 1)
#define PPP_GICR_TYPER_LAST  (1u << 4)
#define PPP_GICR_TYPER_AFFINITY_SHIFT 32
#define PPP_GICR_WAKER_PROCESSORSLEEP (1u << 1)
#define PPP_GICR_WAKER_CHILDRENASLEEP (1u << 2)

/* CPU interface system registers. */
#define PPP_ICC_SRE_SRE      (1u << 0)
#define PPP_ICC_CTLR_CBPR    (1u << 0)
#define PPP_ICC_CTLR_EOIMODE (1u << 1)
#define PPP_ICC_IAR_INTID    0xffffffu
#define PPP_GIC_INTID_SPECIAL 1020u /* 1020..1023 are never completed */

/* The values XNU's VMAPPLE.h gives the same registers, restated so a test
 * can prove both describe one architecture. VMAPPLE.h names bit 0 of
 * ICC_CTLR_EL1 "EOIMODE"; architecturally bit 0 is CBPR and EOImode is bit 1. */
_Static_assert(PPP_GICD_CTLR == 0x0 && PPP_GICR_TYPER == 0x08 && PPP_GICR_WAKER == 0x14 &&
    PPP_GICR_SGI_BASE + PPP_GICD_IGROUPR == 0x10080 &&
    PPP_GICR_SGI_BASE + PPP_GICD_ISENABLER == 0x10100 &&
    PPP_GICR_TYPER_LAST == 0x10 && PPP_GICR_WAKER_PROCESSORSLEEP == 0x2 &&
    PPP_GICR_WAKER_CHILDRENASLEEP == 0x4 && PPP_ICC_SRE_SRE == 0x1 &&
    2 * PPP_GICR_FRAME_SIZE == 0x20000, "GICv3 register map");

#endif /* PENANG_P1_GICV3_REGS_H */
