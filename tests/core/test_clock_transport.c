/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include "../../core/thy_transport.h"

static unsigned assertions;
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 0; \
} } while (0)

static int advance(thy_clock *clock, thy_transport *transport, uint32_t now)
{
    CHECK(thy_clock_advance(clock, now) == THY_TIME_OK);
    CHECK(thy_transport_advance(transport, clock) == THY_TIME_OK);
    return 1;
}

static int midi(thy_clock *clock, thy_transport *transport, uint8_t status, uint32_t now)
{
    CHECK(thy_transport_receive_midi(transport, clock, &status, 1u, now) == THY_TIME_OK);
    return 1;
}

static int test_internal_fractional_clock(void)
{
    thy_clock clock;
    thy_clock_init(&clock, 0u);
    CHECK(clock.tick == 0u && clock.remainder == 0u);
    CHECK(thy_clock_ready(&clock));
    CHECK(clock.tempo_milli_bpm == 120000u && clock.beat_ticks == 960u);
    CHECK(thy_clock_advance(&clock, 1u) == THY_TIME_OK);
    CHECK(clock.tick == 1u && clock.remainder == 55200000u);
    CHECK(thy_clock_advance(&clock, 500u) == THY_TIME_OK);
    CHECK(clock.tick == 960u && clock.remainder == 0u);
    CHECK(thy_clock_advance(&clock, 1000u) == THY_TIME_OK);
    CHECK(clock.tick == 1920u && clock.remainder == 0u);
    CHECK(thy_clock_advance(&clock, 1000u) == THY_TIME_OK);
    CHECK(clock.tick == 1920u);
    return 1;
}

static int test_partition_independent_drift(void)
{
    thy_clock fine, coarse;
    thy_clock_init(&fine, 0u);
    thy_clock_init(&coarse, 0u);
    CHECK(thy_clock_set_tempo(&fine, 123457u, 0u) == THY_TIME_OK);
    CHECK(thy_clock_set_tempo(&coarse, 123457u, 0u) == THY_TIME_OK);
    uint32_t now = 0u;
    for (unsigned n = 0; n < 10000u; ++n) {
        now += (n * 337u % 997u) + 1u;
        CHECK(thy_clock_advance(&fine, now) == THY_TIME_OK);
    }
    CHECK(thy_clock_advance(&coarse, now) == THY_TIME_OK);
    uint64_t rational = (uint64_t)now * 123457u * THY_CLOCK_PPQN;
    CHECK(fine.tick == rational / UINT64_C(60000000));
    CHECK(fine.remainder == rational % UINT64_C(60000000));
    CHECK(fine.tick == coarse.tick && fine.remainder == coarse.remainder);
    return 1;
}

static int test_tempo_edit_preserves_fraction_and_phase(void)
{
    thy_clock clock;
    thy_clock_init(&clock, 0u);
    CHECK(thy_clock_set_tempo(&clock, 120123u, 0u) == THY_TIME_OK);
    CHECK(thy_clock_set_tempo(&clock, 30000u, 1u) == THY_TIME_OK);
    uint64_t scaled = UINT64_C(120123) * THY_CLOCK_PPQN;
    CHECK(clock.tick == scaled / UINT64_C(60000000));
    CHECK(clock.remainder == scaled % UINT64_C(60000000));
    uint32_t generation = clock.generation;
    CHECK(thy_clock_advance(&clock, 2u) == THY_TIME_OK);
    scaled += UINT64_C(30000) * THY_CLOCK_PPQN;
    CHECK(clock.tick == scaled / UINT64_C(60000000));
    CHECK(clock.remainder == scaled % UINT64_C(60000000));
    CHECK(clock.generation == generation);
    CHECK(thy_clock_set_tempo(&clock, 300000u, 2u) == THY_TIME_OK);
    CHECK(thy_clock_advance(&clock, 12u) == THY_TIME_OK);
    scaled += UINT64_C(300000) * THY_CLOCK_PPQN * 10u;
    CHECK(clock.tick == scaled / UINT64_C(60000000));
    CHECK(clock.remainder == scaled % UINT64_C(60000000));
    return 1;
}

