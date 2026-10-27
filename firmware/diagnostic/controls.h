/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_DIAGNOSTIC_CONTROLS_H
#define THY_DIAGNOSTIC_CONTROLS_H

#include <stdint.h>

#define DIAG_CONTROL_BANKS 4u
#define DIAG_KNOWN_CONTROLS 43u

typedef struct {
    /* Raw masks retain all 16 sensor bits, including unmapped electrodes.
     * When invalid, raw and last_sample_ms retain the last good sample. */
    uint32_t raw;
    uint32_t valid;
    uint32_t seen_pressed;
    uint32_t rose_latched;
    uint32_t fell_latched;
    /* samples counts fresh successful publications consumed here; changes
     * counts individual bit transitions between consecutive valid samples.
     * Neither claims to count transitions between unobserved samples. */
    uint32_t changes;
    uint32_t samples;
    uint32_t last_change_ms;
    uint32_t last_sample_ms;
    uint32_t known_mask;
    uint32_t known_seen;
    /* Logical indices follow docs/physical-control-map.md. The center
     * indices do not yet prove association with individual printed symbols. */
    uint32_t logical_pressed;
    uint32_t logical_seen;
} diag_control_bank_t;

typedef struct {
    diag_control_bank_t banks[DIAG_CONTROL_BANKS];
    uint32_t valid_mask;
    uint32_t known_seen_count;
    uint32_t total_known_controls;
    uint32_t last_service_ms;
} diag_controls_report_t;

/* RAM-only observation of board_report() AA status publications. These
 * functions do not access GPIO, generate excitation or change sensors. */
void board_controls_init(void);
void board_controls_task(uint32_t now);
/* Clear history/counts while preserving the sample/baseline. A valid held
 * control remains seen, so clearing cannot invent another press/release. */
void board_controls_clear(void);
const diag_controls_report_t *board_controls_report(void);

#endif
