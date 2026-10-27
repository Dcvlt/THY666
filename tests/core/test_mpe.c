/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_mpe.h"

static unsigned assertions;
#define CHECK(value) do { ++assertions; if (!(value)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#value); return 0; } } while (0)

static void drain(thy_core *core)
{
    thy_midi_event event;
    while (thy_core_poll(core, &event)) { }
}

static int take(thy_core *core, uint8_t destination, uint8_t status,
    uint8_t first, uint8_t second)
{
    thy_midi_event event;
    CHECK(thy_core_poll(core, &event));
    CHECK(event.destination == destination && event.status == status);
    CHECK(event.data1 == first && event.data2 == second);
    return 1;
}

static int enable(thy_mpe *mpe, thy_core *core, uint8_t members)
{
    thy_mpe_config config;
    thy_core_init(core);
    thy_mpe_init(mpe, core);
    thy_mpe_defaults(&config);
    config.enabled = 1u;
    config.members = members;
    CHECK(thy_mpe_configure(mpe, &config) == THY_OK);
    return 1;
}

static int press(thy_mpe *mpe, uint8_t pad, uint8_t logical, uint8_t pitch)
{
    int consumed = -1;
    int16_t note = pitch;
    CHECK(thy_mpe_press(mpe, pad, logical, pitch, &note, 1u, THY_ROUTE_DIN,
        96u, 0u, &consumed) == THY_OK);
    CHECK(consumed == 0);
    return 1;
}

static int invariant(const thy_mpe *mpe)
{
    uint8_t i, j;
    CHECK(thy_core_queued(mpe->core) + thy_core_reserved(mpe->core) <= THY_QUEUE_CAPACITY);
    for (i = 0u; i < THY_MPE_MEMBERS; ++i) {
        const thy_mpe_voice *voice = thy_mpe_voice_at(mpe, i);
        if (voice == NULL) continue;
        CHECK(voice->pad < THY_PAD_COUNT && voice->logical < THY_PAD_COUNT);
        CHECK(thy_mpe_channel_in_zone(&mpe->config, voice->channel));
        CHECK(voice->bend <= 16383u && voice->sent_bend <= 16383u);
        CHECK(voice->pressure <= 127u && voice->sent_pressure <= 127u);
        CHECK(voice->timbre <= 127u && voice->sent_timbre <= 127u);
        for (j = 0u; j < i; ++j) {
            const thy_mpe_voice *other = thy_mpe_voice_at(mpe, j);
            CHECK(other == NULL || other->channel != voice->channel);
        }
        if (voice->state == THY_MPE_HELD) {
            const thy_pad_owner *owner = &mpe->core->owners[voice->pad];
            int found = 0;
            CHECK(owner->held);
            for (j = 0u; j < owner->count; ++j) {
                uint16_t key = thy_pad_owner_key(owner, (uint8_t)(j));
                if ((key / 128u) % 16u == voice->channel && key % 128u == voice->pitch)
                    found = 1;
            }
            CHECK(found);
        }
    }
    return 1;
}

static int test_defaults_and_atomic_configuration(void)
{
    thy_core core;
    thy_mpe mpe;
    thy_mpe_config config, before;
    const int16_t note = 60;
    int consumed = -1;
    thy_core_init(&core);
    thy_mpe_init(&mpe, &core);
    CHECK(sizeof(mpe) <= 1024u);
    CHECK(thy_mpe_quiet(&mpe));
    CHECK(thy_mpe_config_valid(&mpe.config));
    CHECK(!mpe.config.enabled && mpe.config.members == 15u);
    CHECK(mpe.config.bend_range == 48u && mpe.config.manager_bend_range == 2u);
    CHECK(!mpe.config.glide && mpe.config.pad_window == 1u && mpe.config.semitone_window == 2u);
    CHECK(thy_mpe_press(&mpe, 0u, 0u, 60u, &note, 1u, THY_ROUTE_DIN, 96u, 0u,
        &consumed) == THY_INVALID_ARGUMENT && consumed == -1);
    CHECK(thy_core_queued(&core) == 0u);
    config = mpe.config;
    config.enabled = 1u;
    CHECK(thy_mpe_configure(&mpe, &config) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u); /* Setup is never an implicit side effect. */
    CHECK(press(&mpe, 0u, 0u, 60u));
    before = mpe.config;
    config.upper_zone = 1u;
    CHECK(thy_mpe_configure(&mpe, &config) == THY_OWNER_LIMIT);
    CHECK(memcmp(&before, &mpe.config, sizeof(before)) == 0);
    CHECK(thy_mpe_release(&mpe, 0u) == THY_OK);
    CHECK(thy_mpe_quiet(&mpe));
    CHECK(thy_mpe_configure(&mpe, &config) == THY_OK);
    CHECK(thy_mpe_manager(&config) == 15u);
    CHECK(thy_mpe_channel_in_zone(&config, 0u));
    CHECK(!thy_mpe_channel_in_zone(&config, 15u));
    before = mpe.config;
    config.members = 16u;
    CHECK(!thy_mpe_config_valid(&config));
    CHECK(thy_mpe_configure(&mpe, &config) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&before, &mpe.config, sizeof(before)) == 0);
    return 1;
}