static int test_subbeat_tuplets_and_meter(void)
{
    thy_clock clock;
    thy_clock_init(&clock, 0u);
    CHECK(THY_CLOCK_PPQN % 3u == 0u && THY_CLOCK_PPQN % 5u == 0u);
    CHECK(THY_CLOCK_PPQN % 16u == 0u);
    CHECK(thy_clock_set_tempo(&clock, 200000u, 0u) == THY_TIME_OK);
    CHECK(thy_clock_advance(&clock, 100u) == THY_TIME_OK);
    CHECK(clock.tick == THY_CLOCK_PPQN / 3u);
    CHECK(thy_clock_advance(&clock, 150u) == THY_TIME_OK);
    CHECK(clock.tick == THY_CLOCK_PPQN / 2u);
    CHECK(thy_clock_set_meter(&clock, 7u, 8u) == THY_TIME_OK);
    CHECK(clock.beat_ticks == 480u && clock.bar_ticks == 3360u && clock.tick == 480u);
    CHECK(thy_clock_set_meter(&clock, 16u, 32u) == THY_TIME_OK);
    CHECK(clock.beat_ticks == 120u && clock.bar_ticks == 1920u);
    return 1;
}

static int test_large_delay_bounded_conversion(void)
{
    thy_clock clock;
    thy_clock_init(&clock, 0u);
    CHECK(thy_clock_set_tempo(&clock, THY_CLOCK_TEMPO_MAX, 0u) == THY_TIME_OK);
    CHECK(thy_clock_advance(&clock, INT32_MAX) == THY_TIME_OK);
    uint64_t scaled = (uint64_t)INT32_MAX * THY_CLOCK_TEMPO_MAX * THY_CLOCK_PPQN;
    CHECK(clock.tick == scaled / UINT64_C(60000000));
    CHECK(clock.remainder == scaled % UINT64_C(60000000));
    CHECK(clock.tick > UINT32_MAX);
    return 1;
}

static int test_wrap_and_backward_timestamp_rejection(void)
{
    thy_clock clock;
    thy_clock_init(&clock, UINT32_MAX - 100u);
    CHECK(thy_clock_advance(&clock, 200u) == THY_TIME_OK);
    CHECK(clock.tick == 577u && clock.remainder == 55200000u);
    uint64_t tick = clock.tick;
    uint32_t remainder = clock.remainder;
    CHECK(thy_clock_advance(&clock, 199u) == THY_TIME_BACKWARD);
    CHECK(clock.now_ms == 200u && clock.tick == tick && clock.remainder == remainder);
    CHECK(thy_clock_set_tempo(&clock, 100000u, 199u) == THY_TIME_BACKWARD);
    CHECK(clock.tempo_milli_bpm == THY_CLOCK_DEFAULT_TEMPO);
    CHECK(thy_clock_advance(&clock, 201u + (uint32_t)INT32_MAX) == THY_TIME_BACKWARD);
    CHECK(clock.now_ms == 200u);
    return 1;
}

static int test_invalid_clock_configuration_is_atomic(void)
{
    thy_clock clock;
    thy_clock_init(&clock, 0u);
    CHECK(thy_clock_set_tempo(&clock, 19999u, 100u) == THY_TIME_INVALID);
    CHECK(thy_clock_set_tempo(&clock, 300001u, 100u) == THY_TIME_INVALID);
    CHECK(thy_clock_set_source(&clock, 2u, 100u) == THY_TIME_INVALID);
    CHECK(thy_clock_set_meter(&clock, 0u, 4u) == THY_TIME_INVALID);
    CHECK(thy_clock_set_meter(&clock, 17u, 4u) == THY_TIME_INVALID);
    CHECK(thy_clock_set_meter(&clock, 4u, 3u) == THY_TIME_INVALID);
    CHECK(thy_clock_set_meter(&clock, 4u, 0u) == THY_TIME_INVALID);
    CHECK(thy_clock_set_meter(&clock, 4u, 64u) == THY_TIME_INVALID);
    CHECK(thy_clock_set_timeout(&clock, 99u) == THY_TIME_INVALID);
    CHECK(thy_clock_set_timeout(&clock, 10001u) == THY_TIME_INVALID);
    CHECK(clock.now_ms == 0u && clock.tick == 0u && clock.bar_ticks == 3840u);
    CHECK(thy_clock_advance(0, 0u) == THY_TIME_INVALID);
    CHECK(!thy_clock_ready(0));
    return 1;
}

