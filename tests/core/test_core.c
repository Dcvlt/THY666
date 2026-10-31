#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../../core/thy_core.h"

static unsigned assertions;

#define CHECK(condition) do { \
    ++assertions; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

static int invariant(const thy_core *core)
{
    uint8_t expected[THY_KEY_COUNT] = {0};
    uint8_t expected_pitches[128] = {0}, pitches[128];
    uint16_t sounding = 0u;
    uint16_t sustain = 0u;
    uint16_t pending = 0u;
    unsigned pad;
    unsigned key;
    CHECK(thy_core_queued(core) + thy_core_reserved(core) <= THY_QUEUE_CAPACITY);
    for (pad = 0u; pad < THY_PAD_COUNT; ++pad) {
        unsigned i;
        CHECK(core->owners[pad].count <= THY_OWNER_KEYS);
        CHECK(core->owners[pad].activated <= core->owners[pad].count);
        CHECK(core->owners[pad].held != 0u || core->owners[pad].count == 0u);
        pending = (uint16_t)(pending + core->owners[pad].count - core->owners[pad].activated);
        for (i = 0u; i < core->owners[pad].count; ++i) {
            unsigned j;
            uint16_t owned = thy_core_owner_key(core, (uint8_t)pad, (uint8_t)i);
            CHECK(owned < THY_KEY_COUNT);
            if (i < core->owners[pad].activated) {
                ++expected[owned];
            }
            for (j = 0u; j < i; ++j) {
                CHECK(thy_core_owner_key(core, (uint8_t)(pad), (uint8_t)(j)) != owned);
            }
        }
    }
    for (key = 0u; key < THY_KEY_COUNT; ++key) {
        CHECK(thy_core_refcount(core, (uint16_t)key) == expected[key]);
        if (core->refs[key] != 0u || thy_core_deferred(core, (uint16_t)key) != 0u) {
            ++sounding;
            expected_pitches[key % 128u] = 1u;
        }
        if (thy_core_deferred(core, (uint16_t)key) != 0u) {
            CHECK(core->refs[key] == 0u);
            CHECK(core->sustain[key / 128u] != 0u);
        }
    }
    for (key = 0u; key < THY_DEST_COUNT * 16u; ++key) {
        sustain = (uint16_t)(sustain + (core->sustain[key] != 0u ? 1u : 0u));
    }
    CHECK(thy_core_sounding(core) == sounding);
    CHECK(thy_core_pending(core) == pending);
    CHECK(thy_core_reserved(core) == sounding + sustain + 2u * pending);
    CHECK(core->sustain_count == sustain);
    thy_core_sounding_pitches(core, pitches);
    CHECK(memcmp(pitches, expected_pitches, sizeof pitches) == 0);
    return 1;
}

static int take(thy_core *core, uint8_t destination, uint8_t status,
                 uint8_t data1, uint8_t data2)
{
    thy_midi_event event;
    CHECK(thy_core_poll(core, &event) == 1);
    CHECK(event.destination == destination);
    CHECK(event.status == status);
    CHECK(event.data1 == data1);
    CHECK(event.data2 == data2);
    return 1;
}

static void drain(thy_core *core)
{
    thy_midi_event event;
    while (thy_core_poll(core, &event) != 0) { }
}

static int test_overlap_and_duplicates(void)
{
    thy_core core;
    const int16_t chord[] = {60, 64, 60, 67};
    const int16_t overlap[] = {60, 67};
    thy_core_init(&core);
    CHECK(thy_core_press(&core, 0, chord, 4, 0, 0, THY_ROUTE_USB0, 100) == THY_OK);
    CHECK(thy_core_press(&core, 1, overlap, 2, 0, 0, THY_ROUTE_USB0, 70) == THY_OK);
    CHECK(thy_core_queued(&core) == 3u);
    CHECK(thy_core_sounding(&core) == 3u);
    CHECK(thy_core_refcount(&core, thy_core_key(THY_USB0, 0, 60)) == 2u);
    CHECK(core.owners[0].count == 3u);
    CHECK(take(&core, THY_USB0, 0x90, 60, 100));
    CHECK(take(&core, THY_USB0, 0x90, 64, 100));
    CHECK(take(&core, THY_USB0, 0x90, 67, 100));
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x80, 64, 0));
    CHECK(thy_core_sounding(&core) == 2u);
    CHECK(thy_core_release(&core, 1) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x80, 60, 0));
    CHECK(take(&core, THY_USB0, 0x80, 67, 0));
    CHECK(invariant(&core));
    return 1;
}

static int test_same_pitch_separate_channels_and_destinations(void)
{
    thy_core core;
    const int16_t note = 60;
    thy_core_init(&core);
    CHECK(thy_core_press(&core, 0, &note, 1, 0, 0, THY_ROUTE_USB0, 20) == THY_OK);
    CHECK(thy_core_press(&core, 1, &note, 1, 0, 1, THY_ROUTE_USB0, 30) == THY_OK);
    CHECK(thy_core_press(&core, 2, &note, 1, 0, 0, THY_ROUTE_USB1 | THY_ROUTE_DIN, 40) == THY_OK);
    CHECK(thy_core_sounding(&core) == 4u);
    CHECK(take(&core, THY_USB0, 0x90, 60, 20));
    CHECK(take(&core, THY_USB0, 0x91, 60, 30));
    CHECK(take(&core, THY_USB1, 0x90, 60, 40));
    CHECK(take(&core, THY_DIN, 0x90, 60, 40));
    CHECK(thy_core_release(&core, 1) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x81, 60, 0));
    CHECK(thy_core_sounding(&core) == 3u);
    CHECK(thy_core_release(&core, 2) == THY_OK);
    CHECK(take(&core, THY_USB1, 0x80, 60, 0));
    CHECK(take(&core, THY_DIN, 0x80, 60, 0));
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x80, 60, 0));
    CHECK(invariant(&core));
    return 1;
}

static int test_held_root_route_and_octave_changes(void)
{
    thy_core core;
    const int16_t old_chord[] = {48, 52, 55};
    const int16_t new_chord[] = {50, 53, 57};
    thy_core_init(&core);
    CHECK(thy_core_press(&core, 10, old_chord, 3, 12, 2, THY_ROUTE_USB0 | THY_ROUTE_DIN, 127) == THY_OK);
    drain(&core);
    /* Reconfiguration followed by a repeated scan must preserve the old owner. */
    CHECK(thy_core_press(&core, 10, new_chord, 3, -12, 15, THY_ROUTE_USB1, 1) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_core_pressure(&core, 10, 35) == THY_OK);
    CHECK(take(&core, THY_USB0, 0xa2, 60, 35));
    CHECK(take(&core, THY_USB0, 0xa2, 64, 35));
    CHECK(take(&core, THY_USB0, 0xa2, 67, 35));
    CHECK(take(&core, THY_DIN, 0xa2, 60, 35));
    CHECK(take(&core, THY_DIN, 0xa2, 64, 35));
    CHECK(take(&core, THY_DIN, 0xa2, 67, 35));
    CHECK(thy_core_press(&core, 11, new_chord, 3, -12, 15, THY_ROUTE_USB1, 80) == THY_OK);
    drain(&core);
    CHECK(thy_core_release(&core, 10) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x82, 60, 0));
    CHECK(take(&core, THY_USB0, 0x82, 64, 0));
    CHECK(take(&core, THY_USB0, 0x82, 67, 0));
    CHECK(take(&core, THY_DIN, 0x82, 60, 0));
    CHECK(take(&core, THY_DIN, 0x82, 64, 0));
    CHECK(take(&core, THY_DIN, 0x82, 67, 0));
    CHECK(thy_core_sounding(&core) == 3u);
    CHECK(thy_core_refcount(&core, thy_core_key(THY_USB1, 15, 38)) == 1u);
    CHECK(invariant(&core));
    return 1;
}