static int test_chord_identity_and_expression(void)
{
    thy_core core;
    thy_mpe mpe;
    int16_t notes[3] = {60, 64, 67};
    int consumed = -1;
    uint8_t channel, destination;
    CHECK(enable(&mpe, &core, 15u));
    CHECK(thy_mpe_press(&mpe, 0u, 0u, 60u, notes, 3u, THY_ROUTE_USB0 | THY_ROUTE_DIN,
        90u, 0u, &consumed) == THY_OK && consumed == 0);
    CHECK(thy_mpe_active(&mpe) == 3u);
    for (channel = 1u; channel <= 3u; ++channel) {
        for (destination = 0u; destination < THY_DEST_COUNT; destination += 2u) {
            CHECK(take(&core, destination, (uint8_t)(0xd0u | channel), 0u, 0u));
            CHECK(take(&core, destination, (uint8_t)(0xe0u | channel), 0u, 64u));
            CHECK(take(&core, destination, (uint8_t)(0xb0u | channel), 74u, 64u));
        }
    }
    for (channel = 1u; channel <= 3u; ++channel) {
        CHECK(take(&core, THY_USB0, (uint8_t)(0x90u | channel), (uint8_t)notes[channel - 1u], 90u));
        CHECK(take(&core, THY_DIN, (uint8_t)(0x90u | channel), (uint8_t)notes[channel - 1u], 90u));
    }
    CHECK(thy_mpe_pressure(&mpe, 0u, 80u) == THY_OK);
    CHECK(thy_mpe_bend(&mpe, 0u, 9000) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_core_queued(&core) == 8u);
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_core_queued(&core) == 12u);
    for (channel = 1u; channel <= 3u; ++channel) {
        for (destination = 0u; destination < THY_DEST_COUNT; destination += 2u) {
            CHECK(take(&core, destination, (uint8_t)(0xd0u | channel), 80u, 0u));
            CHECK(take(&core, destination, (uint8_t)(0xe0u | channel), 40u, 70u));
        }
    }
    CHECK(press(&mpe, 1u, 1u, 60u)); /* Same pitch owns a different member. */
    CHECK(thy_mpe_voice_at(&mpe, 3u)->channel == 4u);
    drain(&core);
    CHECK(thy_mpe_release(&mpe, 0u) == THY_OK);
    CHECK(thy_mpe_active(&mpe) == 1u && thy_core_sounding(&core) == 1u);
    for (channel = 1u; channel <= 3u; ++channel) {
        CHECK(take(&core, THY_USB0, (uint8_t)(0x80u | channel), (uint8_t)notes[channel - 1u], 0u));
        CHECK(take(&core, THY_DIN, (uint8_t)(0x80u | channel), (uint8_t)notes[channel - 1u], 0u));
    }
    CHECK(invariant(&mpe));
    return 1;
}

static int test_exhaustion_and_queue_rollback(void)
{
    thy_core core, before;
    thy_mpe mpe, before_mpe;
    int16_t notes[16];
    unsigned i;
    int consumed = 33;
    CHECK(enable(&mpe, &core, 15u));
    for (i = 0u; i < 16u; ++i) notes[i] = (int16_t)(48u + i);
    before = core;
    before_mpe = mpe;
    CHECK(thy_mpe_press(&mpe, 0u, 0u, 48u, notes, 16u, THY_ROUTE_ALL, 90u, 0u,
        &consumed) == THY_OWNER_LIMIT);
    CHECK(memcmp(&before, &core, sizeof(core)) == 0 && consumed == 33);
    CHECK(memcmp(&before_mpe, &mpe, sizeof(mpe)) == 0);
    while (thy_core_cc(&core, THY_ROUTE_DIN, 0u, 1u, 0u) == THY_OK) { }
    before = core;
    CHECK(thy_mpe_press(&mpe, 0u, 0u, 48u, notes, 3u, THY_ROUTE_DIN, 90u, 0u,
        &consumed) == THY_QUEUE_FULL);
    CHECK(core.queue_count == before.queue_count && !thy_core_pad_held(&core, 0u));
    CHECK(thy_mpe_active(&mpe) == 0u && consumed == 33);
    drain(&core);
    /* Enough capacity for expression init but not the reserved NoteOn/Off. */
    for (i = 0u; i < THY_QUEUE_CAPACITY - 10u; ++i)
        CHECK(thy_core_cc(&core, THY_ROUTE_DIN, 0u, 1u, 0u) == THY_OK);
    before = core;
    CHECK(thy_mpe_press(&mpe, 0u, 0u, 48u, notes, 3u, THY_ROUTE_DIN, 90u, 0u,
        &consumed) == THY_QUEUE_FULL);
    CHECK(core.queue_count == before.queue_count && !thy_core_pad_held(&core, 0u));
    CHECK(memcmp(core.owners, before.owners, sizeof(core.owners)) == 0);
    CHECK(memcmp(core.refs, before.refs, sizeof(core.refs)) == 0);
    CHECK(thy_mpe_active(&mpe) == 0u && consumed == 33);
    return 1;
}