static int test_clock_overflow_is_rejected(void)
{
    thy_clock clock;
    thy_clock_init(&clock, 0u);
    clock.tick = UINT64_MAX;
    CHECK(thy_clock_advance(&clock, 1u) == THY_TIME_OVERFLOW);
    CHECK(clock.tick == UINT64_MAX && clock.now_ms == 0u && clock.remainder == 0u);
    clock.tick = 0u;
    CHECK(thy_clock_set_source(&clock, THY_CLOCK_SOURCE_USB, 0u) == THY_TIME_OK);
    clock.tick = UINT64_MAX - 39u;
    CHECK(thy_clock_midi_pulse(&clock, 1u) == THY_TIME_OVERFLOW);
    CHECK(clock.tick == UINT64_MAX - 39u && clock.status == THY_CLOCK_WAITING);
    CHECK(clock.now_ms == 0u);
    return 1;
}

static int test_independent_track_loops_and_selection(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    CHECK(thy_clock_advance(&clock, 1000u) == THY_TIME_OK);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_set_loop(&transport, 0u, 100u, 1440u) == THY_TIME_OK);
    CHECK(thy_transport_set_loop(&transport, 1u, 500u, 1000u) == THY_TIME_OK);
    CHECK(thy_transport_set_enabled(&transport, 0u, 1u) == THY_TIME_OK);
    CHECK(thy_transport_set_enabled(&transport, 1u, 1u) == THY_TIME_OK);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    CHECK(clock.tick == 1920u && transport.position == 0u);
    CHECK(advance(&clock, &transport, 1375u));
    CHECK(transport.position == 720u);
    CHECK(transport.tracks[0].playhead == 820u && transport.tracks[1].playhead == 1220u);
    uint32_t generation = transport.tracks[0].generation;
    CHECK(thy_transport_select(&transport, 20u) == THY_TIME_OK);
    CHECK(transport.selected_track == 20u && transport.tracks[0].running);
    CHECK(transport.tracks[0].generation == generation);
    CHECK(advance(&clock, &transport, 4375u));
    CHECK(transport.position == 6480u);
    CHECK(transport.tracks[0].playhead == 820u && transport.tracks[1].playhead == 980u);
    CHECK(thy_transport_select(&transport, 21u) == THY_TIME_INVALID);
    CHECK(transport.selected_track == 20u);
    return 1;
}

static int test_enabled_configuration_is_not_launch(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    for (unsigned n = 0u; n < THY_TRANSPORT_TRACKS; ++n)
        CHECK(!transport.tracks[n].running && !transport.tracks[n].enabled);
    CHECK(thy_transport_set_enabled(&transport, 3u, 1u) == THY_TIME_OK);
    CHECK(!transport.tracks[3].running && transport.tracks[3].enabled);
    CHECK(thy_transport_stop(&transport, &clock) == THY_TIME_OK);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    CHECK(transport.tracks[3].running);
    CHECK(thy_transport_set_enabled(&transport, 3u, 0u) == THY_TIME_OK);
    CHECK(transport.tracks[3].running && !transport.tracks[3].enabled);
    CHECK(thy_transport_set_enabled(&transport, 21u, 1u) == THY_TIME_INVALID);
    CHECK(thy_transport_set_enabled(&transport, 3u, 2u) == THY_TIME_INVALID);
    return 1;
}

