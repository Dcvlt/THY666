/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_instrument.h"

static thy_core core;
static thy_instrument instrument;
static thy_instrument_pad layout[THY_PAD_COUNT], replacement[THY_PAD_COUNT];
static const int16_t major[] = {0, 2, 4, 5, 7, 9, 11};
static unsigned assertions;
static uint32_t now;

#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 0; \
} } while (0)

static void drain(void)
{
    thy_midi_event event;
    while (thy_core_poll(&core, &event) != 0) { }
}

static int take(unsigned destination, unsigned status, unsigned pitch, unsigned value)
{
    thy_midi_event event;
    CHECK(thy_core_poll(&core, &event) != 0);
    CHECK(event.destination == destination);
    CHECK(event.status == status);
    CHECK(event.data1 == pitch);
    CHECK(event.data2 == value);
    return 1;
}

static int initialize(void)
{
    unsigned i;
    const thy_instrument_calibration calibration = {100u, 96u, 32u, 1000u, 1};
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    now = 0u;
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        layout[i] = (thy_instrument_pad){{(int16_t)(i + 12u)}, 1u, (uint8_t)(i % 9u)};
        replacement[i] = layout[i];
        CHECK(thy_instrument_calibrate(&instrument, (uint8_t)i, &calibration) == THY_OK);
    }
    CHECK(thy_instrument_set_layout(&instrument, layout, 60, major, 7u) == THY_OK);
    return 1;
}

static int sample(unsigned electrical, unsigned raw)
{
    return thy_instrument_sample(&instrument, (uint8_t)electrical, (uint16_t)raw, now++);
}

static int test_all_96_owners_all_routes_and_mapping(void)
{
    uint8_t mapping[THY_PAD_COUNT];
    unsigned i;
    CHECK(initialize());
    for (i = 0u; i < THY_PAD_COUNT; ++i) mapping[i] = (uint8_t)(95u - i);
    CHECK(thy_instrument_set_mapping(&instrument, mapping) == THY_OK);
    CHECK(thy_instrument_set_route(&instrument, 0u, 3u, THY_ROUTE_ALL, 0) == THY_OK);
    CHECK(thy_instrument_set_route(&instrument, 1u, 11u, THY_ROUTE_ALL, 0) == THY_OK);
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        unsigned logical = 95u - i, dest;
        CHECK(sample(i, 1500u) == THY_OK);
        CHECK(thy_core_pad_held(&core, (uint8_t)i));
        for (dest = 0u; dest < THY_DEST_COUNT; ++dest)
            CHECK(take(dest, logical >= 48u ? 0x9bu : 0x93u, logical + 12u, 96u));
    }
    CHECK(core.sounding_count == 288u);
    CHECK(instrument.presses == 96u);
    CHECK(thy_instrument_set_mapping(&instrument, mapping) == THY_INVALID_ARGUMENT);
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        unsigned logical = 95u - i, dest;
        CHECK(sample(i, 100u) == THY_OK);
        CHECK(!thy_core_pad_held(&core, (uint8_t)i));
        for (dest = 0u; dest < THY_DEST_COUNT; ++dest)
            CHECK(take(dest, logical >= 48u ? 0x8bu : 0x83u, logical + 12u, 0u));
    }
    CHECK(instrument.releases == 96u);
    CHECK(core.sounding_count == 0u && core.queue_count == 0u && core.pending_keys == 0u);
    CHECK(instrument.errors == 0u);
    return 1;
}

static unsigned callbacks;
static uint32_t callback_rose[4], callback_fell[4];

static void record_action(void *context, const thy_instrument_control_event *event)
{
    unsigned *count = context;
    ++*count;
    if (event->pressed != 0u) callback_rose[event->bank] |= 1u << event->index;
    else callback_fell[event->bank] |= 1u << event->index;
}

static int tap(unsigned bank, unsigned index)
{
    CHECK(thy_instrument_control(&instrument, (uint8_t)bank, (uint8_t)index, 1) == THY_OK);
    CHECK(thy_instrument_control(&instrument, (uint8_t)bank, (uint8_t)index, 0) == THY_OK);
    return 1;
}

static int test_every_control_edge_and_policy(void)
{
    static const unsigned counts[4] = {5u, 14u, 12u, 12u};
    unsigned bank, index;
    CHECK(initialize());
    callbacks = 0u;
    for (bank = 0u; bank < 4u; ++bank) callback_rose[bank] = callback_fell[bank] = 0u;
    thy_instrument_set_action_handler(&instrument, record_action, &callbacks);
    for (bank = 0u; bank < 4u; ++bank) {
        for (index = 0u; index < counts[bank]; ++index) CHECK(tap(bank, index));
        CHECK(callback_rose[bank] == (1u << counts[bank]) - 1u);
        CHECK(callback_fell[bank] == callback_rose[bank]);
        CHECK(instrument.controls_seen[bank] == callback_rose[bank]);
    }
    CHECK(callbacks == 86u && instrument.last_event.sequence == 86u);
    CHECK(instrument.performance.root == 11u);
    CHECK(instrument.performance.shortcut == 11u);
    CHECK(instrument.performance.melody_layout == 1u);
    CHECK(instrument.layout_revision == 24u);
    CHECK(thy_instrument_control(&instrument, 2u, 5u, 1) == THY_OK);
    CHECK(thy_instrument_control(&instrument, 2u, 5u, 1) == THY_OK);
    CHECK(callbacks == 87u); /* No duplicate action for a repeated publication. */
    CHECK(thy_instrument_controls(&instrument, 2u, 0u) == THY_OK);
    CHECK(callbacks == 88u);
    CHECK(thy_instrument_controls(&instrument, 0u, 0x20u) == THY_INVALID_ARGUMENT);
    return 1;
}

static int test_idle_drift_noise_and_held_baseline(void)
{
    unsigned step, tick;
    uint16_t baseline;
    CHECK(initialize());
    /* A slow 250-count drift with noise should be followed only while idle. */
    for (step = 0u; step < 250u; ++step) {
        for (tick = 0u; tick < 128u; ++tick) {
            unsigned raw = 100u + step + tick % 5u;
            CHECK(sample(0u, raw) == THY_OK);
        }
    }
    CHECK(instrument.presses == 0u && core.queue_count == 0u);
    baseline = instrument.sensors[0].calibration.baseline;
    CHECK(baseline >= 347u && baseline <= 353u);
    CHECK(sample(0u, 1000u) == THY_OK);
    drain();
    for (tick = 0u; tick < 1000u; ++tick) CHECK(sample(0u, 3000u + tick % 500u) == THY_OK);
    CHECK(instrument.sensors[0].calibration.baseline == baseline);
    CHECK(instrument.presses == 1u && instrument.sensors[0].held != 0u);
    CHECK(sample(0u, baseline + 32u) == THY_OK);
    CHECK(instrument.releases == 1u && core.sounding_count == 0u);
    drain();
    return 1;
}

static int test_warmup_and_hysteresis_pressure(void)
{
    unsigned i;
    const thy_instrument_calibration calibration = {100u, 96u, 32u, 1000u, 1};
    CHECK(initialize());
    thy_instrument_init(&instrument, &core);
    CHECK(thy_instrument_set_layout(&instrument, layout, 60, major, 7u) == THY_OK);
    for (i = 0u; i < 8u; ++i) CHECK(sample(0u, 2000u) == THY_OK);
    CHECK(instrument.presses == 0u);
    CHECK(instrument.sensors[0].calibration.baseline == 2000u);
    CHECK(thy_instrument_calibrate(&instrument, 0u, &calibration) == THY_OK);
    instrument.pressure_enabled = 1u;
    CHECK(sample(0u, 195u) == THY_OK); /* Below press threshold. */
    CHECK(core.sounding_count == 0u);
    CHECK(sample(0u, 196u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 96u));
    CHECK(sample(0u, 150u) == THY_OK); /* Above release, below press: held. */
    CHECK(instrument.sensors[0].held != 0u);
    CHECK(take(THY_USB0, 0xa0u, 12u, 6u));
    CHECK(sample(0u, 4095u) == THY_OK);
    CHECK(take(THY_USB0, 0xa0u, 12u, 127u));
    CHECK(instrument.sensors[0].pressure == 127u);
    CHECK(thy_instrument_calibrate(&instrument, 0u, &calibration) == THY_INVALID_ARGUMENT);
    CHECK(sample(0u, 132u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 12u, 0u));
    CHECK(instrument.sensors[0].held == 0u);
    return 1;
}

static void replace_for_root(void *context, const thy_instrument_control_event *event)
{
    thy_instrument *state = context;
    if (event->bank == 2u && event->pressed != 0u)
        (void)thy_instrument_set_layout(state, replacement, 72, major, 7u);
}

static int test_held_routes_root_layout_pressure_and_release(void)
{
    CHECK(initialize());
    layout[0].notes[0] = 60;
    replacement[0].notes[0] = 77;
    replacement[1].notes[0] = 79;
    instrument.pressure_enabled = 1u;
    thy_instrument_set_action_handler(&instrument, replace_for_root, &instrument);
    CHECK(sample(0u, 600u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 60u, 96u));
    CHECK(tap(2u, 9u));
    CHECK(thy_instrument_set_route(&instrument, 0u, 7u, THY_ROUTE_DIN, 0) == THY_OK);
    CHECK(sample(0u, 1100u) == THY_OK);
    CHECK(take(THY_USB0, 0xa0u, 60u, 127u));
    CHECK(sample(1u, 600u) == THY_OK);
    CHECK(take(THY_DIN, 0x97u, 79u, 96u));
    CHECK(sample(0u, 100u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 60u, 0u));
    CHECK(sample(1u, 100u) == THY_OK);
    CHECK(take(THY_DIN, 0x87u, 79u, 0u));
    CHECK(core.sounding_count == 0u && core.queue_count == 0u);
    return 1;
}

static int test_original_slot_holes_note_zero_and_edits(void)
{
    CHECK(initialize());
    CHECK(thy_instrument_set_lower_mode(&instrument, THY_LOWER_STORED_NOTES) == THY_OK);
    instrument.slot_scope = THY_SLOTS_SELECTED_PAD;
    layout[0] = (thy_instrument_pad){{0, -999, 24, 127, -1, -1, -1}, 0x0du, 3u};
    CHECK(thy_instrument_set_layout(&instrument, layout, 60, major, 7u) == THY_OK);
    CHECK(sample(0u, 600u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 0u, 96u));
    CHECK(take(THY_USB0, 0x90u, 24u, 96u));
    CHECK(take(THY_USB0, 0x90u, 127u, 96u));
    CHECK(tap(1u, 7u)); /* Disable original slot 0 for the next press only. */
    CHECK(core.sounding_count == 3u);
    CHECK(sample(0u, 100u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 0u, 0u));
    CHECK(take(THY_USB0, 0x80u, 24u, 0u));
    CHECK(take(THY_USB0, 0x80u, 127u, 0u));
    CHECK(sample(0u, 600u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 24u, 96u));
    CHECK(take(THY_USB0, 0x90u, 127u, 96u));
    CHECK(sample(0u, 100u) == THY_OK);
    drain();
    CHECK(tap(1u, 8u)); /* Inactive original hole cannot be enabled by editing. */
    CHECK(instrument.excluded_slots[0] == 1u);
    return 1;
}

static int test_generated_full_melody_and_strum_cancel(void)
{
    unsigned i;
    CHECK(initialize());
    for (i = 80u; i < THY_PAD_COUNT; ++i) layout[i].valid_mask = 0u;
    CHECK(tap(3u, 1u)); /* Stored->chromatic activates all 48 generated cells. */
    CHECK(sample(95u, 600u) == THY_OK);
    CHECK(take(THY_USB0, 0x91u, 35u, 96u)); /* 60 + 11 - 3*12. */
    CHECK(sample(95u, 100u) == THY_OK);
    drain();
    layout[0] = (thy_instrument_pad){{60, 64, 67}, 7u, 1u};
    CHECK(tap(1u, 3u));
    CHECK(sample(0u, 600u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 60u, 96u));
    CHECK(core.pending_keys == 2u);
    CHECK(sample(0u, 100u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 60u, 0u));
    CHECK(thy_instrument_advance(&instrument, now + 2000u) == THY_OK);
    CHECK(core.pending_keys == 0u && core.queue_count == 0u);
    return 1;
}

static int test_panic_blocks_held_until_release(void)
{
    CHECK(initialize());
    CHECK(sample(0u, 600u) == THY_OK);
    drain();
    thy_instrument_panic(&instrument);
    CHECK(take(THY_USB0, 0x80u, 12u, 0u));
    CHECK(sample(0u, 700u) == THY_OK);
    CHECK(core.queue_count == 0u && core.sounding_count == 0u);
    CHECK(sample(0u, 100u) == THY_OK);
    CHECK(sample(0u, 700u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 96u));
    CHECK(sample(0u, 100u) == THY_OK);
    drain();
    return 1;
}

static int test_bad_press_does_not_attack_after_settings_change(void)
{
    CHECK(initialize());
    /* Legacy shaping rejects the entire out-of-range chord; octave-layer
     * copy clipping has its own regression in test_lower_controls.c. */
    CHECK(thy_instrument_set_lower_mode(&instrument, THY_LOWER_STORED_NOTES) == THY_OK);
    CHECK(thy_instrument_set_route(&instrument, 0u, 0u, THY_ROUTE_USB0, 127) == THY_OK);
    CHECK(sample(0u, 600u) == THY_INVALID_ARGUMENT);
    CHECK(core.queue_count == 0u && instrument.sensors[0].blocked != 0u);
    CHECK(thy_instrument_set_route(&instrument, 0u, 0u, THY_ROUTE_USB0, 0) == THY_OK);
    CHECK(sample(0u, 700u) == THY_OK);
    CHECK(core.queue_count == 0u);
    CHECK(sample(0u, 100u) == THY_OK);
    CHECK(sample(0u, 700u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 96u));
    CHECK(sample(0u, 100u) == THY_OK);
    drain();
    return 1;
}

