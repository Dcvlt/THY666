/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_engine.h"
#include "../../core/thy_lighting.h"

static thy_core core, saved_core;
static thy_instrument instrument;
static thy_engine engine, saved_engine;
static thy_instrument_pad layout[THY_PAD_COUNT];
static unsigned assertions;
#define CHECK(x) do { ++assertions; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 0; } } while (0)

static void initialize(void)
{
    static const int16_t degrees[] = {0,2,4,5,7,9,11};
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    for (unsigned n = 0u; n < THY_PAD_COUNT; ++n)
        layout[n] = (thy_instrument_pad){.notes = {60,64,67},
            .valid_mask = n < 48u ? 7u : 1u, .color = 1u};
    layout[4] = (thy_instrument_pad){.notes = {65,69,72}, .valid_mask = 7u, .color = 4u};
    layout[48].notes[0] = 62;
    (void)thy_instrument_set_layout(&instrument, layout, 60, degrees, 7u);
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&engine, &instrument, 21u, 70u, undo_workspace_1);
    (void)thy_engine_advance(&engine, 0u);
}
static unsigned drain(void)
{
    thy_midi_event event; unsigned count = 0u;
    while (thy_core_poll(&core, &event)) ++count;
    return count;
}
static int enable(unsigned index, int layer)
{
    thy_arp_config config = engine.arps[index].config;
    config.enabled = 1u; config.layer = (uint8_t)layer;
    config.channel = (uint8_t)(index + 2u);
    config.destinations = THY_ROUTE_USB0;
    return thy_engine_arp_configure(&engine, (uint8_t)index, &config);
}
static int bit(const uint64_t pitches[2], unsigned note)
{
    return !!(pitches[note / 64u] & (UINT64_C(1) << (note % 64u)));
}

static int real_pads_hold_and_layer(void)
{
    initialize();
    CHECK(!engine.arps[0].config.enabled && !engine.arps[1].config.enabled);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(core.owners[0].count == 3u && drain() == 3u);
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK && drain() == 3u);
    CHECK(enable(0u, 0) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(core.owners[0].held && !core.owners[0].count);
    CHECK(engine.arps[0].held_count == 1u && core.generated_count == 1u && drain() == 1u);
    CHECK(thy_engine_advance(&engine, 63u) == THY_OK && drain() == 1u);
    CHECK(thy_engine_advance(&engine, 125u) == THY_OK && drain() == 1u);
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK && drain() == 1u);
    CHECK(!engine.arps[0].held_count && !core.generated_count);
    CHECK(enable(0u, 1) == THY_OK && enable(1u, 1) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(core.owners[0].count == 3u && core.generated_count == 2u);
    CHECK(thy_core_refcount(&core, thy_core_key(0u, 0u, 60u)) == 1u);
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    CHECK(!core.generated_count && !core.sounding_count);
    return 1;
}

static int newest_chord_and_duplicate_contributions(void)
{
    initialize(); CHECK(enable(0u, 0) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(thy_engine_advance(&engine, 50u) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 48u) == THY_OK);
    CHECK(bit(engine.arps[0].phrases[0].pitches, 62u));
    CHECK(engine.arps[0].phrases[0].anchor == 0u && engine.arps[0].phrases[0].ordinal == 1u);
    CHECK(thy_instrument_press_pad(&instrument, 4u) == THY_OK);
    CHECK(!bit(engine.arps[0].phrases[0].pitches, 62u));
    CHECK(bit(engine.arps[0].phrases[4].pitches, 62u));
    CHECK(engine.arps[0].phrase_count == 2u);
    CHECK(thy_instrument_release_pad(&instrument, 4u) == THY_OK);
    CHECK(bit(engine.arps[0].phrases[0].pitches, 62u));
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    CHECK(engine.arps[0].phrases[THY_ARP_POOL].active);
    CHECK(thy_instrument_release_pad(&instrument, 48u) == THY_OK);
    CHECK(!engine.arps[0].held_count && !core.generated_count);
    /* A right note matching the chord is an independent contribution. */
    const int16_t note = 60;
    CHECK(thy_engine_press_shaped(&engine, 0u, layout[0].notes, 3u, 0, 0u, 1u, 96u, NULL) == THY_OK);
    CHECK(thy_engine_press_shaped(&engine, 48u, &note, 1u, 0, 1u, 1u, 96u, NULL) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument, 48u) == THY_OK);
    CHECK(bit(engine.arps[0].phrases[0].pitches, 60u));
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    return 1;
}