static int test_strum_and_ledger_occupancy(void)
{
    thy_core core;
    thy_mpe mpe;
    int16_t notes[3] = {60, 64, 67};
    const int16_t external = 70;
    thy_generated_handle generated;
    CHECK(enable(&mpe, &core, 4u));
    CHECK(thy_core_generated_acquire(&core, 50u, 1u, 70u, 1u, THY_ROUTE_DIN,
        80u, &generated) == THY_OK);
    CHECK(thy_mpe_press(&mpe, 0u, 0u, 60u, notes, 3u, THY_ROUTE_DIN, 90u, 20u,
        NULL) == THY_OK);
    CHECK(thy_mpe_voice_at(&mpe, 1u)->channel == 2u);
    CHECK(thy_mpe_active(&mpe) == 3u && thy_core_pending(&core) == 2u);
    CHECK(core.owners[0].activated == 1u && core.owners[0].count == 3u);
    CHECK(thy_core_advance(&core, 20u) == THY_OK && core.owners[0].activated == 2u);
    CHECK(thy_core_generated_release(&core, generated) == THY_OK);
    CHECK(press(&mpe, 1u, 1u, 71u)); /* Reuses only the actual free external channel. */
    CHECK(thy_mpe_voice_at(&mpe, 0u)->channel == 1u);
    drain(&core);
    CHECK(thy_mpe_release(&mpe, 0u) == THY_OK);
    CHECK(thy_core_pending(&core) == 0u);
    drain(&core);
    CHECK(thy_core_advance(&core, 100u) == THY_OK && thy_core_queued(&core) == 0u);
    CHECK(thy_core_press(&core, 2u, &external, 1u, 0, 2u, THY_ROUTE_DIN, 90u) == THY_OK);
    CHECK(thy_mpe_release(&mpe, 2u) == THY_OK && thy_core_pad_held(&core, 2u));
    CHECK(invariant(&mpe));
    return 1;
}

static int test_sustain_and_retiring_backpressure(void)
{
    thy_core core;
    thy_mpe mpe;
    const int16_t note = 60;
    CHECK(enable(&mpe, &core, 1u));
    CHECK(press(&mpe, 0u, 0u, 60u));
    drain(&core);
    CHECK(thy_core_sustain(&core, THY_ROUTE_DIN, 1u, 1) == THY_OK);
    drain(&core);
    CHECK(thy_mpe_release(&mpe, 0u) == THY_OK);
    CHECK(thy_mpe_active(&mpe) == 1u && mpe.voices[0].state == THY_MPE_RETIRING);
    CHECK(thy_core_sounding(&core) == 1u && thy_core_queued(&core) == 0u);
    CHECK(thy_mpe_press(&mpe, 1u, 4u, 60u, &note, 1u, THY_ROUTE_DIN, 96u, 0u,
        NULL) == THY_OWNER_LIMIT);
    CHECK(thy_core_sustain(&core, THY_ROUTE_DIN, 1u, 0) == THY_OK);
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_mpe_quiet(&mpe));
    CHECK(take(&core, THY_DIN, 0xb1u, 64u, 0u));
    CHECK(take(&core, THY_DIN, 0x81u, 60u, 0u));
    CHECK(take(&core, THY_DIN, 0xd1u, 0u, 0u));
    CHECK(take(&core, THY_DIN, 0xe1u, 0u, 64u));
    CHECK(take(&core, THY_DIN, 0xb1u, 74u, 64u));
    CHECK(press(&mpe, 0u, 0u, 60u));
    drain(&core);
    while (thy_core_cc(&core, THY_ROUTE_DIN, 0u, 1u, 0u) == THY_OK) { }
    CHECK(thy_mpe_release(&mpe, 0u) == THY_OK); /* Reserved off cannot fail. */
    CHECK(thy_core_queued(&core) == THY_QUEUE_CAPACITY);
    CHECK(thy_mpe_active(&mpe) == 1u && !thy_mpe_quiet(&mpe));
    CHECK(thy_mpe_service(&mpe) == THY_QUEUE_FULL);
    drain(&core);
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_mpe_quiet(&mpe));
    CHECK(take(&core, THY_DIN, 0xd1u, 0u, 0u));
    CHECK(take(&core, THY_DIN, 0xe1u, 0u, 64u));
    CHECK(take(&core, THY_DIN, 0xb1u, 74u, 64u));
    CHECK(press(&mpe, 0u, 0u, 60u));
    CHECK(invariant(&mpe));
    return 1;
}