static int test_invalid_pitch_is_atomic(void)
{
    thy_core core;
    thy_core before;
    const int16_t good = 60;
    const int16_t too_high[] = {60, 128};
    const int16_t too_low[] = {60, -1};
    thy_core_init(&core);
    CHECK(thy_core_press(&core, 1, &good, 1, 0, 0, THY_ROUTE_DIN, 80) == THY_OK);
    before = core;
    CHECK(thy_core_press(&core, 2, too_high, 2, 0, 0, THY_ROUTE_ALL, 80) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    CHECK(thy_core_press(&core, 2, too_low, 2, 0, 0, THY_ROUTE_ALL, 80) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    CHECK(thy_core_press(&core, 2, &good, 1, INT16_MAX, 0, THY_ROUTE_ALL, 80) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    CHECK(thy_core_press(&core, 2, &good, 1, INT16_MIN, 0, THY_ROUTE_ALL, 80) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    CHECK(invariant(&core));
    return 1;
}

static int test_optional_octaves_never_release_inactive_slots(void)
{
    thy_core core;
    const int16_t enabled[] = {60, 72};
    thy_core_init(&core);
    /* The caller supplies only enabled octave slots. No zero/unused slot exists
     * in ownership, including release of inactive optional octaves. */
    CHECK(thy_core_press(&core, 0, enabled, 2, 0, 0, THY_ROUTE_USB0, 80) == THY_OK);
    drain(&core);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x80, 60, 0));
    CHECK(take(&core, THY_USB0, 0x80, 72, 0));
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_core_refcount(&core, thy_core_key(THY_USB0, 0, 12)) == 0u);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(invariant(&core));
    return 1;
}

static int test_melody_and_mode75_pad_identity(void)
{
    thy_core core;
    const int16_t left = 127;
    const int16_t right = 0;
    thy_core_init(&core);
    /* Logical 0/48 remain different owners regardless of a UI mode. The right
     * pad's pitch zero is validated and sent without reading the left slot. */
    CHECK(thy_core_press(&core, 0, &left, 1, 0, 0, THY_ROUTE_USB0, 40) == THY_OK);
    CHECK(thy_core_press(&core, 48, &right, 1, 0, 0, THY_ROUTE_USB0, 50) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x90, 127, 40));
    CHECK(take(&core, THY_USB0, 0x90, 0, 50));
    CHECK(thy_core_release(&core, 48) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x80, 0, 0));
    CHECK(thy_core_pad_held(&core, 0));
    CHECK(!thy_core_pad_held(&core, 48));
    CHECK(thy_core_sounding(&core) == 1u);
    CHECK(invariant(&core));
    return 1;
}

static int test_bend_endpoints_and_real_cc(void)
{
    thy_core core;
    const int16_t note = 60;
    thy_core_init(&core);
    CHECK(thy_core_bend(&core, THY_ROUTE_USB0, 0, 8192) == THY_OK);
    CHECK(take(&core, THY_USB0, 0xe0, 0, 64));
    CHECK(thy_core_bend(&core, THY_ROUTE_DIN, 15, 16384) == THY_OK);
    CHECK(take(&core, THY_DIN, 0xef, 127, 127));
    CHECK(thy_core_bend(&core, THY_ROUTE_DIN, 15, INT32_MAX) == THY_OK);
    CHECK(take(&core, THY_DIN, 0xef, 127, 127));
    CHECK(thy_core_bend(&core, THY_ROUTE_DIN, 15, INT32_MIN) == THY_OK);
    CHECK(take(&core, THY_DIN, 0xef, 0, 0));
    CHECK(thy_core_press(&core, 0, &note, 1, 0, 5, THY_ROUTE_USB1, 80) == THY_OK);
    drain(&core);
    CHECK(thy_core_cc(&core, THY_ROUTE_USB1, 5, 123, 0) == THY_OK);
    CHECK(take(&core, THY_USB1, 0xb5, 123, 0));
    CHECK(thy_core_pad_held(&core, 0));
    CHECK(thy_core_cc(&core, THY_ROUTE_USB1, 5, 64, 64) == THY_OK);
    CHECK(take(&core, THY_USB1, 0xb5, 64, 64));
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(thy_core_sounding(&core) == 1u);
    CHECK(thy_core_cc(&core, THY_ROUTE_USB1, 5, 64, 63) == THY_OK);
    CHECK(take(&core, THY_USB1, 0xb5, 64, 63));
    CHECK(take(&core, THY_USB1, 0x85, 60, 0));
    CHECK(invariant(&core));
    return 1;
}

static int test_sustain_reacquire_and_route_isolation(void)
{
    thy_core core;
    const int16_t note = 60;
    thy_core_init(&core);
    CHECK(thy_core_sustain(&core, THY_ROUTE_USB0, 1, 1) == THY_OK);
    CHECK(take(&core, THY_USB0, 0xb1, 64, 127));
    CHECK(thy_core_sustain(&core, THY_ROUTE_USB0, 1, 1) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_core_press(&core, 0, &note, 1, 0, 1, THY_ROUTE_USB0 | THY_ROUTE_DIN, 80) == THY_OK);
    drain(&core);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(take(&core, THY_DIN, 0x81, 60, 0));
    CHECK(thy_core_sounding(&core) == 1u);
    CHECK(thy_core_refcount(&core, thy_core_key(THY_USB0, 1, 60)) == 0u);
    CHECK(thy_core_press(&core, 3, &note, 1, 0, 1, THY_ROUTE_USB0, 90) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u); /* A sustained voice does not retrigger. */
    CHECK(thy_core_sustain(&core, THY_ROUTE_USB0, 1, 0) == THY_OK);
    CHECK(take(&core, THY_USB0, 0xb1, 64, 0));
    CHECK(thy_core_sounding(&core) == 1u); /* The new owner is still physically held. */
    CHECK(thy_core_release(&core, 3) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x81, 60, 0));
    CHECK(invariant(&core));
    return 1;
}

static int test_reserved_noteoff_at_capacity(void)
{
    thy_core core;
    thy_core before;
    const int16_t note = 60;
    const int16_t different = 61;
    unsigned i;
    thy_core_init(&core);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 2u; ++i) {
        CHECK(thy_core_cc(&core, THY_ROUTE_USB0, 0, 1, (uint8_t)(i % 128u)) == THY_OK);
    }
    CHECK(thy_core_press(&core, 0, &note, 1, 0, 0, THY_ROUTE_USB0, 80) == THY_OK);
    CHECK(thy_core_queued(&core) == 1023u);
    CHECK(thy_core_reserved(&core) == 1u);
    before = core;
    CHECK(thy_core_press(&core, 1, &different, 1, 0, 0, THY_ROUTE_ALL, 80) == THY_QUEUE_FULL);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    CHECK(thy_core_pressure(&core, 0, 50) == THY_QUEUE_FULL);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    CHECK(thy_core_cc(&core, THY_ROUTE_USB0, 0, 1, 40) == THY_QUEUE_FULL);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    /* An overlapping owner creates no new event/reservation and can still press. */
    CHECK(thy_core_press(&core, 1, &note, 1, 0, 0, THY_ROUTE_USB0, 80) == THY_OK);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(thy_core_queued(&core) == 1023u);
    CHECK(thy_core_release(&core, 1) == THY_OK);
    CHECK(thy_core_queued(&core) == 1024u);
    CHECK(thy_core_reserved(&core) == 0u);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 2u; ++i) {
        CHECK(take(&core, THY_USB0, 0xb0, 1, (uint8_t)(i % 128u)));
    }
    CHECK(take(&core, THY_USB0, 0x90, 60, 80));
    CHECK(take(&core, THY_USB0, 0x80, 60, 0));
    CHECK(invariant(&core));
    return 1;
}