static int test_negative_polarity_invalid_atomicity_and_presets(void)
{
    const thy_instrument_calibration calibration = {3000u, 100u, 40u, 1000u, -1};
    uint8_t bad_mapping[THY_PAD_COUNT];
    unsigned i;
    CHECK(initialize());
    CHECK(thy_instrument_calibrate(&instrument, 0u, &calibration) == THY_OK);
    CHECK(sample(0u, 2800u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 96u));
    CHECK(sample(0u, 2980u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 12u, 0u));
    for (i = 0u; i < THY_PAD_COUNT; ++i) bad_mapping[i] = 0u;
    CHECK(thy_instrument_set_mapping(&instrument, bad_mapping) == THY_INVALID_ARGUMENT);
    CHECK(instrument.mapping[95] == 95u);
    replacement[8].valid_mask = 1u;
    replacement[8].notes[0] = 128;
    CHECK(thy_instrument_set_layout(&instrument, replacement, 60, major, 7u) == THY_INVALID_ARGUMENT);
    CHECK(instrument.layout == layout);
    CHECK(thy_instrument_sample(&instrument, 0u, 4096u, now) == THY_INVALID_ARGUMENT);
    CHECK(tap(2u, 7u));
    CHECK(thy_instrument_store_user(&instrument, 0u) == THY_OK);
    CHECK(tap(2u, 1u));
    CHECK(tap(3u, 9u));
    CHECK(instrument.users[0].root == 7u); /* Legacy snapshots remain readable. */
    CHECK(instrument.performance.root == 1u); /* U1 now selects a scale, not a snapshot. */
    CHECK(instrument.performance.shortcut == 9u);
    CHECK(instrument.last_event.action == THY_ACTION_SHORTCUT);
    return 1;
}

static int test_scale_shortcut_defaults_validation_and_held_ownership(void)
{
    static const uint8_t defaults[12] = {56u,70u,21u,22u,23u,24u,25u,26u,27u,14u,35u,60u};
    CHECK(initialize());
    for (uint8_t key = 0u; key < 12u; ++key) {
        CHECK(thy_instrument_scale_shortcut(&instrument,key) == defaults[key]);
        CHECK(thy_instrument_default_scale_shortcut(key) == defaults[key]);
    }
    CHECK(thy_instrument_scale_shortcut(NULL,0u) == UINT8_MAX);
    CHECK(thy_instrument_scale_shortcut(&instrument,12u) == UINT8_MAX);
    CHECK(thy_instrument_default_scale_shortcut(12u) == UINT8_MAX);
    uint32_t revision = instrument.revision, layout_revision = instrument.layout_revision;
    CHECK(thy_instrument_set_scale_shortcut(&instrument,12u,0u) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_scale_shortcut(&instrument,0u,71u) == THY_INVALID_ARGUMENT);
    CHECK(instrument.revision == revision && instrument.layout_revision == layout_revision);
    CHECK(sample(0u,700u) == THY_OK);
    CHECK(take(THY_USB0,0x90u,12u,96u));
    thy_pad_owner held = core.owners[0];
    for (uint8_t family = 0u; family < 70u; ++family) {
        CHECK(thy_instrument_set_scale_shortcut(&instrument,9u,family) == THY_OK);
        CHECK(thy_instrument_scale_shortcut(&instrument,9u) == family);
        CHECK(instrument.layout_revision == layout_revision);
        CHECK(memcmp(&held,&core.owners[0],sizeof held) == 0);
    }
    CHECK(thy_instrument_set_scale_shortcut(&instrument,9u,THY_SCALE_SHORTCUT_LAYOUT) == THY_OK);
    CHECK(tap(3u,9u));
    CHECK(instrument.performance.melody_layout == 1u);
    CHECK(instrument.engine_kind == THY_PLAY_ENGINE_PLAY);
    CHECK(tap(3u,9u));
    CHECK(instrument.performance.melody_layout == THY_LAYOUT_THIRDS);
    CHECK(instrument.engine_kind == THY_PLAY_ENGINE_PLAY);
    CHECK(memcmp(&held,&core.owners[0],sizeof held) == 0 && !core.queue_count);
    CHECK(sample(0u,100u) == THY_OK);
    CHECK(take(THY_USB0,0x80u,12u,0u));
    CHECK(!core.sounding_count);
    return 1;
}

static int test_transport_gating_no_reconnect_ghosts(void)
{
    CHECK(initialize());
    CHECK(instrument.input_enabled == 1u);
    CHECK(thy_instrument_set_input_enabled(&instrument, 0) == THY_OK);
    CHECK(sample(0u, 600u) == THY_OK);
    CHECK(instrument.sensors[0].held != 0u && instrument.sensors[0].blocked != 0u);
    CHECK(core.queue_count == 0u && core.sounding_count == 0u);
    CHECK(thy_instrument_set_input_enabled(&instrument, 1) == THY_OK);
    CHECK(sample(0u, 700u) == THY_OK);
    CHECK(core.queue_count == 0u && core.sounding_count == 0u);
    CHECK(sample(0u, 100u) == THY_OK);
    CHECK(sample(0u, 700u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 96u));
    CHECK(thy_instrument_set_input_enabled(&instrument, 0) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 12u, 0u));
    CHECK(thy_instrument_set_input_enabled(&instrument, 0) == THY_OK);
    CHECK(core.queue_count == 0u);
    CHECK(thy_instrument_set_input_enabled(&instrument, 1) == THY_OK);
    CHECK(sample(0u, 800u) == THY_OK);
    CHECK(core.queue_count == 0u && core.sounding_count == 0u);
    CHECK(sample(0u, 100u) == THY_OK);
    CHECK(sample(0u, 800u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 96u));
    CHECK(sample(0u, 100u) == THY_OK);
    drain();
    CHECK(thy_instrument_set_input_enabled(&instrument, 2) == THY_INVALID_ARGUMENT);
    return 1;
}

static int initialize_factory(void)
{
    unsigned round, pad;
    CHECK(initialize());
    CHECK(thy_instrument_set_detector_profile(&instrument,
        THY_DETECTOR_FACTORY_LINEAR) == THY_OK);
    for (round = 0u; round < THY_INSTRUMENT_WARMUP_SAMPLES; ++round)
        for (pad = 0u; pad < THY_PAD_COUNT; ++pad)
            CHECK(sample(pad, pad == 2u ? 575u : 4u) == THY_OK);
    CHECK(instrument.presses == 0u && core.queue_count == 0u);
    return 1;
}

static int test_factory_floor_coupling_and_residual_release(void)
{
    unsigned trial, tick;
    CHECK(initialize_factory());
    /* Reproduce six deliberate actions alongside the separately observed
     * adjacent183-count signal and575->511 negative background excursion.
     * They must not create extra owners or residual holds. */
    for (trial = 0u; trial < 6u; ++trial) {
        unsigned electrical = trial < 3u ? 0u : 48u;
        unsigned channel = trial < 3u ? 0x90u : 0x91u;
        for (tick = 0u; tick < 100u; ++tick) {
            CHECK(sample(electrical, 3766u) == THY_OK);
            CHECK(sample(1u, 183u) == THY_OK);
            CHECK(sample(2u, 511u) == THY_OK);
        }
        CHECK(take(THY_USB0, channel, electrical + 12u, 96u));
        CHECK(core.queue_count == 0u && core.sounding_count == 1u);
        CHECK(sample(electrical, 183u) == THY_OK);
        CHECK(sample(electrical, 183u) == THY_OK);
        CHECK(take(THY_USB0, channel - 0x10u, electrical + 12u, 0u));
        CHECK(sample(1u, 4u) == THY_OK);
        CHECK(sample(2u, 575u) == THY_OK);
        CHECK(core.queue_count == 0u && core.sounding_count == 0u);
    }
    CHECK(instrument.presses == 6u && instrument.releases == 6u);
    CHECK(instrument.sensors[1].held == 0u && instrument.sensors[2].held == 0u);
    CHECK(instrument.sensors[0].attack_raw == 1377);
    CHECK(instrument.sensors[0].release_raw == 1354);
    return 1;
}

static int test_factory_gate_boundaries_and_spike_filter(void)
{
    unsigned tick;
    CHECK(initialize_factory());
    for (tick = 0u; tick < 100u; ++tick) CHECK(sample(0u, 1376u) == THY_OK);
    CHECK(core.queue_count == 0u && instrument.presses == 0u);
    CHECK(sample(0u, 1377u) == THY_OK);
    CHECK(instrument.sensors[0].attack_count == 1u && core.queue_count == 0u);
    CHECK(sample(0u, 1376u) == THY_OK);
    CHECK(instrument.sensors[0].attack_count == 0u);
    CHECK(sample(0u, 1377u) == THY_OK);
    CHECK(sample(0u, 1377u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 96u));
    CHECK(sample(0u, 1354u) == THY_OK); /* One release spike cannot cut a hold. */
    CHECK(sample(0u, 1355u) == THY_OK);
    CHECK(instrument.sensors[0].held != 0u && core.queue_count == 0u);
    for (tick = 0u; tick < 100u; ++tick) CHECK(sample(0u, 1355u) == THY_OK);
    CHECK(sample(0u, 1354u) == THY_OK);
    CHECK(sample(0u, 1354u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 12u, 0u));
    CHECK(instrument.sensors[0].delta > 32u); /* Absolute release, not tiny offset. */
    CHECK(instrument.presses == 1u && instrument.releases == 1u);
    return 1;
}

static int test_symmetric_drift_negative_excursion_and_rebound(void)
{
    const thy_instrument_calibration quiet = {575u, 96u, 32u, 1000u, 1};
    unsigned tick;
    int32_t upper, lower;
    CHECK(initialize());
    CHECK(thy_instrument_calibrate(&instrument, 0u, &quiet) == THY_OK);
    for (tick = 0u; tick < 2000u; ++tick) CHECK(sample(0u, 0u) == THY_OK);
    CHECK(instrument.sensors[0].calibration.baseline == 575u);
    for (tick = 0u; tick < 50u; ++tick) CHECK(sample(0u, 575u) == THY_OK);
    CHECK(instrument.presses == 0u && core.queue_count == 0u);
    CHECK(sample(0u, 585u) == THY_OK);
    upper = instrument.sensors[0].baseline_q8 - 575 * 256;
    CHECK(thy_instrument_calibrate(&instrument, 0u, &quiet) == THY_OK);
    CHECK(sample(0u, 565u) == THY_OK);
    lower = 575 * 256 - instrument.sensors[0].baseline_q8;
    CHECK(upper == lower && upper > 0);
    /* The same negative excursion/rebound in absolute mode remains quiet,
     * with its signed evidence preserved rather than clamped in telemetry. */
    CHECK(initialize_factory());
    for (tick = 0u; tick < 2000u; ++tick) CHECK(sample(2u, 0u) == THY_OK);
    CHECK(instrument.sensors[2].signed_delta == -575);
    CHECK(instrument.sensors[2].calibration.baseline == 575u);
    for (tick = 0u; tick < 200u; ++tick) CHECK(sample(2u, 575u) == THY_OK);
    CHECK(instrument.presses == 0u && instrument.sensors[2].held == 0u);
    return 1;
}

static int test_factory_startup_stability_and_held_boot(void)
{
    unsigned tick;
    CHECK(initialize());
    CHECK(thy_instrument_set_detector_profile(&instrument,
        THY_DETECTOR_FACTORY_LINEAR) == THY_OK);
    for (tick = 0u; tick < 40u; ++tick)
        CHECK(sample(0u, tick % 2u != 0u ? 500u : 4u) == THY_OK);
    CHECK(instrument.sensors[0].warmup_count == 1u);
    for (tick = 0u; tick < 50u; ++tick) CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(instrument.sensors[0].warmup_count == 0u);
    CHECK(instrument.sensors[0].blocked != 0u && instrument.presses == 0u);
    for (tick = 0u; tick < 7u; ++tick) CHECK(sample(0u, 4u) == THY_OK);
    CHECK(instrument.sensors[0].warmup_count == 7u);
    CHECK(sample(0u, 3766u) == THY_OK); /* Late transient restarts qualification. */
    CHECK(instrument.sensors[0].warmup_count == 0u);
    for (tick = 0u; tick < 8u; ++tick) CHECK(sample(0u, 4u + tick % 3u) == THY_OK);
    CHECK(instrument.sensors[0].warmup_count == 8u);
    CHECK(instrument.sensors[0].blocked == 0u && instrument.presses == 0u);
    CHECK(instrument.sensors[0].calibration.baseline == 5u);
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 96u));
    CHECK(sample(0u, 4u) == THY_OK);
    CHECK(sample(0u, 4u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 12u, 0u));
    return 1;
}

