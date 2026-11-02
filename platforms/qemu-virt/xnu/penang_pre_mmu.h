/* SPDX-License-Identifier: Apache-2.0
 * QEMU_VIRT pre-MMU contract v1. x1=desired XNU TCR, preserved.
 * Clobbers x9-x11 only. No stack, calls, MMIO, MSR, barriers or TLB changes.
 * Must run at privileged XNU entry; it does not detect a physical host CPU.
 */
#include <arm64/penang_translation_v1.h>
.macro PV_PRE_MMU_GUARD
    mov x11, #9
    mrs x9, CurrentEL
    cmp x9, #4
    b.ne Lpv_pre_mmu_reject
    mov x11, #10
    mrs x9, SCTLR_EL1
    tbnz x9, #0, Lpv_pre_mmu_reject
    mov x11, #11
    mrs x9, MIDR_EL1
    ubfx x10, x9, #24, #8
    cmp x10, #0x41
    b.ne Lpv_pre_mmu_reject
    ubfx x10, x9, #16, #4
    cmp x10, #15
    b.ne Lpv_pre_mmu_reject
    ubfx x10, x9, #4, #12
    cmp x10, #0xd0b
    b.ne Lpv_pre_mmu_reject
    mrs x9, ID_AA64MMFR0_EL1
    mov x11, #3
    ubfx x10, x9, #20, #4
    cbz x10, Lpv_pre_mmu_reject
    mov x11, #4
    cmp x10, #2
    b.hi Lpv_pre_mmu_reject
    mov x11, #6
    and x10, x9, #15
    cmp x10, #2
    b.lo Lpv_pre_mmu_reject
    cmp x10, #6
    b.hi Lpv_pre_mmu_reject
    ubfx x10, x1, #32, #3
    cmp x10, #2
    b.ne Lpv_pre_mmu_reject
    mov x11, #5
    ubfx x10, x1, #14, #2
    cmp x10, #2
    b.ne Lpv_pre_mmu_reject
    ubfx x10, x1, #30, #2
    cmp x10, #1
    b.ne Lpv_pre_mmu_reject
    mov x11, #7
    and x10, x1, #63
    cmp x10, #17
    b.ne Lpv_pre_mmu_reject
    ubfx x10, x1, #16, #6
    cmp x10, #17
    b.ne Lpv_pre_mmu_reject
    mov x11, #8
    tbnz x1, #59, Lpv_pre_mmu_reject
    mov x11, #17
    ubfx x10, x1, #8, #6
    cmp x10, #PV_QEMU_TCR_CACHE
    b.ne Lpv_pre_mmu_reject
    ubfx x10, x1, #24, #6
    cmp x10, #PV_QEMU_TCR_CACHE
    b.ne Lpv_pre_mmu_reject
    bic x10, x1, #PV_QEMU_OPTIONAL_ASID16
    movz x11, #(PV_QEMU_TCR_BASE & 0xffff)
    movk x11, #((PV_QEMU_TCR_BASE >> 16) & 0xffff), lsl #16
    movk x11, #((PV_QEMU_TCR_BASE >> 32) & 0xffff), lsl #32
    cmp x10, x11
    mov x11, #8
    b.ne Lpv_pre_mmu_reject
    tbz x1, #36, Lpv_pre_mmu_accept
    mov x11, #18
    ubfx x10, x9, #4, #4
    cmp x10, #2
    b.eq Lpv_pre_mmu_accept
Lpv_pre_mmu_reject:
    /* ESR BRK immediate identifies this guard; x11 is the pv_code.
     * If an earlier exception handler returns, remain stopped, never continue. */
    brk #0x5230
Lpv_pre_mmu_stopped:
    wfe
    b Lpv_pre_mmu_stopped
Lpv_pre_mmu_accept:
.endmacro
