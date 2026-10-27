/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_TRANSPORT_H
#define THY_TRANSPORT_H

#include "thy_clock.h"

#define THY_TRANSPORT_TRACKS 21u
enum thy_transport_state {
    THY_TRANSPORT_STOPPED, THY_TRANSPORT_PLAYING, THY_TRANSPORT_PAUSED
};
enum thy_transport_quantize {
    THY_TRANSPORT_IMMEDIATE, THY_TRANSPORT_BEAT, THY_TRANSPORT_BAR
};
enum thy_transport_pending {
    THY_TRANSPORT_PENDING_NONE, THY_TRANSPORT_PENDING_START,
    THY_TRANSPORT_PENDING_STOP
};
typedef struct thy_transport_track {
    uint64_t pending_position;
    uint32_t loop_start;
    uint32_t loop_length;
    uint32_t playhead;
    uint32_t generation;
    uint8_t enabled;
    uint8_t running;
    uint8_t pending;
} thy_transport_track;

/* No events, note scheduling or capture engine yet. Enabled tracks are the set
 * eligible for the next main Start; selection never changes their run state.
 * An individual launch starts at its loop start. Main Stop cancels pending work
 * and resets positions without resetting the independent clock or advancing
 * overdue pending work; it remains available at timeline exhaustion. MIDI Stop and
 * Pause preserve position for Continue. A run generation changes whenever its
 * old sounding/generated work must be cancelled. Clock loss suspends time and
 * invalidates running generations once; relock resumes without catch-up.
 * Queues use transport position, not milliseconds. A queued boundary is frozen
 * when requested, including across meter edits. Same pending request cancels.
 * Init enables no tracks; caller deliberately chooses the eligible track set.
 * Generation exhaustion retires that run to 0, never reuses an old generation,
 * and rejects future starts. set_loop is an explicit immediate reset/invalidate;
 * future pattern-edit transactions may queue structural edits above this layer. */
typedef struct thy_transport {
    thy_transport_track tracks[THY_TRANSPORT_TRACKS];
    uint64_t position;
    uint64_t last_clock_tick;
    uint32_t generation;
    uint32_t clock_generation;
    uint8_t state;
    uint8_t suspended;
    uint8_t selected_track;
    uint8_t launch_quantize;
} thy_transport;

void thy_transport_init(thy_transport *transport, const thy_clock *clock);
int thy_transport_advance(thy_transport *transport, const thy_clock *clock);
int thy_transport_select(thy_transport *transport, uint8_t track);
int thy_transport_set_enabled(thy_transport *transport, uint8_t track, uint8_t enabled);
int thy_transport_set_loop(thy_transport *transport, uint8_t track,
    uint32_t start, uint32_t length);
int thy_transport_set_quantize(thy_transport *transport, uint8_t quantize);
int thy_transport_start(thy_transport *transport, const thy_clock *clock);
int thy_transport_stop(thy_transport *transport, const thy_clock *clock);
int thy_transport_pause(thy_transport *transport, const thy_clock *clock);
int thy_transport_continue(thy_transport *transport, const thy_clock *clock);
int thy_transport_request_track(thy_transport *transport, uint8_t track,
    uint8_t run, uint8_t quantize, const thy_clock *clock);
int thy_transport_toggle_track(thy_transport *transport, uint8_t track,
    const thy_clock *clock);
/* SPP units are MIDI sixteenths, 0..16383. Seeking invalidates old track work,
 * cancels pending launches, and preserves stopped/playing state. */
int thy_transport_song_position(thy_transport *transport, uint16_t spp,
    const thy_clock *clock);
/* Only follows messages when clock source is USB. Exact 1-byte F8/FA/FB/FC or
 * 3-byte F2 with both data bytes <=127; other messages are ignored. No sends. */
int thy_transport_receive_midi(thy_transport *transport, thy_clock *clock,
    const uint8_t *message, uint8_t length, uint32_t now_ms);

#endif
