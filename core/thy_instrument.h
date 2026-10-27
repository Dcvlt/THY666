/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_INSTRUMENT_H
#define THY_INSTRUMENT_H

#include "thy_core.h"
#include "thy_engines.h"
#include "thy_views.h"
#include "thy_expression_profile.h"
#include "thy_chord_layout.h"
#include "thy_voicing.h"
#include "thy_library.h"
#include "thy_library_ui.h"
#define THY_CHORD_LAYOUT_USER 5u /* Direct library records; caller-owned layout. */

#define THY_INSTRUMENT_CONTROLS 43u
#define THY_INSTRUMENT_SLOTS 7u
#define THY_INSTRUMENT_MENU_ROWS 23u
#define THY_INSTRUMENT_WARMUP_SAMPLES 8u
#define THY_INSTRUMENT_WARMUP_SPAN 32u
#define THY_INSTRUMENT_TRANSITIONS 64u
#define THY_INSTRUMENT_FACTORY_ATTACK 1377u
#define THY_INSTRUMENT_FACTORY_RELEASE 1354u
#define THY_INSTRUMENT_ROUTE_TARGETS 9u
#define THY_INSTRUMENT_CENTER_LIGHTS 19u

enum thy_instrument_slot_scope {
    THY_SLOTS_LEFT_SIDE = 0, THY_SLOTS_SELECTED_PAD = 1
};

enum thy_instrument_lower_mode {
    THY_LOWER_STORED_NOTES = 0, THY_LOWER_OCTAVE_LAYERS = 1
};

/* Settings shortcuts only: none of these starts an unfinished playing mode. */
enum thy_instrument_play_shortcut {
    THY_PLAY_HIDDEN = 0, THY_PLAY_VIEWS, THY_PLAY_PERFORMANCE, THY_PLAY_MIDI,
    THY_PLAY_LIGHTS, THY_PLAY_SENSITIVITY, THY_PLAY_CHORDS,
    THY_PLAY_ARPS, THY_PLAY_ENGINES, THY_PLAY_SHORTCUT_COUNT
};

enum thy_instrument_routing_profile {
    THY_ROUTING_WHOLE = 0,
    THY_ROUTING_SPLIT = 1,
    THY_ROUTING_ZONES = 2
};

/* Resolved next-attack settings. Channels are zero-based MIDI channels;
 * destinations use THY_ROUTE_*. Velocity and pressure never contain inheritance
 * sentinels in this public value. */
typedef struct thy_instrument_route {
    uint8_t channel;
    uint8_t destinations;
    uint8_t velocity;
    uint8_t pressure_enabled;
} thy_instrument_route;

enum thy_instrument_detector_profile {
    THY_DETECTOR_RELATIVE = 0,
    THY_DETECTOR_FACTORY_LINEAR = 1,
    THY_DETECTOR_CUSTOM_ABSOLUTE = 2
};

enum thy_instrument_pressure_curve {
    THY_PRESSURE_LINEAR = 0,
    THY_PRESSURE_SOFT = 1,
    THY_PRESSURE_FIRM = 2
};

enum thy_instrument_velocity_mode { THY_VELOCITY_FIXED=0, THY_VELOCITY_STRIKE=1 };
#define THY_INSTRUMENT_STRIKE_WINDOW_MS 32u
/* Rise speed in ADC counts/ms, measured before the normal attack confirmation.
 * This adds no waiting period to the detector. It is not a force measurement. */
typedef struct thy_instrument_velocity {
    uint16_t soft_rate, hard_rate;
    uint8_t mode, curve, minimum, maximum;
} thy_instrument_velocity;

/* Positive, absolute 12-bit ADC gates. These are user settings, not a fitted
 * calibration for a particular shim modification. Pressure is independent of
 * attack velocity. The soft/firm curves are our proposed response. */
typedef struct thy_instrument_response {
    uint16_t attack_raw;
    uint16_t release_raw;
    uint16_t pressure_floor;
    uint16_t pressure_full_scale;
    uint8_t pressure_curve;
} thy_instrument_response;

/* Release follows Trigger by this positive ADC gap, or is edited manually.
 * Detector overrides own this policy; continuous pressure is independent. */
typedef struct thy_instrument_release_policy {
    uint16_t gap;
    uint8_t linked;
} thy_instrument_release_policy;

enum thy_instrument_pad_action {
    THY_PAD_TRANSITION_PRESS = 1,
    THY_PAD_TRANSITION_RELEASE = 2,
    THY_PAD_TRANSITION_BLOCK = 3
};

