/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_clock.h"

#define CLOCK_DENOMINATOR UINT64_C(60000000)

static uint32_t next_generation(uint32_t generation)
{
    return generation == 0u ? 0u : generation + 1u;
}

void thy_clock_init(thy_clock *clock, uint32_t now_ms)
{
    if (clock == 0) return;
    clock->tick = 0u;
    clock->generation_tick = 0u;
    clock->remainder = 0u;
    clock->now_ms = now_ms;
    clock->tempo_milli_bpm = THY_CLOCK_DEFAULT_TEMPO;
    clock->generation = 1u;
    clock->last_pulse_ms = now_ms;
    clock->last_pulse_interval_ms = 0u;
    clock->timeout_ms = THY_CLOCK_DEFAULT_TIMEOUT_MS;
    clock->beat_ticks = THY_CLOCK_PPQN;
    clock->bar_ticks = THY_CLOCK_PPQN * 4u;
    clock->source = THY_CLOCK_SOURCE_INTERNAL;
    clock->status = THY_CLOCK_INTERNAL;
    clock->numerator = 4u;
    clock->denominator = 4u;
}

int thy_clock_ready(const thy_clock *clock)
{
    return clock != 0 && clock->generation != 0u &&
        ((clock->source == THY_CLOCK_SOURCE_INTERNAL &&
          clock->status == THY_CLOCK_INTERNAL) ||
         (clock->source == THY_CLOCK_SOURCE_USB &&
          clock->status == THY_CLOCK_LOCKED));
}

int thy_clock_advance(thy_clock *clock, uint32_t now_ms)
{
    if (clock == 0 || clock->source > THY_CLOCK_SOURCE_USB)
        return THY_TIME_INVALID;
    uint32_t elapsed = now_ms - clock->now_ms;
    if (elapsed > INT32_MAX) return THY_TIME_BACKWARD;
    if (clock->source == THY_CLOCK_SOURCE_INTERNAL) {
        uint64_t scaled = (uint64_t)elapsed * clock->tempo_milli_bpm *
            THY_CLOCK_PPQN + clock->remainder;
        uint64_t ticks = scaled / CLOCK_DENOMINATOR;
        if (UINT64_MAX - clock->tick < ticks) return THY_TIME_OVERFLOW;
        clock->tick += ticks;
        clock->remainder = (uint32_t)(scaled % CLOCK_DENOMINATOR);
    } else if (clock->status == THY_CLOCK_LOCKED &&
        now_ms - clock->last_pulse_ms >= clock->timeout_ms) {
        clock->status = THY_CLOCK_LOST;
        clock->generation = next_generation(clock->generation);
        clock->generation_tick = clock->tick;
    }
    clock->now_ms = now_ms;
    return THY_TIME_OK;
}

int thy_clock_set_tempo(thy_clock *clock, uint32_t milli_bpm, uint32_t now_ms)
{
    if (clock == 0 || milli_bpm < THY_CLOCK_TEMPO_MIN ||
        milli_bpm > THY_CLOCK_TEMPO_MAX) return THY_TIME_INVALID;
    int result = thy_clock_advance(clock, now_ms);
    if (result != THY_TIME_OK) return result;
    clock->tempo_milli_bpm = milli_bpm;
    return THY_TIME_OK;
}

int thy_clock_set_source(thy_clock *clock, uint8_t source, uint32_t now_ms)
{
    if (clock == 0 || source > THY_CLOCK_SOURCE_USB) return THY_TIME_INVALID;
    if (source != clock->source && (clock->generation == UINT32_MAX ||
        clock->generation == 0u)) return THY_TIME_OVERFLOW;
    int result = thy_clock_advance(clock, now_ms);
    if (result != THY_TIME_OK || clock->source == source) return result;
    if (clock->generation == UINT32_MAX || clock->generation == 0u)
        return THY_TIME_OVERFLOW;
    clock->source = source;
    clock->status = source == THY_CLOCK_SOURCE_INTERNAL ?
        THY_CLOCK_INTERNAL : THY_CLOCK_WAITING;
    clock->remainder = 0u;
    clock->last_pulse_ms = now_ms;
    clock->last_pulse_interval_ms = 0u;
    clock->generation = next_generation(clock->generation);
    clock->generation_tick = clock->tick;
    return THY_TIME_OK;
}

int thy_clock_set_meter(thy_clock *clock, uint8_t numerator, uint8_t denominator)
{
    if (clock == 0 || numerator < 1u || numerator > 16u || denominator < 1u ||
        denominator > 32u || (denominator & (denominator - 1u)) != 0u)
        return THY_TIME_INVALID;
    clock->numerator = numerator;
    clock->denominator = denominator;
    clock->beat_ticks = THY_CLOCK_PPQN * 4u / denominator;
    clock->bar_ticks = clock->beat_ticks * numerator;
    return THY_TIME_OK;
}

int thy_clock_set_timeout(thy_clock *clock, uint32_t timeout_ms)
{
    if (clock == 0 || timeout_ms < 100u || timeout_ms > 10000u)
        return THY_TIME_INVALID;
    clock->timeout_ms = timeout_ms;
    return THY_TIME_OK;
}

int thy_clock_midi_pulse(thy_clock *clock, uint32_t now_ms)
{
    if (clock == 0) return THY_TIME_INVALID;
    if (clock->source != THY_CLOCK_SOURCE_USB) return THY_TIME_IGNORED;
    if (clock->generation == 0u) return THY_TIME_OVERFLOW;
    if (UINT64_MAX - clock->tick < THY_CLOCK_MIDI_PULSE_TICKS)
        return THY_TIME_OVERFLOW;
    int result = thy_clock_advance(clock, now_ms);
    if (result != THY_TIME_OK) return result;
    if (clock->generation == 0u) return THY_TIME_OVERFLOW;
    if (clock->status == THY_CLOCK_LOCKED)
        clock->last_pulse_interval_ms = now_ms - clock->last_pulse_ms;
    else clock->last_pulse_interval_ms = 0u;
    clock->last_pulse_ms = now_ms;
    clock->status = THY_CLOCK_LOCKED;
    clock->tick += THY_CLOCK_MIDI_PULSE_TICKS;
    return THY_TIME_OK;
}