static int test_factory_sustained_ownership_gating_and_pressure(void)
{
    unsigned tick;
    CHECK(initialize_factory());
    instrument.pressure_enabled = 1u;
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 96u));
    CHECK(thy_instrument_set_detector_profile(&instrument, THY_DETECTOR_RELATIVE) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_route(&instrument, 0u, 7u, THY_ROUTE_DIN, 12) == THY_OK);
    replacement[0].notes[0] = 77;
    CHECK(thy_instrument_set_layout(&instrument, replacement, 72, major, 7u) == THY_OK);
    CHECK(sample(0u, 4095u) == THY_OK);
    CHECK(take(THY_USB0, 0xa0u, 12u, 127u));
    instrument.pressure_enabled = 0u;
    /* Global expression changes affect the next attack. This owner opted in
     * when pressed, so its one changed pressure level still uses USB ch1. */
    CHECK(sample(0u, 2000u) == THY_OK);
    CHECK(take(THY_USB0, 0xa0u, 12u,
        thy_instrument_response_pressure(&instrument.response, 2000u)));
    for (tick = 0u; tick < 10000u; ++tick) {
        now += 1000u;
        CHECK(sample(0u, 2000u) == THY_OK);
    }
    CHECK(instrument.sensors[0].calibration.baseline == 4u);
    CHECK(core.sounding_count == 1u && core.queue_count == 0u);
    CHECK(sample(0u, 800u) == THY_OK);
    CHECK(sample(0u, 800u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 12u, 0u)); /* Original USB route/note snapshot. */
    CHECK(thy_instrument_set_input_enabled(&instrument, 0) == THY_OK);
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(instrument.sensors[0].held != 0u && instrument.sensors[0].blocked != 0u);
    CHECK(core.queue_count == 0u);
    CHECK(thy_instrument_set_input_enabled(&instrument, 1) == THY_OK);
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(core.sounding_count == 0u);
    CHECK(sample(0u, 800u) == THY_OK);
    CHECK(sample(0u, 800u) == THY_OK);
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(take(THY_DIN, 0x97u, 89u, 96u));
    thy_instrument_panic(&instrument);
    CHECK(take(THY_DIN, 0x87u, 89u, 0u));
    CHECK(instrument.sensors[0].held != 0u && instrument.sensors[0].blocked != 0u);
    CHECK(sample(0u, 4u) == THY_OK);
    CHECK(sample(0u, 4u) == THY_OK);
    CHECK(core.sounding_count == 0u && instrument.sensors[0].held == 0u);
    return 1;
}

static int test_transition_telemetry_readonly_bounds_and_profile_atomicity(void)
{
    thy_instrument_pad_transition event, before;
    uint32_t first_at;
    unsigned trial;
    CHECK(initialize_factory());
    CHECK(!thy_instrument_get_transition(&instrument, 0u, &event));
    CHECK(sample(0u, 3766u) == THY_OK);
    first_at = now;
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(instrument.transition_sequence == 1u && instrument.transition_count == 1u);
    CHECK(thy_instrument_get_transition(&instrument, 1u, &event));
    CHECK(event.sequence == 1u && event.at_ms == first_at);
    CHECK(event.electrical == 0u && event.logical == 0u);
    CHECK(event.action == THY_PAD_TRANSITION_PRESS && event.blocked == 0u);
    CHECK(event.raw == 3766u && event.baseline == 4u && event.delta == 3762u);
    CHECK(event.signed_delta == 3762 && event.attack_raw == 1377 && event.release_raw == 1354);
    CHECK(event.detector_profile == THY_DETECTOR_FACTORY_LINEAR);
    CHECK(instrument.sensors[0].last_sample_ms == first_at);
    CHECK(instrument.sensors[0].last_transition_ms == first_at);
    before = event;
    CHECK(thy_instrument_get_transition(&instrument, 1u, &event));
    CHECK(event.sequence == before.sequence && instrument.transition_count == 1u);
    CHECK(thy_instrument_set_detector_profile(&instrument, 2u) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_detector_profile(&instrument, THY_DETECTOR_RELATIVE) == THY_INVALID_ARGUMENT);
    CHECK(instrument.sensors[95].detector_profile == THY_DETECTOR_FACTORY_LINEAR);
    thy_instrument_panic(&instrument);
    CHECK(thy_instrument_get_transition(&instrument, 2u, &event));
    CHECK(event.action == THY_PAD_TRANSITION_BLOCK && event.blocked == 1u);
    drain();
    CHECK(sample(0u, 183u) == THY_OK);
    CHECK(sample(0u, 183u) == THY_OK);
    CHECK(thy_instrument_get_transition(&instrument, 3u, &event));
    CHECK(event.action == THY_PAD_TRANSITION_RELEASE && event.blocked == 0u);
    for (trial = 0u; trial < 33u; ++trial) {
        CHECK(sample(0u, 3766u) == THY_OK);
        CHECK(sample(0u, 3766u) == THY_OK);
        drain();
        CHECK(sample(0u, 183u) == THY_OK);
        CHECK(sample(0u, 183u) == THY_OK);
        drain();
    }
    CHECK(instrument.transition_sequence == 69u);
    CHECK(instrument.transition_count == 64u && instrument.transition_overwritten == 5u);
    CHECK(!thy_instrument_get_transition(&instrument, 5u, &event));
    CHECK(thy_instrument_get_transition(&instrument, 6u, &event));
    CHECK(!thy_instrument_get_transition(&instrument, 70u, &event));
    CHECK(!thy_instrument_get_transition(&instrument, 69u, NULL));
    instrument.transition_sequence = UINT32_MAX;
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(sample(0u, 3766u) == THY_OK);
    CHECK(instrument.transition_sequence == 1u && instrument.transition_count == 1u);
    CHECK(thy_instrument_get_transition(&instrument, 1u, &event));
    CHECK(!thy_instrument_get_transition(&instrument, UINT32_MAX, &event));
    drain();
    CHECK(sample(0u, 4u) == THY_OK);
    CHECK(sample(0u, 4u) == THY_OK);
    drain();
    {
        const thy_instrument_calibration explicit = {4u, 96u, 32u, 1000u, 1};
        CHECK(thy_instrument_calibrate(&instrument, 0u, &explicit) == THY_OK);
        CHECK(instrument.sensors[0].detector_profile == THY_DETECTOR_RELATIVE);
        CHECK(instrument.sensors[0].attack_raw == 100 && instrument.sensors[0].release_raw == 36);
        CHECK(sample(0u, 100u) == THY_OK); /* Explicit calibration keeps one-sample API. */
        CHECK(take(THY_USB0, 0x90u, 12u, 96u));
    }
    return 1;
}

static int qualify(unsigned electrical, unsigned raw)
{
    unsigned tick;
    for (tick = 0u; tick < THY_INSTRUMENT_WARMUP_SAMPLES; ++tick)
        CHECK(sample(electrical, raw) == THY_OK);
    return 1;
}

static int test_custom_response_validation_and_atomic_busy_edits(void)
{
    const thy_instrument_response custom = {1200u, 1100u, 1000u, 3500u, THY_PRESSURE_LINEAR};
    thy_instrument_response bad, read;
    uint32_t revision, errors;
    unsigned trial;
    CHECK(initialize_factory());
    CHECK(thy_instrument_get_response(&instrument, &read) == THY_OK);
    CHECK(read.attack_raw == 1377u && read.release_raw == 1354u);
    revision = instrument.revision;
    errors = instrument.errors;
    for (trial = 0u; trial < 6u; ++trial) {
        bad = custom;
        if (trial == 0u) bad.release_raw = bad.attack_raw;
        if (trial == 1u) bad.pressure_floor = 3500u;
        if (trial == 2u) bad.pressure_full_scale = 999u;
        if (trial == 3u) bad.pressure_full_scale = 4096u;
        if (trial == 4u) bad.pressure_curve = 3u;
        if (trial == 5u) bad.attack_raw = 0u;
        CHECK(thy_instrument_set_response(&instrument, &bad) == THY_INVALID_ARGUMENT);
        CHECK(instrument.response.attack_raw == 1377u);
        CHECK(instrument.revision == revision && instrument.errors == errors);
    }
    CHECK(thy_instrument_set_response(&instrument, NULL) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_get_response(NULL, &read) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_get_response(&instrument, NULL) == THY_INVALID_ARGUMENT);
    CHECK(sample(95u, 3766u) == THY_OK);
    CHECK(sample(95u, 3766u) == THY_OK);
    CHECK(take(THY_USB0, 0x91u, 107u, 96u));
    revision = instrument.revision;
    CHECK(thy_instrument_set_response(&instrument, &custom) == THY_INVALID_ARGUMENT);
    CHECK(instrument.menu_notice == 1u && instrument.errors == errors);
    CHECK(instrument.revision == revision && instrument.response.attack_raw == 1377u);
    CHECK(instrument.sensors[0].warmup_count == 8u);
    CHECK(instrument.sensors[0].detector_profile == THY_DETECTOR_FACTORY_LINEAR);
    CHECK(core.sounding_count == 1u && instrument.sensors[95].held != 0u);
    CHECK(sample(95u, 4u) == THY_OK);
    CHECK(sample(95u, 4u) == THY_OK);
    CHECK(take(THY_USB0, 0x81u, 107u, 0u));
    CHECK(thy_instrument_set_response(&instrument, &custom) == THY_OK);
    CHECK(instrument.menu_notice == 0u && instrument.sensors[0].warmup_count == 0u);
    CHECK(instrument.sensors[95].detector_profile == THY_DETECTOR_CUSTOM_ABSOLUTE);
    CHECK(instrument.response.pressure_full_scale == 3500u);
    return 1;
}

static int test_linked_release_and_independent_aftertouch(void)
{
    thy_core_init(&core);thy_instrument_init(&instrument,&core);
    thy_instrument_release_policy policy;uint8_t overridden;
    CHECK(thy_instrument_get_release_policy(&instrument,-1,&policy,&overridden)==THY_OK);
    CHECK(policy.linked && policy.gap==23u && !overridden);
    CHECK(thy_instrument_set_trigger(&instrument,-1,1300u)==THY_OK);
    CHECK(instrument.response.attack_raw==1300u && instrument.response.release_raw==1277u);
    CHECK(instrument.response.pressure_floor==1353u && instrument.response.pressure_full_scale==4095u);
    thy_instrument_response custom={1200u,900u,1700u,2500u,THY_PRESSURE_LINEAR};
    CHECK(thy_instrument_set_pad_response(&instrument,0u,&custom)==THY_OK);
    CHECK(thy_instrument_get_release_policy(&instrument,0,&policy,&overridden)==THY_OK);
    CHECK(!policy.linked && policy.gap==300u && overridden);
    policy=(thy_instrument_release_policy){50u,1u};
    CHECK(thy_instrument_set_release_policy(&instrument,0,&policy)==THY_OK);
    CHECK(instrument.sensors[0].response.release_raw==1150u);
    CHECK(thy_instrument_set_trigger(&instrument,0,1000u)==THY_OK);
    CHECK(instrument.sensors[0].response.release_raw==950u && instrument.sensors[0].response.pressure_floor==1700u);
    custom=instrument.sensors[0].response;custom.pressure_curve=THY_PRESSURE_SOFT;
    CHECK(thy_instrument_set_pad_response(&instrument,0u,&custom)==THY_OK);
    CHECK(instrument.pad_release_policy[0].linked && instrument.pad_release_policy[0].gap==50u);
    CHECK(thy_instrument_set_trigger(&instrument,-1,1400u)==THY_OK);
    CHECK(instrument.response.release_raw==1377u && instrument.sensors[1].response.attack_raw==1400u);
    CHECK(instrument.sensors[0].response.attack_raw==1000u && instrument.pad_release_policy[0].gap==50u);
    thy_instrument_response pressure=instrument.response;pressure.pressure_floor=2000u;pressure.pressure_full_scale=3000u;
    CHECK(thy_instrument_set_response(&instrument,&pressure)==THY_OK);
    CHECK(instrument.release_policy.linked && instrument.release_policy.gap==23u);
    CHECK(thy_instrument_response_pressure(&pressure,2000u)==0u && thy_instrument_response_pressure(&pressure,3000u)==127u);
    CHECK(thy_instrument_set_pad_response(&instrument,0u,NULL)==THY_OK);
    CHECK(!instrument.sensors[0].response_overridden && instrument.pad_release_policy[0].linked && instrument.pad_release_policy[0].gap==23u);
    policy=(thy_instrument_release_policy){23u,0u};
    CHECK(thy_instrument_set_release_policy(&instrument,-1,&policy)==THY_OK);
    uint32_t revision=instrument.revision;
    CHECK(thy_instrument_set_trigger(&instrument,-1,1300u)==THY_INVALID_ARGUMENT);
    CHECK(instrument.response.attack_raw==1400u && instrument.response.release_raw==1377u && instrument.revision==revision);
    pressure=(thy_instrument_response){1300u,1200u,2000u,3000u,THY_PRESSURE_FIRM};
    CHECK(thy_instrument_set_response(&instrument,&pressure)==THY_OK);
    CHECK(!instrument.release_policy.linked && instrument.release_policy.gap==100u);
    CHECK(instrument.response.attack_raw==1300u && instrument.response.release_raw==1200u);
    CHECK(thy_instrument_reset_sensitivity(&instrument,-1)==THY_OK);
    CHECK(instrument.release_policy.linked && instrument.release_policy.gap==23u && instrument.response.attack_raw==1377u);
    CHECK(thy_instrument_set_trigger(&instrument,-1,1u)==THY_OK);
    CHECK(!instrument.response.release_raw && instrument.release_policy.gap==23u);
    CHECK(!thy_core_queued(&core) && !thy_core_sounding(&core));
    return 1;
}

static int test_release_policy_edits_reject_busy_without_partial_publish(void)
{
    CHECK(initialize_factory());
    CHECK(sample(0u,2000u)==THY_OK && sample(0u,2000u)==THY_OK);drain();
    const thy_pad_owner owner=core.owners[0];
    const thy_instrument_response response=instrument.response;
    const thy_instrument_release_policy policy=instrument.release_policy;
    const uint32_t revision=instrument.revision;
    thy_instrument_release_policy linked={50u,1u};
    CHECK(thy_instrument_set_release_policy(&instrument,-1,&linked)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_trigger(&instrument,-1,1300u)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&response,&instrument.response,sizeof response) && instrument.release_policy.gap==policy.gap && instrument.release_policy.linked==policy.linked);
    CHECK(!memcmp(&owner,&core.owners[0],sizeof owner) && instrument.revision==revision && !thy_core_queued(&core));
    CHECK(thy_instrument_set_trigger(&instrument,1,1300u)==THY_OK);
    CHECK(instrument.sensors[1].response.release_raw==1277u && instrument.sensors[1].response_overridden);
    CHECK(sample(0u,4u)==THY_OK && sample(0u,4u)==THY_OK);drain();
    linked.gap=0u;CHECK(thy_instrument_set_release_policy(&instrument,1,&linked)==THY_INVALID_ARGUMENT);
    linked.gap=4096u;CHECK(thy_instrument_set_release_policy(&instrument,1,&linked)==THY_INVALID_ARGUMENT);
    linked.gap=23u;linked.linked=2u;CHECK(thy_instrument_set_release_policy(&instrument,1,&linked)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_get_release_policy(&instrument,96,&linked,(uint8_t[1]){0})==THY_INVALID_ARGUMENT);
    return 1;
}

