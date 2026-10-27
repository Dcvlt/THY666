/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_DIAGNOSTIC_PADS_H
#define THY_DIAGNOSTIC_PADS_H
#include <stddef.h>
#include <stdint.h>

#define DIAG_PAD_BANKS 6u
#define DIAG_PAD_SELECTIONS 16u
#define DIAG_PAD_COUNT 96u
#define DIAG_PAD_TEST_MS UINT32_C(10000)
#define DIAG_PAD_FRAME_TIMEOUT_MS UINT32_C(20)

enum diag_pads_state {
    DIAG_PADS_IDLE = 0,
    DIAG_PADS_QUEUED = 1,
    DIAG_PADS_SETTLE = 2,
    DIAG_PADS_ACQUIRE = 3,
    DIAG_PADS_RESTORE = 4,
    DIAG_PADS_FAULT = 5
};

typedef struct {
    uint32_t state, index, subscan, frames, scans, errors;
    uint32_t dma_resets; /* Legacy field: always zero; no DMA2-wide reset. */
    uint32_t started_ms, expires_ms;
    int32_t last_result, last_failure_result;
    uint32_t last_failure_ms, last_failure_adc_dma_sr;
    uint32_t last_failure_excite_dma_sr; /* Deprecated lighting-stream field, always zero. */
    uint32_t last_failure_adc_sr;
    uint32_t valid[3];
    uint32_t raw[DIAG_PAD_COUNT], minimum[DIAG_PAD_COUNT], maximum[DIAG_PAD_COUNT];
    uint32_t samples[3][DIAG_PAD_BANKS];
    uint32_t lighting_decoupled; /* 1: ADC/mux acquisition owns no lighting hardware. */
    uint32_t adc_resets; /* ADC-block reset attempts during stream0 cleanup only. */
} diag_pads_report_t;

/* Opt-in, volatile raw acquisition: no musical note generation, sensitivity
 * curve, calibration, persistence, or physical pad permutation is inferred. */
void board_pads_init(void); /* RAM only. */
int board_pads_request(int start);
void board_pads_task(uint32_t now);
int board_pads_active(void); /* ADC1 remains owned through stop/restore. */
int board_pads_busy(void); /* True only while ADC acquisition is in flight. */
const diag_pads_report_t *board_pads_report(void);
/* Pure, byte-checkable recovered contracts, in GPIOA..GPIOE order. */
int diag_pads_mux_words(unsigned index, uint32_t words[5]);
#endif
