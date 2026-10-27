/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_instrument.h"

static thy_core core;
static thy_instrument instrument;
static thy_instrument_pad layout[THY_PAD_COUNT];
static unsigned assertions;
static const int16_t major[] = {0, 2, 4, 5, 7, 9, 11};

#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 0; \
} } while (0)

static int initialize(void)
{
    const thy_instrument_calibration calibration = {100u, 96u, 32u, 1000u, 1};
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad) {
        layout[pad] = (thy_instrument_pad){{(int16_t)(20u + pad)}, 1u, 1u};
        CHECK(thy_instrument_calibrate(&instrument, (uint8_t)pad, &calibration) == THY_OK);
    }
    CHECK(thy_instrument_set_layout(&instrument, layout, 60, major, 7u) == THY_OK);
    return 1;
}

static int take(unsigned destination, unsigned status, unsigned pitch, unsigned value)
{
    thy_midi_event event;
    CHECK(thy_core_poll(&core, &event) == 1);
    CHECK(event.destination == destination);
    CHECK(event.status == status);
    CHECK(event.data1 == pitch);
    CHECK(event.data2 == value);
    return 1;
}

static int empty(void)
{
    thy_midi_event event;
    CHECK(thy_core_poll(&core, &event) == 0);
    return 1;
}

static int routed_events(uint8_t target, unsigned logical, int on)
{
    thy_instrument_route route;
    CHECK(thy_instrument_get_pad_route(&instrument, target, &route) == THY_OK);
    for (unsigned destination = 0u; destination < THY_DEST_COUNT; ++destination)
        if (route.destinations & (1u << destination))
            CHECK(take(destination, (on ? 0x90u : 0x80u) | route.channel,
                20u + logical, on ? route.velocity : 0u));
    CHECK(empty());
    return 1;
}

static int test_all_profiles_routes_and_zones_emit_real_messages(void)
{
    CHECK(initialize());
    CHECK(instrument.routing_profile == THY_ROUTING_SPLIT);
    for (uint8_t target = 0u; target < THY_INSTRUMENT_ROUTE_TARGETS; ++target) {
        uint8_t destinations = target % 3u == 0u ? THY_ROUTE_DIN :
            target % 3u == 1u ? THY_ROUTE_USB0 : THY_ROUTE_USB1 | THY_ROUTE_DIN;
        CHECK(thy_instrument_set_pad_route(&instrument, target,
            (uint8_t)(target + 2u), destinations) == THY_OK);
        CHECK(thy_instrument_set_route_expression(&instrument, target,
            (uint8_t)(45u + target), target % 2u) == THY_OK);
    }
    for (uint8_t profile = THY_ROUTING_WHOLE; profile <= THY_ROUTING_ZONES; ++profile) {
        CHECK(thy_instrument_set_routing_profile(&instrument, profile) == THY_OK);
        for (uint8_t side = 0u; side < 2u; ++side)
        for (uint8_t row = 0u; row < 4u; ++row)
        for (uint8_t column = 0u; column < 12u; ++column) {
            uint8_t logical = (uint8_t)(side * 48u + column * 4u + row);
            uint8_t expected = profile == THY_ROUTING_WHOLE ? 0u :
                profile == THY_ROUTING_SPLIT ? (uint8_t)(side + 1u) :
                (uint8_t)(3u + side * 3u + column / 4u);
            CHECK(thy_instrument_route_target(&instrument, logical) == expected);
            CHECK(thy_instrument_press_pad(&instrument, logical) == THY_OK);
            CHECK(routed_events(expected, logical, 1));
            CHECK(thy_instrument_release_pad(&instrument, logical) == THY_OK);
            CHECK(routed_events(expected, logical, 0));
        }
    }
    CHECK(thy_instrument_route_target(&instrument, 0u) == 3u);
    CHECK(thy_instrument_route_target(&instrument, 15u) == 3u);
    CHECK(thy_instrument_route_target(&instrument, 16u) == 4u);
    CHECK(thy_instrument_route_target(&instrument, 31u) == 4u);
    CHECK(thy_instrument_route_target(&instrument, 32u) == 5u);
    CHECK(thy_instrument_route_target(&instrument, 47u) == 5u);
    CHECK(thy_instrument_route_target(&instrument, 48u) == 6u);
    CHECK(thy_instrument_route_target(&instrument, 63u) == 6u);
    CHECK(thy_instrument_route_target(&instrument, 64u) == 7u);
    CHECK(thy_instrument_route_target(&instrument, 79u) == 7u);
    CHECK(thy_instrument_route_target(&instrument, 80u) == 8u);
    CHECK(thy_instrument_route_target(&instrument, 95u) == 8u);
    CHECK(core.sounding_count == 0u && core.pending_keys == 0u);
    CHECK(instrument.errors == 0u);
    return 1;
}

