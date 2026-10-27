/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_transport.h"

static uint32_t next_generation(uint32_t generation)
{
    return generation == 0u ? 0u : generation + 1u;
}

static void invalidate_track(thy_transport_track *track)
{
    track->generation = next_generation(track->generation);
    if (track->generation == 0u) track->running = 0u;
}

static void invalidate_running(thy_transport *transport)
{
    for (unsigned n = 0; n < THY_TRANSPORT_TRACKS; ++n)
        if (transport->tracks[n].running)
            invalidate_track(&transport->tracks[n]);
    transport->generation = next_generation(transport->generation);
}

static void move_track(thy_transport_track *track, uint64_t elapsed)
{
    uint64_t phase = track->playhead - track->loop_start;
    phase = (phase + elapsed % track->loop_length) % track->loop_length;
    track->playhead = track->loop_start + (uint32_t)phase;
}

static void apply_run(thy_transport_track *track, uint8_t run)
{
    invalidate_track(track);
    track->enabled = run;
    track->running = (uint8_t)(run && track->generation != 0u);
    track->playhead = track->loop_start;
    track->pending = THY_TRANSPORT_PENDING_NONE;
    track->pending_position = 0u;
}

void thy_transport_init(thy_transport *transport, const thy_clock *clock)
{
    if (transport == 0 || clock == 0) return;
    for (unsigned n = 0; n < THY_TRANSPORT_TRACKS; ++n) {
        thy_transport_track *track = &transport->tracks[n];
        track->pending_position = 0u;
        track->loop_start = 0u;
        track->loop_length = clock->bar_ticks;
        track->playhead = 0u;
        track->generation = 1u;
        track->enabled = 0u;
        track->running = 0u;
        track->pending = THY_TRANSPORT_PENDING_NONE;
    }
    transport->position = 0u;
    transport->last_clock_tick = clock->tick;
    transport->generation = 1u;
    transport->clock_generation = clock->generation;
    transport->state = THY_TRANSPORT_STOPPED;
    transport->suspended = (uint8_t)!thy_clock_ready(clock);
    transport->selected_track = 0u;
    transport->launch_quantize = THY_TRANSPORT_BAR;
}

int thy_transport_advance(thy_transport *transport, const thy_clock *clock)
{
    if (transport == 0 || clock == 0) return THY_TIME_INVALID;
    if (clock->tick < transport->last_clock_tick) return THY_TIME_BACKWARD;
    int ready = thy_clock_ready(clock);
    if (transport->clock_generation != clock->generation) {
        invalidate_running(transport);
        transport->clock_generation = clock->generation;
        transport->last_clock_tick = clock->generation_tick;
        transport->suspended = (uint8_t)!ready;
    }
    if (!ready) {
        if (!transport->suspended) invalidate_running(transport);
        transport->suspended = 1u;
        transport->last_clock_tick = clock->tick;
        return THY_TIME_OK;
    }
    transport->suspended = 0u;
    uint64_t elapsed = clock->tick - transport->last_clock_tick;
    if (transport->state != THY_TRANSPORT_PLAYING) {
        transport->last_clock_tick = clock->tick;
        return THY_TIME_OK;
    }
    if (UINT64_MAX - transport->position < elapsed) return THY_TIME_OVERFLOW;
    uint64_t position = transport->position + elapsed;
    for (unsigned n = 0; n < THY_TRANSPORT_TRACKS; ++n) {
        thy_transport_track *track = &transport->tracks[n];
        if (track->pending != THY_TRANSPORT_PENDING_NONE &&
            track->pending_position <= position) {
            uint64_t before = track->pending_position - transport->position;
            if (track->running) move_track(track, before);
            uint64_t after = position - track->pending_position;
            apply_run(track, (uint8_t)(track->pending == THY_TRANSPORT_PENDING_START));
            if (track->running) move_track(track, after);
        } else if (track->running) move_track(track, elapsed);
    }
    transport->position = position;
    transport->last_clock_tick = clock->tick;
    return THY_TIME_OK;
}

int thy_transport_select(thy_transport *transport, uint8_t track)
{
    if (transport == 0 || track >= THY_TRANSPORT_TRACKS) return THY_TIME_INVALID;
    transport->selected_track = track;
    return THY_TIME_OK;
}

int thy_transport_set_enabled(thy_transport *transport, uint8_t track, uint8_t enabled)
{
    if (transport == 0 || track >= THY_TRANSPORT_TRACKS || enabled > 1u)
        return THY_TIME_INVALID;
    transport->tracks[track].enabled = enabled;
    return THY_TIME_OK;
}

