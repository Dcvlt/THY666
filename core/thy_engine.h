/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_ENGINE_H
#define THY_ENGINE_H
#include "thy_state.h"
#include "thy_instrument.h"
#include "thy_clock.h"
#include "thy_transport.h"
#include "thy_arp.h"
#include "thy_sequencer.h"
#include "thy_sequencer_ui.h"
#include "thy_mpe.h"
#include "thy_channel_expression.h"
#include "thy_harmony_workspace.h"

typedef struct thy_engine_channel_config {
    uint8_t aftertouch,bend,range; /* 0 inherit,1 Off,2 Poly,3 Channel; bend0..2. */
} thy_engine_channel_config;

/* Physical-half playing policies. Zero inherits the MIDI route; nonzero
 * response modes store menu value + 1, preserving old routed configurations. */
typedef struct thy_engine_performance_config {
    uint8_t velocity, aftertouch, bend, range;
} thy_engine_performance_config;

/* Read-only physical-half icon values. Valid masks use bit 0 for Left and
 * bit 1 for Right; mixed values match performance_get (-1 AT, 2 velocity).
 * This is a transient result, not cached engine state or a saved format. */
typedef struct thy_engine_performance_indicators {
    int32_t aftertouch[2], velocity_mode[2];
    uint8_t aftertouch_valid, velocity_valid;
} thy_engine_performance_indicators;

enum thy_engine_rhythm_result {
    THY_ENGINE_STALE_RUN = 5,
    THY_ENGINE_SOURCE_NOT_READY = 6,
    THY_ENGINE_TIME_BACKWARD = 7,
    THY_ENGINE_TIME_OVERFLOW = 8
};
enum thy_engine_library_result { THY_ENGINE_LIBRARY_IN_USE=9, THY_ENGINE_LIBRARY_BUSY=10 };
enum thy_engine_generated_source {
    THY_ENGINE_ARP1 = 1, THY_ENGINE_ARP2 = 2,
    THY_ENGINE_TRACK_BASE = 16, THY_ENGINE_PREVIEW = 64
};

enum thy_engine_system { THY_ENGINE_CONDITIONING, THY_ENGINE_READY };
enum thy_engine_transport { THY_ENGINE_STOPPED, THY_ENGINE_PLAYING };
enum thy_engine_capture { THY_ENGINE_CAPTURE_OFF, THY_ENGINE_CAPTURE_ARMED,
    THY_ENGINE_RECORDING, THY_ENGINE_CLEAN_DUB };
enum thy_engine_clock { THY_ENGINE_INTERNAL, THY_ENGINE_WAITING,
    THY_ENGINE_LOCKED, THY_ENGINE_LOST };
typedef struct thy_engine_track_runtime {
    uint32_t generation, playhead;
    uint8_t phase, recording;
} thy_engine_track_runtime;
typedef struct thy_engine_session {
    uint32_t epoch;
    uint8_t system, playing_mode, transport, capture, clock;
    thy_engine_track_runtime tracks[21];
} thy_engine_session;

typedef struct thy_engine_hold_repeat {
    uint32_t started_ms, next_ms, state_revision, transition_revision;
    int32_t value;
    uint16_t row;
    uint8_t instance, action, group, level;
} thy_engine_hold_repeat;

struct thy_calibration;

/* Shared application state, not a persistence format. Configuration and UI
 * transactions are owned by state; emitted musical owners stay in instrument's
 * core. Planned processors never run merely because configuration is edited. */