/* Seven original serialized slots, including holes and a valid MIDI note 0.
 * A zero validity mask is an intentionally silent/unassigned pad. The owner
 * retains this layout until it explicitly replaces it with set_layout(). */
typedef struct thy_instrument_pad {
    int16_t notes[THY_INSTRUMENT_SLOTS];
    uint8_t valid_mask;
    uint8_t color;
} thy_instrument_pad;

typedef struct thy_instrument_calibration {
    uint16_t baseline;
    uint16_t press_delta;
    uint16_t release_delta;
    uint16_t full_scale;
    int8_t polarity;
} thy_instrument_calibration;

typedef struct thy_instrument_sensor {
    thy_instrument_calibration calibration;
    thy_instrument_response response;
    int32_t baseline_q8;
    uint32_t warmup_sum;
    uint32_t last_sample_ms;
    uint32_t last_transition_ms;
    uint16_t raw;
    uint16_t delta;
    uint16_t warmup_min;
    uint16_t warmup_max;
    int16_t signed_delta;
    int16_t attack_raw;
    int16_t release_raw;
    uint8_t warmup_count;
    uint8_t held;
    uint8_t pressure;
    uint8_t sent_pressure;
    uint8_t blocked;
    uint8_t detector_profile;
    uint8_t attack_count;
    uint8_t release_count;
    uint8_t response_overridden;
    uint32_t strike_peak_ms;
    uint16_t strike_peak_rate;
    uint8_t strike_velocity, strike_ready;
} thy_instrument_sensor;

typedef struct thy_instrument_pad_transition {
    uint32_t sequence;
    uint32_t at_ms;
    uint16_t raw;
    uint16_t baseline;
    uint16_t delta;
    int16_t signed_delta;
    int16_t attack_raw;
    int16_t release_raw;
    uint8_t electrical;
    uint8_t logical;
    uint8_t action;
    uint8_t blocked;
    uint8_t detector_profile;
    uint8_t pressure;
} thy_instrument_pad_transition;

typedef struct thy_instrument_performance {
    uint8_t root;             /* Chromatic pitch class, 0=C. */
    uint8_t shortcut;         /* Most recently selected physical scale key. */
    uint8_t melody_layout;    /* enum thy_instrument_layout. */
    uint8_t guitar_window;    /* 0=EADG, 1=ADGB, 2=DGBE, low string first. */
    uint8_t inversion;
    uint8_t spread;
    uint8_t strum;
    uint8_t strum_order;
    uint8_t upper_octaves;
    int8_t chord_octave;
    int8_t melody_octave;
    uint16_t strum_ms;
} thy_instrument_performance;

/* All recovered scale families remain available independently of the twelve
 * physical shortcuts. A shortcut can also cycle the right-hand note layout. */
#define THY_INSTRUMENT_SCALE_FAMILIES 70u
#define THY_SCALE_SHORTCUT_COUNT 12u
#define THY_SCALE_SHORTCUT_LAYOUT THY_INSTRUMENT_SCALE_FAMILIES

enum thy_instrument_layout {
    THY_LAYOUT_STORED, THY_LAYOUT_CHROMATIC, THY_LAYOUT_THIRDS,
    THY_LAYOUT_FOURTHS, THY_LAYOUT_CONDENSED, THY_LAYOUT_GUITAR,
    THY_LAYOUT_PIANO, THY_LAYOUT_IN_KEY_LIT, THY_LAYOUT_ALL_LIT,
    THY_LAYOUT_COUNT
};

enum thy_instrument_action {
    THY_ACTION_NONE = 0,
    THY_ACTION_NAVIGATION,
    THY_ACTION_INVERSION,
    THY_ACTION_STRUM,
    THY_ACTION_SPREAD,
    THY_ACTION_NOTE_SLOT,
    THY_ACTION_ROOT,
    THY_ACTION_SHORTCUT,
    THY_ACTION_USER,
    THY_ACTION_PANIC
};

typedef struct thy_instrument_control_event {
    uint8_t bank;
    uint8_t index;
    uint8_t pressed;
    uint8_t action;
    int16_t value;
    uint32_t sequence;
} thy_instrument_control_event;

typedef void (*thy_instrument_action_fn)(void *context,
    const thy_instrument_control_event *event);
typedef void (*thy_instrument_navigation_fn)(void *context, uint8_t action);
/* Observes both physical edges after masks update. A consumed edge keeps
 * telemetry but skips the ordinary musical shortcut and action handler. */