static int test_expression_coalesces_and_retries(void)
{
    thy_core core;
    thy_mpe mpe;
    CHECK(enable(&mpe, &core, 15u));
    CHECK(press(&mpe, 0u, 0u, 60u));
    drain(&core);
    CHECK(thy_mpe_pressure(&mpe, 0u, 20u) == THY_OK);
    CHECK(thy_mpe_pressure(&mpe, 0u, 80u) == THY_OK);
    CHECK(thy_mpe_bend(&mpe, 0u, -20) == THY_OK && mpe.voices[0].bend == 0u);
    CHECK(thy_mpe_bend(&mpe, 0u, 17000) == THY_OK && mpe.voices[0].bend == 16383u);
    while (thy_core_cc(&core, THY_ROUTE_DIN, 0u, 1u, 0u) == THY_OK) { }
    CHECK(thy_mpe_service(&mpe) == THY_QUEUE_FULL);
    CHECK(mpe.voices[0].sent_pressure == 0u && mpe.voices[0].sent_bend == 8192u);
    drain(&core);
    CHECK(thy_mpe_service(&mpe) == THY_OK);
    CHECK(take(&core, THY_DIN, 0xd1u, 80u, 0u));
    CHECK(take(&core, THY_DIN, 0xe1u, 127u, 127u));
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_core_queued(&core) == 0u);
    CHECK(thy_mpe_pressure(&mpe, 0u, 128u) == THY_INVALID_ARGUMENT);
    CHECK(invariant(&mpe));
    return 1;
}

static int test_setup_is_explicit_atomic_and_complete(void)
{
    thy_core core;
    thy_mpe mpe;
    thy_mpe_config config;
    thy_midi_event event;
    unsigned count = 0u, rpn6 = 0u, bend = 0u;
    CHECK(enable(&mpe, &core, 3u));
    config = mpe.config;
    config.upper_zone = 1u;
    CHECK(thy_mpe_configure(&mpe, &config) == THY_OK);
    CHECK(thy_mpe_send_configuration(&mpe, THY_ROUTE_ALL) == THY_OK);
    CHECK(thy_core_queued(&core) == 108u);
    /* Opposite zone disabled before enabling this one. */
    CHECK(take(&core, THY_USB0, 0xb0u, 101u, 0u));
    CHECK(take(&core, THY_USB0, 0xb0u, 100u, 6u));
    CHECK(take(&core, THY_USB0, 0xb0u, 6u, 0u));
    count = 3u;
    while (thy_core_poll(&core, &event)) {
        CHECK((event.status & 0xf0u) == 0xb0u && event.data1 != 64u);
        if (event.data1 == 100u && event.data2 == 6u) ++rpn6;
        if (event.data1 == 100u && event.data2 == 0u) ++bend;
        ++count;
    }
    CHECK(count == 108u && rpn6 == 5u && bend == 12u);
    while (thy_core_cc(&core, THY_ROUTE_DIN, 0u, 1u, 0u) == THY_OK) { }
    count = thy_core_queued(&core);
    CHECK(thy_mpe_send_configuration(&mpe, THY_ROUTE_ALL) == THY_QUEUE_FULL);
    CHECK(thy_core_queued(&core) == count);
    drain(&core);
    CHECK(press(&mpe, 0u, 0u, 60u));
    CHECK(mpe.voices[0].channel == 14u);
    CHECK(thy_mpe_send_configuration(&mpe, THY_ROUTE_DIN) == THY_OWNER_LIMIT);
    CHECK(thy_mpe_release(&mpe, 0u) == THY_OK);
    return 1;
}

