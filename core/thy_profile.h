/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_PROFILE_H
#define THY_PROFILE_H
#include <stdint.h>
#include "thy_core.h"

#define THY_PROFILE_HISTOGRAM_BINS 20u
#define THY_PROFILE_QUEUE_COUNT 2u
#define THY_PROFILE_PEAK_COUNT 4u
#define THY_PROFILE_MAX_SECONDS 60u

enum thy_profile_metric {
    THY_PROFILE_LOOP_US, THY_PROFILE_USB_US, THY_PROFILE_TRANSPORT_US,
    THY_PROFILE_INPUT_US, THY_PROFILE_ENGINE_US, THY_PROFILE_DISPLAY_RENDER_US,
    THY_PROFILE_DISPLAY_CHUNK_US, THY_PROFILE_LIGHTS_US, THY_PROFILE_BACKGROUND_US,
    THY_PROFILE_JOURNAL_US, THY_PROFILE_SETTINGS_ENCODE_US,
    THY_PROFILE_SETTINGS_VALIDATE_US, THY_PROFILE_SETTINGS_CRC_US,
    THY_PROFILE_ADC_FRAME_GAP_US, THY_PROFILE_INPUT_GAP_US,
    THY_PROFILE_USB_WAIT_MS, THY_PROFILE_DIN_WAIT_MS,
    THY_PROFILE_SETTINGS_PROGRAM_US, THY_PROFILE_SETTINGS_ERASE_US,
    THY_PROFILE_METRIC_COUNT
};
enum thy_profile_queue { THY_PROFILE_QUEUE_USB, THY_PROFILE_QUEUE_DIN };
enum thy_profile_peak {
    THY_PROFILE_PEAK_CORE, THY_PROFILE_PEAK_USB, THY_PROFILE_PEAK_DIN,
    THY_PROFILE_PEAK_DIN_BYTES
};
enum thy_profile_status {
    THY_PROFILE_IDLE, THY_PROFILE_RUNNING, THY_PROFILE_STOPPED,
    THY_PROFILE_TIMED_OUT, THY_PROFILE_CLOCK_ERROR
};

typedef struct thy_profile_stat {
    uint64_t sum;
    uint32_t bins[THY_PROFILE_HISTOGRAM_BINS];
    uint32_t count, min, max, saturated;
} thy_profile_stat;

typedef struct thy_profile {
    thy_profile_stat stats[THY_PROFILE_METRIC_COUNT];
    /* Slot timestamps are session elapsed milliseconds + 1. Zero identifies
     * pre-existing/unmeasured entries. Sessions never exceed 60000 ms. */
    uint16_t queue_stamps[THY_PROFILE_QUEUE_COUNT][THY_QUEUE_CAPACITY];
    uint32_t peaks[THY_PROFILE_PEAK_COUNT];
    uint32_t excluded[THY_PROFILE_QUEUE_COUNT], discarded[THY_PROFILE_QUEUE_COUNT];
    uint32_t id, start_ms, duration_ms, elapsed_ms, clock_hz;
    uint32_t invalid_samples, dropped_samples, saturated;
    uint8_t active, status;
} thy_profile;

void thy_profile_init(thy_profile *);
/* Start accepts an idle/stopped session only, seconds 1..60 and nonzero hz.
 * Success resets all samples/stamps, increments id, and returns 1. Invalid
 * arguments, an active session or exhausted UINT32_MAX id leave data intact. */
int thy_profile_start(thy_profile *,uint32_t seconds,uint32_t now_ms,uint32_t hz);
int thy_profile_active(const thy_profile *);
/* Deadlines use wrapping millisecond clocks. Backward/half-range-ambiguous
 * clocks stop with CLOCK_ERROR. Stop is idempotent; stopped data is frozen.
 * Disconnect has no lifecycle action: adapters must not stop on disconnect. */
void thy_profile_stop(thy_profile *,uint32_t now_ms);
int thy_profile_tick(thy_profile *,uint32_t now_ms);
int thy_profile_record(thy_profile *,unsigned metric,uint32_t value);
/* Cycle samples are rounded up to integer microseconds. Caller supplies
 * elapsed milliseconds from an independent clock and calls tick for expiry.
 * Reject phases over 5 s, possible full counter wraps, invalid hz/metric and
 * conversions outside uint32. WAIT_MS metrics cannot accept cycle samples. */
int thy_profile_record_cycles(thy_profile *,unsigned metric,
                             uint32_t delta_cycles,uint32_t delta_ms);
void thy_profile_peak(thy_profile *,unsigned which,uint32_t value);
/* Queue hooks tick expiry themselves and use real destination ring slots.
 * Enqueue renews a reused slot (counting the abandoned stamp as discarded).
 * Accept removes its stamp only after whole-message sink acceptance; an
 * unmeasured entry increments excluded rather than recording zero wait.
 * Discard clears ALL stamps in that destination and adds the supplied number
 * of discarded events, which may also include unstamped core/pending events.
 * Inactive hooks do not read clocks or mutate frozen session state. */
void thy_profile_queue_enqueue(thy_profile *,unsigned which,unsigned slot,
                               uint32_t now_ms,uint32_t depth);
int thy_profile_queue_accept(thy_profile *,unsigned which,unsigned slot,uint32_t now_ms);
void thy_profile_queue_discard(thy_profile *,unsigned which,uint32_t count);
/* Nearest-rank percentile UPPER bound, not an exact percentile. Bins are:
 * 0 => 0, 1 => 1, 2 => 2..3, ... 18 => 131072..262143,
 * 19 => >=262144. Empty returns 0 (check count); invalid percent (not 1..100),
 * saturated accounting or an open-ended percentile bucket returns UINT32_MAX.
 * Sum/count/min/max describe accepted samples; saturation drops later samples. */
uint32_t thy_profile_percentile_upper(const thy_profile_stat *,unsigned percent);
#endif
