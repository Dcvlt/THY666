/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_DIAGNOSTIC_LED_H
#define THY_DIAGNOSTIC_LED_H

#include <stddef.h>
#include <stdint.h>

#define DIAG_LED_COUNT 96u
#define DIAG_LED_BRIGHTNESS 8u
/* 256*16/10.5MHz = 390.1us of actively clocked low on each side of data.
 * This is an independent conservative framing policy for one-shot output,
 * not a claim that the fitted LEDs have a measured reset-time requirement. */
#define DIAG_LED_RESET_WORDS 256u
#define DIAG_LED_DATA_WORDS (DIAG_LED_COUNT * 24u)
#define DIAG_LED_FRAME_WORDS (DIAG_LED_RESET_WORDS * 2u + DIAG_LED_DATA_WORDS)
#define DIAG_LED_TEST_MS UINT32_C(10000)
#define DIAG_LED_FRAME_TIMEOUT_MS UINT32_C(20)

enum diag_led_mode {
    DIAG_LED_OFF = 0,
    DIAG_LED_SINGLE = 1,
    DIAG_LED_ALL = 2,
    DIAG_LED_WALK = 3,
    /* Runtime bitmap API only; the diagnostic command parser is unchanged. */
    DIAG_LED_BITMAP = 4,
    DIAG_LED_COLORS = 5
};

enum diag_led_state {
    DIAG_LED_IDLE = 0,
    DIAG_LED_QUEUED = 1,
    DIAG_LED_SENDING = 2,
    DIAG_LED_HOLDING = 3,
    DIAG_LED_FAULT = 4
};

typedef struct {
    uint32_t state, mode, pixel, component, brightness;
    int32_t last_result;
    uint32_t frames, errors, started_ms, expires_ms, walk_steps;
    /* A completed zero frame is evidence of transmission, not an optical
     * measurement. False at boot, after a lit request, and after a failure. */
    uint32_t off_confirmed;
    int32_t last_failure_result;
    uint32_t last_failure_ms, last_failure_dma_sr, last_failure_spi_sr;
} diag_led_report_t;

/* RAM-only initialization; no peripheral or GPIO writes occur at boot. */
void board_led_init(void);
/* Explicit opt-in volatile test. SINGLE/ALL/WALK use component 0..2 at a
 * fixed amplitude of 8/255. SINGLE/WALK require pixel 0..95. OFF ignores the
 * other arguments and queues a complete zero frame even during a transfer. */
int board_led_request(unsigned mode, unsigned pixel, unsigned component);
/* 96 independent stream slots at 8/255 in all three ordered components.
 * The latest bitmap replaces a queued request, including while DMA owns an
 * older encoded frame. No in-flight DMA word is changed. An unchanged bitmap
 * only renews the ten-second automatic-OFF lease. A zero bitmap queues the
 * same full OFF frame as board_led_request(OFF). */
int board_led_request_bitmap(const uint32_t selected[3]);
/* Ordered physical stream components, not an assertion of RGB/GRB wiring.
 * Components must be <=8, retaining the device-tested amplitude. Independent
 * pending storage protects the DMA buffer; unchanged colors renew the lease. */
int board_led_request_colors(const uint8_t components[DIAG_LED_COUNT][3]);
void board_led_task(uint32_t now);
const diag_led_report_t *board_led_report(void);
/* Pure frame encoder shared by the actual DMA driver and native tests.
 * Components and stream indices deliberately have no physical color/position
 * names until the owner observes their wiring. Returns zero on invalid input. */
size_t diag_led_encode(uint16_t *words, size_t capacity,
                       unsigned mode, unsigned pixel, unsigned component);
size_t diag_led_encode_bitmap(uint16_t *words, size_t capacity,
                              const uint32_t selected[3]);
size_t diag_led_encode_colors(uint16_t *words, size_t capacity,
                              const uint8_t components[DIAG_LED_COUNT][3]);

#endif