static int test_whole_chord_glide_and_consumed_lifetime(void)
{
    thy_core core;
    thy_mpe mpe;
    thy_mpe_config config;
    const int16_t chord[3] = {60, 64, 67};
    const int16_t target = 62;
    int consumed = -1;
    uint8_t i;
    CHECK(enable(&mpe, &core, 15u));
    config = mpe.config;
    config.glide = 1u;
    config.bend_range = 2u;
    CHECK(thy_mpe_configure(&mpe, &config) == THY_OK);
    CHECK(thy_mpe_press(&mpe, 0u, 0u, 60u, chord, 3u, THY_ROUTE_DIN, 90u, 0u,
        &consumed) == THY_OK && consumed == 0);
    CHECK(thy_mpe_pressure(&mpe, 0u, 64u) == THY_OK);
    CHECK(thy_mpe_press(&mpe, 1u, 4u, 62u, &target, 1u, THY_ROUTE_DIN, 90u, 0u,
        &consumed) == THY_OK && consumed == 1);
    CHECK(!thy_core_pad_held(&core, 1u) && thy_mpe_active(&mpe) == 3u);
    CHECK(thy_mpe_consumed(&mpe, 1u));
    CHECK(thy_mpe_pressure(&mpe, 1u, 64u) == THY_OK);
    for (i = 0u; i < 3u; ++i) CHECK(mpe.voices[i].bend == 12287u);
    CHECK(thy_mpe_pressure(&mpe, 0u, 0u) == THY_OK);
    for (i = 0u; i < 3u; ++i) CHECK(mpe.voices[i].bend == 16383u);
    CHECK(thy_mpe_release(&mpe, 1u) == THY_OK);
    for (i = 0u; i < 3u; ++i) CHECK(mpe.voices[i].bend == 8192u);
    CHECK(thy_mpe_press(&mpe, 1u, 4u, 62u, &target, 1u, THY_ROUTE_DIN, 90u, 0u,
        &consumed) == THY_OK && consumed == 1);
    drain(&core);
    CHECK(thy_mpe_release(&mpe, 0u) == THY_OK && thy_mpe_active(&mpe) == 0u);
    CHECK(thy_mpe_consumed(&mpe, 1u) && !thy_mpe_quiet(&mpe));
    drain(&core);
    CHECK(thy_mpe_pressure(&mpe, 1u, 100u) == THY_OK);
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_core_queued(&core) == 0u);
    CHECK(thy_mpe_press(&mpe, 1u, 4u, 62u, &target, 1u, THY_ROUTE_DIN, 90u, 0u,
        &consumed) == THY_OK && consumed == 1);
    CHECK(thy_core_sounding(&core) == 0u); /* No modifier-to-note ghost conversion. */
    CHECK(thy_mpe_release(&mpe, 1u) == THY_OK && thy_mpe_quiet(&mpe));
    CHECK(thy_mpe_glide_bend(0u, 127u, -2, 2u) == 0u);
    CHECK(thy_mpe_glide_bend(0u, 127u, 2, 2u) == 16383u);
    CHECK(thy_mpe_glide_bend(0u, 0u, 2, 2u) == 8192u);
    CHECK(thy_mpe_glide_bend(127u, 0u, 2, 2u) == 8192u);
    CHECK(invariant(&mpe));
    return 1;
}

static int test_glide_windows_and_newest_source(void)
{
    thy_core core;
    thy_mpe mpe;
    thy_mpe_config config;
    const int16_t note = 62;
    int consumed = -1;
    CHECK(enable(&mpe, &core, 15u));
    config = mpe.config;
    config.glide = 1u;
    CHECK(thy_mpe_configure(&mpe, &config) == THY_OK);
    CHECK(press(&mpe, 0u, 0u, 60u));
    /* Physical neighbors can exceed the musical semitone window. */
    CHECK(press(&mpe, 1u, 4u, 64u));
    CHECK(!thy_mpe_consumed(&mpe, 1u));
    CHECK(thy_mpe_release(&mpe, 1u) == THY_OK);
    /* Different physical rows and halves cannot capture each other. */
    CHECK(press(&mpe, 2u, 5u, 62u));
    CHECK(press(&mpe, 3u, 52u, 62u));
    CHECK(thy_mpe_release(&mpe, 2u) == THY_OK);
    CHECK(thy_mpe_release(&mpe, 3u) == THY_OK);
    CHECK(press(&mpe, 4u, 8u, 64u)); /* Source two columns from C, target between. */
    CHECK(thy_mpe_press(&mpe, 1u, 4u, 62u, &note, 1u, THY_ROUTE_DIN, 90u, 0u,
        &consumed) == THY_OK && consumed == 1);
    CHECK(mpe.consumed[1] == 5u); /* Newest source (pad4), not pad0. */
    CHECK(thy_mpe_pressure(&mpe, 1u, 127u) == THY_OK);
    CHECK(mpe.voices[0].modifier == 0u);
    CHECK(mpe.voices[1].pad == 4u && mpe.voices[1].interval == -2);
    CHECK(thy_mpe_release(&mpe, 4u) == THY_OK);
    CHECK(mpe.consumed[1] == 5u); /* Does not re-pair with older C. */
    thy_mpe_release_all(&mpe);
    CHECK(thy_mpe_quiet(&mpe));
    return 1;
}

