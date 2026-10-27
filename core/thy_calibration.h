/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_CALIBRATION_H
#define THY_CALIBRATION_H
#include <stddef.h>
#include <stdint.h>

#define THY_CALIBRATION_PADS 96u
#define THY_CALIBRATION_QUIET_SPAN_MAX 32u
#define THY_CALIBRATION_SIGNAL_MIN 16u
#define THY_CALIBRATION_HOLD_MIN_MS 800u
#define THY_CALIBRATION_LEAK_MARGIN 8u
enum thy_calibration_state {
    THY_CALIBRATION_OFF, THY_CALIBRATION_COLLECT, THY_CALIBRATION_READY,
    THY_CALIBRATION_PREVIEW_ORIGINAL, THY_CALIBRATION_PREVIEW_CANDIDATE,
    THY_CALIBRATION_APPLY_PENDING, THY_CALIBRATION_APPLIED, THY_CALIBRATION_CANCELLED
};
enum thy_calibration_stage { THY_CALIBRATION_SOFT, THY_CALIBRATION_MEDIUM,
    THY_CALIBRATION_FIRM, THY_CALIBRATION_PRESSURE };
enum thy_calibration_action { THY_CALIBRATION_LEARN, THY_CALIBRATION_BEFORE,
    THY_CALIBRATION_AFTER, THY_CALIBRATION_APPLY, THY_CALIBRATION_RETRY_PAD,
    THY_CALIBRATION_CANCEL, THY_CALIBRATION_ACTION_COUNT };
enum thy_calibration_result { THY_CALIBRATION_OK=0, THY_CALIBRATION_IGNORED=1,
    THY_CALIBRATION_RETRY=2, THY_CALIBRATION_INVALID=-1, THY_CALIBRATION_NOT_READY=-2 };
enum thy_calibration_reason { THY_CALIBRATION_REASON_NONE, THY_CALIBRATION_WRONG_PAD,
    THY_CALIBRATION_NOISY_BASELINE, THY_CALIBRATION_WEAK_PRESS,
    THY_CALIBRATION_TOO_SHORT, THY_CALIBRATION_NO_RISE,
    THY_CALIBRATION_OUTLIER, THY_CALIBRATION_LEVEL_ORDER,
    THY_CALIBRATION_RATE_ORDER, THY_CALIBRATION_APPLY_FAILED,
    THY_CALIBRATION_HOLD_TOO_SHORT, THY_CALIBRATION_HOLD_RANGE };

/* Exactly 20 bytes: three raw ADC anchors retain strike-peak diagnostics.
 * Pressure bounds come from a separate continuous soft-to-firm hold.
 * Curves use 0 Linear / 1 Soft / 2 Firm. An applied candidate uses Strike mode,
 * MIDI 1..127, and linked release with gap=trigger-release. The root adapter
 * converts these values to its normal validated response/velocity APIs. */
typedef struct thy_calibration_candidate {
    uint16_t trigger, release, pressure_floor, pressure_full;
    uint16_t soft_rate, hard_rate;
    uint16_t anchors[3];
    uint8_t pressure_curve, velocity_curve;
} thy_calibration_candidate;
typedef struct thy_calibration_tap {
    uint16_t baseline, peak, rate, quiet_span;
} thy_calibration_tap;

/* No instrument, flash, transport or heap ownership. Only one pad's nine taps
 * are retained; completed candidates and a bitset replace the raw sample bank.
 * All pad IDs are logical: left 0..47, right 48..95, column-major within a side. */
typedef struct thy_calibration {
    thy_calibration_candidate candidates[THY_CALIBRATION_PADS];
    thy_calibration_tap taps[9];
    uint8_t completed[12];
    uint32_t revision, variance, hold_started_ms, hold_last_ms;
    uint16_t baseline, quiet_span, peak, rise_rate, samples, live_raw, live_rate;
    uint16_t hold_soft, hold_firm, hold_initial[9], hold_peaks[3];
    uint16_t review_span_median, review_strike_median;
    uint16_t leak_floor[THY_CALIBRATION_PADS];
    uint8_t review_outliers[12];
    uint8_t review_leak_outliers[12];
    uint8_t state, target, selected, stage, hits, contact_active, completed_count, reason, action_cursor;
    uint8_t hold_soft_samples, hold_peak_samples;
    uint8_t review_outlier_count, review_ready, preview_percent;
    uint8_t scope_first, scope_count, review_leak_count;
} thy_calibration;

void thy_calibration_init(thy_calibration *);
int thy_calibration_begin(thy_calibration *);
/* Firmware selects a single cluster with (0,48) or (48,48). The legacy begin
 * wrapper selects all 96 for source-compatible host use. */
int thy_calibration_begin_scope(thy_calibration *,uint8_t first,uint8_t count);
void thy_calibration_cancel(thy_calibration *);
int thy_calibration_select(thy_calibration *,uint8_t logical_pad);
int thy_calibration_retry_pad(thy_calibration *);
int thy_calibration_contact_begin(thy_calibration *,uint8_t logical_pad,
    uint16_t quiet_baseline,uint16_t quiet_span);
int thy_calibration_observe(thy_calibration *,uint8_t logical_pad,
    uint16_t raw,uint16_t rise_rate);
/* Hardware uses the sensor publication timestamp. The legacy observer advances
 * a synthetic 20 ms sample interval for deterministic host callers. */
int thy_calibration_observe_timed(thy_calibration *,uint8_t logical_pad,
    uint16_t raw,uint16_t rise_rate,uint32_t sample_ms);
/* The adapter calls this only while a trusted target dominates a pressure
 * hold. Store an absolute neighboring ADC maximum, not a suppression matrix. */
int thy_calibration_observe_leak(thy_calibration *,uint8_t logical_pad,uint16_t raw);
int thy_calibration_contact_end(thy_calibration *,uint8_t logical_pad);
int thy_calibration_completed(const thy_calibration *,uint8_t logical_pad);
int thy_calibration_candidate_get(const thy_calibration *,uint8_t logical_pad,
    thy_calibration_candidate *);
/* Visual row-major walk, left side then right; IDs remain column-major. */
uint8_t thy_calibration_pad_at(unsigned visual_index);
/* Playable review is available only after the selected scope is complete. Statistics are
 * cached once; strength is the lower of span/firm-strike relative percentages.
 * Flags compare gestures on this board, not mechanical force or fault proof. */
int thy_calibration_review_prepare(thy_calibration *);
unsigned thy_calibration_strength_percent(const thy_calibration *,uint8_t logical_pad);
int thy_calibration_outlier(const thy_calibration *,uint8_t logical_pad);
int thy_calibration_leak_outlier(const thy_calibration *,uint8_t logical_pad);
int thy_calibration_preview(thy_calibration *,int candidate);
int thy_calibration_resume(thy_calibration *);
int thy_calibration_request_apply(thy_calibration *);
int thy_calibration_apply_result(thy_calibration *,int success);
const char *thy_calibration_reason_text(uint8_t reason);
#endif