static int test_full_sensitivity_policy_profile_is_atomic_across_inheritance_sets(void)
{
    static thy_instrument before;
    CHECK(initialize_factory());
    thy_instrument_response local=instrument.response;local.attack_raw=1600u;local.release_raw=1500u;
    CHECK(thy_instrument_set_pad_response(&instrument,1u,&local)==THY_OK);
    thy_instrument_response response={1300u,1277u,1500u,3500u,THY_PRESSURE_FIRM};
    thy_instrument_velocity velocity={3u,144u,THY_VELOCITY_STRIKE,THY_PRESSURE_SOFT,12u,120u};
    thy_instrument_release_policy policy={23u,1u};
    /* A detector override excludes pad 1 from the global detector write,
     * but its inherited velocity still requires a quiet preflight. */
    instrument.sensors[1].held=1u;before=instrument;
    CHECK(thy_instrument_check_sensitivity_policy(&instrument,-1,&response,&velocity,&policy)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_sensitivity_policy(&instrument,-1,&response,&velocity,&policy)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    instrument.sensors[1].held=0u;before=instrument;
    response.release_raw=1276u;
    CHECK(thy_instrument_set_sensitivity_policy(&instrument,-1,&response,&velocity,&policy)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    response.release_raw=1277u;policy.gap=0u;
    CHECK(thy_instrument_set_sensitivity_policy(&instrument,-1,&response,&velocity,&policy)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    policy.gap=23u;
    CHECK(thy_instrument_check_sensitivity_policy(&instrument,-1,&response,&velocity,&policy)==THY_OK);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    CHECK(thy_instrument_set_sensitivity_policy(&instrument,-1,&response,&velocity,&policy)==THY_OK);
    CHECK(instrument.response.attack_raw==1300u && instrument.response.release_raw==1277u && instrument.response.pressure_floor==1500u);
    CHECK(instrument.velocity_response.mode==THY_VELOCITY_STRIKE && instrument.release_policy.linked);
    CHECK(instrument.sensors[1].response.attack_raw==1600u && !instrument.pad_release_policy[1].linked);
    CHECK(!instrument.velocity_overridden[1]);
    policy=(thy_instrument_release_policy){80u,0u};response.release_raw=1000u;
    CHECK(thy_instrument_set_sensitivity_policy(&instrument,1,&response,&velocity,&policy)==THY_OK);
    CHECK(instrument.sensors[1].response_overridden && instrument.velocity_overridden[1]);
    CHECK(!instrument.pad_release_policy[1].linked && instrument.pad_release_policy[1].gap==80u);
    CHECK(instrument.sensors[1].response.release_raw==1000u && !thy_core_queued(&core));
    return 1;
}

static int test_all_sensitivity_preserves_calibrated_overrides_atomically(void)
{
    static thy_instrument before;
    static thy_core before_core;
    thy_core_init(&core);thy_instrument_init(&instrument,&core);
    CHECK(thy_instrument_set_trigger(&instrument,-1,1000u)==THY_OK);
    thy_instrument_response left={800u,750u,1400u,3000u,THY_PRESSURE_SOFT};
    thy_instrument_response right={1200u,1000u,1750u,3500u,THY_PRESSURE_FIRM};
    CHECK(thy_instrument_set_pad_response(&instrument,0u,&left)==THY_OK);
    CHECK(thy_instrument_set_pad_response(&instrument,95u,&right)==THY_OK);
    thy_instrument_release_policy linked={50u,1u};
    CHECK(thy_instrument_set_release_policy(&instrument,0,&linked)==THY_OK);
    thy_instrument_velocity strike={3u,144u,THY_VELOCITY_STRIKE,THY_PRESSURE_FIRM,12u,118u};
    CHECK(thy_instrument_set_pad_velocity(&instrument,0u,&strike)==THY_OK);
    before=instrument;
    CHECK(thy_instrument_scale_sensitivity(&instrument,1250u)==THY_OK);
    CHECK(instrument.revision==before.revision+1u);
    CHECK(instrument.response.attack_raw==1250u && instrument.response.release_raw==1227u);
    CHECK(instrument.sensors[0].response.attack_raw==1000u && instrument.sensors[0].response.release_raw==950u);
    CHECK(instrument.sensors[95].response.attack_raw==1500u && instrument.sensors[95].response.release_raw==1000u);
    CHECK(instrument.pad_release_policy[0].linked && instrument.pad_release_policy[0].gap==50u);
    CHECK(!instrument.pad_release_policy[95].linked && instrument.pad_release_policy[95].gap==200u);
    for(unsigned p=0;p<THY_PAD_COUNT;++p) {
        CHECK(instrument.sensors[p].response_overridden==before.sensors[p].response_overridden);
        CHECK(instrument.sensors[p].response.pressure_floor==before.sensors[p].response.pressure_floor);
        CHECK(instrument.sensors[p].response.pressure_full_scale==before.sensors[p].response.pressure_full_scale);
        CHECK(instrument.sensors[p].response.pressure_curve==before.sensors[p].response.pressure_curve);
        CHECK(instrument.velocity_overridden[p]==before.velocity_overridden[p]);
        CHECK(!memcmp(&instrument.pad_velocity[p],&before.pad_velocity[p],sizeof strike));
        if(p!=0u && p!=95u) {
            CHECK(instrument.sensors[p].response.attack_raw==1250u && instrument.sensors[p].response.release_raw==1227u);
            CHECK(instrument.pad_release_policy[p].linked && instrument.pad_release_policy[p].gap==23u);
        }
    }
    CHECK(!memcmp(&instrument.velocity_response,&before.velocity_response,sizeof strike));
    CHECK(instrument.response.pressure_floor==before.response.pressure_floor && instrument.response.pressure_full_scale==before.response.pressure_full_scale);
    CHECK(thy_instrument_scale_sensitivity(&instrument,1000u)==THY_OK);
    CHECK(instrument.sensors[0].response.attack_raw==800u && instrument.sensors[95].response.attack_raw==1200u);
    before=instrument;
    CHECK(thy_instrument_scale_sensitivity(&instrument,1000u)==THY_OK);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    /* A busy last pad must not let earlier candidates publish. */
    instrument.sensors[95].held=1u;before=instrument;before_core=core;
    CHECK(thy_instrument_scale_sensitivity(&instrument,1100u)==THY_INVALID_ARGUMENT);
    before.menu_notice=instrument.menu_notice;
    CHECK(!memcmp(&before,&instrument,sizeof instrument) && !memcmp(&before_core,&core,sizeof core));
    instrument.sensors[95].held=0u;instrument.menu_notice=0u;before=instrument;
    /* Manual pad 95 would shrink below its unchanged Release. */
    CHECK(thy_instrument_scale_sensitivity(&instrument,800u)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    CHECK(thy_instrument_scale_sensitivity(&instrument,4095u)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    CHECK(thy_instrument_scale_sensitivity(&instrument,0u)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    /* Released under its old threshold, but unsafe under the proposed one. */
    instrument.sensors[0].raw=740u;before=instrument;
    CHECK(thy_instrument_scale_sensitivity(&instrument,900u)==THY_INVALID_ARGUMENT);
    before.menu_notice=instrument.menu_notice;
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    instrument.sensors[0].raw=0u;instrument.menu_notice=0u;
    /* Nearest rounding is explicit, with a non-integral calibrated ratio. */
    CHECK(thy_instrument_set_trigger(&instrument,0,801u)==THY_OK);
    CHECK(thy_instrument_scale_sensitivity(&instrument,1100u)==THY_OK);
    CHECK(instrument.sensors[0].response.attack_raw==881u && instrument.sensors[0].response.release_raw==831u);
    CHECK(!thy_core_queued(&core) && !thy_core_sounding(&core));
    return 1;
}

static int test_custom_quiet_qualification_hysteresis_and_no_ghosts(void)
{
    const thy_instrument_response custom = {1200u, 1100u, 1000u, 3500u, THY_PRESSURE_LINEAR};
    thy_instrument_response lower = custom;
    unsigned tick;
    CHECK(initialize_factory());
    CHECK(thy_instrument_set_pad_response(&instrument, 0u, &custom) == THY_OK);
    for (tick = 0u; tick < 100u; ++tick) CHECK(sample(0u, 2000u) == THY_OK);
    CHECK(instrument.sensors[0].blocked != 0u && instrument.sensors[0].held == 0u);
    CHECK(core.sounding_count == 0u && instrument.presses == 0u);
    CHECK(thy_instrument_set_pad_response(&instrument, 0u, NULL) == THY_INVALID_ARGUMENT);
    CHECK(qualify(0u, 100u));
    CHECK(instrument.sensors[0].blocked == 0u);
    CHECK(sample(0u, 1200u) == THY_OK);
    CHECK(instrument.sensors[0].attack_count == 1u);
    CHECK(thy_instrument_set_pad_response(&instrument, 0u, NULL) == THY_INVALID_ARGUMENT);
    CHECK(sample(0u, 1199u) == THY_OK);
    CHECK(sample(0u, 1200u) == THY_OK);
    CHECK(sample(0u, 1200u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 96u));
    CHECK(sample(0u, 1100u) == THY_OK);
    CHECK(sample(0u, 1101u) == THY_OK);
    CHECK(core.sounding_count == 1u);
    CHECK(sample(0u, 1100u) == THY_OK);
    CHECK(sample(0u, 1100u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 12u, 0u));
    lower.pressure_floor = 800u;
    lower.release_raw = 900u;
    lower.attack_raw = 1000u;
    /* Current raw1100 is released under the old gate but not under the proposed
     * gate. Reject until a genuinely quiet sample, rather than arming a ghost. */
    CHECK(thy_instrument_set_pad_response(&instrument, 0u, &lower) == THY_INVALID_ARGUMENT);
    CHECK(instrument.sensors[0].response.attack_raw == 1200u);
    CHECK(sample(0u, 100u) == THY_OK);
    CHECK(thy_instrument_set_pad_response(&instrument, 0u, &lower) == THY_OK);
    CHECK(qualify(0u, 100u));
    CHECK(instrument.presses == 1u && instrument.releases == 1u && core.queue_count == 0u);
    return 1;
}

static int test_custom_global_inheritance_and_override_clear_atomicity(void)
{
    const thy_instrument_response custom = {1200u, 1100u, 1000u, 3500u, THY_PRESSURE_FIRM};
    const thy_instrument_response override = {1000u, 900u, 800u, 3000u, THY_PRESSURE_SOFT};
    const thy_instrument_calibration relative = {4u, 96u, 32u, 1000u, 1};
    thy_instrument_response read;
    uint8_t overridden;
    CHECK(initialize_factory());
    CHECK(thy_instrument_set_pad_response(&instrument, 48u, &override) == THY_OK);
    CHECK(thy_instrument_calibrate(&instrument, 2u, &relative) == THY_OK);
    CHECK(qualify(48u, 4u));
    CHECK(sample(48u, 2000u) == THY_OK);
    CHECK(sample(48u, 2000u) == THY_OK);
    CHECK(take(THY_USB0, 0x91u, 60u, 96u));
    /* Only inherited pads are affected. A held explicit override may continue
     * sounding while the inherited global response changes atomically. */
    CHECK(thy_instrument_set_response(&instrument, &custom) == THY_OK);
    CHECK(core.sounding_count == 1u && instrument.sensors[48].held != 0u);
    CHECK(thy_instrument_get_pad_response(&instrument, 48u, &read, &overridden) == THY_OK);
    CHECK(overridden == 1u && read.attack_raw == 1000u && read.pressure_curve == THY_PRESSURE_SOFT);
    CHECK(instrument.sensors[2].detector_profile == THY_DETECTOR_RELATIVE);
    CHECK(instrument.sensors[2].warmup_count == 8u && instrument.sensors[2].calibration.baseline == 4u);
    CHECK(thy_instrument_set_pad_response(&instrument, 48u, NULL) == THY_INVALID_ARGUMENT);
    CHECK(instrument.sensors[48].response_overridden == 1u);
    CHECK(sample(48u, 4u) == THY_OK);
    CHECK(sample(48u, 4u) == THY_OK);
    CHECK(take(THY_USB0, 0x81u, 60u, 0u));
    CHECK(thy_instrument_set_pad_response(&instrument, 48u, NULL) == THY_OK);
    CHECK(thy_instrument_get_pad_response(&instrument, 48u, &read, &overridden) == THY_OK);
    CHECK(overridden == 0u && read.attack_raw == 1200u && read.pressure_curve == THY_PRESSURE_FIRM);
    CHECK(instrument.sensors[48].warmup_count == 0u);
    CHECK(thy_instrument_get_pad_response(&instrument, 96u, &read, &overridden) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_get_pad_response(&instrument, 0u, NULL, &overridden) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_pad_response(&instrument, 96u, &custom) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_detector_profile(&instrument, THY_DETECTOR_FACTORY_LINEAR) == THY_OK);
    CHECK(instrument.sensors[2].response_overridden == 0u && instrument.sensors[48].response_overridden == 0u);
    CHECK(instrument.response.attack_raw == 1377u);
    return 1;
}

static int test_pressure_curves_and_fixed_velocity_are_independent(void)
{
    thy_instrument_response response = {1200u, 1100u, 1000u, 3000u, THY_PRESSURE_LINEAR};
    unsigned curve, raw;
    CHECK(initialize_factory());
    for (curve = 0u; curve < 3u; ++curve) {
        unsigned previous = 0u;
        response.pressure_curve = (uint8_t)curve;
        for (raw = 0u; raw <= 4095u; ++raw) {
            unsigned pressure = thy_instrument_response_pressure(&response, (uint16_t)raw);
            CHECK(pressure >= previous && pressure <= 127u);
            if (raw <= 1000u) CHECK(pressure == 0u);
            if (raw >= 3000u) CHECK(pressure == 127u);
            previous = pressure;
        }
        CHECK(thy_instrument_response_pressure(&response, 2000u) ==
            (curve == 0u ? 63u : curve == 1u ? 95u : 31u));
    }
    CHECK(thy_instrument_response_pressure(NULL, 2000u) == 0u);
    CHECK(thy_instrument_set_expression(&instrument, 0u, 0u) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_expression(&instrument, 128u, 0u) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_expression(&instrument, 32u, 2u) == THY_INVALID_ARGUMENT);
    CHECK(instrument.fixed_velocity == 96u && instrument.pressure_enabled == 0u);
    response.pressure_curve = THY_PRESSURE_LINEAR;
    CHECK(thy_instrument_set_pad_response(&instrument, 0u, &response) == THY_OK);
    CHECK(qualify(0u, 4u));
    CHECK(thy_instrument_set_expression(&instrument, 32u, 1u) == THY_OK);
    CHECK(sample(0u, 2000u) == THY_OK);
    CHECK(sample(0u, 2000u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 32u));
    CHECK(thy_instrument_set_expression(&instrument, 127u, 1u) == THY_OK);
    CHECK(sample(0u, 3000u) == THY_OK);
    CHECK(take(THY_USB0, 0xa0u, 12u, 127u));
    CHECK(sample(0u, 4u) == THY_OK);
    CHECK(sample(0u, 4u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 12u, 0u));
    CHECK(sample(0u, 1200u) == THY_OK);
    CHECK(sample(0u, 1200u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 12u, 127u));
    return 1;
}

static int test_readonly_pad_preview_matches_shaped_attack(void)
{
    thy_instrument before;
    thy_core core_before;
    int16_t pitches[THY_MAX_NOTES];
    size_t count = 999u;
    CHECK(initialize());
    CHECK(thy_instrument_set_lower_mode(&instrument, THY_LOWER_STORED_NOTES) == THY_OK);
    layout[0] = (thy_instrument_pad){{60, 0, 67}, 5u, 1u};
    instrument.slot_scope = THY_SLOTS_SELECTED_PAD;
    instrument.excluded_slots[0] = 4u;
    instrument.performance.inversion = 1u;
    instrument.performance.spread = 1u;
    instrument.performance.upper_octaves = 1u;
    instrument.performance.chord_octave = 1;
    instrument.performance.strum_order = THY_STRUM_ALTERNATING;
    CHECK(thy_instrument_set_route(&instrument, 0u, 0u, THY_ROUTE_USB0, 2) == THY_OK);
    before = instrument;
    core_before = core;
    CHECK(thy_instrument_preview_pad(&instrument, 0u, pitches, &count) == THY_OK);
    CHECK(count == 2u && pitches[0] == 74 && pitches[1] == 86);
    CHECK(memcmp(&instrument, &before, sizeof before) == 0);
    CHECK(memcmp(&core, &core_before, sizeof core_before) == 0);
    CHECK(sample(0u, 2000u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 74u, 96u));
    CHECK(take(THY_USB0, 0x90u, 86u, 96u));
    CHECK(sample(0u, 100u) == THY_OK);
    drain();
    instrument.performance.melody_layout = 1u;
    instrument.performance.melody_octave = 1;
    CHECK(thy_instrument_set_route(&instrument, 1u, 1u, THY_ROUTE_USB0, 1) == THY_OK);
    CHECK(thy_instrument_preview_pad(&instrument, 95u, pitches, &count) == THY_OK);
    CHECK(count == 1u && pitches[0] == 48);
    CHECK(sample(95u, 2000u) == THY_OK);
    CHECK(take(THY_USB0, 0x91u, 48u, 96u));
    count = 999u;
    pitches[0] = -17;
    CHECK(thy_instrument_preview_pad(&instrument, 96u, pitches, &count) == THY_INVALID_ARGUMENT);
    CHECK(count == 999u && pitches[0] == -17);
    CHECK(thy_instrument_preview_pad(NULL, 0u, pitches, &count) == THY_INVALID_ARGUMENT);
    layout[0].notes[0] = 120;
    before = instrument;
    core_before = core;
    CHECK(thy_instrument_preview_pad(&instrument, 0u, pitches, &count) == THY_INVALID_ARGUMENT);
    CHECK(count == 999u && pitches[0] == -17);
    CHECK(memcmp(&instrument, &before, sizeof before) == 0);
    CHECK(memcmp(&core, &core_before, sizeof core_before) == 0);
    return 1;
}

static int test_response_menu_and_lighting_bounds(void)
{
    uint32_t errors;
    CHECK(initialize_factory());
    CHECK(THY_INSTRUMENT_MENU_ROWS == 23u);
    CHECK(instrument.lights_enabled == 1u && instrument.light_idle == 3u &&
        instrument.light_held == 8u && instrument.light_fade_ms == 120u);
    CHECK(thy_instrument_set_lighting(&instrument, 2u, 0u, 0u, 0u) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_lighting(&instrument, 0u, 9u, 0u, 0u) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_lighting(&instrument, 0u, 0u, 9u, 0u) == THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_lighting(&instrument, 0u, 0u, 0u, 501u) == THY_INVALID_ARGUMENT);
    CHECK(instrument.lights_enabled == 1u && instrument.light_idle == 3u);
    CHECK(thy_instrument_set_lighting(&instrument, 1u, 2u, 7u, 500u) == THY_OK);
    instrument.menu_open = 1u;
    instrument.menu_editing = 1u;
    instrument.menu_row = 14u;
    CHECK(sample(0u, 2000u) == THY_OK);
    CHECK(sample(0u, 2000u) == THY_OK);
    drain();
    errors = instrument.errors;
    CHECK(tap(0u, 3u));
    CHECK(instrument.menu_notice == 1u && instrument.errors == errors);
    CHECK(instrument.response.attack_raw == 1377u && core.sounding_count == 1u);
    CHECK(sample(0u, 4u) == THY_OK);
    CHECK(sample(0u, 4u) == THY_OK);
    drain();
    CHECK(tap(0u, 3u));
    CHECK(instrument.response.attack_raw == 1393u && instrument.menu_notice == 0u);
    instrument.menu_row = 19u;
    CHECK(tap(0u, 3u));
    CHECK(instrument.lights_enabled == 0u);
    instrument.menu_row = 20u;
    CHECK(tap(0u, 1u));
    CHECK(instrument.light_idle == 1u);
    instrument.menu_row = 21u;
    CHECK(tap(0u, 3u));
    CHECK(tap(0u, 3u));
    CHECK(instrument.light_held == 8u);
    instrument.menu_row = 22u;
    CHECK(tap(0u, 3u));
    CHECK(instrument.light_fade_ms == 500u);
    CHECK(tap(0u, 1u));
    CHECK(instrument.light_fade_ms == 460u);
    return 1;
}

static int test_legacy_center_toggle_and_right_editor_preserve_held_notes(void)
{
    CHECK(initialize());
    CHECK(tap(0u, 2u));
    CHECK(instrument.menu_open && !instrument.menu_editing);
    instrument.menu_row = 1u;
    CHECK(tap(0u, 4u));
    CHECK(instrument.menu_open && instrument.menu_editing);
    CHECK(tap(0u, 4u));
    CHECK(instrument.menu_open && !instrument.menu_editing);
    CHECK(tap(0u, 4u));
    CHECK(tap(0u, 0u));
    CHECK(instrument.menu_open && !instrument.menu_editing);
    CHECK(tap(0u, 2u));
    CHECK(!instrument.menu_open && !instrument.menu_editing && instrument.menu_row == 1u);
    CHECK(tap(0u, 2u));
    CHECK(instrument.menu_open && instrument.menu_row == 1u);
    CHECK(tap(0u, 4u));
    CHECK(tap(0u, 2u));
    CHECK(!instrument.menu_open && !instrument.menu_editing);
    CHECK(sample(0u, 600u) == THY_OK);
    drain();
    const thy_pad_owner owner = core.owners[0];
    CHECK(owner.held);
    CHECK(tap(0u, 2u));
    instrument.menu_row = 13u; /* Center must exit even on the legacy panic action. */
    CHECK(tap(0u, 2u));
    CHECK(!instrument.menu_open && memcmp(&owner, &core.owners[0], sizeof owner) == 0);
    CHECK(thy_core_queued(&core) == 0u && thy_core_sounding(&core) == 1u);
    CHECK(sample(0u, 100u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 12u, 0u));
    CHECK(thy_core_sounding(&core) == 0u);
    return 1;
}

static int strike_fixture(uint32_t start)
{
    CHECK(initialize());
    CHECK(thy_instrument_set_detector_profile(&instrument,THY_DETECTOR_FACTORY_LINEAR)==THY_OK);
    now=start;
    for(unsigned n=0;n<8u;++n)CHECK(thy_instrument_sample(&instrument,0u,1200u,now++)==THY_OK);
    CHECK(instrument.sensors[0].warmup_count==8u && !instrument.sensors[0].blocked);
    return 1;
}
static int test_strike_curves_are_monotonic_bounded_and_validated(void)
{
    thy_instrument_velocity v;thy_instrument_velocity_defaults(&v);
    CHECK(v.mode==THY_VELOCITY_FIXED && thy_instrument_velocity_valid(&v));
    for(unsigned curve=0u;curve<3u;++curve){
        v.curve=(uint8_t)curve;v.minimum=7u;v.maximum=118u;
        unsigned previous=0u;
        for(unsigned rate=0u;rate<=4095u;++rate){
            unsigned velocity=thy_instrument_velocity_value(&v,(uint16_t)rate);
            CHECK(velocity>=7u && velocity<=118u && velocity>=previous);previous=velocity;
        }
        CHECK(thy_instrument_velocity_value(&v,0u)==7u);
        CHECK(thy_instrument_velocity_value(&v,4095u)==118u);
    }
    v.soft_rate=v.hard_rate;CHECK(!thy_instrument_velocity_valid(&v));
    CHECK(thy_instrument_velocity_value(&v,100u)==0u);
    thy_instrument_velocity_defaults(&v);v.minimum=0u;CHECK(!thy_instrument_velocity_valid(&v));
    thy_instrument_velocity_defaults(&v);v.maximum=128u;CHECK(!thy_instrument_velocity_valid(&v));
    thy_instrument_velocity_defaults(&v);v.mode=2u;CHECK(!thy_instrument_velocity_valid(&v));
    thy_instrument_velocity_defaults(&v);v.curve=3u;CHECK(!thy_instrument_velocity_valid(&v));
    return 1;
}
static int test_strike_samples_separate_attacks_without_an_extra_wait(void)
{
    unsigned velocities[3];
    const uint16_t levels[]={1380u,1500u,2200u};
    const uint32_t gaps[]={10u,4u,2u};
    for(unsigned n=0u;n<3u;++n){
        CHECK(strike_fixture(1u));
        thy_instrument_velocity v=instrument.velocity_response;v.mode=THY_VELOCITY_STRIKE;
        CHECK(thy_instrument_set_velocity(&instrument,&v)==THY_OK);
        CHECK(thy_instrument_set_route(&instrument,0u,3u,THY_ROUTE_USB0|THY_ROUTE_DIN,0)==THY_OK);
        now=instrument.sensors[0].last_sample_ms+gaps[n];
        CHECK(thy_instrument_sample(&instrument,0u,levels[n],now)==THY_OK);
        CHECK(!core.owners[0].held && !core.queue_count);
        now+=2u;CHECK(thy_instrument_sample(&instrument,0u,(uint16_t)(levels[n]+10u),now)==THY_OK);
        CHECK(core.owners[0].held && core.owners[0].velocity>=1u);
        CHECK(instrument.sensors[0].last_transition_ms==now && !instrument.sensors[0].strike_ready);
        velocities[n]=core.owners[0].velocity;
        CHECK(take(THY_USB0,0x93u,12u,velocities[n]));CHECK(take(THY_DIN,0x93u,12u,velocities[n]));
        CHECK(!core.queue_count);
        now+=2u;CHECK(thy_instrument_sample(&instrument,0u,4095u,now)==THY_OK);
        CHECK(core.owners[0].velocity==velocities[n] && !core.queue_count); /* Held pressure cannot retrigger. */
        CHECK(thy_instrument_set_pad_route(&instrument,1u,8u,THY_ROUTE_USB1)==THY_OK);
        now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1200u,now)==THY_OK);
        now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1200u,now)==THY_OK);
        CHECK(take(THY_USB0,0x83u,12u,0u));CHECK(take(THY_DIN,0x83u,12u,0u));
        CHECK(!core.sounding_count && !instrument.sensors[0].held);
    }
    CHECK(velocities[0]<velocities[1] && velocities[1]<velocities[2] && velocities[2]==127u);
    return 1;
}
static int test_fixed_velocity_and_direct_press_fallback_remain_available(void)
{
    CHECK(strike_fixture(1u));
    CHECK(thy_instrument_set_route_velocity(&instrument,1u,73u)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,2400u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,2500u,now)==THY_OK);
    CHECK(take(THY_USB0,0x90u,12u,73u));CHECK(instrument.sensors[0].strike_velocity==73u);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1200u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1200u,now)==THY_OK);drain();
    thy_instrument_velocity v=instrument.velocity_response;v.mode=THY_VELOCITY_STRIKE;
    CHECK(thy_instrument_set_velocity(&instrument,&v)==THY_OK);
    CHECK(thy_instrument_press_pad(&instrument,0u)==THY_OK);
    CHECK(take(THY_USB0,0x90u,12u,73u)); /* No fabricated slope for explicit host presses. */
    CHECK(thy_instrument_release_pad(&instrument,0u)==THY_OK);drain();
    return 1;
}
static int test_velocity_override_inheritance_and_reset_are_atomic(void)
{
    CHECK(strike_fixture(1u));
    thy_instrument_velocity v=instrument.velocity_response;v.mode=THY_VELOCITY_STRIKE;
    CHECK(thy_instrument_set_velocity(&instrument,&v)==THY_OK);
    v.curve=THY_PRESSURE_FIRM;v.minimum=19u;v.maximum=109u;
    CHECK(thy_instrument_set_pad_velocity(&instrument,0u,&v)==THY_OK);
    uint8_t overridden;thy_instrument_velocity resolved;
    CHECK(thy_instrument_get_velocity(&instrument,0,&resolved,&overridden)==THY_OK && overridden && resolved.minimum==19u);
    v=instrument.velocity_response;v.curve=THY_PRESSURE_SOFT;
    CHECK(thy_instrument_set_velocity(&instrument,&v)==THY_OK);
    CHECK(thy_instrument_get_velocity(&instrument,0,&resolved,&overridden)==THY_OK && resolved.curve==THY_PRESSURE_FIRM);
    CHECK(thy_instrument_get_velocity(&instrument,1,&resolved,&overridden)==THY_OK && !overridden && resolved.curve==THY_PRESSURE_SOFT);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,2000u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,2000u,now)==THY_OK);drain();
    const thy_instrument_velocity before=instrument.pad_velocity[0];
    CHECK(thy_instrument_clear_pad_velocity(&instrument,0u)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument.pad_velocity[0],sizeof before));
    const thy_instrument_response response_before=instrument.response;
    CHECK(thy_instrument_reset_sensitivity(&instrument,0)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&response_before,&instrument.response,sizeof response_before) && instrument.velocity_overridden[0]);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1200u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1200u,now)==THY_OK);drain();
    CHECK(thy_instrument_reset_sensitivity(&instrument,0)==THY_OK);
    CHECK(!instrument.sensors[0].response_overridden && !instrument.velocity_overridden[0]);
    CHECK(thy_instrument_get_velocity(&instrument,0,&resolved,&overridden)==THY_OK && resolved.curve==THY_PRESSURE_SOFT);
    /* Global reset must preflight velocity-inheriting pads even when their
     * response override would exclude them from a global response edit. */
    thy_instrument_response r=instrument.response;r.attack_raw=1500u;
    CHECK(thy_instrument_set_pad_response(&instrument,1u,&r)==THY_OK);
    instrument.sensors[1].held=1u;
    const thy_instrument_velocity global_before=instrument.velocity_response;
    CHECK(thy_instrument_reset_sensitivity(&instrument,-1)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&global_before,&instrument.velocity_response,sizeof global_before));
    CHECK(!memcmp(&response_before,&instrument.response,sizeof response_before));
    instrument.sensors[1].held=0u;
    CHECK(thy_instrument_reset_sensitivity(&instrument,-1)==THY_OK);
    CHECK(instrument.velocity_response.mode==THY_VELOCITY_FIXED && instrument.sensors[1].response_overridden);
    /* Importing both families must preflight their different inheritance
     * sets: a pressure override must not hide a busy velocity inheritor. */
    r=instrument.response;r.attack_raw=1393u;
    v=instrument.velocity_response;v.mode=THY_VELOCITY_STRIKE;
    instrument.sensors[1].held=1u;
    CHECK(thy_instrument_set_sensitivity(&instrument,-1,&r,&v)==THY_INVALID_ARGUMENT);
    CHECK(instrument.response.attack_raw==1377u && instrument.velocity_response.mode==THY_VELOCITY_FIXED);
    instrument.sensors[1].held=0u;
    CHECK(thy_instrument_set_sensitivity(&instrument,-1,&r,&v)==THY_OK);
    CHECK(instrument.response.attack_raw==1393u && instrument.velocity_response.mode==THY_VELOCITY_STRIKE);
    CHECK(instrument.sensors[1].response_overridden && instrument.sensors[1].response.attack_raw==1500u);
    return 1;
}
static int test_velocity_stale_samples_wrap_and_negative_polarity(void)
{
    CHECK(strike_fixture(UINT32_MAX-7u));
    thy_instrument_velocity v=instrument.velocity_response;v.mode=THY_VELOCITY_STRIKE;
    CHECK(thy_instrument_set_velocity(&instrument,&v)==THY_OK);
    now=9u;CHECK(thy_instrument_sample(&instrument,0u,1380u,now)==THY_OK);
    now=11u;CHECK(thy_instrument_sample(&instrument,0u,1390u,now)==THY_OK);
    CHECK(core.owners[0].held && core.owners[0].velocity>1u && core.owners[0].velocity<127u);drain();
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1200u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1200u,now)==THY_OK);drain();
    now+=100u;CHECK(thy_instrument_sample(&instrument,0u,1380u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1380u,now)==THY_OK);
    CHECK(core.owners[0].velocity==1u); /* No peak survives a stale input gap. */
    drain();CHECK(thy_instrument_release_pad(&instrument,0u)==THY_OK);drain();
    CHECK(initialize());
    const thy_instrument_calibration negative={2000u,96u,32u,1000u,-1};
    CHECK(thy_instrument_calibrate(&instrument,0u,&negative)==THY_OK);
    CHECK(thy_instrument_set_pad_velocity(&instrument,0u,&v)==THY_OK);
    CHECK(thy_instrument_sample(&instrument,0u,2000u,1u)==THY_OK);
    CHECK(thy_instrument_sample(&instrument,0u,1600u,3u)==THY_OK);
    CHECK(core.owners[0].held && core.owners[0].velocity==127u);
    CHECK(initialize());
    const thy_instrument_calibration negative_quiet={1000u,96u,32u,1000u,-1};
    CHECK(thy_instrument_calibrate(&instrument,0u,&negative_quiet)==THY_OK);
    CHECK(thy_instrument_sample(&instrument,0u,1000u,1u)==THY_OK);
    CHECK(thy_instrument_reset_sensitivity(&instrument,0)==THY_OK);
    /* Multiple publications can share a millisecond timestamp. They retain
     * attack slope at the documented minimum 1ms resolution. */
    CHECK(strike_fixture(1u));
    CHECK(thy_instrument_set_velocity(&instrument,&v)==THY_OK);
    now=8u;
    CHECK(thy_instrument_sample(&instrument,0u,1800u,now)==THY_OK);
    CHECK(thy_instrument_sample(&instrument,0u,1900u,now)==THY_OK);
    CHECK(core.owners[0].held && core.owners[0].velocity==127u);
    return 1;
}

