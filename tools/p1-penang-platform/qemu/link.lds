/* SPDX-License-Identifier: Apache-2.0 */
ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    .text : { KEEP(*(.head)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(16);
    _image_end = .;
    _bss_start = .;
    .bss : { *(.bss .bss.* COMMON) }
    . = ALIGN(16);
    _bss_end = .;
    . += 0x10000;
    _stack_top = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
