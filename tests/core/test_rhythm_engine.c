/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_engine.h"

static thy_core core, saved_core;
static thy_instrument instrument;
static thy_engine engine, saved_engine;
static unsigned assertions;
#define CHECK(x) do { ++assertions; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 0; } } while (0)

static void initialize(void)
{
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&engine, &instrument, 21u, 70u, undo_workspace_1);
}
static unsigned drain(void)
{
    unsigned count = 0;
    thy_midi_event event;
    while (thy_core_poll(&core, &event)) ++count;
    return count;
}
static int acquire(uint32_t source, uint8_t pitch, thy_generated_handle *handle)
{
    return thy_engine_generated_acquire(&engine, source,
        thy_engine_source_generation(&engine, source), pitch, 2u, THY_ROUTE_USB0 | THY_ROUTE_DIN, 96u, handle);
}

static int stop_preserves_live_and_independent_generators(void)
{
    initialize();
    CHECK(thy_engine_advance(&engine, 0u) == THY_OK);
    const int16_t note = 60;
    CHECK(thy_core_press(&core, 0u, &note, 1u, 0, 2u, 5u, 96u) == THY_OK);
    CHECK(thy_engine_track_loop(&engine, 0u, 120u, 1920u) == THY_OK);
    CHECK(thy_engine_track_toggle(&engine, 0u) == THY_OK);
    CHECK(thy_engine_transport_start(&engine) == THY_OK);
    thy_generated_handle track, arp;
    CHECK(acquire(THY_ENGINE_TRACK_BASE, 60u, &track) == THY_OK);
    CHECK(acquire(THY_ENGINE_ARP1, 60u, &arp) == THY_OK);
    uint16_t key = thy_core_key(THY_USB0, 2u, 60u);
    CHECK(thy_core_refcount(&core, key) == 3u && drain() == 2u);
    CHECK(thy_engine_advance(&engine, 333u) == THY_OK);
    uint64_t clock_tick = engine.musical_clock.tick;
    uint32_t arp_run = thy_engine_source_generation(&engine, THY_ENGINE_ARP1);
    CHECK(thy_engine_transport_stop(&engine) == THY_OK);
    CHECK(engine.musical_clock.tick == clock_tick);
    CHECK(engine.rhythm_transport.position == 0u && engine.rhythm_transport.tracks[0].playhead == 120u);
    CHECK(thy_core_pad_held(&core, 0u) && thy_core_generated_count(&core) == 1u);
    CHECK(thy_core_refcount(&core, key) == 2u && drain() == 0u);
    CHECK(thy_engine_source_generation(&engine, THY_ENGINE_ARP1) == arp_run);
    CHECK(thy_core_generated_release(&core, track) == THY_OK && drain() == 0u);
    CHECK(thy_core_generated_release(&core, arp) == THY_OK && drain() == 0u);
    CHECK(thy_core_release(&core, 0u) == THY_OK && drain() == 2u);
    return 1;
}

static int queued_stop_and_old_callbacks(void)
{
    initialize();
    CHECK(thy_engine_advance(&engine, 0u) == THY_OK);
    CHECK(thy_engine_track_toggle(&engine, 0u) == THY_OK);
    CHECK(thy_engine_transport_start(&engine) == THY_OK);
    thy_generated_handle handle, sentinel = 999u;
    CHECK(acquire(THY_ENGINE_TRACK_BASE, 64u, &handle) == THY_OK && drain() == 2u);
    uint32_t old_run = thy_engine_source_generation(&engine, THY_ENGINE_TRACK_BASE);
    CHECK(thy_engine_advance(&engine, 100u) == THY_OK);
    CHECK(thy_engine_track_toggle(&engine, 0u) == THY_OK);
    CHECK(engine.rhythm_transport.tracks[0].pending_position == 3840u);
    CHECK(thy_engine_track_toggle(&engine, 0u) == THY_OK);
    CHECK(!engine.rhythm_transport.tracks[0].pending && thy_core_generated_count(&core) == 1u);
    CHECK(thy_engine_track_toggle(&engine, 0u) == THY_OK);
    CHECK(thy_engine_advance(&engine, 2000u) == THY_OK);
    CHECK(!engine.rhythm_transport.tracks[0].running && drain() == 2u);
    CHECK(thy_engine_generated_acquire(&engine, THY_ENGINE_TRACK_BASE, old_run,
        64u, 2u, 5u, 96u, &sentinel) == THY_ENGINE_STALE_RUN && sentinel == 999u);
    CHECK(thy_engine_track_toggle(&engine, 0u) == THY_OK);
    CHECK(thy_engine_advance(&engine, 4000u) == THY_OK);
    CHECK(engine.rhythm_transport.tracks[0].running);
    CHECK(acquire(THY_ENGINE_TRACK_BASE, 64u, &sentinel) == THY_OK);
    CHECK(sentinel != handle && drain() == 2u);
    CHECK(thy_core_generated_release(&core, handle) == THY_OK && thy_core_generated_count(&core) == 1u);
    return 1;
}