static int test_sustain_release_and_panic_at_capacity(void)
{
    thy_core core;
    const int16_t note = 0;
    unsigned i;
    thy_core_init(&core);
    CHECK(thy_core_press(&core, 0, &note, 1, 0, 0, THY_ROUTE_USB0, 80) == THY_OK);
    drain(&core);
    CHECK(thy_core_sustain(&core, THY_ROUTE_USB0, 0, 1) == THY_OK);
    drain(&core);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(thy_core_reserved(&core) == 2u);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 2u; ++i) {
        CHECK(thy_core_cc(&core, THY_ROUTE_USB0, 0, 1, 0) == THY_OK);
    }
    /* Generic repeated CC64 can reject atomically; the canonical transition has
     * guaranteed room and clears the sustained receiver before its NoteOff. */
    CHECK(thy_core_cc(&core, THY_ROUTE_USB0, 0, 64, 127) == THY_QUEUE_FULL);
    CHECK(thy_core_sustain(&core, THY_ROUTE_USB0, 0, 0) == THY_OK);
    CHECK(thy_core_queued(&core) == 1024u);
    CHECK(thy_core_reserved(&core) == 0u);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 2u; ++i) {
        CHECK(take(&core, THY_USB0, 0xb0, 1, 0));
    }
    CHECK(take(&core, THY_USB0, 0xb0, 64, 0));
    CHECK(take(&core, THY_USB0, 0x80, 0, 0));
    /* Repeat the saturated situation and verify panic's independent guarantee. */
    CHECK(thy_core_sustain(&core, THY_ROUTE_USB1, 15, 1) == THY_OK);
    CHECK(thy_core_press(&core, 95, &note, 1, 0, 15, THY_ROUTE_USB1, 80) == THY_OK);
    drain(&core);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 2u; ++i) {
        CHECK(thy_core_cc(&core, THY_ROUTE_USB0, 0, 1, 0) == THY_OK);
    }
    thy_core_panic(&core);
    CHECK(thy_core_queued(&core) == 1024u);
    CHECK(thy_core_reserved(&core) == 0u);
    CHECK(!thy_core_pad_held(&core, 95));
    thy_core_panic(&core);
    CHECK(thy_core_queued(&core) == 1024u);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 2u; ++i) {
        CHECK(take(&core, THY_USB0, 0xb0, 1, 0));
    }
    CHECK(take(&core, THY_USB1, 0xbf, 64, 0));
    CHECK(take(&core, THY_USB1, 0x8f, 0, 0));
    CHECK(invariant(&core));
    return 1;
}

static int test_maximum_owner_size_and_reference_count(void)
{
    thy_core core;
    int16_t notes[THY_MAX_NOTES];
    unsigned i;
    thy_core_init(&core);
    for (i = 0u; i < THY_MAX_NOTES; ++i) {
        notes[i] = (int16_t)i;
    }
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        CHECK(thy_core_press(&core, (uint8_t)i, notes, THY_MAX_NOTES, 0, 15, THY_ROUTE_ALL, 127) == THY_OK);
    }
    CHECK(core.owners[95].count == THY_OWNER_KEYS);
    CHECK(thy_core_refcount(&core, thy_core_key(THY_DIN, 15, 63)) == THY_PAD_COUNT);
    CHECK(thy_core_sounding(&core) == THY_OWNER_KEYS);
    CHECK(thy_core_queued(&core) == THY_OWNER_KEYS);
    CHECK(invariant(&core));
    thy_core_panic(&core);
    CHECK(thy_core_sounding(&core) == 0u);
    CHECK(thy_core_queued(&core) == 2u * THY_OWNER_KEYS);
    CHECK(invariant(&core));
    return 1;
}

static int test_multiroute_sustain_off_at_capacity(void)
{
    thy_core core;
    const int16_t chord[] = {0, 60};
    unsigned i;
    thy_core_init(&core);
    CHECK(thy_core_sustain(&core, THY_ROUTE_ALL, 3, 1) == THY_OK);
    CHECK(thy_core_press(&core, 2, chord, 2, 0, 3, THY_ROUTE_ALL, 100) == THY_OK);
    drain(&core);
    CHECK(thy_core_release(&core, 2) == THY_OK);
    CHECK(thy_core_reserved(&core) == 9u);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 9u; ++i) {
        CHECK(thy_core_cc(&core, THY_ROUTE_USB0, 0, 1, 0) == THY_OK);
    }
    CHECK(thy_core_sustain(&core, THY_ROUTE_ALL, 3, 0) == THY_OK);
    CHECK(thy_core_queued(&core) == THY_QUEUE_CAPACITY);
    CHECK(thy_core_reserved(&core) == 0u);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 9u; ++i) {
        CHECK(take(&core, THY_USB0, 0xb0, 1, 0));
    }
    for (i = 0u; i < THY_DEST_COUNT; ++i) {
        CHECK(take(&core, (uint8_t)i, 0xb3, 64, 0));
        CHECK(take(&core, (uint8_t)i, 0x83, 0, 0));
        CHECK(take(&core, (uint8_t)i, 0x83, 60, 0));
    }
    CHECK(invariant(&core));
    return 1;
}