static int filters_use_logical_zones_with_electrical_lifetimes(void)
{
    initialize();
    uint8_t mapping[THY_PAD_COUNT];
    for (unsigned n = 0u; n < THY_PAD_COUNT; ++n) mapping[n] = (uint8_t)n;
    mapping[0] = 16u; mapping[16] = 0u;
    CHECK(thy_instrument_set_mapping(&instrument, mapping) == THY_OK);
    CHECK(enable(0u, 0) == THY_OK);
    thy_arp_config config = engine.arps[0].config;
    config.source_zones = 1u;
    CHECK(thy_engine_arp_configure(&engine, 0u, &config) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(core.owners[0].count == 3u && !engine.arps[0].held_count);
    CHECK(thy_instrument_press_pad(&instrument, 16u) == THY_OK);
    CHECK(!core.owners[16].count && engine.arps[0].inputs[16].held);
    CHECK(thy_instrument_press_pad(&instrument, 4u) == THY_OK);
    CHECK(engine.arps[0].inputs[4].held);
    config.source_zones = 56u;
    CHECK(thy_engine_arp_configure(&engine, 0u, &config) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument, 16u) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument, 4u) == THY_OK);
    CHECK(!engine.arps[0].held_count && !core.generated_count);
    CHECK(thy_core_pad_held(&core, 0u));
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    return 1;
}