typedef int (*thy_instrument_control_filter_fn)(void *context, uint8_t bank,
    uint8_t index, int pressed);
/* Typed editor dispatch occurs before musical/context planning. Return one
 * only when the edge is consumed and put its THY result in result. Releases
 * must retain the role captured at press, even after a workspace change. */
typedef int (*thy_instrument_pad_filter_fn)(void *context, uint8_t electrical,
    int pressed, int *result);
typedef int (*thy_instrument_light_overlay_fn)(void *context, uint8_t logical,
    uint16_t components[3]);

/* Optional transient sampling response, used for live calibration review.
 * Outputs begin as the saved settings. Return0 to use those settings or1 to
 * use the supplied absolute detector/aftertouch and velocity configuration.
 * Invalid outputs reject that sample. This never edits saved configurations,
 * inheritance flags or release ownership. Selection changes require quiet
 * pads; the provider must not mutate the instrument from inside the callback. */
typedef int (*thy_instrument_sampling_provider_fn)(void *context,
    uint8_t electrical, thy_instrument_response *response,
    thy_instrument_velocity *velocity);

/* Optional shared musical processor. Context pads are resolved before this
 * seam. The attack callback owns the final core press, including a silent
 * physical owner when a generator consumes the resolved input. */
typedef int (*thy_instrument_attack_fn)(void *context, uint8_t electrical,
    const int16_t *notes, size_t count, int16_t transpose,
    const thy_instrument_route *route, const thy_shape *shape);
typedef int (*thy_instrument_release_fn)(void *context, uint8_t electrical);
typedef int (*thy_instrument_pressure_fn)(void *context, uint8_t electrical, uint8_t pressure,uint8_t aftertouch);
typedef void (*thy_instrument_reset_fn)(void *context);

/* One external core, bounded RAM, no allocation, hardware access or libc.
 * Calls are serialized by the caller. Electrical IDs are ADC bank*16+select;
 * layout IDs 0..47 are chords and 48..95 melody. Identity mapping is only a
 * bring-up default, not a claim about the physical orientation of the pads.
 * set_mapping() installs an explicit one-to-one electrical->layout map.
 */
