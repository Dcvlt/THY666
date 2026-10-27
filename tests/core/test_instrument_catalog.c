/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual generated local catalogue + portable instrument + musical core.
 * Root builds this with generated instrument_data.c; no hardware dependency. */
#include <stdio.h>
#include "data.h"

static thy_core core;
static thy_instrument instrument;
static thy_instrument_pad pads[96], next_pads[96];
static unsigned assertions;
static uint32_t now;
static unsigned archive_active, archive_silent, generated_playable, generated_range;

#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s (time=%u)\n", __FILE__, __LINE__, #condition, now); \
    return 0; } } while (0)

static int take(uint8_t status, uint8_t pitch, uint8_t value)
{
    thy_midi_event event;
    CHECK(thy_core_poll(&core, &event));
    CHECK(event.destination == THY_USB0);
    CHECK(event.status == status);
    CHECK(event.data1 == pitch);
    CHECK(event.data2 == value);
    return 1;
}

static unsigned unique_notes(const thy_instrument_pad *pad, uint8_t out[7])
{
    unsigned count = 0u, slot;
    for (slot = 0u; slot < 7u; ++slot) if ((pad->valid_mask & (1u << slot)) != 0u) {
        unsigned seen = 0u, n;
        for (n = 0u; n < count; ++n) if (out[n] == pad->notes[slot]) seen = 1u;
        if (!seen) out[count++] = (uint8_t)pad->notes[slot];
    }
    return count;
}

static int initialize(unsigned scale)
{
    int16_t degrees[12], anchor;
    uint8_t count;
    unsigned pad, round;
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    CHECK(thy_instrument_data_load(scale, pads, &anchor, degrees, &count));
    CHECK(thy_instrument_data_name(scale)[0] != 0);
    CHECK(thy_instrument_set_layout(&instrument, pads, anchor, degrees, count) == THY_OK);
    CHECK(thy_instrument_set_detector_profile(&instrument,
        THY_DETECTOR_FACTORY_LINEAR) == THY_OK);
    for (round = 0u; round < THY_INSTRUMENT_WARMUP_SAMPLES; ++round)
        for (pad = 0u; pad < 96u; ++pad)
            CHECK(thy_instrument_sample(&instrument, (uint8_t)pad, 100u, now++) == THY_OK);
    return 1;
}

static int sample(uint8_t pad, uint16_t raw)
{
    int result = thy_instrument_sample(&instrument, pad, raw, now++);
    if (result != THY_OK) return result;
    return thy_instrument_sample(&instrument, pad, raw, now++);
}

static int test_archive_layout(unsigned scale)
{
    unsigned pad;
    CHECK(initialize(scale));
    for (pad = 0u; pad < 96u; ++pad) {
        uint8_t notes[7], channel = pad >= 48u ? 1u : 0u;
        unsigned count = unique_notes(&pads[pad], notes), n;
        if (count != 0u) ++archive_active;
        else ++archive_silent;
        CHECK(sample((uint8_t)pad, 1500u) == THY_OK);
        CHECK(thy_core_pad_held(&core, (uint8_t)pad));
        CHECK(core.sounding_count == count && core.queue_count == count);
        for (n = 0u; n < count; ++n) CHECK(take((uint8_t)(0x90u | channel), notes[n], 96u));
        CHECK(sample((uint8_t)pad, 100u) == THY_OK);
        CHECK(!thy_core_pad_held(&core, (uint8_t)pad));
        CHECK(core.sounding_count == 0u && core.queue_count == count);
        for (n = 0u; n < count; ++n) CHECK(take((uint8_t)(0x80u | channel), notes[n], 0u));
        CHECK(core.queue_count == 0u && core.pending_keys == 0u);
    }
    CHECK(instrument.errors == 0u && instrument.presses == 96u && instrument.releases == 96u);
    return 1;
}

static int test_generated_modes(void)
{
    unsigned mode, logical;
    for (mode = 1u; mode <= 3u; ++mode) {
        instrument.performance.melody_layout = (uint8_t)mode;
        for (logical = 48u; logical < 96u; ++logical) {
            int16_t expected = -1;
            unsigned position = logical - 48u;
            int result = thy_core_melody_pitch((int)mode, instrument.melody_anchor,
                instrument.degrees, instrument.degree_count,
                (uint8_t)(position / 4u), (uint8_t)(position % 4u), &expected);
            CHECK(sample((uint8_t)logical, 1500u) == result);
            if (result == THY_OK) {
                ++generated_playable;
                CHECK(take(0x91u, (uint8_t)expected, 96u));
                CHECK(thy_core_pad_held(&core, (uint8_t)logical));
            } else {
                ++generated_range;
                CHECK(core.queue_count == 0u && core.sounding_count == 0u);
                CHECK(instrument.sensors[logical].blocked != 0u);
            }
            CHECK(sample((uint8_t)logical, 100u) == THY_OK);
            if (result == THY_OK) CHECK(take(0x81u, (uint8_t)expected, 0u));
            CHECK(core.queue_count == 0u && core.sounding_count == 0u && core.pending_keys == 0u);
        }
    }
    instrument.performance.melody_layout = 0u;
    return 1;
}

