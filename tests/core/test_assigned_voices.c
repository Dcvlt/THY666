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

static int take(thy_core *core, uint8_t dest, uint8_t status,
                uint8_t data1, uint8_t data2)
{
    thy_midi_event event;
    CHECK(thy_core_poll(core, &event));
    CHECK(event.destination == dest && event.status == status);
    CHECK(event.data1 == data1 && event.data2 == data2);
    return 1;
}

static void drain(thy_core *core)
{
    thy_midi_event event;
    while (thy_core_poll(core, &event)) { }
}

static int mixed_channels_capture_and_overlap(void)
{
    thy_core core;
    int16_t notes[] = {60, 60, 67};
    uint8_t channels[] = {1u, 3u, 15u};
    thy_core_init(&core);
    CHECK(thy_core_press_assigned(&core, 0u, notes, channels, 3u,
        THY_ROUTE_USB0 | THY_ROUTE_DIN, 87u, 0u) == THY_OK);
    CHECK(core.owners[0].activated == 6u && thy_core_sounding(&core) == 6u);
    for (unsigned n = 0u; n < 3u; ++n) {
        CHECK(thy_core_owner_key(&core, 0u, (uint8_t)(2u*n)) ==
            thy_core_key(THY_USB0, channels[n], (uint8_t)notes[n]));
        CHECK(take(&core, THY_USB0, (uint8_t)(0x90u | channels[n]), (uint8_t)notes[n], 87u));
        CHECK(take(&core, THY_DIN, (uint8_t)(0x90u | channels[n]), (uint8_t)notes[n], 87u));
    }
    CHECK(thy_core_press(&core, 1u, notes, 1u, 0, 3u, THY_ROUTE_DIN, 99u) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u);
    notes[0] = 12; channels[0] = 8u;
    CHECK(thy_core_press_assigned(&core, 0u, notes, channels, 1u,
        THY_ROUTE_USB1, 70u, 0u) == THY_OK);
    CHECK(thy_core_pressure(&core, 0u, 77u) == THY_OK);
    CHECK(take(&core, THY_USB0, 0xa1u, 60u, 77u));
    CHECK(take(&core, THY_DIN, 0xa1u, 60u, 77u));
    drain(&core);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x81u, 60u, 0u));
    CHECK(take(&core, THY_DIN, 0x81u, 60u, 0u));
    CHECK(take(&core, THY_USB0, 0x83u, 60u, 0u));
    CHECK(take(&core, THY_USB0, 0x8fu, 67u, 0u));
    CHECK(take(&core, THY_DIN, 0x8fu, 67u, 0u));
    CHECK(thy_core_sounding(&core) == 1u && thy_core_queued(&core) == 0u);
    CHECK(thy_core_release(&core, 1u) == THY_OK);
    CHECK(take(&core, THY_DIN, 0x83u, 60u, 0u));
    return 1;
}

static int assigned_strum_schedule_and_cancel(void)
{
    thy_core core;
    const int16_t notes[] = {0, 64, 127};
    const uint8_t channels[] = {1u, 2u, 3u};
    thy_core_init(&core);
    CHECK(thy_core_advance(&core, 100u) == THY_OK);
    CHECK(thy_core_press_assigned(&core, 0u, notes, channels, 3u,
        THY_ROUTE_USB1 | THY_ROUTE_DIN, 80u, 25u) == THY_OK);
    CHECK(thy_core_pending(&core) == 4u && thy_core_reserved(&core) == 10u);
    CHECK(take(&core, THY_USB1, 0x91u, 0u, 80u));
    CHECK(take(&core, THY_DIN, 0x91u, 0u, 80u));
    CHECK(thy_core_advance(&core, 124u) == THY_OK && thy_core_queued(&core) == 0u);
    CHECK(thy_core_advance(&core, 125u) == THY_OK);
    CHECK(take(&core, THY_USB1, 0x92u, 64u, 80u));
    CHECK(take(&core, THY_DIN, 0x92u, 64u, 80u));
    CHECK(thy_core_pending(&core) == 2u && core.owners[0].activated == 4u);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    CHECK(thy_core_pending(&core) == 0u);
    CHECK(take(&core, THY_USB1, 0x81u, 0u, 0u));
    CHECK(take(&core, THY_DIN, 0x81u, 0u, 0u));
    CHECK(take(&core, THY_USB1, 0x82u, 64u, 0u));
    CHECK(take(&core, THY_DIN, 0x82u, 64u, 0u));
    CHECK(thy_core_advance(&core, 500u) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u && thy_core_reserved(&core) == 0u);
    return 1;
}

