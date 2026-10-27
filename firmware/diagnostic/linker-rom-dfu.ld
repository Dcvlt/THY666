/* SPDX-License-Identifier: GPL-3.0-or-later
 * Volatile USB ROM-DFU reader candidate, never an application update image.
 * AN2606 F405/407 boot ROM uses the first 8 or 12 KiB of SRAM. Reserve 16 KiB
 * during download; all vectors, code, initializers and runtime storage are above
 * it. ROM reachability, chip identity, protection and launch remain unverified. */
ENTRY(Reset_Handler)

MEMORY {
    SRAM (rwx) : ORIGIN = 0x20004000, LENGTH = 112K
}

__stack_top__ = ORIGIN(SRAM) + LENGTH(SRAM);
__stack_limit__ = __stack_top__ - 8K;

/* Explicit headers prevent LLD from creating a loadable ELF-header segment
 * below the first section, inside the boot ROM's working RAM. */
PHDRS {
    vectors PT_LOAD FLAGS(4);
    text PT_LOAD FLAGS(5);
    unwinding PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    .isr_vector ORIGIN(SRAM) : ALIGN(512) {
        KEEP(*(.isr_vector))
    } > SRAM :vectors

    .text : ALIGN(4) {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > SRAM :text

    .ARM.extab : ALIGN(4) { *(.ARM.extab* .gnu.linkonce.armextab.*) } > SRAM :unwinding
    .ARM.exidx : ALIGN(4) {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > SRAM :unwinding

    .data : ALIGN(4) {
        __data_start__ = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end__ = .;
    } > SRAM :data
    __data_load__ = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4) {
        __bss_start__ = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > SRAM :data

    .noinit (NOLOAD) : ALIGN(4) {
        *(.noinit .noinit.*)
        . = ALIGN(4);
        __ram_end__ = .;
    } > SRAM :data

    /DISCARD/ : { *(.comment) *(.note.GNU-stack) }
    ASSERT(ADDR(.isr_vector) == 0x20004000, "ROM DFU vectors must leave the low 16 KiB of SRAM untouched")
    ASSERT(SIZEOF(.isr_vector) == 392, "F405 vector table must contain 98 words")
    ASSERT(__data_load__ == __data_start__, "RAM data initializer must have identical LMA and VMA")
    ASSERT(__ram_end__ <= __stack_limit__, "RAM code/data/BSS overlap reserved 8 KiB stack")
    ASSERT(ADDR(.text) >= ORIGIN(SRAM), "ROM DFU text overlaps the boot ROM RAM reservation")
    ASSERT(ADDR(.text) + SIZEOF(.text) <= __stack_limit__, "RAM text exceeds usable SRAM")
}