static int test_cleanup_silent_owners_and_external_panic(void)
{
    thy_core core;
    thy_mpe mpe;
    const int16_t note = 70;
    thy_generated_handle handle;
    CHECK(enable(&mpe, &core, 15u));
    CHECK(thy_mpe_press(&mpe, 0u, 0u, 60u, NULL, 0u, THY_ROUTE_DIN, 90u, 0u,
        NULL) == THY_OK);
    CHECK(!thy_mpe_quiet(&mpe) && thy_mpe_active(&mpe) == 0u);
    CHECK(thy_mpe_release(&mpe, 0u) == THY_OK && thy_mpe_quiet(&mpe));
    CHECK(thy_core_press(&core, 95u, &note, 1u, 0, 0u, THY_ROUTE_DIN, 70u) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 20u, 1u, 71u, 0u, THY_ROUTE_DIN,
        80u, &handle) == THY_OK);
    CHECK(press(&mpe, 0u, 0u, 60u));
    thy_mpe_release_all(&mpe);
    CHECK(thy_core_pad_held(&core, 95u) && thy_core_generated_find(&core, handle) != NULL);
    CHECK(thy_mpe_quiet(&mpe));
    CHECK(press(&mpe, 0u, 0u, 60u));
    thy_core_panic(&core);
    drain(&core);
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_mpe_quiet(&mpe));
    CHECK(press(&mpe, 0u, 0u, 60u));
    CHECK(invariant(&mpe));
    return 1;
}

static int test_timbre_initialization_chord_coalescing_and_defaults(void)
{
    thy_core core, before;
    thy_mpe mpe, before_mpe;
    const int16_t chord[3] = {60,64,67};
    int consumed = -1;
    uint8_t channel, destination;
    CHECK(enable(&mpe,&core,15u));
    before = core; before_mpe = mpe;
    CHECK(thy_mpe_press_ex(&mpe,0u,0u,60u,chord,3u,THY_ROUTE_ALL,96u,20u,
        &consumed,128u) == THY_INVALID_ARGUMENT);
    CHECK(consumed == -1 && memcmp(&core,&before,sizeof core) == 0);
    CHECK(memcmp(&mpe,&before_mpe,sizeof mpe) == 0);
    CHECK(thy_mpe_press_ex(&mpe,0u,0u,60u,chord,3u,THY_ROUTE_ALL,96u,20u,
        &consumed,0u) == THY_OK && !consumed);
    CHECK(thy_mpe_active(&mpe) == 3u && thy_core_pending(&core) == 6u);
    for(channel=1u;channel<=3u;++channel) {
        CHECK(mpe.voices[channel-1u].timbre == 0u && mpe.voices[channel-1u].sent_timbre == 0u);
        for(destination=0u;destination<THY_DEST_COUNT;++destination) {
            CHECK(take(&core,destination,(uint8_t)(0xd0u|channel),0u,0u));
            CHECK(take(&core,destination,(uint8_t)(0xe0u|channel),0u,64u));
            CHECK(take(&core,destination,(uint8_t)(0xb0u|channel),74u,0u));
        }
    }
    for(destination=0u;destination<THY_DEST_COUNT;++destination)
        CHECK(take(&core,destination,0x91u,60u,96u));
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_mpe_pressure(&mpe,0u,40u) == THY_OK);
    CHECK(thy_mpe_timbre(&mpe,0u,20u) == THY_OK);
    CHECK(thy_mpe_timbre(&mpe,0u,100u) == THY_OK);
    CHECK(thy_mpe_bend(&mpe,0u,10000) == THY_OK);
    CHECK(!thy_core_queued(&core));
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_core_queued(&core) == 18u);
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_core_queued(&core) == 27u);
    for(channel=1u;channel<=3u;++channel)
        for(destination=0u;destination<THY_DEST_COUNT;++destination) {
            CHECK(take(&core,destination,(uint8_t)(0xd0u|channel),40u,0u));
            CHECK(take(&core,destination,(uint8_t)(0xe0u|channel),16u,78u));
            CHECK(take(&core,destination,(uint8_t)(0xb0u|channel),74u,100u));
        }
    CHECK(thy_mpe_service(&mpe) == THY_OK && !thy_core_queued(&core));
    CHECK(thy_mpe_timbre(&mpe,0u,128u) == THY_INVALID_ARGUMENT);
    CHECK(thy_mpe_timbre(&mpe,96u,1u) == THY_INVALID_ARGUMENT);
    CHECK(thy_mpe_release(&mpe,0u) == THY_OK && thy_mpe_quiet(&mpe));
    for(destination=0u;destination<THY_DEST_COUNT;++destination)
        CHECK(take(&core,destination,0x81u,60u,0u));
    for(channel=1u;channel<=3u;++channel)
        for(destination=0u;destination<THY_DEST_COUNT;++destination) {
            CHECK(take(&core,destination,(uint8_t)(0xd0u|channel),0u,0u));
            CHECK(take(&core,destination,(uint8_t)(0xe0u|channel),0u,64u));
            CHECK(take(&core,destination,(uint8_t)(0xb0u|channel),74u,64u));
        }
    CHECK(thy_core_advance(&core,100u) == THY_OK && !thy_core_queued(&core));
    CHECK(press(&mpe,1u,4u,62u));
    CHECK(take(&core,THY_DIN,0xd1u,0u,0u));
    CHECK(take(&core,THY_DIN,0xe1u,0u,64u));
    CHECK(take(&core,THY_DIN,0xb1u,74u,64u));
    CHECK(take(&core,THY_DIN,0x91u,62u,96u));
    CHECK(invariant(&mpe));
    return 1;
}