static int assigned_validation_is_atomic(void)
{
    thy_core core, before;
    int16_t notes[] = {60, 64};
    uint8_t channels[] = {1u, 2u};
    thy_core_init(&core);
    before = core;
#define BAD_PRESS(n,c,count,d,v,s) do { \
    CHECK(thy_core_press_assigned(&core, 0u, n, c, count, d, v, s) == THY_INVALID_ARGUMENT); \
    CHECK(memcmp(&before, &core, sizeof core) == 0); \
} while (0)
    BAD_PRESS(NULL, channels, 2u, THY_ROUTE_DIN, 90u, 0u);
    BAD_PRESS(notes, NULL, 2u, THY_ROUTE_DIN, 90u, 0u);
    BAD_PRESS(notes, channels, THY_MAX_NOTES + 1u, THY_ROUTE_DIN, 90u, 0u);
    BAD_PRESS(notes, channels, 2u, 0u, 90u, 0u);
    BAD_PRESS(notes, channels, 2u, 8u, 90u, 0u);
    BAD_PRESS(notes, channels, 2u, THY_ROUTE_DIN, 0u, 0u);
    BAD_PRESS(notes, channels, 2u, THY_ROUTE_DIN, 128u, 0u);
    BAD_PRESS(notes, channels, 2u, THY_ROUTE_DIN, 90u, 1001u);
    notes[1] = -1; BAD_PRESS(notes, channels, 2u, THY_ROUTE_DIN, 90u, 0u);
    notes[1] = 128; BAD_PRESS(notes, channels, 2u, THY_ROUTE_DIN, 90u, 0u);
    notes[1] = 64; channels[1] = 16u;
    BAD_PRESS(notes, channels, 2u, THY_ROUTE_DIN, 90u, 0u);
    notes[1] = 60; channels[1] = 1u;
    BAD_PRESS(notes, channels, 2u, THY_ROUTE_DIN, 90u, 0u);
#undef BAD_PRESS
    CHECK(thy_core_press_assigned(NULL, 0u, NULL, NULL, 0u, THY_ROUTE_DIN, 90u, 0u) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_press_assigned(&core, THY_PAD_COUNT, NULL, NULL, 0u, THY_ROUTE_DIN, 90u, 0u) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_press_assigned(&core, 0u, NULL, NULL, 0u, THY_ROUTE_DIN, 90u, 1000u) == THY_OK);
    CHECK(thy_core_pad_held(&core, 0u) && thy_core_pending(&core) == 0u);
    CHECK(thy_core_release(&core, 0u) == THY_OK && thy_core_queued(&core) == 0u);
    return 1;
}

static int assigned_capacity_preserves_cleanup(void)
{
    thy_core core, before;
    const int16_t notes[] = {60, 64};
    const uint8_t channels[] = {1u, 2u};
    thy_core_init(&core);
    CHECK(thy_core_press_assigned(&core, 0u, notes, channels, 1u, THY_ROUTE_ALL, 80u, 0u) == THY_OK);
    while (thy_core_cc(&core, THY_ROUTE_DIN, 0u, 1u, 0u) == THY_OK) { }
    CHECK(thy_core_queued(&core) + thy_core_reserved(&core) == THY_QUEUE_CAPACITY);
    before = core;
    CHECK(thy_core_press_assigned(&core, 1u, notes, channels, 2u, THY_ROUTE_ALL, 80u, 20u) == THY_QUEUE_FULL);
    CHECK(memcmp(&before, &core, sizeof core) == 0);
    CHECK(thy_core_press_assigned(&core, 1u, notes, channels, 1u, THY_ROUTE_ALL, 80u, 0u) == THY_OK);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    CHECK(thy_core_release(&core, 1u) == THY_OK);
    CHECK(thy_core_queued(&core) == THY_QUEUE_CAPACITY && thy_core_reserved(&core) == 0u);
    thy_core_init(&core);
    CHECK(thy_core_press_assigned(&core, 0u, notes, channels, 2u, THY_ROUTE_ALL, 80u, 20u) == THY_OK);
    while (thy_core_cc(&core, THY_ROUTE_DIN, 0u, 1u, 0u) == THY_OK) { }
    CHECK(thy_core_queued(&core) + thy_core_reserved(&core) == THY_QUEUE_CAPACITY);
    CHECK(thy_core_advance(&core, 20u) == THY_OK);
    CHECK(thy_core_pending(&core) == 0u && thy_core_sounding(&core) == 6u);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    CHECK(thy_core_queued(&core) == THY_QUEUE_CAPACITY && thy_core_reserved(&core) == 0u);
    return 1;
}