typedef struct thy_instrument {
    thy_core *core;
    const thy_instrument_pad *layout;
    thy_instrument_action_fn action;
    void *action_context;
    thy_instrument_navigation_fn navigation;
    void *navigation_context;
    thy_instrument_control_filter_fn control_filter;
    void *control_filter_context;
    thy_instrument_pad_filter_fn pad_filter;
    void *pad_filter_context;
    thy_instrument_light_overlay_fn light_overlay;
    void *light_overlay_context;
    thy_instrument_sampling_provider_fn sampling_provider;
    void *sampling_provider_context;
    thy_instrument_attack_fn musical_attack;
    thy_instrument_release_fn musical_release;
    thy_instrument_pressure_fn musical_pressure;
    thy_instrument_reset_fn musical_reset;
    void *musical_context;
    thy_instrument_sensor sensors[THY_PAD_COUNT];
    thy_instrument_response response;
    thy_instrument_velocity velocity_response;
    thy_instrument_release_policy release_policy, pad_release_policy[THY_PAD_COUNT];
    thy_instrument_response cluster_response[2];
    thy_instrument_velocity cluster_velocity[2];
    thy_expression_profile expression_profiles[2],pad_expression_profiles[THY_PAD_COUNT];
    uint8_t expression_profile_overridden[THY_PAD_COUNT];
    thy_instrument_release_policy cluster_release_policy[2];
    /* Measured physical/electrical minimum Release ADC for the simple
     * sensitivity controls. Zero means no learned guard. Saved separately
     * from detector validity so advanced raw edits remain explicit. */
    uint16_t sensitivity_guard[THY_PAD_COUNT];
    thy_instrument_velocity pad_velocity[THY_PAD_COUNT];
    uint8_t velocity_overridden[THY_PAD_COUNT];
    thy_instrument_performance performance;
    thy_instrument_performance users[3];
    uint8_t scale_shortcuts[THY_SCALE_SHORTCUT_COUNT];
    thy_instrument_control_event last_event;
    thy_instrument_pad_transition transitions[THY_INSTRUMENT_TRANSITIONS];
    uint32_t transition_sequence;
    uint32_t transition_overwritten;
    uint8_t transition_count;
    uint32_t control_masks[4];
    uint32_t controls_seen[4];
    uint32_t layout_revision;  /* Requested root/scale/layout selection. */
    uint32_t revision;
    uint32_t presses;
    uint32_t releases;
    uint32_t errors;
    int last_error;
    uint8_t mapping[THY_PAD_COUNT];
    uint8_t excluded_slots[48];
    uint8_t global_excluded_slots;
    uint8_t slot_scope;
    uint8_t lower_mode;
    uint8_t octave_layers;          /* Whole LEFT side: bits0..6 = -3..+3. */
    uint8_t pad_octave_layers[48];   /* Selected-chord layer masks. */
    uint8_t play_nav[4];      /* LEFT, UP, DOWN, RIGHT; CENTER opens Settings. */
    int16_t degrees[12];
    int16_t melody_anchor;
    int16_t transpose[2];
    uint8_t degree_count;
    uint8_t channels[2];
    uint8_t destinations[2];
    uint8_t routing_profile;
    uint8_t route_channels[THY_INSTRUMENT_ROUTE_TARGETS];
    uint8_t route_destinations[THY_INSTRUMENT_ROUTE_TARGETS];
    uint8_t route_velocity[THY_INSTRUMENT_ROUTE_TARGETS]; /* 0 inherits global. */
    uint8_t route_pressure[THY_INSTRUMENT_ROUTE_TARGETS]; /* 0 inherit, 1 off, 2 on. */
    uint8_t held_pressure_enabled[THY_PAD_COUNT]; /* Captured at each attack. */
    uint8_t fixed_velocity;
    uint8_t pressure_enabled;
    uint8_t lights_enabled;
    uint8_t light_idle;
    uint8_t light_held;
    uint16_t light_fade_ms;
    uint8_t light_chord_mode, light_key_contrast, light_palette;
    uint8_t light_key_contrast_percent; /* Remaining black-key brightness. */
    uint8_t light_held_style, light_held_feedback, display_view;
    uint8_t display_counts, display_pretty_counts, display_motion;
    uint8_t engine_kind; /* enum thy_playing_engine; Play with independent layouts. */
    thy_view_config display_config[THY_VIEW_COUNT];
    uint8_t center_brightness; /* 0..8, state lighting independent of sensing. */
    uint8_t center_poll_ms, context_controls, animations_enabled;
    uint8_t context_location; /* 0 upper-right, 1 lower-left; saved upper/lower ordinal. */
    uint8_t context_mask;     /* Six visible actions in context_pad order. */
    int8_t bass_octave;       /* Lowest enabled LEFT chord pitch, -3..+3. */
    uint8_t held_color_root[THY_PAD_COUNT];
    uint8_t held_color_degree[THY_PAD_COUNT];
    uint8_t held_color_valid[THY_PAD_COUNT];
    /* Resolved attack colors for generator-consumed, silent physical owners.
     * These are visual snapshots, never MIDI ownership or future attacks. */
    uint8_t held_input_first[THY_PAD_COUNT], held_input_bass[THY_PAD_COUNT];
    uint8_t held_input_valid[THY_PAD_COUNT];
    uint8_t context_held[THY_PAD_COUNT];
    uint8_t input_enabled;
    uint8_t selected_pad;
    uint8_t selected_chord;   /* Last left pad; melody presses cannot retarget slot edits. */
    uint8_t keep_transpose, live_update, live_lock_scale, live_mute_chord;
    uint8_t chord_layout, chord_quality, six_bank;
    uint8_t voicing_mode, drop_mask; /* 0 prefix-up inversions;1 independent drops. */
    uint8_t user_scale; /* 0 builtin;1..8 named custom scale. */
    thy_library library;
    thy_library_ui library_editor;
    int16_t catalog_degrees[12];
    uint8_t catalog_degree_count;
    uint8_t live_degree_count; /* Transient successful chord selection, never saved. */
    int16_t live_anchor, live_degrees[12];
    uint8_t menu_open;
    uint8_t menu_editing;
    uint8_t menu_row;
    uint8_t menu_notice;      /* 0 none, 1 release affected pads before editing. */
    uint8_t users_valid;
} thy_instrument;

/* Live Update changes future RIGHT assignments only. Lock Scale keeps the
 * configured key's pitch classes; OFF uses the selected chord's pitch classes.
 * Chromatic/Guitar geometry quantizes upward; scale layouts use ordered tones.
 * An accepted chord selection persists until another chord or catalog change. */
