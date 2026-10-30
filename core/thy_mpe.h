/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_MPE_H
#define THY_MPE_H
#include "thy_core.h"

#define THY_MPE_MEMBERS 15u

enum thy_mpe_pressure_output {
    THY_MPE_PRESSURE = 0, THY_MPE_CC74 = 1, THY_MPE_CC1 = 2
};

/* One whole-instrument MIDI 1.0 MPE zone. All channel numbers are zero-based.
 * Configuration is applied only while quiet. These bytes are settings, not a
 * packed persistence format; serialize fields explicitly. */
typedef struct thy_mpe_config {
    uint8_t enabled, upper_zone, members, bend_range, manager_bend_range;
    uint8_t glide, pad_window, semitone_window;
    /* Physical LEFT/RIGHT clusters, indexed by the mapped position / 48.
     * Pressure uses Channel Pressure; the alternatives use member-channel CC.
     * The optional second axis uses CC74 for Pressure, otherwise Pressure. */
    uint8_t pressure_output[2];
} thy_mpe_config;

enum thy_mpe_voice_state {
    THY_MPE_FREE = 0, THY_MPE_HELD = 1, THY_MPE_RETIRING = 2
};

/* Read-only to callers. One chord owns several member channels; its pressure
 * and glide are shared, while every resolved pitch has an independent lease.
 * A retiring lease is unavailable until sustain and ordered reset finish. */
typedef struct thy_mpe_voice {
    uint32_t started;
    uint16_t bend, sent_bend;
    uint8_t pad, logical, reference, pitch, channel, destinations, state;
    uint8_t pressure, sent_pressure, modifier, modifier_pressure;
    uint8_t timbre, sent_timbre;
    uint8_t pressure_output; /* Captured through retiring cleanup. */
    int8_t interval;
} thy_mpe_voice;

typedef struct thy_mpe {
    thy_core *core;
    thy_mpe_config config;
    thy_mpe_voice voices[THY_MPE_MEMBERS];
    uint32_t started;
    uint8_t consumed[THY_PAD_COUNT]; /* source pad + 1; zero means musical. */
    uint8_t owned[THY_PAD_COUNT / 8u]; /* Includes silent musical owners. */
    uint8_t service_cursor;
} thy_mpe;

void thy_mpe_defaults(thy_mpe_config *config);
int thy_mpe_config_valid(const thy_mpe_config *config);
uint8_t thy_mpe_manager(const thy_mpe_config *config);
int thy_mpe_channel_in_zone(const thy_mpe_config *config, uint8_t channel);
void thy_mpe_init(thy_mpe *mpe, thy_core *core);
int thy_mpe_configure(thy_mpe *mpe, const thy_mpe_config *config);

/* notes are final shaped pitches in attack order, already deduplicated. The
 * physical logical index is cluster*48 + column*4 + row, regardless of the
 * musical role assigned to that half by the sequencer. reference is the
 * resolved pitch used to measure the musical glide interval.
 *
 * One free member is required per note, including future strum attacks.
 * Failure never emits a partial chord. Init pressure=0 and bend=center precede
 * every attack. The caller must prevent ordinary/Arp/track voices from entering
 * this zone while enabled. Existing occupied channels are never taken over.
 *
 * Optional glide captures a new same-row neighbor as a modifier of the newest
 * eligible still-held source. Distance and semitone windows are independent.
 * Only one modifier belongs to a source; no re-pairing or automatic handover.
 * A consumed contact stays consumed until release even if its source ends.
 * out_consumed is written only on success: 0 for a musical pad, 1 for modifier.
 */
int thy_mpe_press(thy_mpe *mpe, uint8_t pad, uint8_t logical,
    uint8_t reference, const int16_t *notes, size_t count,
    uint8_t destinations, uint8_t velocity, uint16_t strum_ms,
    int *out_consumed);
/* Explicit initial value for the second pressure stage. It maps to CC74 in
 * Pressure mode and Channel Pressure in CC74/CC1 mode. The ordinary press API
 * initializes it to neutral CC74=64 or Channel Pressure=0, respectively.
 * This is another mapping of one physical sensor, not an independent axis. */
int thy_mpe_press_ex(thy_mpe *mpe, uint8_t pad, uint8_t logical,
    uint8_t reference, const int16_t *notes, size_t count,
    uint8_t destinations, uint8_t velocity, uint16_t strum_ms,
    int *out_consumed, uint8_t initial_timbre);
int thy_mpe_release(thy_mpe *mpe, uint8_t pad);
/* Capture latest desired expression, without queueing MIDI. The foreground
 * calls service at a transport-appropriate cadence. Whole-chord glide uses
 * target/(source+target) pressure; no independent second sensor axis exists. */
int thy_mpe_pressure(thy_mpe *mpe, uint8_t pad, uint8_t pressure);
/* Source contacts update all their member voices. A consumed glide contact
 * does not change its source's timbre; pressure glide remains independent. */
int thy_mpe_timbre(thy_mpe *mpe, uint8_t pad, uint8_t value);
int thy_mpe_bend(thy_mpe *mpe, uint8_t pad, int32_t value);
uint16_t thy_mpe_glide_bend(uint8_t source_pressure, uint8_t target_pressure,
    int8_t interval, uint8_t bend_range);
/* At most two members' ordered batches (up to eighteen events) per call. Full queues
 * retain desired state and retiring cleanup. Release/attack also try retiring
 * cleanup immediately; no channel is reused before neutral resets are queued.
 */
int thy_mpe_service(thy_mpe *mpe);
/* Release this router's physical owners only. Does not clear another source or
 * pedal state; retiring sustain voices remain leased until the pedal is up. */
void thy_mpe_release_all(thy_mpe *mpe);
/* Explicit receiver setup only: disable opposite zone, configure this zone,
 * then set manager/member Pitch Bend Sensitivity. Never called by init/press.
 * Quiet-only and atomic; no MIDI is sent if the complete batch cannot fit. */
int thy_mpe_send_configuration(thy_mpe *mpe, uint8_t destinations);
uint8_t thy_mpe_active(const thy_mpe *mpe);
/* Workspace-only check: includes retiring channels and consumed gestures. */
int thy_mpe_quiet(const thy_mpe *mpe);
int thy_mpe_consumed(const thy_mpe *mpe, uint8_t pad);
const thy_mpe_voice *thy_mpe_voice_at(const thy_mpe *mpe, uint8_t slot);

#endif