static int test_silent_owners_and_invalid_arguments(void)
{
    thy_core core;
    thy_core before;
    thy_midi_event event;
    const int16_t note = 60;
    thy_core_init(&core);
    CHECK(thy_core_press(&core, 0, NULL, 0, 0, 0, THY_ROUTE_USB0, 127) == THY_OK);
    CHECK(thy_core_pad_held(&core, 0));
    CHECK(thy_core_sounding(&core) == 0u);
    CHECK(thy_core_pressure(&core, 0, 127) == THY_OK);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(thy_core_poll(&core, &event) == 0);
    before = core;
    CHECK(thy_core_press(&core, 96, &note, 1, 0, 0, 1, 1) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_press(&core, 0, &note, 65, 0, 0, 1, 1) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_press(&core, 0, NULL, 1, 0, 0, 1, 1) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_press(&core, 0, &note, 1, 0, 16, 1, 1) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_press(&core, 0, &note, 1, 0, 0, 0, 1) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_press(&core, 0, &note, 1, 0, 0, 8, 1) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_press(&core, 0, &note, 1, 0, 0, 1, 0) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_press(&core, 0, &note, 1, 0, 0, 1, 128) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_release(&core, 96) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_pressure(&core, 0, 128) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_sustain(&core, 8, 0, 1) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_cc(&core, 1, 0, 128, 0) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_cc(&core, 1, 0, 1, 128) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_bend(&core, 1, 16, 0) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    CHECK(thy_core_owner_key(&core, 0, 0) == UINT16_MAX);
    CHECK(thy_core_key(3, 0, 60) == UINT16_MAX);
    CHECK(thy_core_key(0, 16, 60) == UINT16_MAX);
    CHECK(thy_core_key(0, 0, 128) == UINT16_MAX);
    CHECK(thy_core_refcount(&core, UINT16_MAX) == 0u);
    CHECK(thy_core_press(NULL, 0, NULL, 0, 0, 0, 1, 1) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_release(NULL, 0) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_poll(NULL, &event) == 0);
    CHECK(thy_core_poll(&core, NULL) == 0);
    CHECK(thy_core_queued(NULL) == 0u);
    CHECK(thy_core_sounding(NULL) == 0u);
    CHECK(thy_core_reserved(NULL) == 0u);
    CHECK(invariant(&core));
    return 1;
}

static int test_queue_wrap_preserves_event_order(void)
{
    thy_core core;
    unsigned iteration;
    thy_core_init(&core);
    for (iteration = 0u; iteration < 12u; ++iteration) {
        unsigned i;
        for (i = 0u; i < 257u; ++i) {
            CHECK(thy_core_cc(&core, THY_ROUTE_DIN, (uint8_t)(iteration % 16u), 1, (uint8_t)(i % 128u)) == THY_OK);
        }
        for (i = 0u; i < 257u; ++i) {
            CHECK(take(&core, THY_DIN, (uint8_t)(0xb0u | (iteration % 16u)), 1, (uint8_t)(i % 128u)));
        }
    }
    CHECK(invariant(&core));
    return 1;
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static int test_serialized_spread_inversion_and_octave_stacks(void)
{
    thy_core core;
    thy_shape shape = {0};
    const int16_t serialized[] = {67, 60, 64, 72};
    const int16_t triad[] = {60, 64, 67};
    const int16_t octave_pair[] = {60, 72};
    int16_t preview[THY_MAX_NOTES];
    size_t count;
    unsigned spread;
    thy_core_init(&core);
    for (spread = 1u; spread <= 3u; ++spread) {
        shape.spread = (uint8_t)spread;
        CHECK(thy_core_preview(&core, serialized, 4, 0, &shape, preview, &count) == THY_OK);
        CHECK(count == 4u);
        CHECK(preview[0] == 67 - (int)spread * 12); /* First serialized, not lowest. */
        CHECK(preview[1] == 60 && preview[2] == 64 && preview[3] == 72);
    }
    shape.spread = 0;
    shape.inversion = 2;
    CHECK(thy_core_preview(&core, serialized, 4, 0, &shape, preview, &count) == THY_OK);
    CHECK(count == 3u && preview[0] == 79 && preview[1] == 72 && preview[2] == 64);
    shape.spread = 2;
    shape.inversion = 1;
    shape.octave_shift = 1;
    CHECK(thy_core_preview(&core, serialized, 4, 2, &shape, preview, &count) == THY_OK);
    CHECK(count == 4u && preview[0] == 69 && preview[1] == 74 && preview[2] == 78 && preview[3] == 86);
    shape.spread = 0;
    shape.inversion = 0;
    shape.octave_shift = 0;
    shape.upper_octaves = 1;
    CHECK(thy_core_preview(&core, triad, 3, 0, &shape, preview, &count) == THY_OK);
    CHECK(count == 6u && preview[0] == 60 && preview[1] == 64 && preview[2] == 67);
    CHECK(preview[3] == 72 && preview[4] == 76 && preview[5] == 79);
    CHECK(thy_core_preview(&core, octave_pair, 2, 0, &shape, preview, &count) == THY_OK);
    CHECK(count == 3u && preview[0] == 60 && preview[1] == 72 && preview[2] == 84);
    CHECK(thy_core_press_shaped(&core, 0, triad, 3, 0, 0, THY_ROUTE_USB0, 80, &shape) == THY_OK);
    CHECK(core.owners[0].count == 6u && core.owners[0].activated == 6u);
    CHECK(thy_core_pending(&core) == 0u);
    CHECK(invariant(&core));
    return 1;
}

static int test_excluded_original_note_slots_and_silent_selection(void)
{
    thy_core core;
    thy_shape shape = {0};
    const int16_t notes[] = {72, 67, 60, 64};
    const int16_t invalid_excluded[] = {INT16_MIN, 60};
    int16_t preview[THY_MAX_NOTES];
    size_t count;
    thy_core_init(&core);
    shape.excluded_slots = UINT64_C(1);
    shape.spread = 3;
    CHECK(thy_core_preview(&core, notes, 4, 0, &shape, preview, &count) == THY_OK);
    CHECK(count == 3u && preview[0] == 67 && preview[1] == 60 && preview[2] == 64);
    shape.inversion = 2;
    CHECK(thy_core_preview(&core, notes, 4, 0, &shape, preview, &count) == THY_OK);
    CHECK(count == 3u && preview[0] == 79 && preview[1] == 60 && preview[2] == 64);
    shape.inversion = 0;
    CHECK(thy_core_preview(&core, invalid_excluded, 2, 0, &shape, preview, &count) == THY_OK);
    CHECK(count == 1u && preview[0] == 60);
    shape.excluded_slots = UINT64_MAX;
    shape.strum_order = THY_STRUM_ALTERNATING;
    shape.strum_ms = 100;
    CHECK(thy_core_press_shaped(&core, 0, notes, 4, 0, 0, THY_ROUTE_ALL, 80, &shape) == THY_OK);
    CHECK(thy_core_pad_held(&core, 0));
    CHECK(core.owners[0].count == 0u && thy_core_pending(&core) == 0u);
    CHECK(core.alternating_down == 0u);
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(invariant(&core));
    return 1;
}

static int test_shaping_rejection_is_atomic(void)
{
    thy_core core;
    thy_core before;
    thy_shape shape = {0};
    const int16_t triad[] = {60, 64, 67};
    const int16_t edge[] = {0, 127};
    int16_t many[THY_MAX_NOTES];
    int16_t out[THY_MAX_NOTES];
    int16_t out_before[THY_MAX_NOTES];
    size_t count = 99u;
    unsigned i;
    thy_core_init(&core);
    before = core;
    for (i = 0u; i < THY_MAX_NOTES; ++i) {
        out[i] = out_before[i] = -999;
        many[i] = (int16_t)i;
    }
    shape.spread = 1;
    CHECK(thy_core_preview(&core, edge, 2, 0, &shape, out, &count) == THY_INVALID_ARGUMENT);
    CHECK(count == 99u && memcmp(out, out_before, sizeof(out)) == 0);
    CHECK(thy_core_press_shaped(&core, 0, edge, 2, 0, 0, 1, 80, &shape) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    shape.spread = 0;
    shape.upper_octaves = 1;
    CHECK(thy_core_press_shaped(&core, 0, edge, 2, 0, 0, 1, 80, &shape) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_press_shaped(&core, 0, many, THY_MAX_NOTES, 0, 0, 1, 80, &shape) == THY_INVALID_ARGUMENT);
    shape.excluded_slots = UINT64_MAX ^ UINT64_C(1); /* One enabled original slot. */
    CHECK(thy_core_preview(&core, many, THY_MAX_NOTES, 0, &shape, out, &count) == THY_OK);
    CHECK(count == 2u && out[0] == 0 && out[1] == 12);
    shape.excluded_slots = 0;
    shape.upper_octaves = 0;
    shape.spread = 4;
    CHECK(thy_core_press_shaped(&core, 0, triad, 3, 0, 0, 1, 80, &shape) == THY_INVALID_ARGUMENT);
    shape.spread = 0;
    shape.inversion = 4;
    CHECK(thy_core_press_shaped(&core, 0, triad, 3, 0, 0, 1, 80, &shape) == THY_INVALID_ARGUMENT);
    shape.inversion = 0;
    shape.octave_shift = 11;
    CHECK(thy_core_press_shaped(&core, 0, triad, 3, 0, 0, 1, 80, &shape) == THY_INVALID_ARGUMENT);
    shape.octave_shift = 0;
    shape.upper_octaves = 4;
    CHECK(thy_core_press_shaped(&core, 0, triad, 3, 0, 0, 1, 80, &shape) == THY_INVALID_ARGUMENT);
    shape.upper_octaves = 0;
    shape.strum_order = 4;
    CHECK(thy_core_press_shaped(&core, 0, triad, 3, 0, 0, 1, 80, &shape) == THY_INVALID_ARGUMENT);
    shape.strum_order = THY_STRUM_ALTERNATING;
    shape.strum_ms = 1001;
    CHECK(thy_core_press_shaped(&core, 0, triad, 3, 0, 0, 1, 80, &shape) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    CHECK(core.alternating_down == 0u);
    CHECK(invariant(&core));
    return 1;
}

static int test_strum_timing_snapshot_and_active_pressure(void)
{
    thy_core core;
    thy_shape shape = {0};
    thy_shape changed = {0};
    const int16_t notes[] = {67, 60, 67, 64};
    thy_core_init(&core);
    CHECK(thy_core_advance(&core, 1000) == THY_OK);
    shape.strum_ms = 10;
    CHECK(thy_core_press_shaped(&core, 0, notes, 4, 0, 2, THY_ROUTE_USB0 | THY_ROUTE_DIN, 75, &shape) == THY_OK);
    CHECK(thy_core_pending(&core) == 4u && thy_core_sounding(&core) == 2u);
    CHECK(thy_core_reserved(&core) == 10u);
    CHECK(take(&core, THY_USB0, 0x92, 67, 75));
    CHECK(take(&core, THY_DIN, 0x92, 67, 75));
    changed.excluded_slots = UINT64_MAX;
    CHECK(thy_core_press_shaped(&core, 0, notes, 4, 12, 15, THY_ROUTE_USB1, 1, &changed) == THY_OK);
    CHECK(thy_core_pressure(&core, 0, 44) == THY_OK);
    CHECK(take(&core, THY_USB0, 0xa2, 67, 44));
    CHECK(take(&core, THY_DIN, 0xa2, 67, 44));
    CHECK(thy_core_advance(&core, 1009) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_core_advance(&core, 1010) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x92, 60, 75));
    CHECK(take(&core, THY_DIN, 0x92, 60, 75));
    CHECK(thy_core_advance(&core, 1020) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x92, 64, 75));
    CHECK(take(&core, THY_DIN, 0x92, 64, 75));
    CHECK(thy_core_pending(&core) == 0u && thy_core_sounding(&core) == 6u);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x82, 67, 0));
    CHECK(take(&core, THY_DIN, 0x82, 67, 0));
    CHECK(take(&core, THY_USB0, 0x82, 60, 0));
    CHECK(take(&core, THY_DIN, 0x82, 60, 0));
    CHECK(take(&core, THY_USB0, 0x82, 64, 0));
    CHECK(take(&core, THY_DIN, 0x82, 64, 0));
    CHECK(invariant(&core));
    return 1;
}