static int test_timbre_full_batch_retry_and_reset_before_reuse(void)
{
    thy_core core;
    thy_mpe mpe;
    const int16_t note=60;
    thy_midi_event event,events[THY_QUEUE_CAPACITY];
    unsigned i,count=0;
    CHECK(enable(&mpe,&core,1u));
    CHECK(thy_mpe_press_ex(&mpe,0u,0u,60u,&note,1u,THY_ROUTE_ALL,96u,0u,NULL,0u) == THY_OK);
    drain(&core);
    CHECK(thy_mpe_pressure(&mpe,0u,99u) == THY_OK);
    CHECK(thy_mpe_timbre(&mpe,0u,127u) == THY_OK);
    CHECK(thy_mpe_bend(&mpe,0u,0) == THY_OK);
    while(thy_core_cc(&core,THY_ROUTE_DIN,0u,1u,0u) == THY_OK) { }
    CHECK(thy_mpe_service(&mpe) == THY_QUEUE_FULL);
    for(i=0;i<7u;++i) CHECK(thy_core_poll(&core,&event));
    count=thy_core_queued(&core);
    CHECK(thy_mpe_service(&mpe) == THY_QUEUE_FULL && thy_core_queued(&core) == count);
    CHECK(mpe.voices[0].sent_pressure == 0u && mpe.voices[0].sent_bend == 8192u && mpe.voices[0].sent_timbre == 0u);
    for(i=0;i<2u;++i) CHECK(thy_core_poll(&core,&event));
    CHECK(thy_mpe_service(&mpe) == THY_OK);
    CHECK(mpe.voices[0].sent_pressure == 99u && mpe.voices[0].sent_bend == 0u && mpe.voices[0].sent_timbre == 127u);
    CHECK(thy_mpe_release(&mpe,0u) == THY_OK && thy_core_queued(&core) == THY_QUEUE_CAPACITY);
    CHECK(mpe.voices[0].state == THY_MPE_RETIRING);
    for(i=0;i<8u;++i) CHECK(thy_core_poll(&core,&event));
    count=thy_core_queued(&core);
    CHECK(thy_mpe_service(&mpe) == THY_QUEUE_FULL && thy_core_queued(&core) == count);
    CHECK(mpe.voices[0].state == THY_MPE_RETIRING);
    CHECK(thy_core_poll(&core,&event));
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_mpe_quiet(&mpe));
    count=0;
    while(thy_core_poll(&core,&event))events[count++]=event;
    CHECK(count == THY_QUEUE_CAPACITY);
    for(i=0;i<3u;++i) {
        const thy_midi_event *off=&events[count-12u+i];
        CHECK(off->destination == i && off->status == 0x81u && off->data1 == 60u && off->data2 == 0u);
        const thy_midi_event *reset=&events[count-9u+3u*i];
        CHECK(reset[0].destination == i && reset[0].status == 0xd1u && reset[0].data1 == 0u && reset[0].data2 == 0u);
        CHECK(reset[1].destination == i && reset[1].status == 0xe1u && reset[1].data1 == 0u && reset[1].data2 == 64u);
        CHECK(reset[2].destination == i && reset[2].status == 0xb1u && reset[2].data1 == 74u && reset[2].data2 == 64u);
    }
    CHECK(thy_mpe_press_ex(&mpe,1u,4u,60u,&note,1u,THY_ROUTE_DIN,96u,0u,NULL,0u) == THY_OK);
    CHECK(take(&core,THY_DIN,0xd1u,0u,0u));
    CHECK(take(&core,THY_DIN,0xe1u,0u,64u));
    CHECK(take(&core,THY_DIN,0xb1u,74u,0u));
    CHECK(take(&core,THY_DIN,0x91u,60u,96u));
    CHECK(thy_mpe_service(&mpe) == THY_OK && !thy_core_queued(&core));
    CHECK(invariant(&mpe));
    return 1;
}