static int test_logical_mapping_drives_sides_and_zones(void)
{
    uint8_t mapping[THY_PAD_COUNT];
    CHECK(initialize());
    for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad) mapping[pad] = (uint8_t)pad;
    /* Electrical 0 now plays right column 5 / bottom row (logical 67),
     * deliberately distinguishing column-major zoning from ADC bank order. */
    mapping[0] = 67u;
    mapping[67] = 0u;
    CHECK(thy_instrument_set_mapping(&instrument, mapping) == THY_OK);
    CHECK(thy_instrument_set_routing_profile(&instrument, THY_ROUTING_ZONES) == THY_OK);
    CHECK(thy_instrument_set_pad_route(&instrument, 7u, 14u, THY_ROUTE_DIN) == THY_OK);
    CHECK(thy_instrument_set_route_expression(&instrument, 7u, 111u, 0u) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(take(THY_DIN, 0x9eu, 87u, 111u));
    CHECK(empty());
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    CHECK(take(THY_DIN, 0x8eu, 87u, 0u));
    CHECK(thy_instrument_set_pad_route(&instrument, 3u, 7u, THY_ROUTE_USB1) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 67u) == THY_OK);
    CHECK(take(THY_USB1, 0x97u, 20u, 96u));
    CHECK(thy_instrument_release_pad(&instrument, 67u) == THY_OK);
    CHECK(take(THY_USB1, 0x87u, 20u, 0u));
    CHECK(empty());
    return 1;
}

static int test_legacy_route_defaults_and_atomic_validation(void)
{
    thy_instrument saved;
    thy_instrument_route route, unchanged = {9u, 9u, 9u, 9u};
    CHECK(initialize());
    CHECK(thy_instrument_set_route(&instrument, 0u, 5u, THY_ROUTE_USB0 | THY_ROUTE_DIN, 12) == THY_OK);
    CHECK(thy_instrument_set_route(&instrument, 1u, 12u, THY_ROUTE_DIN, -12) == THY_OK);
    for (uint8_t target = 0u; target < THY_INSTRUMENT_ROUTE_TARGETS; ++target) {
        unsigned right = target == 2u || target >= 6u;
        CHECK(thy_instrument_get_pad_route(&instrument, target, &route) == THY_OK);
        CHECK(route.channel == (right ? 12u : 5u));
        CHECK(route.destinations == (right ? THY_ROUTE_DIN : THY_ROUTE_USB0 | THY_ROUTE_DIN));
    }
    /* Existing adapters may access the two Split fields directly. */
    instrument.channels[0] = 8u;
    instrument.destinations[0] = THY_ROUTE_USB1;
    CHECK(thy_instrument_get_pad_route(&instrument, 1u, &route) == THY_OK);
    CHECK(route.channel == 8u && route.destinations == THY_ROUTE_USB1);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(take(THY_USB1, 0x98u, 32u, 96u));
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    CHECK(take(THY_USB1, 0x88u, 32u, 0u));
    saved = instrument;
    for (unsigned invalid = 0u; invalid < 6u; ++invalid) {
        int result = invalid == 0u ? thy_instrument_set_pad_route(&instrument, 9u, 0u, THY_ROUTE_DIN) :
            invalid == 1u ? thy_instrument_set_pad_route(&instrument, 0u, 16u, THY_ROUTE_DIN) :
            invalid == 2u ? thy_instrument_set_pad_route(&instrument, 0u, 0u, 0u) :
            invalid == 3u ? thy_instrument_set_pad_route(&instrument, 0u, 0u, 8u) :
            invalid == 4u ? thy_instrument_set_route_expression(&instrument, 1u, 0u, 0u) :
                           thy_instrument_set_route_expression(&instrument, 1u, 96u, 2u);
        CHECK(result == THY_INVALID_ARGUMENT);
        CHECK(memcmp(&saved, &instrument, sizeof(saved)) == 0);
    }
    CHECK(thy_instrument_set_routing_profile(&instrument, 3u) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&saved, &instrument, sizeof(saved)) == 0);
    CHECK(thy_instrument_get_pad_route(&instrument, 9u, &unchanged) == THY_INVALID_ARGUMENT);
    CHECK(unchanged.channel == 9u && unchanged.destinations == 9u && unchanged.velocity == 9u);
    CHECK(thy_instrument_route_target(&instrument, 96u) == UINT8_MAX);
    CHECK(thy_instrument_route_target(NULL, 0u) == UINT8_MAX);
    CHECK(empty());
    return 1;
}