int thy_instrument_set_live_update(thy_instrument *,uint8_t enabled,uint8_t lock_scale,uint8_t mute_chord);
void thy_instrument_follow_chord(thy_instrument *,uint8_t logical,const int16_t *resolved,size_t count,int16_t transpose);
void thy_instrument_key_changed(thy_instrument *);
int thy_instrument_chord_cell(const thy_instrument *,uint8_t logical,thy_chord_cell *);
int thy_instrument_chord_degree(const thy_instrument *,uint8_t logical,uint8_t *degree);
void thy_instrument_capture_silent_input(thy_instrument *,uint8_t electrical,uint8_t logical,const int16_t *,size_t);
uint8_t thy_instrument_drop_available(const thy_instrument *);
int thy_instrument_apply_scale(thy_instrument *);
/* Effective right-hand pitch set, including Live Update; Piano retains its
 * fixed keyboard mapping and configured key. Includes current transpose. */
int thy_instrument_note_in_key(const thy_instrument *,int note);

int thy_instrument_get_expression_profile(const thy_instrument *,uint8_t pad,
    thy_expression_profile *,uint8_t *overridden);
int thy_instrument_set_cluster_expression_profile(thy_instrument *,uint8_t side,
    const thy_expression_profile *);
int thy_instrument_set_pad_expression_profile(thy_instrument *,uint8_t pad,
    const thy_expression_profile *);
int thy_instrument_clear_expression_profile(thy_instrument *,uint8_t pad);
/* Read the same effective calibrated axes as raw acquisition, including a
 * transient calibration-review provider. No source/sensor mutation. */
int thy_instrument_expression_axes(const thy_instrument *,uint8_t pad,uint16_t raw,
    uint8_t *pressure,uint8_t *aftertouch);

void thy_instrument_init(thy_instrument *instrument, thy_core *core);
void thy_instrument_velocity_defaults(thy_instrument_velocity *config);
int thy_instrument_velocity_valid(const thy_instrument_velocity *config);
uint8_t thy_instrument_velocity_value(const thy_instrument_velocity *config,uint16_t rise_rate);
int thy_instrument_set_velocity(thy_instrument *,const thy_instrument_velocity *);
int thy_instrument_set_pad_velocity(thy_instrument *,uint8_t electrical,const thy_instrument_velocity *);
int thy_instrument_clear_pad_velocity(thy_instrument *,uint8_t electrical);
int thy_instrument_get_velocity(const thy_instrument *,int electrical,thy_instrument_velocity *,uint8_t *overridden);
void thy_instrument_release_defaults(thy_instrument_release_policy *);
int thy_instrument_release_policy_valid(const thy_instrument_release_policy *);
int thy_instrument_get_release_policy(const thy_instrument *,int electrical,thy_instrument_release_policy *,uint8_t *overridden);
int thy_instrument_set_release_policy(thy_instrument *,int electrical,const thy_instrument_release_policy *);
/* Atomic Trigger change; linked Release follows, manual Release stays fixed. */
int thy_instrument_set_trigger(thy_instrument *,int electrical,uint16_t attack);
/* Basic All-pad sensitivity scales explicit detector Trigger overrides by
 * new_global_attack / previous_global_attack, preserving their relative fit.
 * Advanced global Trigger retains its inherited-default-only semantics. */
int thy_instrument_scale_sensitivity(thy_instrument *,uint16_t new_global_attack);
/* Independent logical LEFT/RIGHT cluster defaults; electrical pads inherit
 * their mapped side unless explicitly overridden. Legacy global setters
 * update both cluster defaults. Getters never expose transient preview data. */
int thy_instrument_get_cluster_response(const thy_instrument *,uint8_t side,thy_instrument_response *);
int thy_instrument_set_cluster_response(thy_instrument *,uint8_t side,const thy_instrument_response *);
int thy_instrument_get_cluster_velocity(const thy_instrument *,uint8_t side,thy_instrument_velocity *);
int thy_instrument_set_cluster_velocity(thy_instrument *,uint8_t side,const thy_instrument_velocity *);
int thy_instrument_get_cluster_release_policy(const thy_instrument *,uint8_t side,thy_instrument_release_policy *);
int thy_instrument_set_cluster_release_policy(thy_instrument *,uint8_t side,const thy_instrument_release_policy *);
int thy_instrument_check_cluster_sensitivity_policy(const thy_instrument *,uint8_t side,
    const thy_instrument_response *,const thy_instrument_velocity *,const thy_instrument_release_policy *);
int thy_instrument_set_cluster_sensitivity_policy(thy_instrument *,uint8_t side,
    const thy_instrument_response *,const thy_instrument_velocity *,const thy_instrument_release_policy *);