int thy_transport_set_loop(thy_transport *transport, uint8_t track,
    uint32_t start, uint32_t length)
{
    if (transport == 0 || track >= THY_TRANSPORT_TRACKS || length == 0u ||
        UINT32_MAX - start < length) return THY_TIME_INVALID;
    thy_transport_track *run = &transport->tracks[track];
    if (run->loop_start == start && run->loop_length == length) return THY_TIME_OK;
    if (run->generation == 0u || run->generation == UINT32_MAX)
        return THY_TIME_OVERFLOW;
    run->loop_start = start;
    run->loop_length = length;
    run->playhead = start;
    invalidate_track(run);
    return THY_TIME_OK;
}

int thy_transport_set_quantize(thy_transport *transport, uint8_t quantize)
{
    if (transport == 0 || quantize > THY_TRANSPORT_BAR) return THY_TIME_INVALID;
    transport->launch_quantize = quantize;
    return THY_TIME_OK;
}

int thy_transport_start(thy_transport *transport, const thy_clock *clock)
{
    int result = thy_transport_advance(transport, clock);
    if (result != THY_TIME_OK) return result;
    for (unsigned n = 0; n < THY_TRANSPORT_TRACKS; ++n)
        if (transport->tracks[n].enabled && (transport->tracks[n].generation == 0u ||
            transport->tracks[n].generation == UINT32_MAX)) return THY_TIME_OVERFLOW;
    transport->state = THY_TRANSPORT_PLAYING;
    transport->position = 0u;
    transport->generation = next_generation(transport->generation);
    for (unsigned n = 0; n < THY_TRANSPORT_TRACKS; ++n) {
        thy_transport_track *track = &transport->tracks[n];
        invalidate_track(track);
        track->running = track->enabled;
        track->playhead = track->loop_start;
        track->pending = THY_TRANSPORT_PENDING_NONE;
        track->pending_position = 0u;
    }
    return THY_TIME_OK;
}

int thy_transport_stop(thy_transport *transport, const thy_clock *clock)
{
    if (transport == 0 || clock == 0) return THY_TIME_INVALID;
    /* Stop is always available: do not launch queued work or traverse overdue
     * time before cancelling it, including at a saturated timeline. */
    transport->last_clock_tick = clock->tick;
    transport->clock_generation = clock->generation;
    transport->suspended = (uint8_t)!thy_clock_ready(clock);
    transport->state = THY_TRANSPORT_STOPPED;
    transport->position = 0u;
    transport->generation = next_generation(transport->generation);
    for (unsigned n = 0; n < THY_TRANSPORT_TRACKS; ++n) {
        thy_transport_track *track = &transport->tracks[n];
        invalidate_track(track);
        track->running = 0u;
        track->playhead = track->loop_start;
        track->pending = THY_TRANSPORT_PENDING_NONE;
        track->pending_position = 0u;
    }
    return THY_TIME_OK;
}

int thy_transport_pause(thy_transport *transport, const thy_clock *clock)
{
    int result = thy_transport_advance(transport, clock);
    if (result != THY_TIME_OK || transport->state != THY_TRANSPORT_PLAYING)
        return result;
    transport->state = THY_TRANSPORT_PAUSED;
    invalidate_running(transport);
    for (unsigned n = 0; n < THY_TRANSPORT_TRACKS; ++n) {
        transport->tracks[n].pending = THY_TRANSPORT_PENDING_NONE;
        transport->tracks[n].pending_position = 0u;
    }
    return THY_TIME_OK;
}

int thy_transport_continue(thy_transport *transport, const thy_clock *clock)
{
    int result = thy_transport_advance(transport, clock);
    if (result != THY_TIME_OK || transport->state == THY_TRANSPORT_PLAYING)
        return result;
    if (transport->state == THY_TRANSPORT_STOPPED) {
        for (unsigned n = 0; n < THY_TRANSPORT_TRACKS; ++n)
            if (transport->tracks[n].enabled && (transport->tracks[n].generation == 0u ||
                transport->tracks[n].generation == UINT32_MAX)) return THY_TIME_OVERFLOW;
        for (unsigned n = 0; n < THY_TRANSPORT_TRACKS; ++n) {
            transport->tracks[n].running = transport->tracks[n].enabled;
            invalidate_track(&transport->tracks[n]);
        }
    }
    transport->state = THY_TRANSPORT_PLAYING;
    return THY_TIME_OK;
}