static int test_43_controls_and_held_layout_change(unsigned scale)
{
    static const unsigned counts[4] = {5u, 14u, 12u, 12u};
    unsigned bank, index, held = 96u;
    uint8_t old_notes[7], old_count, channel;
    int16_t degrees[12], anchor;
    uint8_t count;
    for (index = 0u; index < 96u; ++index)
        if (pads[index].valid_mask != 0u) { held = index; break; }
    CHECK(held < 96u);
    old_count = (uint8_t)unique_notes(&pads[held], old_notes);
    channel = held >= 48u ? 1u : 0u;
    CHECK(sample((uint8_t)held, 1500u) == THY_OK);
    for (index = 0u; index < old_count; ++index)
        CHECK(take((uint8_t)(0x90u | channel), old_notes[index], 96u));
    for (bank = 0u; bank < 4u; ++bank) {
        for (index = 0u; index < counts[bank]; ++index) {
            CHECK(thy_instrument_control(&instrument, (uint8_t)bank, (uint8_t)index, 1) == THY_OK);
            CHECK(thy_instrument_control(&instrument, (uint8_t)bank, (uint8_t)index, 0) == THY_OK);
        }
        CHECK(instrument.controls_seen[bank] == (1u << counts[bank]) - 1u);
    }
    CHECK(instrument.last_event.sequence == 86u);
    CHECK(thy_instrument_data_load((scale + 1u) % 840u, next_pads, &anchor, degrees, &count));
    CHECK(thy_instrument_set_layout(&instrument, next_pads, anchor, degrees, count) == THY_OK);
    CHECK(thy_instrument_set_route(&instrument, held >= 48u ? 1u : 0u, 15u, THY_ROUTE_USB1, 7) == THY_OK);
    CHECK(core.sounding_count == old_count && core.queue_count == 0u);
    CHECK(sample((uint8_t)held, 100u) == THY_OK);
    for (index = 0u; index < old_count; ++index)
        CHECK(take((uint8_t)(0x80u | channel), old_notes[index], 0u));
    CHECK(core.queue_count == 0u && core.sounding_count == 0u && core.pending_keys == 0u);
    return 1;
}

static int test_lookup_bounds_and_major_reference(void)
{
    uint8_t seen[840] = {0};
    unsigned group, root, scale;
    for (group = 0u; group < 70u; ++group)
        for (root = 0u; root < 12u; ++root) {
            int index = thy_instrument_data_find(group, root);
            CHECK(index >= 0 && index < 840);
            CHECK(seen[index] == 0u);
            seen[index] = 1u;
        }
    for (scale = 0u; scale < 840u; ++scale) CHECK(seen[scale] == 1u);
    CHECK(thy_instrument_data_find(70u, 0u) == -1);
    CHECK(thy_instrument_data_find(0u, 12u) == -1);
    CHECK(thy_instrument_data_find(21u, 0u) == 256);
    CHECK(thy_instrument_data_load(840u, pads, NULL, NULL, NULL) == 0);
    return 1;
}

int main(void)
{
    unsigned scale;
    if (!test_lookup_bounds_and_major_reference()) return 1;
    for (scale = 0u; scale < THY_INSTRUMENT_SCALE_COUNT; ++scale) {
        if (!test_archive_layout(scale) || !test_generated_modes() ||
            !test_43_controls_and_held_layout_change(scale)) return 1;
    }
    if (archive_active != 63024u || archive_silent != 17616u) {
        fprintf(stderr, "Unexpected archive cell counts: %u active, %u silent\n",
                archive_active, archive_silent);
        return 1;
    }
    printf("840 instrument catalogue layouts passed (%u assertions): %u active / %u silent archive cells, "
           "%u generated notes / %u range-rejected cells, 43 controls per layout, immutable held releases\n",
           assertions, archive_active, archive_silent, generated_playable, generated_range);
    return 0;
}