/* Reference is the cluster default Trigger, or0 on invalid input. Scaling
 * updates that default and relative explicit detector overrides on the same
 * side, preserving override flags, AT, velocity and release policies. Every
 * target is preflighted before the first bounded commit. */
uint16_t thy_instrument_cluster_trigger(const thy_instrument *,uint8_t side);
int thy_instrument_scale_cluster_sensitivity(thy_instrument *,uint8_t side,
    uint16_t new_reference_attack);
int thy_instrument_get_pad_guard(const thy_instrument *,uint8_t electrical,uint16_t *minimum_release);
/* Quiet-only measured guard update,0..4094. Does not change detector/AT/velocity
 * settings. Use Cluster/reset clears only that electrical pad's guard. */
int thy_instrument_set_pad_guard(thy_instrument *,uint8_t electrical,uint16_t minimum_release);
/* -1 restores global defaults without deleting pad overrides; an electrical
 * pad clears both response/velocity overrides as one quiet-only operation. */
int thy_instrument_reset_sensitivity(thy_instrument *,int electrical);
/* Atomically replace both response families for one electrical pad or -1 for
 * inherited global defaults. Any invalid/busy affected pad rejects both. */
int thy_instrument_set_sensitivity(thy_instrument *,int electrical,
    const thy_instrument_response *,const thy_instrument_velocity *);
/* Full profile transaction. The read-only preflight checks the union of the
 * independent detector and velocity inheritance sets; Linked release must
 * match Trigger/gap exactly. No partial configuration is published on error. */
int thy_instrument_check_sensitivity_policy(const thy_instrument *,int electrical,
    const thy_instrument_response *,const thy_instrument_velocity *,const thy_instrument_release_policy *);
int thy_instrument_set_sensitivity_policy(thy_instrument *,int electrical,
    const thy_instrument_response *,const thy_instrument_velocity *,const thy_instrument_release_policy *);
/* Detector/AT/release policy only: does not touch velocity configuration or
 * its independent inheritance flag. The read-only check has no side effects.
 * Electrical -1 retains the inherited-global semantics of set_response. */
int thy_instrument_check_response_policy(const thy_instrument *,int electrical,
    const thy_instrument_response *,const thy_instrument_release_policy *);
int thy_instrument_set_response_policy(thy_instrument *,int electrical,
    const thy_instrument_response *,const thy_instrument_release_policy *);
/* Lower controls affect the next attack. Enabled and available bits are
 * separate so absent stored notes never masquerade as working controls.
 * Octave mode always keeps at least the original octave (bit3) when a user
 * switches the last selected layer off. Mode changes preserve both profiles. */
uint8_t thy_instrument_lower_mask(const thy_instrument *instrument);
uint8_t thy_instrument_lower_available(const thy_instrument *instrument);
int thy_instrument_set_lower_mode(thy_instrument *instrument, uint8_t mode);
void thy_instrument_set_action_handler(thy_instrument *instrument,
    thy_instrument_action_fn action, void *context);
/* Optional shared menu controller. NULL retains the original reference menu. */
void thy_instrument_set_navigation_handler(thy_instrument *instrument,
    thy_instrument_navigation_fn navigation, void *context);
void thy_instrument_set_control_filter(thy_instrument *instrument,
    thy_instrument_control_filter_fn filter, void *context);
void thy_instrument_set_pad_filter(thy_instrument *instrument,
    thy_instrument_pad_filter_fn filter, void *context);
void thy_instrument_set_musical_handler(thy_instrument *instrument,
    thy_instrument_attack_fn attack, thy_instrument_release_fn release,
    thy_instrument_reset_fn reset, void *context);
void thy_instrument_set_sampling_provider(thy_instrument *instrument,
    thy_instrument_sampling_provider_fn provider, void *context);
/* Validates all active original notes and scale degrees atomically. A new
 * layout clears selected-pad slot edits, never global slot switches or
 * existing core owner snapshots.
 * notes storage must remain valid until the next successful set_layout().
 * anchor is the top-left generated melody pitch; degrees start at zero.
 */
int thy_instrument_set_layout(thy_instrument *instrument,
    const thy_instrument_pad pads[THY_PAD_COUNT], int16_t anchor,
    const int16_t *degrees, uint8_t degree_count);
/* Predict a future logical pad press using the exact attack shaping path.
 * Does not mutate owners, clocks, counters, selected pad or alternating strum
 * direction. Outputs stay untouched on errors. Held notes use owner snapshots
 * instead; this preview describes the current next-press settings. */
int thy_instrument_preview_pad(const thy_instrument *instrument, uint8_t logical,
    int16_t out[THY_MAX_NOTES], size_t *count);
