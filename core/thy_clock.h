/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_CLOCK_H
#define THY_CLOCK_H

#include <stdint.h>

#define THY_CLOCK_PPQN 960u
#define THY_CLOCK_MIDI_PULSE_TICKS (THY_CLOCK_PPQN / 24u)
#define THY_CLOCK_TEMPO_MIN 20000u
#define THY_CLOCK_TEMPO_MAX 300000u
#define THY_CLOCK_DEFAULT_TEMPO 120000u
#define THY_CLOCK_DEFAULT_TIMEOUT_MS 500u

enum thy_time_result {
    THY_TIME_OK = 0,
    THY_TIME_IGNORED = 1,
    THY_TIME_INVALID = -1,
    THY_TIME_BACKWARD = -2,
    THY_TIME_OVERFLOW = -3
};
enum thy_clock_source { THY_CLOCK_SOURCE_INTERNAL, THY_CLOCK_SOURCE_USB };
enum thy_clock_status {
    THY_CLOCK_INTERNAL, THY_CLOCK_WAITING, THY_CLOCK_LOCKED, THY_CLOCK_LOST
};

/* Runtime only, not a persistence format. Caller timestamps must move forward
 * by no more than INT32_MAX milliseconds per service call. uint32 wrap is valid.
 * Millisecond conversion retains a rational remainder across tempo changes.
 * USB follow advances exactly 40 ticks/F8; it never extrapolates or falls back
 * to internal timing. tick is independent of sequencer transport and never seeks.
 * generation changes on source changes or loss of a previously locked source.
 * Exhausted generation retires to 0, never aliases an old generation; reinit is
 * required and ready becomes false. An exhausting source edit is rejected. */
typedef struct thy_clock {
    uint64_t tick;
    uint64_t generation_tick; /* Exact source/loss anchor, before a relock pulse. */
    uint32_t remainder;
    uint32_t now_ms;
    uint32_t tempo_milli_bpm;
    uint32_t generation;
    uint32_t last_pulse_ms;
    uint32_t last_pulse_interval_ms;
    uint32_t timeout_ms;
    uint32_t beat_ticks;
    uint32_t bar_ticks;
    uint8_t source;
    uint8_t status;
    uint8_t numerator;
    uint8_t denominator;
} thy_clock;

void thy_clock_init(thy_clock *clock, uint32_t now_ms);
int thy_clock_advance(thy_clock *clock, uint32_t now_ms);
int thy_clock_set_tempo(thy_clock *clock, uint32_t milli_bpm, uint32_t now_ms);
int thy_clock_set_source(thy_clock *clock, uint8_t source, uint32_t now_ms);
/* Meter: numerator 1..16, denominator 1,2,4,8,16,32. No phase reset. */
int thy_clock_set_meter(thy_clock *clock, uint8_t numerator, uint8_t denominator);
int thy_clock_set_timeout(thy_clock *clock, uint32_t timeout_ms);
int thy_clock_midi_pulse(thy_clock *clock, uint32_t now_ms);
int thy_clock_ready(const thy_clock *clock);

#endif
