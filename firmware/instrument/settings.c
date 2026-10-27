/* SPDX-License-Identifier: GPL-3.0-or-later
 * STM32F405 internal flash journal backend, RM0090 flash interface. Byte
 * programming (PSIZE=0) avoids assuming the board meets x32 voltage limits.
 * Single-bank flash stalls instruction fetch while busy. The caller must
 * defer maintenance to a musical idle window; sector erase has a visible
 * maintenance pause. No IRQ, bootloader, option-byte or external-NOR writes. */
#include "settings.h"
#include "stm32f405xx.h"
#include "board.h"

#define FLASH_FAILURES (FLASH_SR_OPERR|FLASH_SR_WRPERR|FLASH_SR_PGAERR|FLASH_SR_PGPERR|FLASH_SR_PGSERR)
static int range(uint32_t offset,size_t count) {
    return offset<=THY_INSTRUMENT_SETTINGS_END-THY_INSTRUMENT_SETTINGS_BASE &&
        count<=THY_INSTRUMENT_SETTINGS_END-THY_INSTRUMENT_SETTINGS_BASE-offset;
}
static int read_flash(void *context,uint32_t offset,uint8_t *out,size_t count) {
    (void)context;
    if(!out || !range(offset,count)) return 0;
    const volatile uint8_t *in=(const volatile uint8_t *)(uintptr_t)(THY_INSTRUMENT_SETTINGS_BASE+offset);
    for(size_t n=0;n<count;++n) out[n]=in[n];
    return 1;
}
static int unlock(void) {
    if(FLASH->SR&FLASH_SR_BSY) return 0;
    if(FLASH->CR&FLASH_CR_LOCK) {FLASH->KEYR=UINT32_C(0x45670123); FLASH->KEYR=UINT32_C(0xcdef89ab);}
    if(FLASH->CR&FLASH_CR_LOCK) return 0;
    FLASH->SR=FLASH_FAILURES|FLASH_SR_EOP;
    return 1;
}
static int finish(uint32_t started,uint32_t budget) {
    /* A flash operation cannot safely be canceled in software. The MCU stalls
     * flash fetches until completion; never start another operation while BSY.
     * Host journal tests validate failure/power-loss handling, not MCU timings. */
    while(FLASH->SR&FLASH_SR_BSY) {__asm volatile("nop");}
    uint32_t failed=FLASH->SR&FLASH_FAILURES;
    if((uint32_t)(DWT->CYCCNT-started)>budget) failed|=FLASH_SR_OPERR;
    FLASH->CR=FLASH_CR_LOCK;
    FLASH->SR=FLASH_FAILURES|FLASH_SR_EOP;
    /* Readback must not use earlier cached lines. Flush both ART caches only
     * while disabled and restore their previous enablement (RM0090). */
    uint32_t acr=FLASH->ACR;
    FLASH->ACR=acr&~(FLASH_ACR_DCEN|FLASH_ACR_ICEN);
    FLASH->ACR=(acr&~(FLASH_ACR_DCEN|FLASH_ACR_ICEN))|FLASH_ACR_DCRST|FLASH_ACR_ICRST;
    FLASH->ACR=acr&~(FLASH_ACR_DCEN|FLASH_ACR_ICEN);
    FLASH->ACR=acr;
    __asm volatile("dsb\n isb" ::: "memory");
    return !failed;
}
static int program(void *context,uint32_t offset,const uint8_t word[4]) {
    (void)context;
    if(!word || (offset&3u) || !range(offset,4u)) return 0;
    volatile uint8_t *out=(volatile uint8_t *)(uintptr_t)(THY_INSTRUMENT_SETTINGS_BASE+offset);
    for(unsigned n=0;n<4u;++n) {
        if(out[n]!=255u) return 0; /* Never reprogram a partial/torn slot. */
        if(word[n]==255u) continue;
        if(!unlock()) return 0;
        uint32_t started=DWT->CYCCNT;
        FLASH->CR=FLASH_CR_PG; /* PSIZE=0: byte programming. */
        out[n]=word[n];
        if(!finish(started,(g_diag_mailbox.hclk_hz/1000u)*25u) || out[n]!=word[n]) return 0;
    }
    return 1;
}
static int erase(void *context,unsigned sector) {
    (void)context;
    if(sector>1u || !unlock()) return 0;
    uint32_t started=DWT->CYCCNT;
    FLASH->CR=FLASH_CR_SER|((10u+sector)<<FLASH_CR_SNB_Pos);
    FLASH->CR|=FLASH_CR_STRT;
    return finish(started,g_diag_mailbox.hclk_hz*5u);
}
int thy_instrument_settings_io(thy_settings_io *io) {
    if(!io) return 0;
    /* Fail closed if the runtime chip is not the established F405/407 1MiB
     * family. A smaller flash must never alias application or bootloader data. */
    uint32_t device=DBGMCU->IDCODE&DBGMCU_IDCODE_DEV_ID;
    uint16_t kib=*(const volatile uint16_t *)(uintptr_t)UINT32_C(0x1fff7a22);
    if(device!=UINT32_C(0x413) || kib!=1024u ||
        !(DWT->CTRL&DWT_CTRL_CYCCNTENA_Msk) || g_diag_mailbox.hclk_hz<1000000u ||
        g_diag_mailbox.hclk_hz>168000000u) return 0;
    *io=(thy_settings_io){NULL,read_flash,program,erase};
    return 1;
}