/* Shared foreground/pointer path; physical acquisition also uses this attack
 * planner. Context pads consume a press without emitting a note. */
int thy_instrument_press_pad(thy_instrument *instrument, uint8_t electrical);
/* Sequencer input bank: real electrical owner, chosen Notes/Chords source.
 * Bypasses editor filtering and performance context-pad overlays. */
int thy_instrument_press_source(thy_instrument *instrument, uint8_t electrical,
    uint8_t logical_source);
int thy_instrument_preview_source(const thy_instrument *instrument,
    uint8_t logical_source, int16_t out[THY_MAX_NOTES], size_t *count);
int thy_instrument_release_pad(thy_instrument *instrument, uint8_t electrical);
int thy_instrument_context_pad(const thy_instrument *instrument, uint8_t logical);
int thy_instrument_set_context_controls(thy_instrument *instrument, uint8_t enabled);
int thy_instrument_set_context_location(thy_instrument *instrument, uint8_t location);
int thy_instrument_set_context_mask(thy_instrument *instrument, uint8_t mask);
/* Root is resolved from the lossless degree reference, never from a rewritten
 * voicing. A successful new attack snapshots it for held-note illumination. */
int thy_instrument_chord_root(const thy_instrument *instrument, uint8_t logical,
    int16_t *root);
void thy_instrument_capture_pad_color(thy_instrument *instrument, uint8_t electrical);
int thy_instrument_set_mapping(thy_instrument *instrument,
    const uint8_t electrical_to_layout[THY_PAD_COUNT]);
int thy_instrument_set_route(thy_instrument *instrument, uint8_t side,
    uint8_t channel, uint8_t destinations, int16_t transpose);
/* Independent configurations: target 0 whole board; 1/2 left/right split;
 * 3..5 left and 6..8 right four-column zones. The profile chooses the target
 * from the logical layout, not the electrical ADC ID. Invalid inputs return
 * UINT8_MAX from route_target or THY_INVALID_ARGUMENT from the other APIs.
 * Routing/expression edits affect new attacks only; held and queued strum
 * notes retain their route, velocity, and pressure opt-in through release.
 * Legacy set_route seeds the corresponding split and three zone routes, and
 * the left-side call seeds Whole. Direct channels[]/destinations[] accesses
 * remain the live Split profile values for existing adapters.
 */
uint8_t thy_instrument_route_target(const thy_instrument *instrument, uint8_t logical);
int thy_instrument_get_pad_route(const thy_instrument *instrument, uint8_t target,
    thy_instrument_route *route);
int thy_instrument_set_pad_route(thy_instrument *instrument, uint8_t target,
    uint8_t channel, uint8_t destinations);
int thy_instrument_set_routing_profile(thy_instrument *instrument, uint8_t profile);
/* Explicit per-route expression; unedited routes inherit set_expression. */
int thy_instrument_set_route_expression(thy_instrument *instrument, uint8_t target,
    uint8_t fixed_velocity, uint8_t pressure_enabled);
int thy_instrument_set_route_velocity(thy_instrument *instrument, uint8_t target,
    uint8_t fixed_velocity);
int thy_instrument_set_route_pressure(thy_instrument *instrument, uint8_t target,
    uint8_t pressure_enabled);
/* Select only with all pads released. Resets startup conditioning for all
 * inputs. The factory linear profile uses positive absolute ADC levels
 * >=1377 attack / <=1354 release, recovered for factory word40, b41, curve0.
 * Two consecutive fresh samples confirm either edge; that spike filter is our
 * policy. Baselines never move these gates.
 * Default and explicitly calibrated inputs use relative delta thresholds.
 */
int thy_instrument_set_detector_profile(thy_instrument *instrument, uint8_t profile);
/* Custom response edits are atomic and return THY_INVALID_ARGUMENT for invalid
 * settings or affected pads that are held, blocked, partially attacking, or
 * above either the old or new release gate. They never panic, clear ownership,
 * or increment the runtime error counter. Successful edits require eight new
 * quiet samples before the affected pad can attack. Global edits affect only
 * inherited pads; explicit relative calibration is also an override.
 * NULL in set_pad_response clears that override to the current global response.
 * Explicit raw Trigger/Release pair changes select Manual release. Changes to
 * aftertouch fields alone retain the existing release policy. Use set_trigger
 * for linked editing; detector and aftertouch ranges have independent bounds.
 * set_detector_profile remains the explicit legacy 0/1 reset of ALL overrides.
 * Getters return settings, not inferred calibration or nonvolatile persistence.
 */