static int test_held_route_profile_and_pending_strum_snapshots(void)
{
    CHECK(initialize());
    layout[0] = (thy_instrument_pad){{60, 64, 67}, 7u, 1u};
    instrument.performance.strum = 1u;
    instrument.performance.strum_ms = 10u;
    CHECK(thy_instrument_set_pad_route(&instrument, 1u, 4u, THY_ROUTE_USB0 | THY_ROUTE_DIN) == THY_OK);
    CHECK(thy_instrument_set_route_expression(&instrument, 1u, 51u, 1u) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 200u, 0u) == THY_OK);
    CHECK(take(THY_USB0, 0x94u, 60u, 51u));
    CHECK(take(THY_DIN, 0x94u, 60u, 51u));
    CHECK(thy_instrument_set_pad_route(&instrument, 1u, 9u, THY_ROUTE_USB1) == THY_OK);
    CHECK(thy_instrument_set_route_expression(&instrument, 1u, 99u, 0u) == THY_OK);
    CHECK(thy_instrument_set_pad_route(&instrument, 0u, 6u, THY_ROUTE_DIN) == THY_OK);
    CHECK(thy_instrument_set_route_expression(&instrument, 0u, 88u, 0u) == THY_OK);
    CHECK(thy_instrument_set_routing_profile(&instrument, THY_ROUTING_WHOLE) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 400u, 1u) == THY_OK);
    CHECK(take(THY_USB0, 0xa4u, 60u, 38u));
    CHECK(take(THY_DIN, 0xa4u, 60u, 38u));
    CHECK(empty());
    CHECK(thy_instrument_advance(&instrument, 10u) == THY_OK);
    CHECK(take(THY_USB0, 0x94u, 64u, 51u));
    CHECK(take(THY_DIN, 0x94u, 64u, 51u));
    CHECK(thy_instrument_advance(&instrument, 20u) == THY_OK);
    CHECK(take(THY_USB0, 0x94u, 67u, 51u));
    CHECK(take(THY_DIN, 0x94u, 67u, 51u));
    CHECK(thy_instrument_sample(&instrument, 0u, 100u, 21u) == THY_OK);
    for (unsigned note = 0u; note < 3u; ++note) {
        unsigned pitch = note == 0u ? 60u : note == 1u ? 64u : 67u;
        CHECK(take(THY_USB0, 0x84u, pitch, 0u));
        CHECK(take(THY_DIN, 0x84u, pitch, 0u));
    }
    CHECK(thy_instrument_sample(&instrument, 0u, 200u, 22u) == THY_OK);
    CHECK(take(THY_DIN, 0x96u, 60u, 88u));
    CHECK(thy_instrument_sample(&instrument, 0u, 100u, 23u) == THY_OK);
    CHECK(take(THY_DIN, 0x86u, 60u, 0u));
    CHECK(thy_instrument_advance(&instrument, 100u) == THY_OK);
    CHECK(empty());
    CHECK(core.sounding_count == 0u && core.pending_keys == 0u);
    return 1;
}

static int test_expression_inheritance_and_held_opt_in(void)
{
    thy_instrument_route route;
    CHECK(initialize());
    CHECK(thy_instrument_set_expression(&instrument, 70u, 1u) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 200u, 0u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 20u, 70u));
    CHECK(thy_instrument_set_expression(&instrument, 90u, 0u) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 400u, 1u) == THY_OK);
    CHECK(take(THY_USB0, 0xa0u, 20u, 38u));
    CHECK(thy_instrument_sample(&instrument, 0u, 100u, 2u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 20u, 0u));
    CHECK(thy_instrument_sample(&instrument, 0u, 200u, 3u) == THY_OK);
    CHECK(take(THY_USB0, 0x90u, 20u, 90u));
    CHECK(thy_instrument_set_expression(&instrument, 70u, 1u) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 400u, 4u) == THY_OK);
    CHECK(empty()); /* New opt-in cannot alter a sounding owner's policy. */
    CHECK(thy_instrument_sample(&instrument, 0u, 100u, 5u) == THY_OK);
    CHECK(take(THY_USB0, 0x80u, 20u, 0u));
    CHECK(thy_instrument_set_route_expression(&instrument, 1u, 33u, 0u) == THY_OK);
    CHECK(thy_instrument_set_expression(&instrument, 99u, 1u) == THY_OK);
    CHECK(thy_instrument_get_pad_route(&instrument, 1u, &route) == THY_OK);
    CHECK(route.velocity == 33u && route.pressure_enabled == 0u);
    CHECK(thy_instrument_get_pad_route(&instrument, 2u, &route) == THY_OK);
    CHECK(route.velocity == 99u && route.pressure_enabled == 1u);
    CHECK(thy_instrument_set_route_velocity(&instrument, 2u, 34u) == THY_OK);
    CHECK(thy_instrument_set_route_pressure(&instrument, 3u, 1u) == THY_OK);
    CHECK(thy_instrument_set_expression(&instrument, 72u, 0u) == THY_OK);
    CHECK(thy_instrument_get_pad_route(&instrument, 2u, &route) == THY_OK);
    CHECK(route.velocity == 34u && route.pressure_enabled == 0u);
    CHECK(thy_instrument_get_pad_route(&instrument, 3u, &route) == THY_OK);
    CHECK(route.velocity == 72u && route.pressure_enabled == 1u);
    return 1;
}

int main(void)
{
    int (*tests[])(void) = {
        test_all_profiles_routes_and_zones_emit_real_messages,
        test_logical_mapping_drives_sides_and_zones,
        test_legacy_route_defaults_and_atomic_validation,
        test_held_route_profile_and_pending_strum_snapshots,
        test_expression_inheritance_and_held_opt_in
    };
    for (unsigned test = 0u; test < sizeof(tests) / sizeof(tests[0]); ++test)
        if (!tests[test]()) return 1;
    printf("5 MIDI routing tests passed (%u assertions)\n", assertions);
    return 0;
}
