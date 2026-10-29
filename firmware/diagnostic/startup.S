/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independently written startup. IRQ ordering/reserved slots checked against
 * ST's pinned STM32F405 CMSIS device header and startup reference. */
.syntax unified
.cpu cortex-m4
.thumb

.section .isr_vector,"a",%progbits
.balign 512
.global g_pfnVectors
.type g_pfnVectors,%object
g_pfnVectors:
    .word __stack_top__, Reset_Handler
    .word NMI_Handler, HardFault_Handler, MemManage_Handler
    .word BusFault_Handler, UsageFault_Handler
    .word 0, 0, 0, 0
    .word SVC_Handler, DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler
    .word WWDG_IRQHandler, PVD_IRQHandler, TAMP_STAMP_IRQHandler, RTC_WKUP_IRQHandler
    .word FLASH_IRQHandler, RCC_IRQHandler, EXTI0_IRQHandler, EXTI1_IRQHandler
    .word EXTI2_IRQHandler, EXTI3_IRQHandler, EXTI4_IRQHandler
    .word DMA1_Stream0_IRQHandler, DMA1_Stream1_IRQHandler, DMA1_Stream2_IRQHandler
    .word DMA1_Stream3_IRQHandler, DMA1_Stream4_IRQHandler, DMA1_Stream5_IRQHandler
    .word DMA1_Stream6_IRQHandler, ADC_IRQHandler
    .word CAN1_TX_IRQHandler, CAN1_RX0_IRQHandler, CAN1_RX1_IRQHandler, CAN1_SCE_IRQHandler
    .word EXTI9_5_IRQHandler, TIM1_BRK_TIM9_IRQHandler, TIM1_UP_TIM10_IRQHandler
    .word TIM1_TRG_COM_TIM11_IRQHandler, TIM1_CC_IRQHandler
    .word TIM2_IRQHandler, TIM3_IRQHandler, TIM4_IRQHandler
    .word I2C1_EV_IRQHandler, I2C1_ER_IRQHandler, I2C2_EV_IRQHandler, I2C2_ER_IRQHandler
    .word SPI1_IRQHandler, SPI2_IRQHandler, USART1_IRQHandler, USART2_IRQHandler
    .word USART3_IRQHandler, EXTI15_10_IRQHandler, RTC_Alarm_IRQHandler, OTG_FS_WKUP_IRQHandler
    .word TIM8_BRK_TIM12_IRQHandler, TIM8_UP_TIM13_IRQHandler, TIM8_TRG_COM_TIM14_IRQHandler
    .word TIM8_CC_IRQHandler, DMA1_Stream7_IRQHandler, FSMC_IRQHandler, SDIO_IRQHandler
    .word TIM5_IRQHandler, SPI3_IRQHandler, UART4_IRQHandler, UART5_IRQHandler
    .word TIM6_DAC_IRQHandler, TIM7_IRQHandler
    .word DMA2_Stream0_IRQHandler, DMA2_Stream1_IRQHandler, DMA2_Stream2_IRQHandler
    .word DMA2_Stream3_IRQHandler, DMA2_Stream4_IRQHandler, 0, 0
    .word CAN2_TX_IRQHandler, CAN2_RX0_IRQHandler, CAN2_RX1_IRQHandler, CAN2_SCE_IRQHandler
    .word OTG_FS_IRQHandler, DMA2_Stream5_IRQHandler, DMA2_Stream6_IRQHandler
    .word DMA2_Stream7_IRQHandler, USART6_IRQHandler, I2C3_EV_IRQHandler, I2C3_ER_IRQHandler
    .word OTG_HS_EP1_OUT_IRQHandler, OTG_HS_EP1_IN_IRQHandler, OTG_HS_WKUP_IRQHandler
    .word OTG_HS_IRQHandler, 0, 0, HASH_RNG_IRQHandler, FPU_IRQHandler
.size g_pfnVectors, .-g_pfnVectors