int thy_instrument_set_response(thy_instrument *instrument,
    const thy_instrument_response *response);
int thy_instrument_get_response(const thy_instrument *instrument,
    thy_instrument_response *response);
int thy_instrument_set_pad_response(thy_instrument *instrument, uint8_t electrical,
    const thy_instrument_response *response);
int thy_instrument_get_pad_response(const thy_instrument *instrument,
    uint8_t electrical, thy_instrument_response *response, uint8_t *overridden);
uint8_t thy_instrument_response_pressure(const thy_instrument_response *response,
    uint16_t raw);
/* Attack velocity changes affect future presses; existing owner snapshots and
 * pending strums keep their original velocity and pressure opt-in. Unedited
 * per-route expression settings inherit these defaults for future presses. */
int thy_instrument_set_expression(thy_instrument *instrument,
    uint8_t fixed_velocity, uint8_t pressure_enabled);
/* Bounded note-lighting settings consumed by the shared preview/board adapter.
 * Brightness uses the already exercised 0..8 component range; no hardware
 * write is performed here. Settings are currently retained in RAM only. */
int thy_instrument_set_lighting(thy_instrument *instrument, uint8_t enabled,
    uint8_t idle, uint8_t held, uint16_t fade_ms);
/* Call only with a quiet, released pad. Explicit calibration selects relative
 * detection for this pad and bypasses startup baseline capture. Uncalibrated
 * inputs need eight consecutive samples spanning at most32 ADC counts; the
 * factory profile also requires all these readings <=1354. Idle drift follows
 * only small signed excursions symmetrically, never while held. Larger shifts
 * remain visible in telemetry and cannot lower the factory absolute floor.
 */
int thy_instrument_calibrate(thy_instrument *instrument, uint8_t electrical,
    const thy_instrument_calibration *calibration);
/* Supply a fresh valid ADC publication once, not repeated stale report data.
 * Invalid/missing samples are skipped by the adapter, never treated as zero.
 * Fixed mode uses route velocity. Strike mode measures positive ADC rise over
 * recent samples (sub-millisecond intervals quantized to 1ms), without another
 * attack delay. Continuous pressure remains independent and may emit poly AT.
 */
int thy_instrument_sample(thy_instrument *instrument, uint8_t electrical,
    uint16_t raw, uint32_t now_ms);
/* Non-destructive bounded history. Sequences start at1; zero is never stored.
 * Returns1 for a retained entry,0 for absent/overwritten/invalid. The ring is
 * updated only by detector edges and panic blocking, never by this reader.
 * Sequence rollover clears retained history and starts again at1, allowing
 * readers to recognize the epoch change by newest < their previous cursor.
 */
int thy_instrument_get_transition(const thy_instrument *instrument,
    uint32_t sequence, thy_instrument_pad_transition *transition);
int thy_instrument_advance(thy_instrument *instrument, uint32_t now_ms);
/* Banks: 0 navigation5, 1 function14, 2 roots12, 3 shortcuts12. Indices follow
 * docs/physical-control-map.md, not assumed printed center-symbol routing.
 * All edges notify the handler. Only rising edges execute actions. The
 * handler may replace layout synchronously after a root/mode request.
 */
int thy_instrument_control(thy_instrument *instrument, uint8_t bank,
    uint8_t index, int pressed);
int thy_instrument_controls(thy_instrument *instrument, uint8_t bank,
    uint32_t logical_mask);
/* Physical order: 5, 12, M1..M7, U1..U3. Editing a mapping never retunes a
 * sounding or held pad. 0..69 select a family; 70 cycles the melody layout;
 * 71..78 select an existing named custom scale. */
int thy_instrument_set_scale_shortcut(thy_instrument *instrument, uint8_t key,
    uint8_t assignment);
uint8_t thy_instrument_default_scale_shortcut(uint8_t key);
uint8_t thy_instrument_scale_shortcut(const thy_instrument *instrument, uint8_t key);
int thy_instrument_store_user(thy_instrument *instrument, uint8_t slot);
/* Disable at a USB transport epoch boundary before accepting more pads.
 * Disabling panics once. ADC/baseline/release observation continues, but a
 * pad pressed while gated stays blocked until physical release. Enabling
 * therefore cannot create a ghost attack from an already held pad.
 */
int thy_instrument_set_input_enabled(thy_instrument *instrument, int enabled);
void thy_instrument_panic(thy_instrument *instrument);

#endif
