/* SPDX-License-Identifier: GPL-3.0-or-later
 * Candidate volatile SWD-loaded reader. All loadable bytes remain in SRAM;
 * this script does not contain a nonvolatile programming/installation path.
 * Hardware identity, SWD access and readout protection remain unverified. */
ENTRY(Reset_Handler)

MEMORY {
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

__stack_top__ = ORIGIN(SRAM) + LENGTH(SRAM);
__stack_limit__ = __stack_top__ - 8K;

SECTIONS {
    .isr_vector ORIGIN(SRAM) : ALIGN(512) {
        KEEP(*(.isr_vector))
    } > SRAM

    .text : ALIGN(4) {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > SRAM

    .ARM.extab : ALIGN(4) { *(.ARM.extab* .gnu.linkonce.armextab.*) } > SRAM
    .ARM.exidx : ALIGN(4) {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > SRAM

    .data : ALIGN(4) {
        __data_start__ = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end__ = .;
    } > SRAM
    /* The debugger loads the data initializer at its runtime SRAM address.
     * Startup's ordinary forward copy is safe when source equals destination. */
    __data_load__ = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4) {
        __bss_start__ = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > SRAM

    .noinit (NOLOAD) : ALIGN(4) {
        *(.noinit .noinit.*)
        . = ALIGN(4);
        __ram_end__ = .;
    } > SRAM

    /DISCARD/ : { *(.comment) *(.note.GNU-stack) }
    ASSERT(ADDR(.isr_vector) == ORIGIN(SRAM), "RAM vector table must begin at SRAM base")
    ASSERT(SIZEOF(.isr_vector) == 392, "F405 vector table must contain 98 words")
    ASSERT(__data_load__ == __data_start__, "RAM data initializer must have identical LMA and VMA")
    ASSERT(__ram_end__ <= __stack_limit__, "RAM code/data/BSS overlap reserved 8 KiB stack")
    ASSERT(ADDR(.text) >= ORIGIN(SRAM), "RAM text must reside in SRAM")
    ASSERT(ADDR(.text) + SIZEOF(.text) <= __stack_limit__, "RAM text exceeds usable SRAM")
}