static int test_exact_next_bar_and_repeated_queue_cancellation(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    CHECK(thy_transport_toggle_track(&transport, 0u, &clock) == THY_TIME_OK);
    CHECK(transport.tracks[0].pending_position == 3840u);
    CHECK(!transport.tracks[0].running && !transport.tracks[0].enabled);
    CHECK(thy_transport_toggle_track(&transport, 0u, &clock) == THY_TIME_OK);
    CHECK(transport.tracks[0].pending == THY_TRANSPORT_PENDING_NONE);
    CHECK(thy_transport_request_track(&transport, 0u, 1u, THY_TRANSPORT_BAR, &clock) == THY_TIME_OK);
    CHECK(thy_transport_request_track(&transport, 0u, 1u, THY_TRANSPORT_BAR, &clock) == THY_TIME_OK);
    CHECK(transport.tracks[0].pending == THY_TRANSPORT_PENDING_NONE);
    CHECK(thy_transport_toggle_track(&transport, 0u, &clock) == THY_TIME_OK);
    CHECK(advance(&clock, &transport, 1999u));
    CHECK(!transport.tracks[0].running);
    CHECK(advance(&clock, &transport, 2000u));
    CHECK(transport.tracks[0].running && transport.tracks[0].enabled);
    CHECK(transport.tracks[0].playhead == 0u && transport.position == 3840u);
    CHECK(thy_transport_toggle_track(&transport, 0u, &clock) == THY_TIME_OK);
    CHECK(transport.tracks[0].pending_position == 7680u);
    CHECK(advance(&clock, &transport, 4000u));
    CHECK(!transport.tracks[0].running && !transport.tracks[0].enabled);
    CHECK(transport.tracks[0].playhead == 0u);
    return 1;
}

static int test_delayed_advance_applies_boundaries_once(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_set_loop(&transport, 0u, 33u, 713u) == THY_TIME_OK);
    CHECK(thy_transport_set_loop(&transport, 1u, 99u, 997u) == THY_TIME_OK);
    CHECK(thy_transport_set_enabled(&transport, 0u, 1u) == THY_TIME_OK);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    CHECK(advance(&clock, &transport, 1000u));
    CHECK(thy_transport_request_track(&transport, 1u, 1u, THY_TRANSPORT_BEAT, &clock) == THY_TIME_OK);
    CHECK(transport.tracks[1].pending_position == 2880u);
    CHECK(thy_transport_request_track(&transport, 0u, 0u, THY_TRANSPORT_BAR, &clock) == THY_TIME_OK);
    uint32_t generation = transport.tracks[1].generation;
    CHECK(advance(&clock, &transport, 100000u));
    CHECK(transport.position == 192000u && transport.tracks[1].running);
    CHECK(transport.tracks[1].playhead == 99u + (192000u - 2880u) % 997u);
    CHECK(transport.tracks[1].generation == generation + 1u);
    CHECK(!transport.tracks[0].running && transport.tracks[0].playhead == 33u);
    CHECK(transport.tracks[0].pending == THY_TRANSPORT_PENDING_NONE);
    return 1;
}

static int test_meter_edits_preserve_pending_boundary(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    CHECK(advance(&clock, &transport, 100u));
    CHECK(thy_transport_request_track(&transport, 0u, 1u, THY_TRANSPORT_BAR, &clock) == THY_TIME_OK);
    CHECK(thy_clock_set_meter(&clock, 7u, 8u) == THY_TIME_OK);
    CHECK(transport.position == 192u && clock.tick == 192u);
    CHECK(thy_transport_request_track(&transport, 1u, 1u, THY_TRANSPORT_BAR, &clock) == THY_TIME_OK);
    CHECK(transport.tracks[0].pending_position == 3840u);
    CHECK(transport.tracks[1].pending_position == 3360u);
    CHECK(advance(&clock, &transport, 2000u));
    CHECK(transport.tracks[0].playhead == 0u && transport.tracks[1].playhead == 480u);
    return 1;
}

static int test_main_stop_reset_and_clock_independence(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_set_loop(&transport, 0u, 500u, 1000u) == THY_TIME_OK);
    CHECK(thy_transport_set_enabled(&transport, 0u, 1u) == THY_TIME_OK);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    CHECK(advance(&clock, &transport, 100u));
    CHECK(thy_transport_toggle_track(&transport, 1u, &clock) == THY_TIME_OK);
    uint32_t generation = transport.tracks[0].generation;
    uint64_t tick = clock.tick;
    CHECK(thy_transport_stop(&transport, &clock) == THY_TIME_OK);
    CHECK(clock.tick == tick && transport.position == 0u);
    CHECK(transport.tracks[0].generation != generation && transport.tracks[0].enabled);
    CHECK(!transport.tracks[0].running && transport.tracks[0].playhead == 500u);
    CHECK(transport.tracks[1].pending == THY_TRANSPORT_PENDING_NONE);
    CHECK(advance(&clock, &transport, 5000u));
    CHECK(clock.tick == 9600u && transport.position == 0u);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    CHECK(clock.tick == 9600u && transport.tracks[0].playhead == 500u);
    CHECK(!transport.tracks[1].running);
    return 1;
}