static int lifecycle_no_readoption_or_live_transport_stop(void)
{
    initialize(); CHECK(enable(0u, 0) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(thy_engine_transport_start(&engine) == THY_OK);
    CHECK(thy_engine_transport_stop(&engine) == THY_OK);
    CHECK(engine.arps[0].held_count == 1u && core.generated_count == 1u);
    CHECK(thy_engine_navigate(&engine, THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(engine.arps[0].held_count == 1u);
    CHECK(thy_engine_arp_clear(&engine, 0u) == THY_OK);
    CHECK(!core.generated_count && thy_core_pad_held(&core, 0u));
    CHECK(thy_engine_advance(&engine, 1000u) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(!engine.arps[0].held_count && !core.generated_count);
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(core.generated_count == 1u);
    thy_instrument_panic(&instrument);
    CHECK(!engine.arps[0].held_count && !core.generated_count);
    CHECK(thy_engine_advance(&engine, 2000u) == THY_OK && !core.generated_count);
    return 1;
}

static int physical_snapshot_lighting(void)
{
    initialize(); CHECK(enable(0u, 0) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(instrument.held_input_valid[0] && !core.owners[0].count);
    thy_lighting lighting; thy_lighting_config config; uint32_t colors[THY_PAD_COUNT];
    thy_lighting_init(&lighting); thy_lighting_defaults(&config);
    config.fade_ms = 0u; config.held_style = THY_LIGHTING_HELD_WHITE;
    config.held_brightness = 255u;
    CHECK(thy_lighting_render(&lighting, &instrument, &config, 0u, colors) == THY_OK);
    CHECK(colors[0] == UINT32_C(0xffffff));
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    CHECK(!instrument.held_input_valid[0]);
    return 1;
}

static int atomic_invalid_and_exhausted_admission(void)
{
    initialize(); CHECK(enable(0u, 0) == THY_OK && enable(1u, 0) == THY_OK);
    engine.arps[1].press_order = UINT64_MAX;
    saved_core = core; saved_engine = engine;
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OWNER_LIMIT);
    CHECK(memcmp(&core, &saved_core, sizeof core) == 0);
    CHECK(memcmp(&engine, &saved_engine, sizeof engine) == 0);
    thy_arp_config config = engine.arps[0].config;
    config.rate_ticks = 0u;
    CHECK(thy_engine_arp_configure(&engine, 0u, &config) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&engine, &saved_engine, sizeof engine) == 0);
    CHECK(thy_engine_advance(&engine, UINT32_MAX) == THY_ENGINE_TIME_BACKWARD);
    CHECK(memcmp(&engine, &saved_engine, sizeof engine) == 0);
    return 1;
}

static int usb_loss_waits_for_new_physical_edge(void)
{
    initialize(); CHECK(enable(0u, 0) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK); drain();
    CHECK(thy_engine_clock_configure(&engine, 120000u, THY_CLOCK_SOURCE_USB,
        4u, 4u, THY_TRANSPORT_BAR) == THY_OK);
    CHECK(!engine.arps[0].held_count && !core.generated_count && drain() == 1u);
    const uint8_t pulse = 0xf8u;
    CHECK(thy_engine_receive_time(&engine, &pulse, 1u, 10u) == THY_OK);
    CHECK(!core.generated_count);
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(core.generated_count == 1u); drain();
    CHECK(thy_engine_advance(&engine, 510u) == THY_OK);
    CHECK(!engine.arps[0].held_count && !core.generated_count && drain() == 1u);
    CHECK(thy_engine_receive_time(&engine, &pulse, 1u, 520u) == THY_OK);
    CHECK(!core.generated_count && drain() == 0u);
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    return 1;
}

static int release_edge_precedes_due_attack(void)
{
    initialize(); CHECK(enable(0u, 0) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK && drain() == 1u);
    /* A sensor clock reaches the next step, with no foreground generator
     * service yet. Releasing this contact must not attack its next note. */
    CHECK(thy_instrument_advance(&instrument, 125u) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    thy_midi_event event;
    CHECK(thy_core_poll(&core, &event) && event.status == 0x82u);
    CHECK(!thy_core_poll(&core, &event));
    CHECK(!engine.arps[0].held_count && !core.generated_count);
    return 1;
}

static int raw_sensor_and_input_disable_share_lifecycle(void)
{
    initialize(); CHECK(enable(0u, 0) == THY_OK);
    CHECK(thy_instrument_set_detector_profile(&instrument, THY_DETECTOR_FACTORY_LINEAR) == THY_OK);
    for (uint32_t n = 1u; n <= 8u; ++n)
        CHECK(thy_instrument_sample(&instrument, 0u, 100u, n) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 1600u, 9u) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 1600u, 10u) == THY_OK);
    CHECK(engine.musical_clock.now_ms == 10u && engine.arps[0].held_count == 1u);
    CHECK(core.generated_count == 1u); drain();
    CHECK(thy_instrument_set_input_enabled(&instrument, 0) == THY_OK);
    CHECK(!engine.arps[0].held_count && !core.generated_count);
    CHECK(thy_instrument_set_input_enabled(&instrument, 1) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 1600u, 11u) == THY_OK);
    CHECK(!core.generated_count);
    CHECK(thy_instrument_sample(&instrument, 0u, 100u, 12u) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 100u, 13u) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 1600u, 14u) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 1600u, 15u) == THY_OK);
    CHECK(engine.arps[0].held_count == 1u && core.generated_count == 1u);
    thy_instrument_panic(&instrument);
    return 1;
}

int main(void)
{
    if (!real_pads_hold_and_layer() || !newest_chord_and_duplicate_contributions() ||
        !filters_use_logical_zones_with_electrical_lifetimes() ||
        !lifecycle_no_readoption_or_live_transport_stop() || !physical_snapshot_lighting() ||
        !atomic_invalid_and_exhausted_admission() || !usb_loss_waits_for_new_physical_edge() ||
        !release_edge_precedes_due_attack() || !raw_sensor_and_input_disable_share_lifecycle()) return 1;
    printf("9 Arp engine scenarios passed (%u assertions).\n", assertions);
    return 0;
}