static int control_batches_are_atomic_and_ordered(void)
{
    thy_core core, before;
    thy_midi_event events[] = {
        {THY_DIN, 0xe1u, 0u, 64u},
        {THY_USB0, 0xd1u, 0u, 0u},
        {THY_USB1, 0xbfu, 101u, 0u}
    };
    thy_core_init(&core);
    CHECK(thy_core_emit_batch(&core, NULL, 0u) == THY_OK);
    CHECK(thy_core_emit_batch(&core, events, 3u) == THY_OK);
    CHECK(take(&core, THY_DIN, 0xe1u, 0u, 64u));
    CHECK(take(&core, THY_USB0, 0xd1u, 0u, 0u));
    CHECK(take(&core, THY_USB1, 0xbfu, 101u, 0u));
    before = core;
    const thy_midi_event invalid[] = {
        {THY_DEST_COUNT, 0xe0u, 0u, 64u}, {THY_DIN, 0x90u, 60u, 80u},
        {THY_DIN, 0xa0u, 60u, 80u}, {THY_DIN, 0xc0u, 60u, 0u},
        {THY_DIN, 0xf0u, 0u, 0u}, {THY_DIN, 0xb0u, 64u, 127u},
        {THY_DIN, 0xd0u, 1u, 1u}, {THY_DIN, 0xe0u, 128u, 0u},
        {THY_DIN, 0xe0u, 0u, 128u}
    };
    for (unsigned n = 0u; n < sizeof invalid / sizeof invalid[0]; ++n) {
        events[1] = invalid[n];
        CHECK(thy_core_emit_batch(&core, events, 3u) == THY_INVALID_ARGUMENT);
        CHECK(memcmp(&core, &before, sizeof core) == 0);
    }
    CHECK(thy_core_emit_batch(&core, NULL, 1u) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_emit_batch(&core, events, (size_t)-1) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_emit_batch(NULL, NULL, 0u) == THY_INVALID_ARGUMENT);
    return 1;
}

static int control_capacity_and_channel_pressure(void)
{
    thy_core core, before;
    const int16_t note = 60;
    const uint8_t channel = 3u;
    const thy_midi_event events[] = {{THY_DIN, 0xe0u, 0u, 64u}, {THY_DIN, 0xd0u, 0u, 0u}};
    thy_core_init(&core);
    CHECK(thy_core_channel_pressure(&core, THY_ROUTE_ALL, 15u, 127u) == THY_OK);
    CHECK(take(&core, THY_USB0, 0xdfu, 127u, 0u));
    CHECK(take(&core, THY_USB1, 0xdfu, 127u, 0u));
    CHECK(take(&core, THY_DIN, 0xdfu, 127u, 0u));
    CHECK(thy_core_press_assigned(&core, 0u, &note, &channel, 1u, THY_ROUTE_DIN, 90u, 0u) == THY_OK);
    while (thy_core_queued(&core) + thy_core_reserved(&core) < THY_QUEUE_CAPACITY - 1u)
        CHECK(thy_core_emit_batch(&core, events, 1u) == THY_OK);
    before = core;
    CHECK(thy_core_emit_batch(&core, events, 2u) == THY_QUEUE_FULL);
    CHECK(memcmp(&core, &before, sizeof core) == 0);
    CHECK(thy_core_channel_pressure(&core, THY_ROUTE_ALL, 3u, 55u) == THY_QUEUE_FULL);
    CHECK(memcmp(&core, &before, sizeof core) == 0);
    CHECK(thy_core_channel_pressure(&core, THY_ROUTE_DIN, 16u, 55u) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_channel_pressure(&core, 0u, 3u, 55u) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_channel_pressure(&core, THY_ROUTE_DIN, 3u, 128u) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_emit_batch(&core, events, 1u) == THY_OK);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    CHECK(thy_core_queued(&core) == THY_QUEUE_CAPACITY && thy_core_reserved(&core) == 0u);
    drain(&core);
    return 1;
}