typedef struct sampling_preview {
    thy_instrument_response response;
    thy_instrument_velocity velocity;
    unsigned mode,calls,saved_inputs_seen;
} sampling_preview;
static int preview_sampling(void *context,uint8_t electrical,
    thy_instrument_response *response,thy_instrument_velocity *velocity)
{
    sampling_preview *preview=context;
    ++preview->calls;
    if(electrical!=0u || !preview->mode)return 0;
    if(response->attack_raw==1600u && velocity->mode==THY_VELOCITY_FIXED)
        ++preview->saved_inputs_seen;
    *response=preview->response;*velocity=preview->velocity;
    if(preview->mode==2u)response->release_raw=response->attack_raw;
    if(preview->mode==3u)velocity->hard_rate=velocity->soft_rate;
    if(preview->mode==4u)return -1;
    if(preview->mode==5u)return 2;
    if(preview->mode==6u){response->release_raw=response->attack_raw;return 0;}
    return 1;
}
static int saved_sampling_settings_equal(const thy_instrument *saved)
{
    CHECK(!memcmp(&instrument.response,&saved->response,sizeof instrument.response));
    CHECK(!memcmp(&instrument.velocity_response,&saved->velocity_response,sizeof instrument.velocity_response));
    CHECK(!memcmp(&instrument.release_policy,&saved->release_policy,sizeof instrument.release_policy));
    CHECK(!memcmp(instrument.cluster_response,saved->cluster_response,sizeof instrument.cluster_response));
    CHECK(!memcmp(instrument.cluster_velocity,saved->cluster_velocity,sizeof instrument.cluster_velocity));
    CHECK(!memcmp(instrument.cluster_release_policy,saved->cluster_release_policy,sizeof instrument.cluster_release_policy));
    CHECK(!memcmp(instrument.sensitivity_guard,saved->sensitivity_guard,sizeof instrument.sensitivity_guard));
    for(unsigned p=0;p<THY_PAD_COUNT;++p){
        CHECK(!memcmp(&instrument.sensors[p].response,&saved->sensors[p].response,sizeof instrument.sensors[p].response));
        CHECK(instrument.sensors[p].response_overridden==saved->sensors[p].response_overridden);
        CHECK(instrument.sensors[p].detector_profile==saved->sensors[p].detector_profile);
        CHECK(!memcmp(&instrument.pad_velocity[p],&saved->pad_velocity[p],sizeof instrument.pad_velocity[p]));
        CHECK(instrument.velocity_overridden[p]==saved->velocity_overridden[p]);
        CHECK(!memcmp(&instrument.pad_release_policy[p],&saved->pad_release_policy[p],sizeof instrument.pad_release_policy[p]));
    }
    return 1;
}
static int test_transient_sampling_preview_uses_candidate_without_saving_it(void)
{
    static thy_instrument saved;
    CHECK(strike_fixture(1u));
    thy_instrument_response stored={1600u,1500u,1490u,3500u,THY_PRESSURE_FIRM};
    thy_instrument_velocity stored_velocity=instrument.velocity_response;
    CHECK(thy_instrument_set_pad_response(&instrument,0u,&stored)==THY_OK);
    CHECK(thy_instrument_set_pad_velocity(&instrument,0u,&stored_velocity)==THY_OK);
    CHECK(thy_instrument_set_route(&instrument,0u,3u,THY_ROUTE_USB0|THY_ROUTE_DIN,0)==THY_OK);
    CHECK(thy_instrument_set_route_pressure(&instrument,1u,1)==THY_OK);
    for(unsigned n=0;n<THY_INSTRUMENT_WARMUP_SAMPLES;++n)
        CHECK(thy_instrument_sample(&instrument,0u,1200u,now++)==THY_OK);
    sampling_preview preview={{1250u,1230u,1200u,1600u,THY_PRESSURE_LINEAR},
        {0u,100u,THY_VELOCITY_STRIKE,THY_PRESSURE_LINEAR,10u,110u},1u,0u,0u};
    saved=instrument;
    thy_instrument_set_sampling_provider(&instrument,preview_sampling,&preview);
    now+=10u;CHECK(thy_instrument_sample(&instrument,0u,1280u,now)==THY_OK);
    CHECK(!core.owners[0].held && !core.queue_count);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1300u,now)==THY_OK);
    CHECK(core.owners[0].held && core.owners[0].velocity==19u);
    CHECK(instrument.sensors[0].attack_raw==1250 && instrument.sensors[0].release_raw==1230);
    CHECK(take(THY_USB0,0x93u,12u,19u));CHECK(take(THY_DIN,0x93u,12u,19u));
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1500u,now)==THY_OK);
    CHECK(instrument.sensors[0].pressure==95u && core.owners[0].velocity==19u);
    CHECK(take(THY_USB0,0xa3u,12u,95u));CHECK(take(THY_DIN,0xa3u,12u,95u));
    CHECK(saved_sampling_settings_equal(&saved));
    thy_instrument_response read_response;thy_instrument_velocity read_velocity;uint8_t overridden;
    CHECK(thy_instrument_get_pad_response(&instrument,0u,&read_response,&overridden)==THY_OK && overridden);
    CHECK(read_response.attack_raw==1600u && read_response.pressure_curve==THY_PRESSURE_FIRM);
    CHECK(thy_instrument_get_velocity(&instrument,0,&read_velocity,&overridden)==THY_OK && overridden);
    CHECK(read_velocity.mode==THY_VELOCITY_FIXED);
    /* Preview expression follows the attack's original channel/destinations
     * even when future live routes change during the held gesture. */
    CHECK(thy_instrument_set_route(&instrument,0u,7u,THY_ROUTE_USB1,0)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1220u,now)==THY_OK);
    CHECK(core.owners[0].held && !core.queue_count);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1220u,now)==THY_OK);
    CHECK(take(THY_USB0,0x83u,12u,0u));CHECK(take(THY_DIN,0x83u,12u,0u));
    CHECK(!core.sounding_count && !core.queue_count);
    preview.mode=0u; /* Before/saved response, while entirely released. */
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1280u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1300u,now)==THY_OK);
    CHECK(!core.owners[0].held && !core.queue_count && !instrument.sensors[0].pressure);
    CHECK(instrument.sensors[0].attack_raw==1600 && instrument.sensors[0].release_raw==1500);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1700u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1800u,now)==THY_OK);
    CHECK(take(THY_USB1,0x97u,12u,96u));
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1200u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1200u,now)==THY_OK);
    CHECK(take(THY_USB1,0x87u,12u,0u));
    CHECK(preview.calls>0u && preview.saved_inputs_seen>0u);
    CHECK(saved_sampling_settings_equal(&saved));
    thy_instrument_set_sampling_provider(&instrument,NULL,NULL);
    CHECK(!instrument.sampling_provider && !instrument.sampling_provider_context);
    CHECK(!core.sounding_count && !core.queue_count && !instrument.errors);
    return 1;
}
static int test_transient_sampling_preview_rejects_invalid_outputs_atomically(void)
{
    static thy_instrument saved;
    CHECK(strike_fixture(1u));
    sampling_preview preview={{1250u,1230u,1200u,1600u,THY_PRESSURE_LINEAR},
        {0u,100u,THY_VELOCITY_STRIKE,THY_PRESSURE_LINEAR,10u,110u},2u,0u,0u};
    thy_instrument_set_sampling_provider(&instrument,preview_sampling,&preview);
    for(unsigned mode=2u;mode<=5u;++mode){
        preview.mode=mode;saved=instrument;now+=2u;
        CHECK(thy_instrument_sample(&instrument,0u,1300u,now)==THY_INVALID_ARGUMENT);
        CHECK(!memcmp(&instrument,&saved,sizeof instrument));
        CHECK(!core.queue_count && !core.sounding_count);
    }
    /* Return0 means saved settings, even if the scratch outputs were changed
     * to invalid values before the provider declined the override. */
    preview.mode=6u;
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1300u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1300u,now)==THY_OK);
    CHECK(!core.owners[0].held && instrument.sensors[0].attack_raw==1377);
    CHECK(!core.queue_count && !instrument.errors);
    thy_instrument_set_sampling_provider(NULL,preview_sampling,&preview);
    return 1;
}
static int test_transient_sampling_preview_is_absolute_over_saved_relative_polarity(void)
{
    static thy_instrument saved;
    CHECK(initialize());
    const thy_instrument_calibration negative={1000u,96u,32u,1000u,-1};
    CHECK(thy_instrument_calibrate(&instrument,0u,&negative)==THY_OK);
    sampling_preview preview={{1100u,1050u,1000u,2000u,THY_PRESSURE_LINEAR},
        {0u,100u,THY_VELOCITY_STRIKE,THY_PRESSURE_LINEAR,10u,110u},1u,0u,0u};
    saved=instrument;
    thy_instrument_set_sampling_provider(&instrument,preview_sampling,&preview);
    now=1u;CHECK(thy_instrument_sample(&instrument,0u,1000u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1100u,now)==THY_OK);
    CHECK(!core.owners[0].held);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1200u,now)==THY_OK);
    CHECK(core.owners[0].held && core.owners[0].velocity==60u);
    CHECK(instrument.sensors[0].pressure==25u);
    CHECK(instrument.sensors[0].detector_profile==THY_DETECTOR_RELATIVE);
    CHECK(instrument.sensors[0].calibration.polarity==-1);
    CHECK(take(THY_USB0,0x90u,12u,60u));
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1000u,now)==THY_OK);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1000u,now)==THY_OK);
    CHECK(take(THY_USB0,0x80u,12u,0u));
    CHECK(saved_sampling_settings_equal(&saved));
    thy_instrument_set_sampling_provider(&instrument,NULL,NULL);
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,800u,now)==THY_OK);
    CHECK(core.owners[0].held && core.owners[0].velocity==96u);
    CHECK(take(THY_USB0,0x90u,12u,96u));
    now+=2u;CHECK(thy_instrument_sample(&instrument,0u,1000u,now)==THY_OK);
    CHECK(take(THY_USB0,0x80u,12u,0u));
    CHECK(!core.queue_count && !core.sounding_count && !instrument.errors);
    return 1;
}