typedef struct thy_engine {
    thy_state state;
    thy_instrument *instrument;
    thy_engine_session session;
    thy_harmony_workspace harmony;
    uint16_t scale;
    uint8_t route_target; /* Editor target, independent of sounding owners. */
    uint8_t shortcut_target; /* Editor target for the twelve scale shortcuts. */
    uint8_t sensitivity_target; /* 0 Left,97 Right; 1..96 individual logical pads. */
    uint8_t velocity_target; /* Transient Performance editor: 0 Both,1 Left,2 Right. */
    uint8_t sensitivity_pick, sensitivity_picked;
    uint8_t sensitivity_muted[12]; /* A picked contact remains muted until release. */
    thy_engine_hold_repeat navigation_repeat; /* Transient physical UP/DOWN hold. */
    struct thy_calibration *calibration; /* Host/app owns the optional workspace. */
    thy_mpe *expression; /* Optional caller-owned MPE workspace, never serialized. */
    thy_mpe_config expression_config;
    thy_channel_expression *channel_expression;
    thy_engine_channel_config channel_routes[THY_INSTRUMENT_ROUTE_TARGETS];
    thy_engine_performance_config performance[2];
    uint32_t expression_service_ms;
    uint8_t calibration_open;
    uint8_t calibration_pick, calibration_pick_pad;
    uint8_t calibration_rejected;
    uint8_t calibration_review_cursor, calibration_review_editing, calibration_review_draft;
    uint16_t calibration_review_reference, calibration_review_attack;
    uint32_t calibration_sample_ms[THY_PAD_COUNT];
    uint8_t calibration_contacts[12]; /* Raw gestures stay muted through exit. */
    uint8_t arp_target;   /* Persisted editor selection: 0 or 1. */
    uint8_t arp_quick_keys; /* Persisted opt-in temporary M-key layer. */
    uint8_t arp_quick_open; /* Runtime page state, never persisted. */
    uint8_t arp_quick_cursor;
    thy_clock musical_clock;
    thy_transport rhythm_transport;
    uint32_t live_generation[3]; /* Arp 1, Arp 2, ownership preview. */
    uint32_t observed_clock_generation;
    uint8_t rhythm_epoch_set;
    thy_arp arps[2];
    thy_sequencer sequencer;
    thy_sequencer_ui sequencer_ui;
    uint64_t input_pitches[THY_PAD_COUNT][2];
    uint64_t captured_strum[THY_PAD_COUNT][2];
    uint32_t input_identity[THY_PAD_COUNT];
    uint32_t captured_generated[THY_GENERATED_OWNER_COUNT];
    uint32_t input_serial, observed_capture_epoch;
    uint8_t input_velocity[THY_PAD_COUNT], input_role[THY_PAD_COUNT];
    uint8_t input_assignment[THY_PAD_COUNT], input_strummed[THY_PAD_COUNT];
    uint8_t song_save_requested, song_recall_requested, song_status;
    uint8_t usb_input_velocity[16][128];
    uint8_t remote_record, remote_position, remote_channel;
    uint8_t captured_pressure[THY_PAD_COUNT];
    uint32_t capture_pressure_ms;
} thy_engine;

/* history is an exclusive caller-owned 512-event undo arena; keep it alive
 * until the engine is discarded. It may reside in a different memory bank. */
void thy_engine_init(thy_engine *engine, thy_instrument *instrument,
    uint16_t scale, uint16_t scale_count, thy_seq_event history[THY_SEQ_EVENTS]);
/* Called only by the confirmed shared-menu transaction. No storage/device I/O. */
int thy_engine_reset_options(thy_engine *,uint8_t group,const uint8_t targets[9],uint8_t track);
void thy_engine_bind_controls(thy_engine *engine);
void thy_engine_bind_music(thy_engine *engine);
void thy_engine_bind_expression(thy_engine *, thy_mpe *);
int thy_engine_expression_can_configure(const thy_engine *, const thy_mpe_config *);
int thy_engine_expression_configure(thy_engine *, const thy_mpe_config *);
int thy_engine_expression_send_setup(thy_engine *);
int thy_engine_expression_pressure(thy_engine *, uint8_t pad, uint8_t pressure);
int thy_engine_expression_bend(thy_engine *, uint8_t pad, uint16_t value);
void thy_engine_expression_tick(thy_engine *, uint32_t now_ms);
void thy_engine_bind_channel_expression(thy_engine *,thy_channel_expression *);
int thy_engine_channel_quiet(const thy_engine *);
int thy_engine_channel_configure(thy_engine *,uint8_t target,const thy_engine_channel_config *);
uint8_t thy_engine_channel_aftertouch(const thy_engine *,uint8_t target);
int thy_engine_performance_get(const thy_engine *,uint16_t row,uint8_t scope,int32_t *);
/* Set include_aftertouch to zero when MPE hides ordinary AT indicators. */
void thy_engine_performance_indicators_read(const thy_engine *,int include_aftertouch,
    thy_engine_performance_indicators *);