static int test_strum_up_down_and_alternating_policy(void)
{
    thy_core core;
    thy_shape shape = {0};
    const int16_t notes[] = {67, 60, 64};
    int16_t preview[THY_MAX_NOTES];
    size_t count;
    thy_core_init(&core);
    shape.strum_ms = 10;
    shape.strum_order = THY_STRUM_UP;
    CHECK(thy_core_preview(&core, notes, 3, 0, &shape, preview, &count) == THY_OK);
    CHECK(count == 3u && preview[0] == 60 && preview[1] == 64 && preview[2] == 67);
    shape.strum_order = THY_STRUM_DOWN;
    CHECK(thy_core_preview(&core, notes, 3, 0, &shape, preview, &count) == THY_OK);
    CHECK(preview[0] == 67 && preview[1] == 64 && preview[2] == 60);
    shape.strum_order = THY_STRUM_ALTERNATING;
    CHECK(thy_core_preview(&core, notes, 3, 0, &shape, preview, &count) == THY_OK);
    CHECK(preview[0] == 60 && core.alternating_down == 0u);
    CHECK(thy_core_press_shaped(&core, 0, notes, 3, 0, 0, 1, 80, &shape) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x90, 60, 80));
    CHECK(thy_core_advance(&core, 20) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x90, 64, 80));
    CHECK(take(&core, THY_USB0, 0x90, 67, 80));
    CHECK(thy_core_release(&core, 0) == THY_OK);
    drain(&core);
    CHECK(core.alternating_down == 1u);
    CHECK(thy_core_preview(&core, notes, 3, 0, &shape, preview, &count) == THY_OK);
    CHECK(preview[0] == 67 && core.alternating_down == 1u);
    CHECK(thy_core_press_shaped(&core, 1, notes, 3, 0, 0, 1, 81, &shape) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x90, 67, 81));
    CHECK(thy_core_advance(&core, 40) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x90, 64, 81));
    CHECK(take(&core, THY_USB0, 0x90, 60, 81));
    CHECK(core.alternating_down == 0u);
    CHECK(invariant(&core));
    return 1;
}

static int test_strum_quick_release_cancels_ghost_notes(void)
{
    thy_core core;
    thy_shape shape = {0};
    const int16_t notes[] = {60, 64, 67};
    thy_core_init(&core);
    shape.strum_ms = 100;
    CHECK(thy_core_press_shaped(&core, 0, notes, 3, 0, 0, THY_ROUTE_ALL, 80, &shape) == THY_OK);
    CHECK(thy_core_pending(&core) == 6u);
    drain(&core);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(thy_core_pending(&core) == 0u);
    CHECK(take(&core, THY_USB0, 0x80, 60, 0));
    CHECK(take(&core, THY_USB1, 0x80, 60, 0));
    CHECK(take(&core, THY_DIN, 0x80, 60, 0));
    CHECK(thy_core_advance(&core, 100000) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u && thy_core_reserved(&core) == 0u);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(invariant(&core));
    return 1;
}

static int test_pending_notes_do_not_hold_other_owners(void)
{
    thy_core core;
    thy_shape shape = {0};
    const int16_t existing = 64;
    const int16_t notes[] = {60, 64, 67};
    thy_core_init(&core);
    shape.strum_ms = 10;
    CHECK(thy_core_press(&core, 0, &existing, 1, 0, 0, 1, 80) == THY_OK);
    CHECK(thy_core_press_shaped(&core, 1, notes, 3, 0, 0, 1, 81, &shape) == THY_OK);
    drain(&core);
    CHECK(thy_core_refcount(&core, thy_core_key(0, 0, 64)) == 1u);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x80, 64, 0));
    CHECK(thy_core_advance(&core, 10) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x90, 64, 81));
    CHECK(thy_core_release(&core, 1) == THY_OK);
    drain(&core);
    /* With the original owner retained at deadline, the delayed attack joins
     * it without retriggering; one owner's release cannot stop the other. */
    CHECK(thy_core_press(&core, 0, &existing, 1, 0, 0, 1, 80) == THY_OK);
    CHECK(thy_core_press_shaped(&core, 1, notes, 3, 0, 0, 1, 81, &shape) == THY_OK);
    drain(&core);
    CHECK(thy_core_advance(&core, 20) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_core_refcount(&core, thy_core_key(0, 0, 64)) == 2u);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_core_release(&core, 1) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x80, 60, 0));
    CHECK(take(&core, THY_USB0, 0x80, 64, 0));
    CHECK(invariant(&core));
    return 1;
}

static int test_strum_capacity_reservations_are_atomic(void)
{
    thy_core core;
    thy_core before;
    thy_shape shape = {0};
    const int16_t notes[] = {60, 64, 67};
    unsigned i;
    thy_core_init(&core);
    shape.strum_ms = 10;
    shape.strum_order = THY_STRUM_ALTERNATING;
    for (i = 0u; i < THY_QUEUE_CAPACITY - 12u; ++i) {
        CHECK(thy_core_cc(&core, 1, 0, 1, 0) == THY_OK);
    }
    before = core;
    CHECK(thy_core_press_shaped(&core, 0, notes, 3, 0, 0, THY_ROUTE_ALL, 80, &shape) == THY_QUEUE_FULL);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    thy_core_init(&core);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 18u; ++i) {
        CHECK(thy_core_cc(&core, 1, 0, 1, 0) == THY_OK);
    }
    CHECK(thy_core_press_shaped(&core, 0, notes, 3, 0, 0, THY_ROUTE_ALL, 80, &shape) == THY_OK);
    CHECK(thy_core_queued(&core) == 1009u && thy_core_reserved(&core) == 15u);
    CHECK(thy_core_pressure(&core, 0, 70) == THY_QUEUE_FULL);
    CHECK(thy_core_advance(&core, 10) == THY_OK);
    CHECK(thy_core_queued(&core) == 1012u && thy_core_reserved(&core) == 12u);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(thy_core_queued(&core) == 1018u && thy_core_reserved(&core) == 0u);
    CHECK(thy_core_pending(&core) == 0u);
    for (i = 0u; i < 6u; ++i) {
        CHECK(thy_core_cc(&core, 1, 0, 1, 0) == THY_OK);
    }
    CHECK(thy_core_queued(&core) == THY_QUEUE_CAPACITY);
    CHECK(thy_core_advance(&core, 1000) == THY_OK);
    CHECK(thy_core_queued(&core) == THY_QUEUE_CAPACITY);
    CHECK(invariant(&core));
    return 1;
}

