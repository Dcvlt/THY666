/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_SEQUENCER_H
#define THY_SEQUENCER_H
#include "thy_transport.h"
#include "thy_arp.h"
#include <stddef.h>

#define THY_SEQ_TRACKS 21u
#define THY_SEQ_EVENTS 512u
#define THY_SEQ_TAKE_EVENTS 256u
#define THY_SEQ_VOICES 128u
#define THY_SEQ_MAX_TICK 0x3fffffffu
#define THY_SEQ_SONG_MAX (32u + THY_SEQ_TRACKS * 28u + THY_SEQ_EVENTS * 16u)
#define THY_SEQ_FLAG_ONESHOT 1u
#define THY_SEQ_QUANTIZE_SHIFT 1u
#define THY_SEQ_QUANTIZE_MASK 14u
#define THY_SEQ_FLAG_RECORD_CONTROLLERS 16u
#define THY_SEQ_FLAG_SEND_PRESSURE 32u
#define THY_SEQ_FLAG_DISABLED 64u
#define THY_SEQ_FLAG_CREATED 128u
enum thy_seq_result { THY_SEQ_FULL = 20, THY_SEQ_BUSY, THY_SEQ_NOT_FOUND,
    THY_SEQ_CORRUPT, THY_SEQ_NO_HISTORY, THY_SEQ_CAPTURE_FAILED };
enum thy_seq_type { THY_SEQ_NOTE, THY_SEQ_CC, THY_SEQ_PRESSURE, THY_SEQ_PROGRAM,
    THY_SEQ_CHANNEL_PRESSURE, THY_SEQ_BEND };
enum thy_seq_source { THY_SEQ_SOURCE_PADS, THY_SEQ_SOURCE_ARP1,
    THY_SEQ_SOURCE_ARP2, THY_SEQ_SOURCE_LIVE, THY_SEQ_SOURCE_STRUM,
    THY_SEQ_SOURCE_USB };
enum thy_seq_capture_state { THY_SEQ_CAPTURE_OFF, THY_SEQ_ARMED,
    THY_SEQ_QUEUED, THY_SEQ_RECORDING, THY_SEQ_FINISHING,
    THY_SEQ_FAILED };
enum thy_seq_policy { THY_SEQ_REPLACE, THY_SEQ_OVERDUB };
enum thy_seq_processor { THY_SEQ_DIRECT, THY_SEQ_ARP1, THY_SEQ_ARP2 };

/* Exactly 16 bytes on host and target. kind bit7 preserves native chord role;
 * type is kind&0x7f. id/group are stable nonzero identities, never pool slots.
 * For BEND, pitch/value encode low/high seven bits; CC/pressure use data1/2. */
typedef struct thy_seq_event {
    uint32_t tick, duration;
    uint16_t id, group;
    uint8_t track, kind, pitch, value;
} thy_seq_event;
typedef struct thy_seq_track {
    uint32_t content_end, loop_start, loop_end, revision;
    uint8_t channel, destinations, velocity, muted;
    uint8_t processor, record_source, record_policy, flags;
    uint32_t grid_ticks; /* Persisted editor spacing; 48 pads = 48 steps. */
} thy_seq_track;
typedef struct thy_seq_voice {
    uint64_t off_position, on_position;
    uint32_t handle, generation;
    uint32_t event_tick;
    uint16_t event_id;
    uint8_t track, generated;
} thy_seq_voice;
typedef struct thy_seq_run {
    uint64_t last_position, anchor, ordinal, restart_anchor;
    uint32_t generation, input_signature;
    uint8_t initialized, processor;
} thy_seq_run;
typedef struct thy_seq_input {
    uint32_t identity;
    uint16_t event_index;
    uint8_t source, active;
} thy_seq_input;
typedef struct thy_seq_capture {
    uint64_t start_position, end_position, queued_position;
    uint32_t region_start, region_end, start_epoch;
    uint16_t count, quantize_ticks;
    uint8_t state, track, source, policy, growing, failed, quantize, flags;
} thy_seq_capture;
typedef struct thy_sequencer {
    thy_seq_event events[THY_SEQ_EVENTS];
    thy_seq_event *history; /* Caller-owned THY_SEQ_EVENTS arena; lifetime matches seq. */
    thy_seq_event take[THY_SEQ_TAKE_EVENTS];
    thy_seq_track tracks[THY_SEQ_TRACKS], history_tracks[THY_SEQ_TRACKS];
    thy_seq_run runs[THY_SEQ_TRACKS];
    thy_seq_voice voices[THY_SEQ_VOICES];
    uint64_t sustain_tracks[THY_SEQ_TRACKS], sustain_baseline;
    thy_seq_input inputs[THY_SEQ_TAKE_EVENTS];
    thy_seq_capture capture;
    uint32_t revision, attacks, skipped, failures, capture_epoch;
    uint16_t count, history_count, next_id, next_group, voice_count;
    uint8_t selected_track, history_valid, history_is_redo, last_error;
    uint8_t transaction_open, transaction_saved;
} thy_sequencer;

