/* SPDX-License-Identifier: GPL-3.0-or-later
 * Instrument application partition. Existing bootloader occupies the region
 * below 0x08040000; sectors10/11 are reserved for the settings journal. */
ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0x08040000, LENGTH = 512K
    SETTINGS (r) : ORIGIN = 0x080c0000, LENGTH = 256K
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    CCM (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
}
__stack_top__ = ORIGIN(SRAM) + LENGTH(SRAM);
__stack_limit__ = __stack_top__ - 8K;
__flash_used_end__ = LOADADDR(.data) + SIZEOF(.data);
__settings_start__ = ORIGIN(SETTINGS);
__settings_end__ = ORIGIN(SETTINGS) + LENGTH(SETTINGS);
SECTIONS {
    .isr_vector ORIGIN(FLASH) : ALIGN(512) {KEEP(*(.isr_vector))} > FLASH
    .text : ALIGN(4) {*(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4);} > FLASH
    .ARM.extab : ALIGN(4) {*(.ARM.extab* .gnu.linkonce.armextab.*)} > FLASH
    .ARM.exidx : ALIGN(4) {__exidx_start__ = .; *(.ARM.exidx* .gnu.linkonce.armexidx.*) __exidx_end__ = .;} > FLASH
    .data : ALIGN(4) {
        __data_start__ = .; *(.data .data.*) . = ALIGN(4); __data_end__ = .;
    } > SRAM AT > FLASH
    __data_load__ = LOADADDR(.data);
    .bss (NOLOAD) : ALIGN(4) {__bss_start__ = .; *(.bss .bss.*) *(COMMON) . = ALIGN(4); __bss_end__ = .;} > SRAM
    .noinit (NOLOAD) : ALIGN(4) {*(.noinit .noinit.*) . = ALIGN(4); __ram_end__ = .;} > SRAM
    /* Engine contains no DMA buffers. Its explicit init owns this NOLOAD
     * region; generic startup continues to clear only ordinary SRAM .bss. */
    .ccm_noinit (NOLOAD) : ALIGN(8) {
        __ccm_noinit_start__ = .; *(.ccm_noinit) . = ALIGN(8); __ccm_noinit_end__ = .;
    } > CCM
    /DISCARD/ : {*(.comment) *(.note.GNU-stack)}
    ASSERT(SIZEOF(.isr_vector) == 392, "F405 vector table must contain 98 words")
    ASSERT(__ram_end__ <= __stack_limit__, "RAM overlaps reserved 8 KiB stack")
    ASSERT(__ccm_noinit_end__ <= ORIGIN(CCM) + LENGTH(CCM), "CPU engine exceeds CCM")
    ASSERT(LOADADDR(.data) + SIZEOF(.data) <= ORIGIN(SETTINGS), "Application overlaps reserved settings sectors")
}