static int test_pause_and_continue_keep_position(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_set_enabled(&transport, 0u, 1u) == THY_TIME_OK);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    CHECK(advance(&clock, &transport, 500u));
    CHECK(thy_transport_toggle_track(&transport, 1u, &clock) == THY_TIME_OK);
    uint32_t generation = transport.tracks[0].generation;
    CHECK(thy_transport_pause(&transport, &clock) == THY_TIME_OK);
    CHECK(transport.state == THY_TRANSPORT_PAUSED && transport.position == 960u);
    CHECK(transport.tracks[0].generation != generation);
    CHECK(transport.tracks[1].pending == THY_TRANSPORT_PENDING_NONE);
    CHECK(advance(&clock, &transport, 3000u));
    CHECK(transport.position == 960u && transport.tracks[0].playhead == 960u);
    CHECK(thy_transport_continue(&transport, &clock) == THY_TIME_OK);
    CHECK(advance(&clock, &transport, 3500u));
    CHECK(transport.position == 1920u && transport.tracks[0].playhead == 1920u);
    return 1;
}

static int test_loop_and_request_validation(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_set_loop(&transport, 0u, UINT32_MAX, 1u) == THY_TIME_INVALID);
    CHECK(thy_transport_set_loop(&transport, 0u, 0u, 0u) == THY_TIME_INVALID);
    CHECK(thy_transport_set_loop(&transport, 21u, 0u, 1u) == THY_TIME_INVALID);
    CHECK(thy_transport_set_loop(&transport, 0u, 0u, UINT32_MAX) == THY_TIME_OK);
    CHECK(thy_transport_set_quantize(&transport, 3u) == THY_TIME_INVALID);
    CHECK(thy_transport_request_track(&transport, 21u, 1u, 0u, &clock) == THY_TIME_INVALID);
    CHECK(thy_transport_request_track(&transport, 0u, 2u, 0u, &clock) == THY_TIME_INVALID);
    CHECK(thy_transport_request_track(&transport, 0u, 1u, 3u, &clock) == THY_TIME_INVALID);
    CHECK(thy_transport_song_position(&transport, 16384u, &clock) == THY_TIME_INVALID);
    CHECK(thy_transport_song_position(&transport, 16383u, &clock) == THY_TIME_OK);
    CHECK(transport.position == UINT64_C(16383) * 240u);
    CHECK(thy_transport_toggle_track(&transport, 0u, &clock) == THY_TIME_OK);
    CHECK(transport.tracks[0].enabled && !transport.tracks[0].running);
    CHECK(transport.tracks[0].pending == THY_TRANSPORT_PENDING_NONE);
    return 1;
}

static int test_usb_follow_is_explicit_and_messages_validated(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    thy_transport_init(&transport, &clock);
    const uint8_t start[] = {0xfau};
    const uint8_t pulse[] = {0xf8u};
    const uint8_t invalid_spp[] = {0xf2u, 128u, 0u};
    const uint8_t valid_spp[] = {0xf2u, 1u, 0u};
    const uint8_t note[] = {0x90u, 60u, 100u};
    CHECK(thy_transport_receive_midi(&transport, &clock, start, 1u, 100u) == THY_TIME_IGNORED);
    CHECK(thy_transport_receive_midi(&transport, &clock, pulse, 1u, 100u) == THY_TIME_IGNORED);
    CHECK(clock.tick == 0u && transport.state == THY_TRANSPORT_STOPPED);
    CHECK(thy_clock_set_source(&clock, THY_CLOCK_SOURCE_USB, 0u) == THY_TIME_OK);
    CHECK(thy_transport_advance(&transport, &clock) == THY_TIME_OK);
    CHECK(thy_transport_receive_midi(&transport, &clock, invalid_spp, 3u, 100u) == THY_TIME_INVALID);
    CHECK(thy_transport_receive_midi(&transport, &clock, valid_spp, 2u, 100u) == THY_TIME_INVALID);
    CHECK(thy_transport_receive_midi(&transport, &clock, start, 2u, 100u) == THY_TIME_INVALID);
    CHECK(thy_transport_receive_midi(&transport, &clock, note, 3u, 100u) == THY_TIME_IGNORED);
    CHECK(thy_transport_receive_midi(&transport, &clock, 0, 1u, 100u) == THY_TIME_INVALID);
    CHECK(clock.now_ms == 0u && transport.position == 0u);
    return 1;
}

