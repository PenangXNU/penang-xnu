/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Bare-metal EL1 entry for the QEMU `virt` harness of the P1 platform
 * services. Linux arm64 Image header so the emulator passes the DTB in x0.
 * Research test fixture only; it is not a boot path for any device.
 */
    .section .head, "ax"
    .globl _start
_start:
    b       entry
    .long   0
    .quad   0x80000                 /* text_offset */
    .quad   _image_end - _start     /* image_size */
    .quad   0                       /* flags: little endian, legacy placement */
    .quad   0, 0, 0
    .ascii  "ARM\x64"
    .long   0

entry:
    mov     x19, x0                 /* DTB */
    mrs     x1, CurrentEL
    cmp     x1, #(1 << 2)           /* the harness contract is EL1 only */
    b.ne    wrong_el
    adrp    x1, _stack_top
    add     x1, x1, :lo12:_stack_top
    mov     sp, x1
    adrp    x1, vectors
    add     x1, x1, :lo12:vectors
    msr     vbar_el1, x1
    isb
    adrp    x1, _bss_start
    add     x1, x1, :lo12:_bss_start
    adrp    x2, _bss_end
    add     x2, x2, :lo12:_bss_end
1:  cmp     x1, x2
    b.hs    2f
    str     xzr, [x1], #8
    b       1b
2:  mov     x0, x19
    bl      harness_main
wrong_el:
    mov     x0, #2
    bl      harness_exit
3:  wfi
    b       3b

    .macro  unexpected kind
    .balign 0x80
    mov     x0, #\kind
    mrs     x1, esr_el1
    mrs     x2, elr_el1
    b       harness_unexpected
    .endm

    .balign 0x800
vectors:
    unexpected 0                    /* current EL, SP0 */
    unexpected 1
    unexpected 2
    unexpected 3
    unexpected 4                    /* current EL, SPx: synchronous */
    .balign 0x80
    b       irq_entry               /* IRQ */
    unexpected 6                    /* FIQ: must never be taken */
    unexpected 7
    unexpected 8
    unexpected 9
    unexpected 10
    unexpected 11
    unexpected 12
    unexpected 13
    unexpected 14
    unexpected 15

irq_entry:
    sub     sp, sp, #192
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x30, [sp, #144]
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #160]
    bl      harness_irq
    ldp     x0, x1, [sp, #160]
    msr     elr_el1, x0
    msr     spsr_el1, x1
    ldp     x18, x30, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8, x9, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #192
    eret