static int assigned_sustain_is_channel_specific(void)
{
    thy_core core;
    const int16_t notes[] = {60, 64};
    const uint8_t channels[] = {1u, 2u};
    thy_core_init(&core);
    CHECK(thy_core_sustain(&core, THY_ROUTE_DIN, 1u, 1) == THY_OK);
    CHECK(thy_core_press_assigned(&core, 0u, notes, channels, 2u, THY_ROUTE_DIN, 90u, 0u) == THY_OK);
    drain(&core);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    CHECK(take(&core, THY_DIN, 0x82u, 64u, 0u));
    CHECK(thy_core_deferred(&core, thy_core_key(THY_DIN, 1u, 60u)) == 1u);
    CHECK(thy_core_sounding(&core) == 1u);
    CHECK(thy_core_sustain(&core, THY_ROUTE_DIN, 1u, 0) == THY_OK);
    CHECK(take(&core, THY_DIN, 0xb1u, 64u, 0u));
    CHECK(take(&core, THY_DIN, 0x81u, 60u, 0u));
    CHECK(thy_core_queued(&core) == 0u && thy_core_reserved(&core) == 0u);
    return 1;
}

static int factored_owner_orders_and_boundaries(void)
{
    thy_core core;
    int16_t notes[THY_MAX_NOTES];
    uint8_t channels[THY_MAX_NOTES];
    CHECK(sizeof(thy_pad_owner) == 140u);
    CHECK(THY_MAX_NOTES == 64u && THY_OWNER_KEYS == 192u);
    CHECK(THY_PAD_COUNT == 96u && THY_GENERATED_OWNER_COUNT == 128u);
    CHECK(THY_QUEUE_CAPACITY == 1024u);
    CHECK(thy_pad_owner_key(NULL, 0u) == UINT16_MAX);
    for (unsigned mask = 1u; mask <= THY_ROUTE_ALL; ++mask) {
        for (unsigned assigned = 0u; assigned <= 1u; ++assigned) {
            thy_core_init(&core);
            for (unsigned n = 0u; n < THY_MAX_NOTES; ++n) {
                notes[n] = (int16_t)(assigned ? (n == 1u ? 0u : n * 2u) : n);
                channels[n] = (uint8_t)(n % 16u);
            }
            if (assigned) notes[THY_MAX_NOTES - 1u] = 127;
            if (assigned)
                CHECK(thy_core_press_assigned(&core, 95u, notes, channels,
                    THY_MAX_NOTES, (uint8_t)mask, 99u, 0u) == THY_OK);
            else
                CHECK(thy_core_press(&core, 95u, notes, THY_MAX_NOTES, 0,
                    15u, (uint8_t)mask, 99u) == THY_OK);
            unsigned count = 0u;
            /* Different loops deliberately specify both public order contracts. */
            for (unsigned major = 0u; major < (assigned ? THY_MAX_NOTES : THY_DEST_COUNT); ++major)
                for (unsigned minor = 0u; minor < (assigned ? THY_DEST_COUNT : THY_MAX_NOTES); ++minor) {
                    unsigned destination = assigned ? minor : major;
                    unsigned n = assigned ? major : minor;
                    if (!(mask & (1u << destination))) continue;
                    uint8_t channel = assigned ? channels[n] : 15u;
                    uint16_t key = thy_core_key((uint8_t)destination, channel, (uint8_t)notes[n]);
                    CHECK(thy_core_owner_key(&core, 95u, (uint8_t)count) == key);
                    CHECK(thy_pad_owner_key(&core.owners[95], (uint8_t)count) == key);
                    CHECK(take(&core, (uint8_t)destination, (uint8_t)(0x90u | channel), (uint8_t)notes[n], 99u));
                    ++count;
                }
            CHECK(core.owners[95].count == count && core.owners[95].activated == count);
            CHECK(thy_pad_owner_key(&core.owners[95], (uint8_t)count) == UINT16_MAX);
            CHECK(thy_core_owner_key(&core, 95u, 255u) == UINT16_MAX);
            CHECK(thy_core_owner_key(&core, 96u, 0u) == UINT16_MAX);
            CHECK(thy_core_queued(&core) == 0u && thy_core_sounding(&core) == count);
            CHECK(thy_core_pressure(&core, 95u, 75u) == THY_OK);
            for (unsigned n = 0u; n < count; ++n) {
                uint16_t key = thy_core_owner_key(&core, 95u, (uint8_t)n);
                CHECK(take(&core, (uint8_t)(key / 2048u), (uint8_t)(0xa0u | ((key / 128u) % 16u)), (uint8_t)(key % 128u), 75u));
            }
            thy_pad_owner held = core.owners[95];
            CHECK(thy_core_release(&core, 95u) == THY_OK);
            for (unsigned n = 0u; n < count; ++n) {
                uint16_t key = thy_pad_owner_key(&held, (uint8_t)n);
                CHECK(take(&core, (uint8_t)(key / 2048u), (uint8_t)(0x80u | ((key / 128u) % 16u)), (uint8_t)(key % 128u), 0u));
            }
            CHECK(thy_core_sounding(&core) == 0u && thy_core_queued(&core) == 0u);
            CHECK(thy_pad_owner_key(&core.owners[95], 0u) == UINT16_MAX);
        }
    }
    thy_core_init(&core);
    CHECK(thy_core_press_assigned(&core, 0u, NULL, NULL, 0u, THY_ROUTE_ALL, 1u, 15u) == THY_OK);
    CHECK(core.owners[0].held && !core.owners[0].count && !core.pending_keys);
    CHECK(thy_pad_owner_key(&core.owners[0], 0u) == UINT16_MAX);
    return 1;
}

