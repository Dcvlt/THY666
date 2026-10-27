/* SPDX-License-Identifier: GPL-3.0-or-later
 * Application slot: bootloader occupies 0x08000000..0x0803ffff. */
ENTRY(Reset_Handler)

MEMORY {
    FLASH (rx) : ORIGIN = 0x08040000, LENGTH = 768K
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    CCM (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
}

__stack_top__ = ORIGIN(SRAM) + LENGTH(SRAM);
__stack_limit__ = __stack_top__ - 8K;

SECTIONS {
    .isr_vector ORIGIN(FLASH) : ALIGN(512) {
        KEEP(*(.isr_vector))
    } > FLASH

    .text : ALIGN(4) {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > FLASH

    .ARM.extab : ALIGN(4) { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx : ALIGN(4) {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    .data : ALIGN(4) {
        __data_start__ = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end__ = .;
    } > SRAM AT > FLASH
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
    ASSERT(SIZEOF(.isr_vector) == 392, "F405 vector table must contain 98 words")
    ASSERT(__ram_end__ <= __stack_limit__, "RAM overlaps reserved 8 KiB stack")
    ASSERT(LOADADDR(.data) + SIZEOF(.data) <= ORIGIN(FLASH) + LENGTH(FLASH), "Application slot overflow")
}