static int test_timbre_modifier_isolation_and_sustained_cleanup(void)
{
    thy_core core;
    thy_mpe mpe;
    thy_mpe_config config;
    const int16_t note=60,target=62;
    int consumed=-1;
    CHECK(enable(&mpe,&core,2u));
    config=mpe.config;config.glide=1u;
    CHECK(thy_mpe_configure(&mpe,&config) == THY_OK);
    CHECK(thy_mpe_press_ex(&mpe,0u,0u,60u,&note,1u,THY_ROUTE_DIN,96u,0u,NULL,0u) == THY_OK);
    drain(&core);
    CHECK(thy_mpe_pressure(&mpe,0u,64u) == THY_OK);
    CHECK(thy_mpe_timbre(&mpe,0u,100u) == THY_OK);
    CHECK(thy_mpe_service(&mpe) == THY_OK);
    CHECK(take(&core,THY_DIN,0xd1u,64u,0u));
    CHECK(take(&core,THY_DIN,0xb1u,74u,100u));
    CHECK(thy_mpe_press_ex(&mpe,4u,4u,62u,&target,1u,THY_ROUTE_DIN,96u,0u,&consumed,0u) == THY_OK && consumed);
    CHECK(thy_mpe_timbre(&mpe,4u,127u) == THY_OK);
    CHECK(mpe.voices[0].timbre == 100u && !thy_core_queued(&core));
    CHECK(thy_mpe_pressure(&mpe,4u,64u) == THY_OK);
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_core_queued(&core) == 1u);
    drain(&core);
    CHECK(thy_core_sustain(&core,THY_ROUTE_DIN,1u,1) == THY_OK);drain(&core);
    CHECK(thy_mpe_release(&mpe,0u) == THY_OK);
    CHECK(thy_mpe_timbre(&mpe,0u,0u) == THY_OK);
    CHECK(thy_mpe_service(&mpe) == THY_OK && !thy_core_queued(&core));
    CHECK(mpe.voices[0].state == THY_MPE_RETIRING && mpe.voices[0].timbre == 100u);
    CHECK(thy_mpe_release(&mpe,4u) == THY_OK);
    CHECK(thy_core_sustain(&core,THY_ROUTE_DIN,1u,0) == THY_OK);
    CHECK(thy_mpe_service(&mpe) == THY_OK && thy_mpe_quiet(&mpe));
    CHECK(take(&core,THY_DIN,0xb1u,64u,0u));
    CHECK(take(&core,THY_DIN,0x81u,60u,0u));
    CHECK(take(&core,THY_DIN,0xd1u,0u,0u));
    CHECK(take(&core,THY_DIN,0xe1u,0u,64u));
    CHECK(take(&core,THY_DIN,0xb1u,74u,64u));
    CHECK(!thy_core_queued(&core));
    CHECK(invariant(&mpe));
    return 1;
}

static int test_deterministic_stress(void)
{
    thy_core core;
    thy_mpe mpe;
    uint32_t random = 0x12678abcu;
    unsigned step;
    CHECK(enable(&mpe, &core, 15u));
    for (step = 0u; step < 5000u; ++step) {
        uint8_t pad;
        random = random * 1664525u + 1013904223u;
        pad = (uint8_t)((random >> 8u) % 16u);
        switch (random % 6u) {
        case 0: {
            const int16_t note = (int16_t)(48u + pad);
            int result = thy_mpe_press(&mpe, pad, pad, (uint8_t)note, &note, 1u,
                THY_ROUTE_ALL, 96u, 0u, NULL);
            CHECK(result == THY_OK || result == THY_OWNER_LIMIT || result == THY_QUEUE_FULL);
            break;
        }
        case 1: CHECK(thy_mpe_release(&mpe, pad) == THY_OK); break;
        case 2: CHECK(thy_mpe_pressure(&mpe, pad, (uint8_t)(random % 128u)) == THY_OK); break;
        case 3: CHECK(thy_mpe_bend(&mpe, pad, (int32_t)(random % 18000u) - 100) == THY_OK); break;
        case 4: CHECK(thy_mpe_timbre(&mpe, pad, (uint8_t)(random % 128u)) == THY_OK); break;
        default: {
            int result = thy_mpe_service(&mpe);
            CHECK(result == THY_OK || result == THY_QUEUE_FULL);
            break;
        }
        }
        if (step % 7u == 0u) drain(&core);
        CHECK(invariant(&mpe));
    }
    drain(&core);
    thy_mpe_release_all(&mpe);
    CHECK(thy_mpe_quiet(&mpe) && thy_core_sounding(&core) == 0u);
    CHECK(invariant(&mpe));
    return 1;
}

int main(void)
{
    int (*tests[])(void) = {test_defaults_and_atomic_configuration,
        test_chord_identity_and_expression, test_exhaustion_and_queue_rollback,
        test_strum_and_ledger_occupancy, test_sustain_and_retiring_backpressure,
        test_expression_coalesces_and_retries, test_setup_is_explicit_atomic_and_complete,
        test_whole_chord_glide_and_consumed_lifetime, test_glide_windows_and_newest_source,
        test_cleanup_silent_owners_and_external_panic,
        test_timbre_initialization_chord_coalescing_and_defaults,
        test_timbre_full_batch_retry_and_reset_before_reuse,
        test_timbre_modifier_isolation_and_sustained_cleanup,
        test_deterministic_stress};
    size_t i;
    for (i = 0u; i < sizeof(tests) / sizeof(tests[0]); ++i) if (!tests[i]()) return 1;
    printf("%zu MPE scenarios passed (%u assertions; workspace %zu bytes)\n",
        sizeof(tests) / sizeof(tests[0]), assertions, sizeof(thy_mpe));
    return 0;
}