static int test_usb_pulses_loss_and_relock_without_fallback(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    CHECK(thy_clock_set_source(&clock, THY_CLOCK_SOURCE_USB, 0u) == THY_TIME_OK);
    thy_transport_init(&transport, &clock);
    CHECK(clock.status == THY_CLOCK_WAITING && !thy_clock_ready(&clock));
    CHECK(thy_transport_set_enabled(&transport, 0u, 1u) == THY_TIME_OK);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    CHECK(advance(&clock, &transport, 1000u));
    CHECK(clock.status == THY_CLOCK_WAITING && clock.tick == 0u && transport.position == 0u);
    for (unsigned n = 1u; n <= 24u; ++n)
        CHECK(midi(&clock, &transport, 0xf8u, 1000u + n * 20u));
    CHECK(clock.tick == 960u && transport.position == 960u);
    CHECK(clock.status == THY_CLOCK_LOCKED && clock.last_pulse_interval_ms == 20u);
    uint32_t clock_generation = clock.generation;
    uint32_t track_generation = transport.tracks[0].generation;
    CHECK(advance(&clock, &transport, 1979u));
    CHECK(clock.status == THY_CLOCK_LOCKED);
    CHECK(advance(&clock, &transport, 1980u));
    CHECK(clock.status == THY_CLOCK_LOST && !thy_clock_ready(&clock));
    CHECK(clock.generation != clock_generation && transport.suspended);
    CHECK(transport.tracks[0].generation != track_generation);
    track_generation = transport.tracks[0].generation;
    CHECK(advance(&clock, &transport, 10000u));
    CHECK(clock.tick == 960u && transport.position == 960u);
    CHECK(transport.tracks[0].generation == track_generation);
    CHECK(midi(&clock, &transport, 0xf8u, 10020u));
    CHECK(clock.status == THY_CLOCK_LOCKED && clock.tick == 1000u);
    CHECK(transport.position == 1000u && !transport.suspended);
    CHECK(clock.last_pulse_interval_ms == 0u);
    return 1;
}

static int test_usb_start_stop_continue_and_song_position(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    CHECK(thy_clock_set_source(&clock, THY_CLOCK_SOURCE_USB, 0u) == THY_TIME_OK);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_set_enabled(&transport, 0u, 1u) == THY_TIME_OK);
    CHECK(thy_transport_set_loop(&transport, 0u, 100u, 960u) == THY_TIME_OK);
    CHECK(midi(&clock, &transport, 0xfau, 0u));
    for (unsigned n = 1u; n <= 24u; ++n)
        CHECK(midi(&clock, &transport, 0xf8u, n * 20u));
    CHECK(transport.position == 960u && transport.tracks[0].playhead == 100u);
    CHECK(midi(&clock, &transport, 0xfcu, 500u));
    CHECK(transport.state == THY_TRANSPORT_PAUSED && transport.position == 960u);
    const uint8_t spp[] = {0xf2u, 5u, 0u};
    CHECK(thy_transport_receive_midi(&transport, &clock, spp, 3u, 520u) == THY_TIME_OK);
    CHECK(transport.position == 1200u && transport.tracks[0].playhead == 340u);
    CHECK(midi(&clock, &transport, 0xfbu, 540u));
    CHECK(midi(&clock, &transport, 0xf8u, 560u));
    CHECK(transport.position == 1240u && transport.tracks[0].playhead == 380u);
    uint64_t clock_tick = clock.tick;
    CHECK(midi(&clock, &transport, 0xfau, 580u));
    CHECK(transport.position == 0u && clock.tick == clock_tick);
    CHECK(transport.tracks[0].playhead == 100u);
    CHECK(midi(&clock, &transport, 0xf8u, 600u));
    CHECK(transport.position == 40u);
    return 1;
}