static int cluster_fixture(void)
{
    CHECK(initialize_factory());CHECK(sample(2u,4u)==THY_OK);
    return 1;
}
static unsigned cluster_electrical(unsigned logical)
{
    for(unsigned p=0;p<THY_PAD_COUNT;++p)if(instrument.mapping[p]==logical)return p;
    return THY_PAD_COUNT;
}
static int test_cluster_defaults_are_independent_and_per_pad_reset_inherits_side(void)
{
    static thy_instrument before;
    CHECK(cluster_fixture());
    uint8_t mapping[THY_PAD_COUNT];
    for(unsigned p=0;p<THY_PAD_COUNT;++p)mapping[p]=(uint8_t)((p*37u+11u)%THY_PAD_COUNT);
    CHECK(thy_instrument_set_mapping(&instrument,mapping)==THY_OK);
    thy_instrument_response left={900u,870u,1200u,3500u,THY_PRESSURE_SOFT};
    thy_instrument_velocity left_velocity={3u,120u,THY_VELOCITY_STRIKE,THY_PRESSURE_SOFT,5u,118u};
    thy_instrument_velocity right_velocity={5u,180u,THY_VELOCITY_FIXED,THY_PRESSURE_FIRM,10u,120u};
    thy_instrument_release_policy linked={30u,1u};
    before=instrument;
    CHECK(thy_instrument_check_cluster_sensitivity_policy(&instrument,0u,&left,&left_velocity,&linked)==THY_OK);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,0u,&left,&left_velocity,&linked)==THY_OK);
    for(unsigned p=0;p<THY_PAD_COUNT;++p){
        thy_instrument_velocity got;uint8_t overridden;
        CHECK(!instrument.sensors[p].response_overridden && !instrument.velocity_overridden[p]);
        CHECK(thy_instrument_get_velocity(&instrument,(int)p,&got,&overridden)==THY_OK && !overridden);
        if(mapping[p]<48u){
            CHECK(instrument.sensors[p].response.attack_raw==900u && instrument.pad_release_policy[p].gap==30u);
            CHECK(got.mode==THY_VELOCITY_STRIKE && got.hard_rate==120u);
        }else{
            CHECK(!memcmp(&instrument.sensors[p],&before.sensors[p],sizeof instrument.sensors[p]));
            CHECK(got.mode==THY_VELOCITY_FIXED && got.hard_rate==128u);
        }
    }
    CHECK(thy_instrument_set_cluster_velocity(&instrument,1u,&right_velocity)==THY_OK);
    CHECK(instrument.velocity_response.hard_rate==128u && instrument.response.attack_raw==1377u);
    unsigned p=cluster_electrical(0u);CHECK(p<THY_PAD_COUNT);
    thy_instrument_response override={1000u,900u,1500u,3000u,THY_PRESSURE_FIRM};
    CHECK(thy_instrument_set_pad_response(&instrument,(uint8_t)p,&override)==THY_OK);
    CHECK(thy_instrument_set_pad_velocity(&instrument,(uint8_t)p,&right_velocity)==THY_OK);
    left.attack_raw=1100u;left.release_raw=1070u;left.pressure_floor=1600u;left.pressure_full_scale=4000u;
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,0u,&left,&left_velocity,&linked)==THY_OK);
    CHECK(instrument.sensors[p].response.attack_raw==1000u && instrument.velocity_overridden[p]);
    CHECK(thy_instrument_set_pad_response(&instrument,(uint8_t)p,NULL)==THY_OK);
    CHECK(thy_instrument_clear_pad_velocity(&instrument,(uint8_t)p)==THY_OK);
    CHECK(!instrument.sensors[p].response_overridden && !instrument.velocity_overridden[p]);
    CHECK(instrument.sensors[p].response.attack_raw==1100u && instrument.sensors[p].response.pressure_floor==1600u);
    thy_instrument_velocity got_velocity;thy_instrument_release_policy got_policy;uint8_t overridden;
    CHECK(thy_instrument_get_velocity(&instrument,(int)p,&got_velocity,&overridden)==THY_OK && !overridden);
    CHECK(got_velocity.mode==THY_VELOCITY_STRIKE && got_velocity.hard_rate==120u);
    CHECK(thy_instrument_get_release_policy(&instrument,(int)p,&got_policy,&overridden)==THY_OK && !overridden);
    CHECK(got_policy.linked && got_policy.gap==30u);
    CHECK(thy_instrument_get_cluster_response(&instrument,0u,&override)==THY_OK && override.attack_raw==1100u);
    CHECK(thy_instrument_get_cluster_velocity(&instrument,1u,&got_velocity)==THY_OK && got_velocity.hard_rate==180u);
    CHECK(thy_instrument_get_cluster_release_policy(&instrument,0u,&got_policy)==THY_OK && got_policy.gap==30u);
    CHECK(thy_instrument_cluster_trigger(&instrument,0u)==1100u && thy_instrument_cluster_trigger(&instrument,1u)==1377u);
    CHECK(thy_instrument_set_trigger(&instrument,-1,1200u)==THY_OK);
    CHECK(instrument.cluster_response[0].attack_raw==1200u && instrument.cluster_response[1].attack_raw==1200u);
    CHECK(instrument.cluster_release_policy[0].gap==23u && instrument.cluster_release_policy[1].gap==23u);
    thy_instrument_velocity_defaults(&got_velocity);
    CHECK(thy_instrument_set_velocity(&instrument,&got_velocity)==THY_OK);
    CHECK(!memcmp(&instrument.cluster_velocity[0],&got_velocity,sizeof got_velocity));
    CHECK(!memcmp(&instrument.cluster_velocity[1],&got_velocity,sizeof got_velocity));
    CHECK(!core.queue_count && !core.sounding_count && !instrument.errors);
    return 1;
}
static int test_cluster_scaling_preserves_fit_flags_expression_and_other_half(void)
{
    static thy_instrument before;
    static thy_core before_core;
    CHECK(cluster_fixture());
    uint8_t mapping[THY_PAD_COUNT];
    for(unsigned p=0;p<THY_PAD_COUNT;++p)mapping[p]=(uint8_t)((p*37u+11u)%THY_PAD_COUNT);
    CHECK(thy_instrument_set_mapping(&instrument,mapping)==THY_OK);
    thy_instrument_response left={1000u,977u,1353u,4095u,THY_PRESSURE_LINEAR};
    thy_instrument_response right={1500u,1450u,1700u,3500u,THY_PRESSURE_FIRM};
    thy_instrument_velocity fixed=instrument.velocity_response;
    thy_instrument_velocity strike_velocity={3u,144u,THY_VELOCITY_STRIKE,THY_PRESSURE_FIRM,12u,118u};
    thy_instrument_release_policy left_policy={23u,1u},right_policy={50u,1u};
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,0u,&left,&fixed,&left_policy)==THY_OK);
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,1u,&right,&strike_velocity,&right_policy)==THY_OK);
    unsigned a=cluster_electrical(0u),b=cluster_electrical(47u),other=cluster_electrical(48u);
    CHECK(a<THY_PAD_COUNT && b<THY_PAD_COUNT && other<THY_PAD_COUNT);
    thy_instrument_response local={800u,750u,1400u,3000u,THY_PRESSURE_SOFT};
    thy_instrument_release_policy linked={50u,1u};
    CHECK(thy_instrument_set_response_policy(&instrument,(int)a,&local,&linked)==THY_OK);
    local=(thy_instrument_response){1200u,900u,1700u,3600u,THY_PRESSURE_FIRM};
    thy_instrument_release_policy manual={300u,0u};
    CHECK(thy_instrument_set_response_policy(&instrument,(int)b,&local,&manual)==THY_OK);
    CHECK(thy_instrument_set_pad_velocity(&instrument,(uint8_t)a,&strike_velocity)==THY_OK);
    before=instrument;
    CHECK(thy_instrument_scale_cluster_sensitivity(&instrument,0u,1250u)==THY_OK);
    CHECK(thy_instrument_cluster_trigger(&instrument,0u)==1250u && thy_instrument_cluster_trigger(&instrument,1u)==1500u);
    CHECK(instrument.response.attack_raw==1377u);
    CHECK(instrument.sensors[a].response.attack_raw==1000u && instrument.sensors[a].response.release_raw==950u);
    CHECK(instrument.sensors[b].response.attack_raw==1500u && instrument.sensors[b].response.release_raw==900u);
    for(unsigned p=0;p<THY_PAD_COUNT;++p){
        CHECK(instrument.sensors[p].response_overridden==before.sensors[p].response_overridden);
        CHECK(instrument.velocity_overridden[p]==before.velocity_overridden[p]);
        CHECK(!memcmp(&instrument.pad_velocity[p],&before.pad_velocity[p],sizeof instrument.pad_velocity[p]));
        CHECK(instrument.sensors[p].response.pressure_floor==before.sensors[p].response.pressure_floor);
        CHECK(instrument.sensors[p].response.pressure_full_scale==before.sensors[p].response.pressure_full_scale);
        CHECK(instrument.sensors[p].response.pressure_curve==before.sensors[p].response.pressure_curve);
        if(mapping[p]>=48u){
            CHECK(!memcmp(&instrument.sensors[p],&before.sensors[p],sizeof instrument.sensors[p]));
            CHECK(!memcmp(&instrument.pad_release_policy[p],&before.pad_release_policy[p],sizeof instrument.pad_release_policy[p]));
        }else if(p!=a && p!=b){
            CHECK(!instrument.sensors[p].response_overridden);
            CHECK(instrument.sensors[p].response.attack_raw==1250u && instrument.sensors[p].response.release_raw==1227u);
        }
    }
    CHECK(!memcmp(instrument.cluster_velocity,before.cluster_velocity,sizeof instrument.cluster_velocity));
    CHECK(thy_instrument_scale_cluster_sensitivity(&instrument,0u,1000u)==THY_OK);
    CHECK(instrument.sensors[a].response.attack_raw==800u && instrument.sensors[b].response.attack_raw==1200u);
    before=instrument;
    CHECK(thy_instrument_scale_cluster_sensitivity(&instrument,0u,1000u)==THY_OK);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    instrument.sensors[b].held=1u;before=instrument;before_core=core;
    CHECK(thy_instrument_scale_cluster_sensitivity(&instrument,0u,1100u)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument) && !memcmp(&before_core,&core,sizeof core));
    instrument.sensors[b].held=0u;before=instrument;
    CHECK(thy_instrument_scale_cluster_sensitivity(&instrument,0u,700u)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    CHECK(thy_instrument_scale_cluster_sensitivity(&instrument,0u,4095u)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    CHECK(thy_instrument_scale_cluster_sensitivity(&instrument,0u,0u)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_scale_cluster_sensitivity(&instrument,2u,1100u)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    instrument.sensors[other].held=1u;before=instrument;
    CHECK(thy_instrument_scale_cluster_sensitivity(&instrument,0u,1100u)==THY_OK);
    CHECK(!memcmp(&instrument.sensors[other],&before.sensors[other],sizeof instrument.sensors[other]));
    instrument.sensors[other].held=0u;
    return 1;
}
static int test_cluster_profile_checks_independent_inheritance_union_atomically(void)
{
    static thy_instrument before;
    CHECK(cluster_fixture());
    thy_instrument_response local={1600u,1500u,1700u,3500u,THY_PRESSURE_FIRM};
    CHECK(thy_instrument_set_pad_response(&instrument,0u,&local)==THY_OK);
    thy_instrument_response requested={1300u,1277u,1400u,3000u,THY_PRESSURE_SOFT};
    thy_instrument_velocity velocity={4u,150u,THY_VELOCITY_STRIKE,THY_PRESSURE_LINEAR,8u,120u};
    thy_instrument_release_policy policy={23u,1u};
    instrument.sensors[0].held=1u;before=instrument;
    CHECK(thy_instrument_check_cluster_sensitivity_policy(&instrument,0u,&requested,&velocity,&policy)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,0u,&requested,&velocity,&policy)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    instrument.sensors[0].held=0u;
    CHECK(thy_instrument_set_pad_response(&instrument,0u,NULL)==THY_OK);
    CHECK(thy_instrument_set_pad_velocity(&instrument,0u,&instrument.velocity_response)==THY_OK);
    instrument.sensors[0].held=1u;before=instrument;
    CHECK(thy_instrument_check_cluster_sensitivity_policy(&instrument,0u,&requested,&velocity,&policy)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    instrument.sensors[0].held=0u;
    CHECK(thy_instrument_set_pad_response(&instrument,0u,&local)==THY_OK);
    instrument.sensors[0].held=1u;before=instrument;
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,0u,&requested,&velocity,&policy)==THY_OK);
    CHECK(!memcmp(&instrument.sensors[0],&before.sensors[0],sizeof instrument.sensors[0]));
    CHECK(instrument.velocity_overridden[0] && instrument.pad_velocity[0].mode==THY_VELOCITY_FIXED);
    CHECK(instrument.cluster_response[1].attack_raw==1377u && instrument.cluster_velocity[1].mode==THY_VELOCITY_FIXED);
    instrument.sensors[0].held=0u;before=instrument;
    requested.release_raw=1276u;
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,0u,&requested,&velocity,&policy)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    CHECK(thy_instrument_get_cluster_response(&instrument,2u,&requested)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_get_cluster_velocity(&instrument,2u,&velocity)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_get_cluster_release_policy(&instrument,2u,&policy)==THY_INVALID_ARGUMENT);
    return 1;
}
static int test_mapping_rebinds_cluster_inheritance_without_rewriting_overrides(void)
{
    static thy_instrument before;
    CHECK(cluster_fixture());
    thy_instrument_response left={900u,870u,1000u,3000u,THY_PRESSURE_SOFT};
    thy_instrument_response right={1600u,1550u,1700u,4000u,THY_PRESSURE_FIRM};
    thy_instrument_velocity fixed=instrument.velocity_response;
    thy_instrument_velocity strike_velocity={4u,140u,THY_VELOCITY_STRIKE,THY_PRESSURE_SOFT,10u,120u};
    thy_instrument_release_policy left_policy={30u,1u},right_policy={50u,1u};
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,0u,&left,&strike_velocity,&left_policy)==THY_OK);
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,1u,&right,&fixed,&right_policy)==THY_OK);
    uint8_t swapped[THY_PAD_COUNT];for(unsigned p=0;p<THY_PAD_COUNT;++p)swapped[p]=(uint8_t)p;
    swapped[0]=48u;swapped[48]=0u;
    instrument.sensors[48].raw=1000u;before=instrument;
    CHECK(thy_instrument_set_mapping(&instrument,swapped)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    instrument.sensors[48].raw=4u;
    CHECK(thy_instrument_set_mapping(&instrument,swapped)==THY_OK);
    CHECK(instrument.sensors[0].response.attack_raw==1600u && instrument.pad_release_policy[0].gap==50u);
    CHECK(instrument.sensors[48].response.attack_raw==900u && instrument.pad_release_policy[48].gap==30u);
    CHECK(!instrument.sensors[0].response_overridden && !instrument.sensors[48].response_overridden);
    thy_instrument_velocity got;uint8_t overridden;
    CHECK(thy_instrument_get_velocity(&instrument,0,&got,&overridden)==THY_OK && !overridden && got.mode==THY_VELOCITY_FIXED);
    CHECK(thy_instrument_get_velocity(&instrument,48,&got,&overridden)==THY_OK && !overridden && got.mode==THY_VELOCITY_STRIKE);
    thy_instrument_response local={1800u,1700u,2000u,3900u,THY_PRESSURE_LINEAR};
    CHECK(thy_instrument_set_pad_response(&instrument,0u,&local)==THY_OK);
    CHECK(thy_instrument_set_pad_velocity(&instrument,0u,&fixed)==THY_OK);
    before=instrument;
    swapped[0]=0u;swapped[48]=48u;
    CHECK(thy_instrument_set_mapping(&instrument,swapped)==THY_OK);
    CHECK(!memcmp(&instrument.sensors[0].response,&before.sensors[0].response,sizeof local));
    CHECK(instrument.sensors[0].response_overridden && instrument.velocity_overridden[0]);
    CHECK(!memcmp(&instrument.pad_velocity[0],&before.pad_velocity[0],sizeof fixed));
    CHECK(instrument.sensors[48].response.attack_raw==1600u && !instrument.sensors[48].response_overridden);
    CHECK(thy_instrument_get_velocity(&instrument,48,&got,&overridden)==THY_OK && !overridden && got.mode==THY_VELOCITY_FIXED);
    return 1;
}
static int test_learned_guards_persist_across_edits_and_clear_only_selected_reset(void)
{
    static thy_instrument before;
    CHECK(cluster_fixture());
    for(unsigned p=0;p<THY_PAD_COUNT;++p)CHECK(!instrument.sensitivity_guard[p]);
    CHECK(thy_instrument_set_trigger(&instrument,-1,1000u)==THY_OK);
    thy_instrument_response local={800u,750u,1353u,3500u,THY_PRESSURE_SOFT};
    thy_instrument_release_policy linked={50u,1u};
    CHECK(thy_instrument_set_response_policy(&instrument,0,&local,&linked)==THY_OK);
    thy_instrument_velocity strike={3u,140u,THY_VELOCITY_STRIKE,THY_PRESSURE_FIRM,10u,120u};
    CHECK(thy_instrument_set_pad_velocity(&instrument,0u,&strike)==THY_OK);
    CHECK(thy_instrument_set_pad_guard(&instrument,0u,600u)==THY_OK);
    CHECK(thy_instrument_set_pad_guard(&instrument,48u,300u)==THY_OK);
    uint16_t guard=0u;
    CHECK(thy_instrument_get_pad_guard(&instrument,0u,&guard)==THY_OK && guard==600u);
    before=instrument;
    CHECK(thy_instrument_scale_cluster_sensitivity(&instrument,0u,800u)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_scale_sensitivity(&instrument,800u)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    CHECK(thy_instrument_scale_cluster_sensitivity(&instrument,0u,4095u)==THY_OK);
    CHECK(!memcmp(instrument.sensitivity_guard,before.sensitivity_guard,sizeof instrument.sensitivity_guard));
    CHECK(instrument.sensors[0].response_overridden && instrument.velocity_overridden[0]);
    CHECK(!memcmp(&instrument.pad_velocity[0],&strike,sizeof strike));
    CHECK(instrument.cluster_velocity[0].mode==THY_VELOCITY_FIXED);
    thy_instrument_response inherited={1300u,1277u,1353u,4095u,THY_PRESSURE_LINEAR};
    CHECK(thy_instrument_set_cluster_response(&instrument,0u,&inherited)==THY_OK);
    CHECK(instrument.sensitivity_guard[0]==600u && instrument.sensitivity_guard[48]==300u);
    CHECK(thy_instrument_reset_sensitivity(&instrument,-1)==THY_OK);
    CHECK(instrument.sensitivity_guard[0]==600u && instrument.sensitivity_guard[48]==300u);
    /* Advanced ADC edits retain the learned source guard. Its enforcement is
     * deliberately in the simple control bounds, not detector wire validity. */
    local=(thy_instrument_response){1500u,1400u,1500u,3500u,THY_PRESSURE_LINEAR};
    CHECK(thy_instrument_set_pad_response(&instrument,0u,&local)==THY_OK);
    CHECK(instrument.sensitivity_guard[0]==600u);
    uint8_t mapping[THY_PAD_COUNT];for(unsigned p=0;p<THY_PAD_COUNT;++p)mapping[p]=(uint8_t)p;
    mapping[0]=48u;mapping[48]=0u;
    CHECK(thy_instrument_set_mapping(&instrument,mapping)==THY_OK);
    CHECK(instrument.sensitivity_guard[0]==600u && instrument.sensitivity_guard[48]==300u);
    CHECK(thy_instrument_reset_sensitivity(&instrument,48)==THY_OK);
    CHECK(instrument.sensitivity_guard[48]==0u && instrument.sensitivity_guard[0]==600u);
    instrument.sensors[0].held=1u;before=instrument;
    CHECK(thy_instrument_set_pad_guard(&instrument,0u,500u)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_pad_response(&instrument,0u,NULL)==THY_INVALID_ARGUMENT);
    before.menu_notice=instrument.menu_notice;
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    instrument.sensors[0].held=0u;instrument.menu_notice=0u;before=instrument;
    CHECK(thy_instrument_set_pad_guard(&instrument,0u,4095u)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_pad_guard(&instrument,96u,0u)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    guard=123u;CHECK(thy_instrument_get_pad_guard(&instrument,96u,&guard)==THY_INVALID_ARGUMENT && guard==123u);
    CHECK(thy_instrument_set_pad_response(&instrument,0u,NULL)==THY_OK);
    CHECK(!instrument.sensitivity_guard[0] && !instrument.sensitivity_guard[48]);
    CHECK(instrument.velocity_overridden[0] && instrument.pad_velocity[0].mode==THY_VELOCITY_STRIKE);
    return 1;
}

int main(void)
{
    static int (*const tests[])(void) = {
        test_all_96_owners_all_routes_and_mapping,
        test_every_control_edge_and_policy,
        test_idle_drift_noise_and_held_baseline,
        test_warmup_and_hysteresis_pressure,
        test_held_routes_root_layout_pressure_and_release,
        test_original_slot_holes_note_zero_and_edits,
        test_generated_full_melody_and_strum_cancel,
        test_panic_blocks_held_until_release,
        test_bad_press_does_not_attack_after_settings_change,
        test_negative_polarity_invalid_atomicity_and_presets,
        test_scale_shortcut_defaults_validation_and_held_ownership,
        test_transport_gating_no_reconnect_ghosts,
        test_factory_floor_coupling_and_residual_release,
        test_factory_gate_boundaries_and_spike_filter,
        test_symmetric_drift_negative_excursion_and_rebound,
        test_factory_startup_stability_and_held_boot,
        test_factory_sustained_ownership_gating_and_pressure,
        test_transition_telemetry_readonly_bounds_and_profile_atomicity,
        test_custom_response_validation_and_atomic_busy_edits,
        test_custom_quiet_qualification_hysteresis_and_no_ghosts,
        test_custom_global_inheritance_and_override_clear_atomicity,
        test_pressure_curves_and_fixed_velocity_are_independent,
        test_readonly_pad_preview_matches_shaped_attack,
        test_response_menu_and_lighting_bounds,
        test_legacy_center_toggle_and_right_editor_preserve_held_notes,
        test_strike_curves_are_monotonic_bounded_and_validated,
        test_strike_samples_separate_attacks_without_an_extra_wait,
        test_fixed_velocity_and_direct_press_fallback_remain_available,
        test_velocity_override_inheritance_and_reset_are_atomic,
        test_velocity_stale_samples_wrap_and_negative_polarity
        ,test_linked_release_and_independent_aftertouch
        ,test_release_policy_edits_reject_busy_without_partial_publish
        ,test_full_sensitivity_policy_profile_is_atomic_across_inheritance_sets
        ,test_all_sensitivity_preserves_calibrated_overrides_atomically
        ,test_transient_sampling_preview_uses_candidate_without_saving_it
        ,test_transient_sampling_preview_rejects_invalid_outputs_atomically
        ,test_transient_sampling_preview_is_absolute_over_saved_relative_polarity
        ,test_cluster_defaults_are_independent_and_per_pad_reset_inherits_side
        ,test_cluster_scaling_preserves_fit_flags_expression_and_other_half
        ,test_cluster_profile_checks_independent_inheritance_union_atomically
        ,test_mapping_rebinds_cluster_inheritance_without_rewriting_overrides
        ,test_learned_guards_persist_across_edits_and_clear_only_selected_reset
    };
    unsigned i;
    for (i = 0u; i < sizeof tests / sizeof tests[0]; ++i)
        if (!tests[i]()) return 1;
    printf("%u instrument regression tests passed (%u assertions); state=%zu bytes, one external core\n",
        i, assertions, sizeof(thy_instrument));
    return 0;
}