/* history is required, exclusive to this sequencer, and fully initialized here. */
void thy_sequencer_init(thy_sequencer *seq, thy_seq_event history[THY_SEQ_EVENTS]);
int thy_sequencer_select(thy_sequencer *seq, uint8_t track);
int thy_sequencer_track_exists(const thy_sequencer *seq, uint8_t track);
int thy_sequencer_create(thy_sequencer *seq, uint8_t track);
/* Content/config deletion is undoable. Caller stops this track's owners. */
int thy_sequencer_delete(thy_sequencer *seq, uint8_t track);
const thy_seq_event *thy_sequencer_event_at(const thy_sequencer *seq,
    uint8_t track, uint16_t index);
const thy_seq_event *thy_sequencer_find(const thy_sequencer *seq, uint16_t id);
uint16_t thy_sequencer_track_count(const thy_sequencer *seq, uint8_t track);
/* One transaction/one undo entry, including multi-note chord assignment. */
int thy_sequencer_add(thy_sequencer *seq, uint8_t track,
    const thy_seq_event *events, uint16_t count);
int thy_sequencer_transaction_begin(thy_sequencer *seq);
void thy_sequencer_transaction_end(thy_sequencer *seq);
/* Replace=1 clears the exact onset; 2 clears onsets in this step's duration.
 * Continuation uses replace=0. Controller events are retained.
 * begin/end wraps all assignment contributions into one undo entry. */
int thy_sequencer_assign(thy_sequencer *seq, uint8_t track, uint32_t tick,
    uint32_t duration, const thy_seq_event *events, uint16_t count, int replace);
int thy_sequencer_remove(thy_sequencer *seq, uint16_t id);
int thy_sequencer_update(thy_sequencer *seq, uint16_t id,
    uint32_t tick, uint32_t duration, uint8_t pitch, uint8_t velocity);
int thy_sequencer_clear(thy_sequencer *seq, uint8_t track);
int thy_sequencer_duplicate(thy_sequencer *seq, uint8_t track,
    uint32_t start, uint32_t end, uint32_t destination, int extend_loop);
int thy_sequencer_trim(thy_sequencer *seq, uint8_t track,
    uint32_t start, uint32_t end);
/* Loop edits retain content. Extending the same start preserves phase/owners;
 * shortening/moving explicitly resets only this track's playback run. */
int thy_sequencer_loop(thy_sequencer *seq, thy_transport *transport,
    uint8_t track, uint32_t start, uint32_t end);
/* Change step duration while retaining step positions and relative note
 * lengths. Preflight every event, then publish as one undoable edit. */
int thy_sequencer_grid(thy_sequencer *seq, thy_transport *transport,
    uint8_t track, uint32_t ticks);
int thy_sequencer_history(thy_sequencer *seq, thy_core *core,
    thy_transport *transport, int redo);
/* Capture region_end=0 means existing loop, or growing take when empty.
 * Calling record again cancels ARM/QUEUE or requests quantized Finish. */
int thy_sequencer_record(thy_sequencer *seq, thy_transport *transport,
    const thy_clock *clock, uint8_t track, uint8_t source, uint8_t policy,
    uint32_t region_start, uint32_t region_end);
int thy_sequencer_finish(thy_sequencer *seq, const thy_transport *transport,
    const thy_clock *clock, int immediate);
void thy_sequencer_cancel(thy_sequencer *seq);
/* The root snapshots already-held inputs when capture.start_epoch changes.
 * identity is per-note lifetime; group preserves one native gesture. Duplicate
 * snapshot/stream identities are idempotent. position is transport position. */
int thy_sequencer_input_on(thy_sequencer *seq, uint8_t source,
    uint32_t identity, uint16_t group, uint8_t role, uint8_t pitch,
    uint8_t velocity, uint64_t position);
int thy_sequencer_input_off(thy_sequencer *seq, uint8_t source,
    uint32_t identity, uint64_t position);
int thy_sequencer_input_event(thy_sequencer *seq, uint8_t source,
    uint8_t type, uint8_t data1, uint8_t data2, uint64_t position);
/* Lightweight boundary service before a physical edge mutates held input.
 * Does not schedule playback/generator attacks; then snapshot on epoch change. */
int thy_sequencer_capture_advance(thy_sequencer *seq, thy_transport *transport,
    const thy_clock *clock);
/* Call AFTER transport advance; cleanup always precedes attacks. Up to 60 ticks
 * late is admitted; old attacks are dropped rather than replaying backlog.
 * Track-to-Arp uses the selected live PRESET, its own track-only input context,
 * and that track's MIDI route. No live arp inputs/runtime are modified. */
int thy_sequencer_advance(thy_sequencer *seq, thy_core *core,
    thy_transport *transport, const thy_clock *clock,
    const thy_arp_config presets[2]);
/* Call before resetting/stopping transport, closing a take immediately. */
int thy_sequencer_stop(thy_sequencer *seq, thy_core *core,
    const thy_transport *transport, const thy_clock *clock);
void thy_sequencer_release(thy_sequencer *seq, thy_core *core);
/* Explicit little-endian v2 format; v1 import retained. CRC32 payload,
 * fully validate then publish. V2 stores created tracks and editor spacing.
 * Import never restores sounding/runtime state. Caller stops before decode. */
size_t thy_sequencer_song_size(const thy_sequencer *seq);
int thy_sequencer_encode(const thy_sequencer *seq, uint8_t *out,
    size_t capacity, size_t *written);
int thy_sequencer_validate(const uint8_t *bytes, size_t length);
int thy_sequencer_decode(thy_sequencer *seq, const uint8_t *bytes, size_t length);
#endif