static int test_usb_timeout_across_millisecond_wrap(void)
{
    thy_clock clock;
    thy_clock_init(&clock, UINT32_MAX - 100u);
    CHECK(thy_clock_set_source(&clock, THY_CLOCK_SOURCE_USB, UINT32_MAX - 100u) == THY_TIME_OK);
    CHECK(thy_clock_set_timeout(&clock, 100u) == THY_TIME_OK);
    CHECK(thy_clock_midi_pulse(&clock, UINT32_MAX - 10u) == THY_TIME_OK);
    CHECK(thy_clock_midi_pulse(&clock, 20u) == THY_TIME_OK);
    CHECK(clock.last_pulse_interval_ms == 31u && clock.tick == 80u);
    CHECK(thy_clock_advance(&clock, 119u) == THY_TIME_OK);
    CHECK(clock.status == THY_CLOCK_LOCKED);
    CHECK(thy_clock_advance(&clock, 120u) == THY_TIME_OK);
    CHECK(clock.status == THY_CLOCK_LOST);
    CHECK(thy_clock_midi_pulse(&clock, 119u) == THY_TIME_BACKWARD);
    CHECK(clock.status == THY_CLOCK_LOST && clock.tick == 80u);
    return 1;
}

static int test_clock_source_change_invalidates_without_catchup(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_set_enabled(&transport, 0u, 1u) == THY_TIME_OK);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    CHECK(advance(&clock, &transport, 500u));
    uint32_t generation = transport.tracks[0].generation;
    CHECK(thy_clock_set_source(&clock, THY_CLOCK_SOURCE_USB, 500u) == THY_TIME_OK);
    CHECK(thy_transport_advance(&transport, &clock) == THY_TIME_OK);
    CHECK(transport.position == 960u && transport.suspended);
    CHECK(transport.tracks[0].generation != generation);
    CHECK(advance(&clock, &transport, 10000u));
    CHECK(transport.position == 960u);
    CHECK(midi(&clock, &transport, 0xf8u, 10001u));
    CHECK(transport.position == 1000u);
    CHECK(thy_clock_set_source(&clock, THY_CLOCK_SOURCE_INTERNAL, 10001u) == THY_TIME_OK);
    CHECK(thy_transport_advance(&transport, &clock) == THY_TIME_OK);
    CHECK(advance(&clock, &transport, 10501u));
    CHECK(transport.position == 1960u);
    return 1;
}

static int test_relock_phase_independent_of_timeout_service_cadence(void)
{
    thy_clock early, late;
    thy_transport early_transport, late_transport;
    thy_clock_init(&early, 0u);
    CHECK(thy_clock_set_source(&early, THY_CLOCK_SOURCE_USB, 0u) == THY_TIME_OK);
    thy_transport_init(&early_transport, &early);
    CHECK(thy_transport_set_enabled(&early_transport, 0u, 1u) == THY_TIME_OK);
    CHECK(thy_transport_start(&early_transport, &early) == THY_TIME_OK);
    CHECK(midi(&early, &early_transport, 0xf8u, 10u));
    late = early;
    late_transport = early_transport;
    CHECK(advance(&early, &early_transport, 510u));
    CHECK(early.status == THY_CLOCK_LOST);
    CHECK(midi(&early, &early_transport, 0xf8u, 1000u));
    CHECK(midi(&late, &late_transport, 0xf8u, 1000u));
    CHECK(early.tick == late.tick && early.generation == late.generation);
    CHECK(early_transport.position == 80u && late_transport.position == 80u);
    CHECK(early_transport.tracks[0].playhead == late_transport.tracks[0].playhead);
    CHECK(early_transport.tracks[0].generation == late_transport.tracks[0].generation);
    return 1;
}

