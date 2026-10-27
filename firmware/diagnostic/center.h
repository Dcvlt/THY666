/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_DIAGNOSTIC_CENTER_H
#define THY_DIAGNOSTIC_CENTER_H

#include <stddef.h>
#include <stdint.h>

#define DIAG_CENTER_CHANNELS 24u
#define DIAG_CENTER_KNOWN_MASK UINT32_C(0x00ffffe0)
#define DIAG_CENTER_MAX_BRIGHTNESS 4095u
#define DIAG_CENTER_DEFAULT_BRIGHTNESS 128u
/* Three phases per bit provide data setup before each rising clock edge;
 * the final 24 words preserve the recovered latch pulse/hold/low contract. */
#define DIAG_CENTER_FRAME_WORDS (DIAG_CENTER_CHANNELS * 12u * 3u + 24u)
#define DIAG_CENTER_PHASE_NOPS 64u

enum diag_center_mode {
    DIAG_CENTER_OFF = 0,
    DIAG_CENTER_ALL = 1,
    DIAG_CENTER_CHANNEL = 2,
    DIAG_CENTER_LEVELS = 3 /* Only board_center_request_levels accepts this mode. */
};

enum diag_center_state {
    DIAG_CENTER_IDLE = 0,
    DIAG_CENTER_QUEUED = 1,
    DIAG_CENTER_READY = 2,
    DIAG_CENTER_FAULT = 3
};

typedef struct {
    uint32_t state, mode, channel, brightness;
    /* Last completed frame's nonzero known channels. frames==0 means no
     * frame has been sent; this is transmission state, not optical sensing. */
    uint32_t enabled_mask, frames, errors, updated_ms;
    int32_t last_result;
} diag_center_report_t;

/* RAM-only initialization. Root startup explicitly requests ALL at the dim
 * default after validating its clock/entry, rather than init driving pins. */
void board_center_init(void);
/* Queue a finite foreground transfer. Inputs are bounded even when unused.
 * Channels 0..4 always serialize zero. OFF can replace a pending request. */
int board_center_request(unsigned mode, unsigned channel, unsigned brightness);
/* Independent brightness for the nineteen known channels. All 24 entries
 * must be 0..4095 and reserved channels0..4 must be zero. Invalid input does
 * not replace a pending frame. Caller memory is copied on successful queue. */
int board_center_request_levels(const uint16_t input[DIAG_CENTER_CHANNELS]);
void board_center_task(uint32_t now);
const diag_center_report_t *board_center_report(void);
/* Last completed serialized level, not a pending request or optical reading.
 * Decodes existing frame words; no additional persistent level array. */
uint16_t board_center_published_level(unsigned channel);

/* Pure encoder used by the actual driver. Levels must all be 0..4095;
 * channels 0..4 are reserved and forced off. Invalid inputs write nothing. */
size_t diag_center_encode(uint32_t *words, size_t capacity,
                          const uint16_t levels[DIAG_CENTER_CHANNELS]);
/* group0: five navigation records; group1: fourteen logical functions.
 * Returns -1 on invalid input. Printed-symbol wiring still needs observation. */
int diag_center_logical_channel(unsigned group, unsigned index);

#endif