static int assigned_aliased_input_is_captured_before_mutation(void)
{
    thy_core core;
    thy_core_init(&core);
    int16_t *notes = (int16_t *)core.owners[0].notes;
    uint8_t *channels = (uint8_t *)&core.owners[0].notes[8];
    for (unsigned n = 0u; n < 8u; ++n) { notes[n] = (int16_t)(12u+n); channels[n] = (uint8_t)n; }
    CHECK(thy_core_press_assigned(&core, 0u, notes, channels, 8u, THY_ROUTE_ALL, 91u, 0u) == THY_OK);
    for (unsigned n = 0u; n < 8u; ++n)
        for (unsigned destination = 0u; destination < THY_DEST_COUNT; ++destination) {
            CHECK(thy_core_owner_key(&core, 0u, (uint8_t)(n*THY_DEST_COUNT+destination)) ==
                thy_core_key((uint8_t)destination, (uint8_t)n, (uint8_t)(12u+n)));
            CHECK(take(&core, (uint8_t)destination, (uint8_t)(0x90u|n), (uint8_t)(12u+n), 91u));
        }
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    for (unsigned n = 0u; n < 8u; ++n)
        for (unsigned destination = 0u; destination < THY_DEST_COUNT; ++destination)
            CHECK(take(&core, (uint8_t)destination, (uint8_t)(0x80u|n), (uint8_t)(12u+n), 0u));
    CHECK(!core.sounding_count && !core.queue_count);
    return 1;
}

int main(void)
{
    if (!mixed_channels_capture_and_overlap() || !assigned_strum_schedule_and_cancel() ||
        !assigned_validation_is_atomic() || !assigned_capacity_preserves_cleanup() ||
        !control_batches_are_atomic_and_ordered() || !control_capacity_and_channel_pressure() ||
        !assigned_sustain_is_channel_specific() || !factored_owner_orders_and_boundaries() ||
        !assigned_aliased_input_is_captured_before_mutation()) return 1;
    printf("Assigned voice tests passed: 9 scenarios, %u assertions\n", assertions);
    return 0;
}