static int test_generation_exhaustion_never_reuses_run(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_set_enabled(&transport, 0u, 1u) == THY_TIME_OK);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    transport.tracks[0].generation = UINT32_MAX;
    CHECK(thy_transport_stop(&transport, &clock) == THY_TIME_OK);
    CHECK(transport.tracks[0].generation == 0u && !transport.tracks[0].running);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OVERFLOW);
    CHECK(transport.state == THY_TRANSPORT_STOPPED);
    CHECK(thy_transport_request_track(&transport, 0u, 1u, 0u, &clock) == THY_TIME_OVERFLOW);
    CHECK(thy_transport_stop(&transport, &clock) == THY_TIME_OK);
    CHECK(transport.tracks[0].generation == 0u);
    clock.generation = UINT32_MAX;
    CHECK(thy_clock_set_source(&clock, THY_CLOCK_SOURCE_USB, 0u) == THY_TIME_OVERFLOW);
    CHECK(clock.source == THY_CLOCK_SOURCE_INTERNAL && clock.generation == UINT32_MAX);
    clock.source = THY_CLOCK_SOURCE_USB;
    clock.status = THY_CLOCK_LOCKED;
    clock.last_pulse_ms = 0u;
    CHECK(thy_clock_advance(&clock, 500u) == THY_TIME_OK);
    CHECK(clock.generation == 0u && !thy_clock_ready(&clock));
    CHECK(thy_clock_midi_pulse(&clock, 501u) == THY_TIME_OVERFLOW);
    CHECK(clock.generation == 0u && clock.status == THY_CLOCK_LOST);
    return 1;
}

static int test_transport_position_overflow_and_clock_rewind_reject(void)
{
    thy_clock clock;
    thy_transport transport;
    thy_clock_init(&clock, 0u);
    thy_transport_init(&transport, &clock);
    CHECK(thy_transport_start(&transport, &clock) == THY_TIME_OK);
    transport.position = UINT64_MAX - 50u;
    transport.last_clock_tick = UINT64_MAX - 100u;
    clock.tick = UINT64_MAX;
    CHECK(thy_transport_advance(&transport, &clock) == THY_TIME_OVERFLOW);
    CHECK(transport.position == UINT64_MAX - 50u);
    CHECK(transport.last_clock_tick == UINT64_MAX - 100u);
    clock.tick = UINT64_MAX - 101u;
    CHECK(thy_transport_advance(&transport, &clock) == THY_TIME_BACKWARD);
    CHECK(transport.position == UINT64_MAX - 50u);
    /* Emergency stop must not fail on time overflow or apply overdue launches. */
    transport.tracks[1].pending = THY_TRANSPORT_PENDING_START;
    transport.tracks[1].pending_position = 1u;
    CHECK(thy_transport_stop(&transport, &clock) == THY_TIME_OK);
    CHECK(transport.position == 0u && transport.state == THY_TRANSPORT_STOPPED);
    CHECK(!transport.tracks[1].running && !transport.tracks[1].enabled);
    CHECK(transport.tracks[1].pending == THY_TRANSPORT_PENDING_NONE);
    return 1;
}

int main(void)
{
    static int (*const cases[])(void) = {
        test_internal_fractional_clock, test_partition_independent_drift,
        test_tempo_edit_preserves_fraction_and_phase, test_subbeat_tuplets_and_meter,
        test_large_delay_bounded_conversion, test_wrap_and_backward_timestamp_rejection,
        test_invalid_clock_configuration_is_atomic, test_clock_overflow_is_rejected,
        test_independent_track_loops_and_selection, test_enabled_configuration_is_not_launch,
        test_exact_next_bar_and_repeated_queue_cancellation,
        test_delayed_advance_applies_boundaries_once, test_meter_edits_preserve_pending_boundary,
        test_main_stop_reset_and_clock_independence, test_pause_and_continue_keep_position,
        test_loop_and_request_validation, test_usb_follow_is_explicit_and_messages_validated,
        test_usb_pulses_loss_and_relock_without_fallback,
        test_usb_start_stop_continue_and_song_position,
        test_usb_timeout_across_millisecond_wrap,
        test_clock_source_change_invalidates_without_catchup,
        test_relock_phase_independent_of_timeout_service_cadence,
        test_generation_exhaustion_never_reuses_run,
        test_transport_position_overflow_and_clock_rewind_reject
    };
    unsigned count = sizeof cases / sizeof cases[0];
    for (unsigned n = 0u; n < count; ++n) if (!cases[n]()) return 1;
    printf("%u clock/transport C tests passed (%u assertions)\n", count, assertions);
    return 0;
}
