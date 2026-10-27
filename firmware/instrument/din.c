/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent USART1 TX implementation. Board mapping is documented in
 * docs/hardware.md.
 * No GPIO, timer, DMA or peripheral other than PB6/USART1 is repurposed.
 */
#include "din.h"
#ifndef THY_DIN_READ32
#define THY_DIN_READ32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#endif
#ifndef THY_DIN_WRITE32
#define THY_DIN_WRITE32(address, value) \
    (*(volatile uint32_t *)(uintptr_t)(address) = (uint32_t)(value))
#endif
#ifndef THY_DIN_BARRIER
#define THY_DIN_BARRIER() __asm volatile("dmb" ::: "memory")
#endif
#define DIN_RCC UINT32_C(0x40023800)
#define DIN_GPIOB UINT32_C(0x40020400)
#define DIN_USART1 UINT32_C(0x40011000)
#define DIN_NVIC_ISER1 UINT32_C(0xe000e104)
#define DIN_NVIC_ICER1 UINT32_C(0xe000e184)
#define DIN_NVIC_ICPR1 UINT32_C(0xe000e284)
#define DIN_NVIC_IPR9 UINT32_C(0xe000e424) /* IRQ36..39 priorities, USART1 byte1 */
#define DIN_IRQ_MASK (UINT32_C(1) << 5u) /* USART1 IRQ37 */
#define DIN_CR1_UE (UINT32_C(1) << 13u)
#define DIN_CR1_TE (UINT32_C(1) << 3u)
#define DIN_CR1_TXEIE (UINT32_C(1) << 7u)
#define DIN_SR_TXE (UINT32_C(1) << 7u)

static uint8_t tx_bytes[THY_INSTRUMENT_DIN_QUEUE_BYTES];
static volatile uint16_t tx_head, tx_tail;
static thy_instrument_din_stats stats;
static volatile uint32_t sent_bytes;
static uint32_t last_sent, last_progress_ms;
static bool clock_started;