int thy_engine_performance_set(thy_engine *,uint16_t row,uint8_t scope,int32_t);
uint8_t thy_engine_performance_route(const thy_engine *,uint8_t pad);
thy_engine_channel_config thy_engine_performance_response(const thy_engine *,uint8_t pad,uint8_t target);
int thy_engine_channel_send_setup(thy_engine *);
int thy_engine_expression_pressure_axes(thy_engine *,uint8_t pad,uint8_t pressure,uint8_t aftertouch);
int thy_engine_profile_get(const thy_engine *,uint8_t instance,thy_expression_profile *);
int thy_engine_profile_configure(thy_engine *,uint8_t instance,const thy_expression_profile *);
int thy_engine_user_scale(thy_engine *,uint8_t slot);
int thy_engine_library_quiet(const thy_engine *);
int thy_engine_library_set(thy_engine *,uint8_t kind,uint8_t slot,const void *,size_t);
int thy_engine_library_delete(thy_engine *,uint8_t kind,uint8_t slot);
int thy_engine_library_capture(const thy_engine *,uint8_t kind,uint8_t slot,thy_library_ui_draft *);
int thy_engine_library_import(thy_engine *,const uint8_t *,size_t);
int thy_engine_library_open(thy_engine *,uint8_t kind,uint8_t slot);
void thy_engine_library_close(thy_engine *);
int thy_engine_library_navigate(thy_engine *,uint8_t action);
/* Resolved physical input enters both subscribed Arps exactly once. Layer
 * policy is captured at attack; configuration never adopts already-held pads.
 * Both Arps use Hold. Latch gestures and track-to-Arp contexts are separate. */
int thy_engine_press_shaped(thy_engine *engine, uint8_t pad, const int16_t *notes,
    size_t count, int16_t transpose, uint8_t channel, uint8_t destinations,
    uint8_t velocity, const thy_shape *shape);
int thy_engine_arp_configure(thy_engine *engine, uint8_t index,
    const thy_arp_config *config);
int thy_engine_arp_clear(thy_engine *engine, uint8_t index);
#define THY_ENGINE_ARP_QUICK_ROWS 15u
int thy_engine_arp_quick_open(thy_engine *engine);
int thy_engine_arp_quick_close(thy_engine *engine);
uint16_t thy_engine_arp_quick_row(uint8_t cursor);
/* Called by the instrument filter after physical edge bookkeeping. Returns
 * 1 only for M1..M7 while the quick page and its temporary layer are enabled. */
int thy_engine_control(thy_engine *engine, uint8_t bank, uint8_t index, int pressed);
/* One bounded draft adjustment per foreground advance. Physical press/release
 * edges own arming; logical navigation alone never starts a hold. */
void thy_engine_repeat_tick(thy_engine *, uint32_t now_ms);
void thy_engine_repeat_cancel(thy_engine *);
/* Change future pad planning only. Existing physical/generated owners retain
 * captured pitches/routes; unavailable workspaces are rejected atomically. */
int thy_engine_workspace(thy_engine *engine, uint8_t workspace);
int thy_engine_sequencer_open(thy_engine *engine, uint8_t view);
int thy_engine_sequencer_close(thy_engine *engine);
int thy_engine_sequencer_options_close(thy_engine *engine);
int thy_engine_sequencer_record(thy_engine *engine);
/* Physical/web actions share this entry. RECORD starts directly when stopped. */
int thy_engine_sequencer_action(thy_engine *engine, uint8_t command, uint8_t track);
int thy_engine_sequencer_finish(thy_engine *engine, int immediate);
int thy_engine_sequencer_track_configure(thy_engine *engine, uint8_t track,
    const thy_seq_track *config);
int thy_engine_sequencer_loop(thy_engine *engine, uint8_t track,
    uint32_t start, uint32_t end);
void thy_engine_sequencer_changed(thy_engine *engine, uint8_t track);
int thy_engine_sequencer_song_import(thy_engine *engine,
    const uint8_t *bytes, size_t length);
int thy_engine_sequencer_pad(void *context, uint8_t pad, int pressed, int *result);
/* Apply a logical pad-light overlay; input bank colors use their chosen role. */
int thy_engine_sequencer_color(const thy_engine *engine, uint8_t logical,
    uint16_t rgb[3]);
int thy_engine_receive_midi(thy_engine *engine, const uint8_t *message,
    uint8_t length, uint32_t now_ms);
void thy_engine_sequencer_init(thy_engine *engine);
void thy_engine_sequencer_tick(thy_engine *engine);
void thy_engine_sequencer_capture(thy_engine *engine);
void thy_engine_sequencer_input(thy_engine *engine, uint8_t pad,
    const int16_t *notes, size_t count, uint8_t velocity, uint8_t role);
void thy_engine_sequencer_release_input(thy_engine *engine, uint8_t pad);
int thy_engine_sequencer_read(const thy_engine *engine, uint16_t row,
    uint8_t instance, int32_t *value);
int thy_engine_sequencer_write(thy_engine *engine, uint16_t row,
    uint8_t instance, int32_t value);