static int usb_loss_blocks_attacks_without_touching_pads(void)
{
    initialize();
    CHECK(thy_engine_advance(&engine, 0u) == THY_OK);
    const int16_t note = 60;
    CHECK(thy_core_press(&core, 0u, &note, 1u, 0, 2u, 5u, 96u) == THY_OK);
    thy_generated_handle handle;
    CHECK(acquire(THY_ENGINE_ARP1, 60u, &handle) == THY_OK);
    uint32_t old_run = thy_engine_source_generation(&engine, THY_ENGINE_ARP1);
    CHECK(thy_engine_clock_configure(&engine, 120000u, THY_CLOCK_SOURCE_USB, 4u, 4u, THY_TRANSPORT_BAR) == THY_OK);
    CHECK(thy_core_generated_count(&core) == 0u && thy_core_pad_held(&core, 0u));
    CHECK(acquire(THY_ENGINE_ARP1, 60u, &handle) == THY_ENGINE_SOURCE_NOT_READY);
    const uint8_t pulse = 0xf8u;
    CHECK(thy_engine_receive_time(&engine, &pulse, 1u, 0u) == THY_OK);
    CHECK(acquire(THY_ENGINE_ARP1, 60u, &handle) == THY_OK);
    drain();
    CHECK(thy_engine_advance(&engine, 500u) == THY_OK);
    CHECK(engine.musical_clock.status == THY_CLOCK_LOST && thy_core_generated_count(&core) == 0u);
    CHECK(thy_core_pad_held(&core, 0u) && drain() == 0u);
    CHECK(thy_engine_generated_acquire(&engine, THY_ENGINE_ARP1, old_run,
        64u, 2u, 5u, 96u, &handle) == THY_ENGINE_STALE_RUN);
    CHECK(thy_engine_receive_time(&engine, &pulse, 1u, 520u) == THY_OK);
    CHECK(engine.musical_clock.status == THY_CLOCK_LOCKED && thy_core_generated_count(&core) == 0u);
    CHECK(acquire(THY_ENGINE_ARP1, 60u, &handle) == THY_OK);
    CHECK(thy_core_release(&core, 0u) == THY_OK && drain() == 0u);
    CHECK(thy_core_generated_release(&core, handle) == THY_OK && drain() == 2u);
    return 1;
}

static int panic_invalidates_pending_generated_runs(void)
{
    initialize();
    thy_generated_handle old_handle, new_handle;
    uint32_t run = thy_engine_source_generation(&engine, THY_ENGINE_ARP1);
    CHECK(acquire(THY_ENGINE_ARP1, 67u, &old_handle) == THY_OK);
    drain();
    thy_engine_panic(&engine);
    CHECK(!thy_core_generated_count(&core) && drain() == 2u);
    CHECK(thy_engine_generated_acquire(&engine, THY_ENGINE_ARP1, run,
        67u, 2u, 5u, 96u, &new_handle) == THY_ENGINE_STALE_RUN);
    CHECK(acquire(THY_ENGINE_ARP1, 67u, &new_handle) == THY_OK && new_handle > old_handle);
    drain();
    CHECK(thy_core_generated_release(&core, old_handle) == THY_OK);
    CHECK(thy_core_generated_count(&core) == 1u && drain() == 0u);
    return 1;
}