static unsigned queued(void)
{
    return ((unsigned)tx_tail - (unsigned)tx_head) & (THY_INSTRUMENT_DIN_QUEUE_BYTES - 1u);
}
static void replace_bits(uint32_t address, uint32_t mask, uint32_t value)
{
    THY_DIN_WRITE32(address, (THY_DIN_READ32(address) & ~mask) | value);
}
bool thy_instrument_din_init(uint32_t pclk2_hz)
{
    stats = (thy_instrument_din_stats){0};
    tx_head = tx_tail = 0u;
    sent_bytes = last_sent = last_progress_ms = 0u;
    clock_started = false;
    /* Oversampling16 BRR is peripheral clock / baud. These are the two
     * board_init clock contracts; reject invented/rounded baud settings. */
    if ((pclk2_hz != UINT32_C(84000000) && pclk2_hz != UINT32_C(16000000)) ||
        pclk2_hz % THY_INSTRUMENT_DIN_BAUD) {
        ++stats.errors;
        return false;
    }
    stats.baud = THY_INSTRUMENT_DIN_BAUD;
    stats.brr = pclk2_hz / THY_INSTRUMENT_DIN_BAUD;
    THY_DIN_WRITE32(DIN_NVIC_ICER1, DIN_IRQ_MASK);
    replace_bits(DIN_RCC + 0x30u, 0u, UINT32_C(1) << 1u); /* GPIOB */
    replace_bits(DIN_RCC + 0x44u, 0u, UINT32_C(1) << 4u); /* USART1 APB2 */
    (void)THY_DIN_READ32(DIN_RCC + 0x44u);
    /* Reset USART1 only; ADC, RGB DMA2 and I2C remain untouched. */
    replace_bits(DIN_RCC + 0x24u, 0u, UINT32_C(1) << 4u);
    replace_bits(DIN_RCC + 0x24u, UINT32_C(1) << 4u, 0u);
    THY_DIN_WRITE32(DIN_USART1 + 0x0cu, 0u);
    THY_DIN_WRITE32(DIN_USART1 + 0x10u, 0u); /* 1stop bit */
    THY_DIN_WRITE32(DIN_USART1 + 0x14u, 0u); /* no flow control/DMA */
    THY_DIN_WRITE32(DIN_USART1 + 0x08u, stats.brr);
    /* Idle high before handing PB6 to USART1. PB7 and all other pins retain
     * their exact prior settings; only PB6's fields are updated. */
    THY_DIN_WRITE32(DIN_GPIOB + 0x18u, UINT32_C(1) << 6u);
    replace_bits(DIN_GPIOB + 0x04u, UINT32_C(1) << 6u, 0u);
    replace_bits(DIN_GPIOB + 0x08u, UINT32_C(3) << 12u, UINT32_C(1) << 12u);
    replace_bits(DIN_GPIOB + 0x0cu, UINT32_C(3) << 12u, 0u);
    replace_bits(DIN_GPIOB + 0x20u, UINT32_C(15) << 24u, UINT32_C(7) << 24u);
    replace_bits(DIN_GPIOB, UINT32_C(3) << 12u, UINT32_C(2) << 12u);
    THY_DIN_WRITE32(DIN_USART1 + 0x0cu, DIN_CR1_UE | DIN_CR1_TE);
    /* Four implemented priority bits: level5, above USB's level6. Preserve
     * adjacent IRQ36/38/39 bytes instead of inheriting bootloader priorities. */
    replace_bits(DIN_NVIC_IPR9, UINT32_C(0x0000ff00), UINT32_C(0x00005000));
    THY_DIN_WRITE32(DIN_NVIC_ICPR1, DIN_IRQ_MASK);
    THY_DIN_WRITE32(DIN_NVIC_ISER1, DIN_IRQ_MASK);
    stats.initialized = stats.ready = 1u;
    return true;
}
bool thy_instrument_din_write(const uint8_t *bytes, size_t count)
{
    if (!stats.ready || bytes == NULL || count == 0u || count > 3u) return false;
    unsigned free = THY_INSTRUMENT_DIN_QUEUE_BYTES - 1u - queued();
    if (count > free) {
        ++stats.backpressure;
        return false;
    }
    uint16_t tail = tx_tail;
    for (size_t i = 0u; i < count; ++i) {
        tx_bytes[tail] = bytes[i];
        tail = (uint16_t)((tail + 1u) & (THY_INSTRUMENT_DIN_QUEUE_BYTES - 1u));
    }
    THY_DIN_BARRIER();
    tx_tail = tail; /* Publish the entire message in one aligned store. */
    ++stats.accepted_messages;
    stats.accepted_bytes += (uint32_t)count;
    /* IRQ may have disabled TXEIE while we filled previously free space.
     * Enabling it after publishing tail avoids a lost empty->nonempty wake. */
    replace_bits(DIN_USART1 + 0x0cu, 0u, DIN_CR1_TXEIE);
    return true;
}
void USART1_IRQHandler(void)
{
    uint32_t cr1 = THY_DIN_READ32(DIN_USART1 + 0x0cu);
    if (!(cr1 & DIN_CR1_TXEIE) || !(THY_DIN_READ32(DIN_USART1) & DIN_SR_TXE)) return;
    uint16_t head = tx_head;
    if (head != tx_tail) {
        THY_DIN_BARRIER();
        THY_DIN_WRITE32(DIN_USART1 + 0x04u, tx_bytes[head]);
        tx_head = (uint16_t)((head + 1u) & (THY_INSTRUMENT_DIN_QUEUE_BYTES - 1u));
        ++sent_bytes; /* DR acceptance; not proof the external receiver heard it. */
    }
    if (tx_head == tx_tail)
        replace_bits(DIN_USART1 + 0x0cu, DIN_CR1_TXEIE, 0u);
}
void thy_instrument_din_task(uint32_t now_ms)
{
    if (!stats.initialized || !stats.ready) return;
    uint32_t cr1 = THY_DIN_READ32(DIN_USART1 + 0x0cu);
    bool configuration_ok =
        (cr1 & ~DIN_CR1_TXEIE) == (DIN_CR1_UE | DIN_CR1_TE) &&
        THY_DIN_READ32(DIN_USART1 + 0x08u) == stats.brr &&
        THY_DIN_READ32(DIN_USART1 + 0x10u) == 0u &&
        THY_DIN_READ32(DIN_USART1 + 0x14u) == 0u &&
        (THY_DIN_READ32(DIN_RCC + 0x44u) & (UINT32_C(1) << 4u)) != 0u;
    uint32_t sent = sent_bytes;
    unsigned waiting = queued();
    if (!clock_started || sent != last_sent || waiting == 0u) {
        clock_started = true;
        last_progress_ms = now_ms;
        last_sent = sent;
    }
    stats.stall_ms = waiting ? now_ms - last_progress_ms : 0u;
    if (!configuration_ok || stats.stall_ms >= THY_INSTRUMENT_DIN_STALL_MS) {
        ++stats.errors;
        stats.ready = 0u;
        /* Keep queued bytes for evidence. Do not reset USART mid-message or
         * turn a partially sent NoteOff into a silently discarded message. */
    }
}
const thy_instrument_din_stats *thy_instrument_din_report(void)
{
    stats.queued = queued();
    stats.transmitted_bytes = sent_bytes;
    return &stats;
}