void thy_engine_sequencer_usb_clear(thy_engine *engine);
/* Shared physical/browser navigation entry. Play shortcuts are settings-only;
 * menus retain the same directional editor behavior. */
int thy_engine_navigate(thy_engine *engine, uint8_t action);
/* Call before musical/sequencer pad filters. Pick mode is read-only selection;
 * its captured releases remain consumed after leaving the Sensitivity page. */
int thy_engine_sensitivity_pad(thy_engine *,uint8_t electrical,int pressed,int *result);
/* PAD instances0..95 are logical;96 Left(-1),97 Right(-3);-2 invalid. */
int thy_engine_sensitivity_electrical(const thy_engine *,uint8_t instance);
/* Raw guard interval behind the basic 0..100% control. No settings mutate. */
int thy_engine_sensitivity_raw_bounds(const thy_engine *,uint8_t instance,int32_t *low,int32_t *high);
/* Logical light order: LEFT, UP, CENTER, DOWN, RIGHT, then function indices
 * 0..13. Components are 0..4095, independent of physical output mapping.
 * Hidden Play arrows remain fully dark even while touched. */
void thy_engine_center_levels(const thy_engine *engine,
    uint16_t levels[THY_INSTRUMENT_CENTER_LIGHTS]);
/* Catalogue provider acknowledgement. Values come from the validated library;
 * callers load that layout before accepting the next physical attack. */
int thy_engine_catalog(thy_engine *engine, uint16_t scale, uint16_t scale_count);

/* Runtime, separate from settings/menu transactions. The first advance
 * establishes the caller's uint32 millisecond epoch. One foreground call
 * advances musical time, transport, enabled Arps and legacy live strums.
 * Sequencer event storage, recording and playback are separate processors. */
int thy_engine_advance(thy_engine *engine, uint32_t now_ms);
int thy_engine_clock_configure(thy_engine *engine, uint32_t milli_bpm,
    uint8_t source, uint8_t numerator, uint8_t denominator, uint8_t quantize);
int thy_engine_transport_start(thy_engine *engine);
int thy_engine_transport_stop(thy_engine *engine);
int thy_engine_track_toggle(thy_engine *engine, uint8_t track);
int thy_engine_track_loop(thy_engine *engine, uint8_t track,
    uint32_t start, uint32_t length);
int thy_engine_receive_time(thy_engine *engine, const uint8_t *message,
    uint8_t length, uint32_t now_ms);
int thy_engine_receive_time_deferred(thy_engine *engine, const uint8_t *message,
    uint8_t length, uint32_t now_ms);
int thy_engine_usb_reset(thy_engine *engine, uint32_t now_ms);
/* A source generation is captured by a scheduler when work is queued. Stale
 * acquisitions are rejected before touching the shared ledger. Cleanup and
 * stale-handle releases never use global panic or release physical owners.
 * Source/run exhaustion retires the source rather than reusing a generation.
 * Reinitialization starts a fresh lifetime; discard external callbacks then. */
uint32_t thy_engine_source_generation(const thy_engine *engine, uint32_t source);
int thy_engine_generated_acquire(thy_engine *engine, uint32_t source,
    uint32_t run, uint8_t note, uint8_t channel, uint8_t destinations,
    uint8_t velocity, thy_generated_handle *handle);
int thy_engine_generated_clear(thy_engine *engine, uint32_t source);
/* Explicit global user Panic: stop tracks, invalidate all generated callbacks,
 * then release instrument owners/sustain using the existing panic contract. */
void thy_engine_panic(thy_engine *engine);
void thy_engine_bind_calibration(thy_engine *,struct thy_calibration *);
int thy_engine_calibration_open(thy_engine *);
void thy_engine_calibration_close(thy_engine *);
int thy_engine_calibration_navigate(thy_engine *,uint8_t action);
int thy_engine_calibration_pad(thy_engine *,uint8_t electrical,int pressed,int *result);
void thy_engine_calibration_tick(thy_engine *);
int thy_engine_calibration_apply(thy_engine *);
int thy_engine_calibration_select(thy_engine *,uint8_t logical);
int thy_engine_calibration_set_sensitivity(thy_engine *,int percent);
/* Packed GRB matches the shared lighting renderer. */
int thy_engine_calibration_lights(const thy_engine *,uint32_t now_ms,uint32_t logical_colors[96]);
int thy_engine_calibration_value(const thy_engine *,uint8_t logical,uint16_t raw,
    uint16_t rise_rate,int candidate,int velocity);
int thy_engine_guidance_open(thy_engine *engine);
#endif