static int test_strum_panic_with_sustain_and_full_obligations(void)
{
    thy_core core;
    thy_shape shape = {0};
    const int16_t notes[] = {60, 64, 67};
    unsigned i;
    thy_core_init(&core);
    shape.strum_ms = 10;
    CHECK(thy_core_sustain(&core, THY_ROUTE_ALL, 0, 1) == THY_OK);
    drain(&core);
    CHECK(thy_core_press_shaped(&core, 0, notes, 3, 0, 0, THY_ROUTE_ALL, 80, &shape) == THY_OK);
    drain(&core);
    CHECK(thy_core_reserved(&core) == 18u);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 18u; ++i) {
        CHECK(thy_core_cc(&core, 1, 0, 1, 0) == THY_OK);
    }
    thy_core_panic(&core);
    CHECK(thy_core_pending(&core) == 0u && thy_core_reserved(&core) == 0u);
    CHECK(thy_core_queued(&core) == 1012u); /* Canceled future notes need no offs. */
    for (i = 0u; i < THY_QUEUE_CAPACITY - 18u; ++i) {
        CHECK(take(&core, THY_USB0, 0xb0, 1, 0));
    }
    CHECK(take(&core, THY_USB0, 0xb0, 64, 0));
    CHECK(take(&core, THY_USB1, 0xb0, 64, 0));
    CHECK(take(&core, THY_DIN, 0xb0, 64, 0));
    CHECK(take(&core, THY_USB0, 0x80, 60, 0));
    CHECK(take(&core, THY_USB1, 0x80, 60, 0));
    CHECK(take(&core, THY_DIN, 0x80, 60, 0));
    CHECK(thy_core_advance(&core, 100) == THY_OK && thy_core_queued(&core) == 0u);
    CHECK(invariant(&core));
    return 1;
}

static int test_strum_clock_wrap_and_backward_atomicity(void)
{
    thy_core core;
    thy_core before;
    thy_shape shape = {0};
    const int16_t notes[] = {60, 64, 67};
    thy_core_init(&core);
    shape.strum_ms = 10;
    CHECK(thy_core_advance(&core, UINT32_MAX - 5u) == THY_OK);
    CHECK(thy_core_press_shaped(&core, 0, notes, 3, 0, 0, 1, 80, &shape) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x90, 60, 80));
    CHECK(thy_core_advance(&core, UINT32_MAX) == THY_OK && thy_core_queued(&core) == 0u);
    CHECK(thy_core_advance(&core, 4) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x90, 64, 80));
    CHECK(thy_core_advance(&core, 14) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x90, 67, 80));
    before = core;
    CHECK(thy_core_advance(&core, 13) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    CHECK(thy_core_advance(&core, 14u + UINT32_C(0x80000000)) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&core, &before, sizeof(core)) == 0);
    CHECK(thy_core_advance(&core, 14) == THY_OK);
    CHECK(thy_core_advance(&core, 14u + UINT32_C(0x7fffffff)) == THY_OK);
    CHECK(thy_core_advance(NULL, 0) == THY_INVALID_ARGUMENT);
    CHECK(invariant(&core));
    return 1;
}

static int test_strum_global_deadline_order_and_pad_ties(void)
{
    thy_core core;
    thy_shape slow = {0};
    thy_shape fast = {0};
    const int16_t low[] = {60, 61, 62};
    const int16_t high[] = {70, 71, 72};
    thy_core_init(&core);
    slow.strum_ms = 10;
    fast.strum_ms = 6;
    CHECK(thy_core_advance(&core, 1000) == THY_OK);
    CHECK(thy_core_press_shaped(&core, 5, low, 3, 0, 0, 1, 80, &slow) == THY_OK);
    CHECK(thy_core_advance(&core, 1002) == THY_OK);
    CHECK(thy_core_press_shaped(&core, 1, high, 3, 0, 0, 1, 81, &fast) == THY_OK);
    drain(&core);
    CHECK(thy_core_advance(&core, 1020) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x90, 71, 81)); /* 1008 */
    CHECK(take(&core, THY_USB0, 0x90, 61, 80)); /* 1010 */
    CHECK(take(&core, THY_USB0, 0x90, 72, 81)); /* 1014 */
    CHECK(take(&core, THY_USB0, 0x90, 62, 80)); /* 1020 */
    CHECK(invariant(&core));
    thy_core_init(&core);
    CHECK(thy_core_press_shaped(&core, 5, low, 3, 0, 0, 1, 80, &slow) == THY_OK);
    CHECK(thy_core_press_shaped(&core, 1, high, 3, 0, 0, 1, 81, &slow) == THY_OK);
    drain(&core);
    CHECK(thy_core_advance(&core, 10) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x90, 71, 81));
    CHECK(take(&core, THY_USB0, 0x90, 61, 80));
    CHECK(invariant(&core));
    return 1;
}

static int test_mixed_strum_state_invariants(void)
{
    thy_core core;
    uint32_t random = UINT32_C(0x51a0333);
    uint32_t now = 0u;
    unsigned i;
    thy_core_init(&core);
    for (i = 0u; i < 1800u; ++i) {
        uint32_t value = next_random(&random);
        uint8_t pad = (uint8_t)((value >> 7) % THY_PAD_COUNT);
        uint8_t channel = (uint8_t)((value >> 15) % 16u);
        uint8_t mask = (uint8_t)((value >> 20) % 7u + 1u);
        int16_t notes[] = {60, 64, 67};
        thy_shape shape = {0};
        int result = THY_OK;
        shape.spread = (uint8_t)((value >> 4) % 4u);
        shape.inversion = (uint8_t)((value >> 6) % 4u);
        shape.octave_shift = (int8_t)((int)((value >> 9) % 3u) - 1);
        shape.upper_octaves = (uint8_t)((value >> 11) % 2u);
        shape.strum_order = (uint8_t)((value >> 13) % 4u);
        shape.strum_ms = (uint16_t)((value >> 24) % 101u);
        shape.excluded_slots = (value >> 23) & 7u;
        switch (value % 8u) {
        case 0u:
        case 1u:
        case 2u:
            result = thy_core_press_shaped(&core, pad, notes, 3, 0, channel, mask, 100, &shape);
            break;
        case 3u:
            result = thy_core_release(&core, pad);
            break;
        case 4u:
            now += value % 57u;
            result = thy_core_advance(&core, now);
            break;
        case 5u:
            result = thy_core_sustain(&core, mask, channel, (int)((value >> 26) & 1u));
            break;
        case 6u:
            if (i % 71u == 0u) {
                thy_core_panic(&core);
            } else {
                drain(&core);
            }
            break;
        default:
            result = thy_core_pressure(&core, pad, 80);
            break;
        }
        CHECK(result == THY_OK || result == THY_QUEUE_FULL);
        CHECK(invariant(&core));
    }
    thy_core_panic(&core);
    CHECK(thy_core_pending(&core) == 0u && thy_core_reserved(&core) == 0u);
    CHECK(invariant(&core));
    return 1;
}

