/* SPDX-License-Identifier: MIT
 * ELF carrier layout only, not a signed Mach-O or XNU executable.
 */
ENTRY(_fixture_main)
SECTIONS {
    . = 0x100001000;
    .text : { *(.text.entry) *(.text .text.*) *(.rodata .rodata.*) }
    ASSERT(SIZEOF(.text) <= 0x3000, "T73 GETPID TEXT budget")
    . = 0x100004000;
    .data : { *(.data .data.*) }
    ASSERT(SIZEOF(.data) == 40, "T73 GETPID Q5-layout data must be 40 bytes")
    .symtab 0 : { *(.symtab) }
    .strtab 0 : { *(.strtab) }
    .shstrtab 0 : { *(.shstrtab) }
    /DISCARD/ : { *(.comment) *(.note.GNU-stack) }
}