static int invalid_operations_are_atomic(void)
{
    initialize();
    CHECK(thy_engine_advance(&engine, 1000u) == THY_OK);
    CHECK(thy_engine_advance(&engine, 1100u) == THY_OK);
    saved_engine = engine; saved_core = core;
    CHECK(thy_engine_advance(&engine, 1099u) == THY_ENGINE_TIME_BACKWARD);
    CHECK(memcmp(&engine, &saved_engine, sizeof engine) == 0 && memcmp(&core, &saved_core, sizeof core) == 0);
    CHECK(thy_engine_clock_configure(&engine, 120000u, 0u, 4u, 3u, 2u) == THY_INVALID_ARGUMENT);
    CHECK(thy_engine_track_toggle(&engine, 21u) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&engine, &saved_engine, sizeof engine) == 0);
    engine.rhythm_transport.tracks[0].enabled = 1u;
    engine.rhythm_transport.tracks[0].generation = UINT32_MAX;
    engine.musical_clock.tick += 100u;
    saved_engine = engine;
    CHECK(thy_engine_transport_start(&engine) == THY_ENGINE_TIME_OVERFLOW);
    CHECK(memcmp(&engine, &saved_engine, sizeof engine) == 0);
    return 1;
}

static int track_stop_preserves_live_strum_schedule(void)
{
    initialize();
    CHECK(thy_engine_advance(&engine, 0u) == THY_OK);
    const int16_t notes[2] = {60, 64};
    const thy_shape shape = {.strum_ms = 100u};
    CHECK(thy_core_press_shaped(&core, 0u, notes, 2u, 0, 0u, 1u, 96u, &shape) == THY_OK);
    CHECK(thy_core_pending(&core) == 1u && drain() == 1u);
    CHECK(thy_engine_transport_stop(&engine) == THY_OK && thy_core_pending(&core) == 1u);
    CHECK(thy_engine_advance(&engine, 100u) == THY_OK && thy_core_pending(&core) == 0u);
    CHECK(drain() == 1u && thy_core_pad_held(&core, 0u));
    return 1;
}

static int menu_navigation_does_not_touch_musical_runtime(void)
{
    initialize();
    CHECK(thy_engine_advance(&engine, 0u) == THY_OK);
    CHECK(thy_engine_transport_start(&engine) == THY_OK);
    thy_generated_handle handle;
    CHECK(acquire(THY_ENGINE_ARP1, 60u, &handle) == THY_OK);
    drain();
    const thy_clock clock = engine.musical_clock;
    const thy_transport transport = engine.rhythm_transport;
    CHECK(thy_engine_navigate(&engine, THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine, THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(memcmp(&clock, &engine.musical_clock, sizeof clock) == 0);
    CHECK(memcmp(&transport, &engine.rhythm_transport, sizeof transport) == 0);
    CHECK(thy_core_generated_count(&core) == 1u && drain() == 0u);
    return 1;
}

static int midi_seek_releases_only_track_owners(void)
{
    initialize();
    CHECK(thy_engine_clock_configure(&engine, 120000u, 1u, 4u, 4u, 2u) == THY_OK);
    const uint8_t pulse = 0xf8u;
    CHECK(thy_engine_receive_time(&engine, &pulse, 1u, 0u) == THY_OK);
    CHECK(thy_engine_track_toggle(&engine, 0u) == THY_OK);
    CHECK(thy_engine_transport_start(&engine) == THY_OK);
    thy_generated_handle track, arp;
    CHECK(acquire(THY_ENGINE_TRACK_BASE, 64u, &track) == THY_OK);
    CHECK(acquire(THY_ENGINE_ARP1, 67u, &arp) == THY_OK);
    drain();
    const uint8_t position[3] = {0xf2u, 8u, 0u};
    uint64_t tick = engine.musical_clock.tick;
    CHECK(thy_engine_receive_time(&engine, position, 3u, 0u) == THY_OK);
    CHECK(engine.rhythm_transport.position == 1920u && engine.musical_clock.tick == tick);
    CHECK(thy_core_generated_count(&core) == 1u && thy_core_generated_find(&core, arp));
    CHECK(drain() == 2u);
    const uint8_t malformed[3] = {0xf2u, 128u, 0u};
    saved_engine = engine;
    CHECK(thy_engine_receive_time(&engine, malformed, 3u, 1u) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&engine, &saved_engine, sizeof engine) == 0);
    return 1;
}

int main(void)
{
    if (!stop_preserves_live_and_independent_generators() || !queued_stop_and_old_callbacks() ||
        !usb_loss_blocks_attacks_without_touching_pads() || !panic_invalidates_pending_generated_runs() ||
        !invalid_operations_are_atomic() || !track_stop_preserves_live_strum_schedule() ||
        !menu_navigation_does_not_touch_musical_runtime() || !midi_seek_releases_only_track_owners()) return 1;
    printf("Rhythm engine tests passed: 8 scenarios, %u assertions\n", assertions);
    return 0;
}