static int test_replacement_melody_layouts_full_grid(void)
{
    const int16_t major[] = {0, 2, 4, 5, 7, 9, 11};
    const int16_t expected_thirds[] = {60, 64, 67, 71, 74, 77, 81, 84, 88, 91, 95, 98};
    const int16_t expected_fourths[] = {60, 65, 71, 76, 81, 86, 91, 96, 101, 107, 112, 117};
    uint8_t row;
    uint8_t column;
    for (row = 0u; row < 4u; ++row) {
        for (column = 0u; column < 12u; ++column) {
            int16_t pitch = -1;
            CHECK(thy_core_melody_pitch(1, 60, NULL, 0, column, row, &pitch) == THY_OK);
            CHECK(pitch == 60 + column - 12 * row);
            CHECK(thy_core_melody_pitch(2, 60, major, 7, column, row, &pitch) == THY_OK);
            CHECK(pitch == expected_thirds[column] - 12 * row);
            CHECK(thy_core_melody_pitch(3, 60, major, 7, column, row, &pitch) == THY_OK);
            CHECK(pitch == expected_fourths[column] - 12 * row);
        }
    }
    return 1;
}

static int test_replacement_melody_layout_validation(void)
{
    const int16_t major[] = {0, 2, 4, 5, 7, 9, 11};
    const int16_t duplicate[] = {0, 2, 2};
    const int16_t unsorted[] = {0, 4, 2};
    const int16_t missing_root[] = {2, 4};
    const int16_t too_high[] = {0, 12};
    int16_t out = -999;
    CHECK(thy_core_melody_pitch(0, 60, major, 7, 0, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(4, 60, major, 7, 0, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(1, -1, NULL, 0, 0, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(1, 128, NULL, 0, 0, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(1, 0, NULL, 0, 0, 1, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(1, 127, NULL, 0, 1, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(2, 100, major, 7, 11, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(2, 60, NULL, 0, 0, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(2, 60, major, 13, 0, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(2, 60, duplicate, 3, 0, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(2, 60, unsorted, 3, 0, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(2, 60, missing_root, 2, 0, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(2, 60, too_high, 2, 0, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(2, 60, major, 7, 12, 0, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(2, 60, major, 7, 0, 4, &out) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_melody_pitch(2, 60, major, 7, 0, 0, NULL) == THY_INVALID_ARGUMENT);
    CHECK(out == -999);
    return 1;
}

static int test_deterministic_mixed_event_invariants(void)
{
    thy_core core;
    uint32_t random = UINT32_C(0x333666);
    unsigned i;
    thy_core_init(&core);
    for (i = 0u; i < 4000u; ++i) {
        uint32_t value = next_random(&random);
        uint8_t pad = (uint8_t)((value >> 7) % THY_PAD_COUNT);
        uint8_t channel = (uint8_t)((value >> 15) % 16u);
        uint8_t mask = (uint8_t)((value >> 20) % 7u + 1u);
        int16_t notes[4];
        thy_midi_event event;
        int result = THY_OK;
        notes[0] = (int16_t)((value >> 3) % 116u);
        notes[1] = (int16_t)(notes[0] + 4);
        notes[2] = (int16_t)(notes[0] + 7);
        notes[3] = notes[0];
        switch (value % 11u) {
        case 0u:
        case 1u:
        case 2u:
            result = thy_core_press(&core, pad, notes, 4, 0, channel, mask, 100);
            break;
        case 3u:
        case 4u:
            result = thy_core_release(&core, pad);
            break;
        case 5u:
            result = thy_core_sustain(&core, mask, channel, (int)((value >> 26) & 1u));
            break;
        case 6u:
            result = thy_core_pressure(&core, pad, (uint8_t)((value >> 24) % 128u));
            break;
        case 7u:
            result = thy_core_bend(&core, mask, channel, (int32_t)(value % 20000u));
            break;
        case 8u:
            result = thy_core_cc(&core, mask, channel, 1, 100);
            break;
        case 9u:
            (void)thy_core_poll(&core, &event);
            break;
        default:
            if (i % 101u == 0u) {
                thy_core_panic(&core);
            } else {
                unsigned n;
                for (n = 0u; n < 32u; ++n) {
                    (void)thy_core_poll(&core, &event);
                }
            }
            break;
        }
        CHECK(result == THY_OK || result == THY_QUEUE_FULL);
        CHECK(invariant(&core));
    }
    thy_core_panic(&core);
    CHECK(invariant(&core));
    CHECK(thy_core_sounding(&core) == 0u);
    CHECK(thy_core_reserved(&core) == 0u);
    return 1;
}

int run_midi_tests(unsigned *test_count, unsigned *assertion_count);

static int test_deferred_keyset_storage_and_isolation(void)
{
    thy_core core;
    const uint16_t route_count = THY_DEST_COUNT * 16u;
    uint16_t route;
    CHECK(sizeof core.deferred_bits == THY_DEFERRED_BYTES);
    CHECK(THY_DEFERRED_BYTES == 768u);
    CHECK(THY_KEY_COUNT - THY_DEFERRED_BYTES == 5376u);
    CHECK(thy_core_deferred(NULL, 0u) == 0u);
    CHECK(thy_core_deferred(&core, THY_KEY_COUNT) == 0u);
    CHECK(thy_core_deferred(&core, UINT16_MAX) == 0u);
    for (route = 0u; route < route_count; ++route) {
        uint8_t destination = (uint8_t)(route / 16u);
        uint8_t channel = (uint8_t)(route % 16u);
        uint8_t mask = (uint8_t)(1u << destination);
        uint16_t pitch, other;
        thy_core_init(&core);
        CHECK(thy_core_sustain(&core, mask, channel, 1) == THY_OK);
        /* Populate every bit through the actual note/sustain lifecycle. Each
         * new deferred voice must leave every neighboring pitch untouched. */
        for (pitch = 0u; pitch < 128u; ++pitch) {
            int16_t note = (int16_t)pitch;
            CHECK(thy_core_press(&core, 0u, &note, 1u, 0, channel, mask, 93u) == THY_OK);
            CHECK(thy_core_release(&core, 0u) == THY_OK);
            for (other = 0u; other < 128u; ++other)
                CHECK(thy_core_deferred(&core, (uint16_t)(route * 128u + other)) ==
                    (uint8_t)(other <= pitch));
        }
        for (other = 0u; other < THY_KEY_COUNT; ++other)
            CHECK(thy_core_deferred(&core, other) == (uint8_t)(other / 128u == route));
        CHECK(thy_core_queued(&core) == 129u);
        CHECK(thy_core_sounding(&core) == 128u);
        CHECK(invariant(&core));
        /* Reacquiring a sustained key clears ONLY its bit, with no new
         * attack. Releasing it again restores the same deferred voice. */
        for (pitch = 0u; pitch < 128u; ++pitch) {
            int16_t note = (int16_t)pitch;
            CHECK(thy_core_press(&core, 0u, &note, 1u, 0, channel, mask, 70u) == THY_OK);
            for (other = 0u; other < 128u; ++other)
                CHECK(thy_core_deferred(&core, (uint16_t)(route * 128u + other)) ==
                    (uint8_t)(other != pitch));
            CHECK(thy_core_queued(&core) == 129u);
            CHECK(thy_core_sounding(&core) == 128u);
            CHECK(thy_core_release(&core, 0u) == THY_OK);
            CHECK(thy_core_deferred(&core, (uint16_t)(route * 128u + pitch)) == 1u);
        }
        CHECK(thy_core_sustain(&core, mask, channel, 0) == THY_OK);
        CHECK(take(&core, destination, (uint8_t)(0xb0u | channel), 64u, 127u));
        for (pitch = 0u; pitch < 128u; ++pitch)
            CHECK(take(&core, destination, (uint8_t)(0x90u | channel), (uint8_t)pitch, 93u));
        CHECK(take(&core, destination, (uint8_t)(0xb0u | channel), 64u, 0u));
        for (pitch = 0u; pitch < 128u; ++pitch)
            CHECK(take(&core, destination, (uint8_t)(0x80u | channel), (uint8_t)pitch, 0u));
        CHECK(thy_core_queued(&core) == 0u);
        CHECK(thy_core_sounding(&core) == 0u);
        CHECK(invariant(&core));
        for (other = 0u; other < THY_KEY_COUNT; ++other)
            CHECK(thy_core_deferred(&core, other) == 0u);
    }
    return 1;
}

static int snapshot_matches_scalar(const thy_core *core)
{
    uint8_t expected[128] = {0}, guarded[130];
    memset(guarded, 0xa5, sizeof guarded);
    if (core != NULL && core->sounding_count != 0u)
        for (unsigned key = 0u; key < THY_KEY_COUNT; ++key)
            if (core->refs[key] || thy_core_deferred(core, (uint16_t)key))
                expected[key % 128u] = 1u;
    thy_core_sounding_pitches(core, guarded + 1u);
    CHECK(guarded[0] == 0xa5u && guarded[129] == 0xa5u);
    CHECK(memcmp(guarded + 1u, expected, sizeof expected) == 0);
    return 1;
}

static int test_sounding_pitch_snapshot(void)
{
    thy_core core, before;
    CHECK(snapshot_matches_scalar(NULL));
    thy_core_sounding_pitches(NULL, NULL);
    thy_core_init(&core);
    thy_core_sounding_pitches(&core, NULL);
    CHECK(snapshot_matches_scalar(&core));

    /* Every key exercises word/byte, channel and destination boundaries,
     * independently for owned and sustain-deferred notes. */
    core.sounding_count = 1u;
    for (unsigned key = 0u; key < THY_KEY_COUNT; ++key) {
        core.refs[key] = (uint8_t)(1u + key % 224u);
        CHECK(snapshot_matches_scalar(&core));
        core.refs[key] = 0u;
        core.deferred_bits[key / 8u] = (uint8_t)(1u << (key & 7u));
        CHECK(snapshot_matches_scalar(&core));
        core.deferred_bits[key / 8u] = 0u;
    }

    /* Inspection also remains equivalent on synthetic sparse/dense ledgers,
     * without treating owner metadata or a partial count as the pitch source. */
    uint32_t random = 0x413782bdu;
    for (unsigned trial = 0u; trial < 64u; ++trial) {
        thy_core_init(&core);
        for (unsigned key = 0u; key < THY_KEY_COUNT; ++key) {
            random = random * 1664525u + 1013904223u;
            if ((random >> 28u) == 0u) {
                core.refs[key] = (uint8_t)(1u + random % 255u);
                ++core.sounding_count;
            } else if ((random >> 28u) == 1u) {
                core.deferred_bits[key / 8u] |= (uint8_t)(1u << (key & 7u));
                ++core.sounding_count;
            }
        }
        before = core;
        CHECK(snapshot_matches_scalar(&core));
        CHECK(memcmp(&core, &before, sizeof core) == 0);
    }
    memset(core.refs, 0xff, sizeof core.refs);
    memset(core.deferred_bits, 0xff, sizeof core.deferred_bits);
    core.sounding_count = THY_KEY_COUNT;
    CHECK(snapshot_matches_scalar(&core));
    core.sounding_count = 0u;
    CHECK(snapshot_matches_scalar(&core));

    /* Generated voices, overlap, captured routes and deferred release must
     * appear exactly like physical ownership, without changing the core. */
    thy_core_init(&core);
    thy_generated_handle first, second;
    const int16_t notes[] = {0, 60, 127};
    CHECK(thy_core_generated_acquire(&core, 1u, 1u, 0u, 15u,
        THY_ROUTE_ALL, 90u, &first) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 2u, 1u, 0u, 0u,
        THY_ROUTE_DIN, 91u, &second) == THY_OK);
    CHECK(thy_core_press(&core, 0u, notes, 3u, 0, 0u,
        THY_ROUTE_ALL, 92u) == THY_OK);
    CHECK(snapshot_matches_scalar(&core));
    CHECK(thy_core_sustain(&core, THY_ROUTE_ALL, 15u, 1) == THY_OK);
    CHECK(thy_core_generated_release(&core, first) == THY_OK);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    CHECK(snapshot_matches_scalar(&core));
    CHECK(thy_core_generated_release(&core, second) == THY_OK);
    CHECK(snapshot_matches_scalar(&core));
    CHECK(thy_core_sustain(&core, THY_ROUTE_ALL, 15u, 0) == THY_OK);
    CHECK(snapshot_matches_scalar(&core));
    CHECK(thy_core_press_shaped(&core, 1u, notes, 3u, 0, 0u,
        THY_ROUTE_ALL, 93u, &(thy_shape){.strum_ms = 20u}) == THY_OK);
    CHECK(snapshot_matches_scalar(&core));
    CHECK(thy_core_advance(&core, 20u) == THY_OK);
    CHECK(snapshot_matches_scalar(&core));
    thy_core_panic(&core);
    CHECK(snapshot_matches_scalar(&core));
    return 1;
}

int main(void)
{
    struct named_test { const char *name; int (*run)(void); };
    const struct named_test tests[] = {
        {"sounding pitch snapshots match the reference ledger", test_sounding_pitch_snapshot},
        {"overlap and duplicate pitches", test_overlap_and_duplicates},
        {"channel and destination isolation", test_same_pitch_separate_channels_and_destinations},
        {"held root, route and octave snapshots", test_held_root_route_and_octave_changes},
        {"invalid pitch atomicity", test_invalid_pitch_is_atomic},
        {"inactive octave release", test_optional_octaves_never_release_inactive_slots},
        {"melody and mode 75 pad identity", test_melody_and_mode75_pad_identity},
        {"bend endpoints and typed CC", test_bend_endpoints_and_real_cc},
        {"sustain reacquire and route isolation", test_sustain_reacquire_and_route_isolation},
        {"packed sustain flags preserve every key and event order", test_deferred_keyset_storage_and_isolation},
        {"reserved noteoff at queue capacity", test_reserved_noteoff_at_capacity},
        {"sustain release and panic at capacity", test_sustain_release_and_panic_at_capacity},
        {"maximum owner size and reference count", test_maximum_owner_size_and_reference_count},
        {"multiroute sustain-off at capacity", test_multiroute_sustain_off_at_capacity},
        {"silent owners and validation", test_silent_owners_and_invalid_arguments},
        {"queue wrap ordering", test_queue_wrap_preserves_event_order},
        {"deterministic mixed event invariants", test_deterministic_mixed_event_invariants},
        {"serialized spread, inversion and octave stacks", test_serialized_spread_inversion_and_octave_stacks},
        {"excluded original note slots", test_excluded_original_note_slots_and_silent_selection},
        {"shaping rejection atomicity", test_shaping_rejection_is_atomic},
        {"strum timing and immutable snapshots", test_strum_timing_snapshot_and_active_pressure},
        {"strum up/down/alternating policy", test_strum_up_down_and_alternating_policy},
        {"strum release cancels ghost notes", test_strum_quick_release_cancels_ghost_notes},
        {"pending notes do not own other voices", test_pending_notes_do_not_hold_other_owners},
        {"strum queue reservation atomicity", test_strum_capacity_reservations_are_atomic},
        {"strum panic with full obligations", test_strum_panic_with_sustain_and_full_obligations},
        {"strum clock wrap and backward rejection", test_strum_clock_wrap_and_backward_atomicity},
        {"strum global deadline ordering", test_strum_global_deadline_order_and_pad_ties},
        {"mixed strum state invariants", test_mixed_strum_state_invariants},
        {"replacement melody layouts full grid", test_replacement_melody_layouts_full_grid},
        {"replacement melody layout validation", test_replacement_melody_layout_validation}
    };
    size_t i;
    unsigned test_count = 0u;
    for (i = 0u; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!tests[i].run()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            return 1;
        }
        ++test_count;
        printf("PASS: %s\n", tests[i].name);
    }
    if (!run_midi_tests(&test_count, &assertions)) {
        return 1;
    }
    printf("%u C regression tests passed (%u assertions); core state %zu bytes.\n",
            test_count, assertions, sizeof(thy_core));
    return 0;
}
