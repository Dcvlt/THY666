/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_INSTRUMENT_DIN_H
#define THY_INSTRUMENT_DIN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define THY_INSTRUMENT_DIN_QUEUE_BYTES 256u
#define THY_INSTRUMENT_DIN_BAUD 31250u
#define THY_INSTRUMENT_DIN_STALL_MS 100u
typedef struct thy_instrument_din_stats {
    uint32_t initialized, ready, baud, brr, queued;
    uint32_t accepted_messages, accepted_bytes, transmitted_bytes;
    uint32_t backpressure, errors, stall_ms;
} thy_instrument_din_stats;
/* Board wiring: USART1, PB6 AF7 TX, 31250baud, 8N1, TX-only.
 * PB7 is intentionally untouched: MIDI input circuitry is not established.
 * Call once after board_init and before enabling global interrupts. */
bool thy_instrument_din_init(uint32_t pclk2_hz);
/* Single main-loop producer. Atomic whole-message enqueue, 1..3 MIDI bytes.
 * False accepts zero bytes. IRQ sends them without busy waits/running-status
 * compression. Queued partial messages survive USB reset and backpressure. */
bool thy_instrument_din_write(const uint8_t *bytes, size_t count);
void thy_instrument_din_task(uint32_t now_ms);
const thy_instrument_din_stats *thy_instrument_din_report(void);
void USART1_IRQHandler(void);
#endif