int thy_transport_request_track(thy_transport *transport, uint8_t track,
    uint8_t run, uint8_t quantize, const thy_clock *clock)
{
    if (transport == 0 || clock == 0 || track >= THY_TRANSPORT_TRACKS ||
        run > 1u || quantize > THY_TRANSPORT_BAR) return THY_TIME_INVALID;
    int result = thy_transport_advance(transport, clock);
    if (result != THY_TIME_OK) return result;
    thy_transport_track *target = &transport->tracks[track];
    uint8_t pending = run ? THY_TRANSPORT_PENDING_START : THY_TRANSPORT_PENDING_STOP;
    if (target->pending == pending) {
        target->pending = THY_TRANSPORT_PENDING_NONE;
        target->pending_position = 0u;
        return THY_TIME_OK;
    }
    if (run && !target->running && (target->generation == 0u ||
        target->generation == UINT32_MAX)) return THY_TIME_OVERFLOW;
    if (transport->state != THY_TRANSPORT_PLAYING) {
        target->enabled = run;
        if (transport->state == THY_TRANSPORT_PAUSED && target->running != run)
            apply_run(target, run);
        target->pending = THY_TRANSPORT_PENDING_NONE;
        target->pending_position = 0u;
        return THY_TIME_OK;
    }
    if (target->running == run) {
        target->pending = THY_TRANSPORT_PENDING_NONE;
        target->pending_position = 0u;
        return THY_TIME_OK;
    }
    if (quantize == THY_TRANSPORT_IMMEDIATE) {
        apply_run(target, run);
        return THY_TIME_OK;
    }
    uint32_t quantum = quantize == THY_TRANSPORT_BEAT ? clock->beat_ticks : clock->bar_ticks;
    uint64_t until = quantum - transport->position % quantum;
    if (UINT64_MAX - transport->position < until) return THY_TIME_OVERFLOW;
    target->pending_position = transport->position + until;
    target->pending = pending;
    return THY_TIME_OK;
}

int thy_transport_toggle_track(thy_transport *transport, uint8_t track,
    const thy_clock *clock)
{
    if (transport == 0 || clock == 0 || track >= THY_TRANSPORT_TRACKS)
        return THY_TIME_INVALID;
    int result = thy_transport_advance(transport, clock);
    if (result != THY_TIME_OK) return result;
    thy_transport_track *target = &transport->tracks[track];
    if (target->pending != THY_TRANSPORT_PENDING_NONE) {
        target->pending = THY_TRANSPORT_PENDING_NONE;
        target->pending_position = 0u;
        return THY_TIME_OK;
    }
    uint8_t current = transport->state == THY_TRANSPORT_STOPPED ? target->enabled : target->running;
    return thy_transport_request_track(transport, track, (uint8_t)!current,
        transport->launch_quantize, clock);
}

int thy_transport_song_position(thy_transport *transport, uint16_t spp,
    const thy_clock *clock)
{
    if (transport == 0 || clock == 0 || spp > 16383u) return THY_TIME_INVALID;
    int result = thy_transport_advance(transport, clock);
    if (result != THY_TIME_OK) return result;
    transport->position = (uint64_t)spp * (THY_CLOCK_PPQN / 4u);
    transport->generation = next_generation(transport->generation);
    for (unsigned n = 0; n < THY_TRANSPORT_TRACKS; ++n) {
        thy_transport_track *track = &transport->tracks[n];
        track->playhead = track->loop_start +
            (uint32_t)(transport->position % track->loop_length);
        invalidate_track(track);
        track->pending = THY_TRANSPORT_PENDING_NONE;
        track->pending_position = 0u;
    }
    return THY_TIME_OK;
}

int thy_transport_receive_midi(thy_transport *transport, thy_clock *clock,
    const uint8_t *message, uint8_t length, uint32_t now_ms)
{
    if (transport == 0 || clock == 0 || message == 0 || length == 0u)
        return THY_TIME_INVALID;
    uint8_t status = message[0];
    if (status != 0xf8u && status != 0xfau && status != 0xfbu &&
        status != 0xfcu && status != 0xf2u) return THY_TIME_IGNORED;
    if ((status == 0xf2u && (length != 3u || message[1] > 127u || message[2] > 127u)) ||
        (status != 0xf2u && length != 1u)) return THY_TIME_INVALID;
    if (clock->source != THY_CLOCK_SOURCE_USB) return THY_TIME_IGNORED;
    if (status == 0xf8u) {
        int result = thy_clock_midi_pulse(clock, now_ms);
        return result == THY_TIME_OK ? thy_transport_advance(transport, clock) : result;
    }
    int result = thy_clock_advance(clock, now_ms);
    if (result != THY_TIME_OK) return result;
    if (status == 0xfau) return thy_transport_start(transport, clock);
    if (status == 0xfbu) return thy_transport_continue(transport, clock);
    if (status == 0xfcu) return thy_transport_pause(transport, clock);
    return thy_transport_song_position(transport,
        (uint16_t)(message[1] | ((uint16_t)message[2] << 7)), clock);
}
