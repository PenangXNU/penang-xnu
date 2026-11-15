/* SPDX-License-Identifier: MIT
 * T73-U0 original BSD getpid fixture, distinct from pinned T72-Q5.
 * Source ABI: bsd/kern/syscalls.master (getpid=20),
 * osfmk/arm64/proc_reg.h (ARM64_SYSCALL_CODE_REG_NUM=16),
 * bsd/dev/arm/systemcalls.c (result x0, error CPSR.C),
 * osfmk/arm64/sleh.c (handle_svc -> unix_syscall for nonnegative trap).
 * HOST_ONLY until exact Mach-O admission and guest execution qualify.
 */
.section .text.entry,"ax"
.global _fixture_main, _fixture_success, _fixture_fail
.global _fixture_before_syscall, _fixture_after_syscall
.type _fixture_main,%function
_fixture_main:
    adrp x9, _fixture_pointer
    add x9, x9, :lo12:_fixture_pointer
    ldr x10, [x9]
    adrp x11, _fixture_cookie
    add x11, x11, :lo12:_fixture_cookie
    cmp x10, x11
    b.ne _fixture_precondition_fail
    ldr x13, [x10] /* Consume real relocated pointer */
    movz x14, #0x5437
    movk x14, #0x4e47, lsl #16
    movk x14, #0x4e41, lsl #32
    movk x14, #0x5045, lsl #48
    cmp x13, x14
    b.ne _fixture_precondition_fail

    mov w16, #20        /* SYS_getpid, zero arguments */
_fixture_before_syscall:
    svc #0x80
_fixture_after_syscall:
    adrp x9, _fixture_pointer
    add x9, x9, :lo12:_fixture_pointer
    str x0, [x9, #8]   /* raw return, not a synthesized PID */
    mrs x14, nzcv
    str x14, [x9, #32] /* raw carry flag preserved */
    tbnz x14, #29, _fixture_fail
    cbz x0, _fixture_fail
    tbnz x0, #63, _fixture_fail

    /* T73_GET1 distinct from Q5 T72_CON1. */
    movz x14, #0x5431
    movk x14, #0x4745, lsl #16
    movk x14, #0x335f, lsl #32
    movk x14, #0x5437, lsl #48
    str x14, [x9, #24]
_fixture_success:
    b _fixture_success
_fixture_precondition_fail:
    mov x14, #1
    str x14, [x9, #32]
_fixture_fail:
    b _fixture_fail
.size _fixture_main, .-_fixture_main

.section .data,"aw"
.balign 8
.global _fixture_pointer, _fixture_cookie, _fixture_result, _fixture_error
.global _fixture_syscall_raw_return
_fixture_pointer: .quad _fixture_cookie
_fixture_syscall_raw_return: .quad 0
_fixture_cookie: .quad 0x50454e414e475437
_fixture_result: .quad 0
_fixture_error: .quad 0
.section .note.GNU-stack,"",%progbits