.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.type Reset_Handler,%function
.thumb_func
Reset_Handler:
    /* Preserve incoming CPU state without trusting any inherited stack/RAM.
     * r4-r12 remain untouched by stack selection and DMA reset below. */
    mrs r4, control
    mrs r5, ipsr
    mrs r6, primask
    mrs r7, basepri
    mrs r8, faultmask
    mrs r9, msp
    mrs r10, psp
    cpsid i
    ldr r1, =0xe000ed08
    ldr r11, [r1]
    ldr r1, =0xe000ed94
    ldr r12, [r1]
    /* A privileged Thread-mode launch is required. Do not inherit a loader's
     * region policy for our differently sized code/data layout. */
    movs r0, #0
    dsb
    str r0, [r1]
    dsb
    isb
    /* The updater could use PSP. Write both physical stack registers before
     * selecting MSP; writing SP alone would otherwise update the wrong one. */
    ldr r0, =__stack_top__
    msr msp, r0
    msr psp, r0
    movs r0, #0
    msr control, r0
    msr basepri, r0
    isb
    /* NVIC masking does not stop DMA writes into the application RAM. Enable
     * the DMA clocks and reset both engines before any .data/.bss operation. */
    ldr r1, =0x40023800
    ldr r2, [r1, #0x30]
    orr r2, r2, #0x00600000
    str r2, [r1, #0x30]
    dsb
    ldr r2, [r1, #0x10]
    orr r2, r2, #0x00600000
    str r2, [r1, #0x10]
    dsb
    bic r2, r2, #0x00600000
    str r2, [r1, #0x10]
    dsb
    /* No stack/RAM stores precede DMA quiescence. .noinit is excluded from
     * the .data/.bss loops, and magic is committed only after every sample. */
    ldr r1, =g_diag_entry + 8
    stmia r1!, {r4-r12}
    .macro entry_mmio address
        ldr r2, =\address
        ldr r3, [r2]
        str r3, [r1], #4
    .endm
    entry_mmio 0xe000ed88 /* CPACR */
    entry_mmio 0xe000ef34 /* FPCCR; LSPACT is retained, not repaired */
    entry_mmio 0xe000ed0c /* AIRCR */
    entry_mmio 0xe000ed14 /* CCR */
    entry_mmio 0xe000ed04 /* ICSR, after PRIMASK but before pending clear */
    entry_mmio 0xe000ed24 /* SHCSR */
    entry_mmio 0xe000ed28 /* CFSR */
    entry_mmio 0xe000ed2c /* HFSR */
    entry_mmio 0x40023800 /* RCC CR */
    entry_mmio 0x40023808 /* RCC CFGR */
    entry_mmio 0x40023874 /* RCC CSR reset flags */
    entry_mmio 0x40023840 /* RCC APB1ENR; watchdog clock context */
    entry_mmio 0x4000300c /* IWDG SR */
    entry_mmio 0x40003004 /* IWDG PR */
    entry_mmio 0x40003008 /* IWDG RLR */
    entry_mmio 0x40002c00 /* WWDG CR, meaningful only if accessible/clocked */
    entry_mmio 0x40002c04 /* WWDG CFR */
    .purgem entry_mmio
    ldr r1, =g_diag_entry
    movs r2, #1
    str r2, [r1, #4]
    ldr r2, =0x5448454e
    dsb
    str r2, [r1]
    /* Match the board PRIGROUP=3 explicitly. VECTKEY only;
     * SYSRESETREQ and VECTCLRACTIVE remain zero. Normalize Cortex-M4 CCR. */
    ldr r1, =0xe000ed0c
    ldr r2, =0x05fa0300
    str r2, [r1]
    ldr r1, =0xe000ed14
    ldr r2, =0x00000210
    str r2, [r1]
    dsb
    isb
    /* Snapshot stores used r3; restore zero for the SysTick stores below. */
    movs r0, #0
    /* Clear inherited SysTick and all implemented NVIC enables/pending bits. */
    ldr r1, =0xe000e010
    str r0, [r1]
    str r0, [r1, #4]
    str r0, [r1, #8]
    ldr r1, =0xe000e180
    ldr r2, =0xe000e280
    mvn r3, r0
    .rept 3
        str r3, [r1], #4
        str r3, [r2], #4
    .endr
    ldr r1, =0xe000ed08
    ldr r2, =g_pfnVectors
    str r2, [r1]
    /* Preserve incoming fault evidence above, then clear stale W1C flags so
     * a diagnostic fault describes this execution rather than the loader. */
    ldr r1, =0xe000ed28
    mvn r2, r0
    str r2, [r1]
    str r2, [r1, #4]
    ldr r1, =0xe000ed04
    ldr r2, =0x0a000000
    str r2, [r1]
    dsb
    isb
    movs r0, #0
    msr faultmask, r0
    ldr r0, =__data_load__
    ldr r1, =__data_start__
    ldr r2, =__data_end__
1:
    cmp r1, r2
    bhs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:
    ldr r1, =__bss_start__
    ldr r2, =__bss_end__
    movs r3, #0
3:
    cmp r1, r2
    bhs 4f
    str r3, [r1], #4
    b 3b
4:
#ifdef THY_INSTRUMENT_STACK_WATERMARK
    /* Interrupts masked, DMA quiesced, no calls or pushes yet. */
    ldr r1, =__stack_limit__
    ldr r2, =__stack_top__
    ldr r3, =0xa55aa55a
5:
    cmp r1, r2
    bhs 6f
    str r3, [r1], #4
    b 5b
6:
#endif
    bl main
    b Default_Handler
.size Reset_Handler, .-Reset_Handler

.section .text.Fault_Handler,"ax",%progbits
.type Fault_Handler,%function
.thumb_func
Fault_Handler:
    tst lr, #4
    ite eq
    mrseq r1, msp
    mrsne r1, psp
    mrs r0, ipsr
    mov r2, lr
    b board_fault_capture
.size Fault_Handler, .-Fault_Handler

.section .text.Default_Handler,"ax",%progbits
.global Default_Handler
.type Default_Handler,%function
.thumb_func
Default_Handler:
    mrs r0, ipsr
    b board_unexpected_irq
.size Default_Handler, .-Default_Handler

.macro weak_default name
    .weak \name
    .thumb_set \name, Default_Handler
.endm
.macro weak_fault name
    .weak \name
    .thumb_set \name, Fault_Handler
.endm

weak_fault NMI_Handler
weak_fault HardFault_Handler
weak_fault MemManage_Handler
weak_fault BusFault_Handler
weak_fault UsageFault_Handler
weak_default SVC_Handler
weak_default DebugMon_Handler
weak_default PendSV_Handler
weak_default SysTick_Handler

.irp name, WWDG_IRQHandler,PVD_IRQHandler,TAMP_STAMP_IRQHandler,RTC_WKUP_IRQHandler,FLASH_IRQHandler,RCC_IRQHandler,EXTI0_IRQHandler,EXTI1_IRQHandler,EXTI2_IRQHandler,EXTI3_IRQHandler,EXTI4_IRQHandler,DMA1_Stream0_IRQHandler,DMA1_Stream1_IRQHandler,DMA1_Stream2_IRQHandler,DMA1_Stream3_IRQHandler,DMA1_Stream4_IRQHandler,DMA1_Stream5_IRQHandler,DMA1_Stream6_IRQHandler,ADC_IRQHandler,CAN1_TX_IRQHandler,CAN1_RX0_IRQHandler,CAN1_RX1_IRQHandler,CAN1_SCE_IRQHandler,EXTI9_5_IRQHandler,TIM1_BRK_TIM9_IRQHandler,TIM1_UP_TIM10_IRQHandler,TIM1_TRG_COM_TIM11_IRQHandler,TIM1_CC_IRQHandler,TIM2_IRQHandler,TIM3_IRQHandler,TIM4_IRQHandler,I2C1_EV_IRQHandler,I2C1_ER_IRQHandler,I2C2_EV_IRQHandler,I2C2_ER_IRQHandler,SPI1_IRQHandler,SPI2_IRQHandler,USART1_IRQHandler,USART2_IRQHandler,USART3_IRQHandler,EXTI15_10_IRQHandler,RTC_Alarm_IRQHandler,OTG_FS_WKUP_IRQHandler,TIM8_BRK_TIM12_IRQHandler,TIM8_UP_TIM13_IRQHandler,TIM8_TRG_COM_TIM14_IRQHandler,TIM8_CC_IRQHandler,DMA1_Stream7_IRQHandler,FSMC_IRQHandler,SDIO_IRQHandler,TIM5_IRQHandler,SPI3_IRQHandler,UART4_IRQHandler,UART5_IRQHandler,TIM6_DAC_IRQHandler,TIM7_IRQHandler,DMA2_Stream0_IRQHandler,DMA2_Stream1_IRQHandler,DMA2_Stream2_IRQHandler,DMA2_Stream3_IRQHandler,DMA2_Stream4_IRQHandler,CAN2_TX_IRQHandler,CAN2_RX0_IRQHandler,CAN2_RX1_IRQHandler,CAN2_SCE_IRQHandler,OTG_FS_IRQHandler,DMA2_Stream5_IRQHandler,DMA2_Stream6_IRQHandler,DMA2_Stream7_IRQHandler,USART6_IRQHandler,I2C3_EV_IRQHandler,I2C3_ER_IRQHandler,OTG_HS_EP1_OUT_IRQHandler,OTG_HS_EP1_IN_IRQHandler,OTG_HS_WKUP_IRQHandler,OTG_HS_IRQHandler,HASH_RNG_IRQHandler,FPU_IRQHandler
    weak_default \name
.endr
