/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_arp.h"

static thy_core core, saved_core;
static thy_arp arp, second, saved_arp;
static thy_arp_config config;
static thy_midi_event events[THY_QUEUE_CAPACITY];
static thy_generated_handle manual[THY_GENERATED_OWNER_COUNT];
static unsigned assertions;
#define CHECK(x) do { ++assertions; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 0; } } while (0)

static unsigned drain(void)
{
    unsigned count = 0u;
    while (thy_core_poll(&core, &events[count])) ++count;
    return count;
}
static int initialize(void)
{
    thy_core_init(&core);
    thy_arp_init(&arp, 1u);
    thy_arp_default_config(&config);
    config.enabled = 1u;
    CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
    return 1;
}
static int press(unsigned id, unsigned role, const int16_t *notes, size_t count, uint64_t tick)
{
    CHECK(thy_arp_press(&arp, &core, (uint8_t)id, (uint8_t)role, notes, count,
        96u, tick, 1u, 1) == THY_OK);
    return 1;
}
static int advance(uint64_t tick)
{
    CHECK(thy_arp_advance(&arp, &core, tick, 1u, 1) == THY_OK);
    return 1;
}
static int release(unsigned id, uint64_t tick)
{
    CHECK(thy_arp_release(&arp, &core, (uint8_t)id, tick, 1u, 1) == THY_OK);
    return 1;
}
static int phrase_has(unsigned phrase, unsigned pitch)
{
    return !!(arp.phrases[phrase].pitches[pitch / 64u] & (UINT64_C(1) << (pitch % 64u)));
}

static int notes_and_gates(void)
{
    CHECK(initialize());
    const int16_t chord[] = {60, 64, 67};
    CHECK(press(0u, 1u, chord, 3u, 0u));
    CHECK(arp.phrase_count == 1u && arp.held_count == 1u && arp.output_count == 1u);
    CHECK(drain() == 2u && events[0].status == 0x92u && events[0].data1 == 60u);
    CHECK(advance(119u) && drain() == 0u);
    CHECK(advance(120u) && drain() == 2u && events[0].status == 0x82u);
    CHECK(advance(240u) && drain() == 2u && events[0].data1 == 64u);
    CHECK(advance(480u) && drain() == 4u && events[2].data1 == 67u);
    CHECK(release(0u, 490u) && arp.output_count == 0u && arp.phrase_count == 0u);
    CHECK(drain() == 2u);
    CHECK(advance(1000u) && drain() == 0u);
    return 1;
}

static int hybrid_duplicate_contributions_and_fallback(void)
{
    CHECK(initialize());
    const int16_t c[] = {60, 64, 67}, f[] = {65, 69, 72}, d[] = {62}, e[] = {64};
    CHECK(press(0u, 1u, c, 3u, 0u));
    drain();
    CHECK(press(48u, 0u, d, 1u, 10u));
    CHECK(arp.phrases[0].anchor == 0u && arp.phrases[0].ordinal == 1u);
    CHECK(drain() == 0u && phrase_has(0u, 62u));
    CHECK(press(49u, 0u, d, 1u, 20u));
    CHECK(press(50u, 0u, e, 1u, 30u));
    CHECK(release(48u, 40u) && phrase_has(0u, 62u));
    CHECK(release(50u, 45u) && phrase_has(0u, 64u));
    CHECK(press(1u, 1u, f, 3u, 50u));
    CHECK(arp.phrase_count == 2u && phrase_has(1u, 62u) && !phrase_has(0u, 62u));
    CHECK(arp.phrases[0].anchor == 0u && arp.phrases[0].ordinal == 1u);
    CHECK(release(1u, 60u));
    CHECK(arp.phrase_count == 1u && phrase_has(0u, 62u));
    CHECK(arp.phrases[0].anchor == 0u && arp.phrases[0].ordinal == 1u);
    drain();
    CHECK(advance(240u) && drain() == 4u && events[2].data1 == 62u);
    CHECK(release(0u, 250u));
    CHECK(arp.phrase_count == 1u && arp.phrases[THY_ARP_POOL].active);
    CHECK(phrase_has(THY_ARP_POOL, 62u));
    CHECK(arp.phrases[THY_ARP_POOL].anchor == 0u && arp.phrases[THY_ARP_POOL].ordinal == 2u);
    CHECK(arp.output_count == 0u && drain() == 2u);
    CHECK(release(49u, 260u) && arp.phrase_count == 0u && arp.output_count == 0u);
    return 1;
}

static int final_chord_release_keeps_single_pool_deadline(void)
{
    CHECK(initialize());
    const int16_t chord[] = {60,64,67}, single[] = {62};
    CHECK(press(0u, 1u, chord, 3u, 0u)); drain();
    CHECK(press(48u, 0u, single, 1u, 50u));
    CHECK(release(0u, 100u));
    CHECK(drain() == 2u && arp.output_count == 0u);
    CHECK(arp.phrases[96].anchor == 0u && arp.phrases[96].ordinal == 1u);
    CHECK(advance(239u) && drain() == 0u);
    CHECK(advance(240u) && drain() == 2u && events[0].data1 == 62u);
    return 1;
}

static int combined_and_each_grouping(void)
{
    CHECK(initialize());
    config.grouping = THY_ARP_COMBINED;
    CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
    const int16_t c[] = {60, 64, 67}, f[] = {65, 69, 72};
    CHECK(press(0u, 1u, c, 3u, 0u)); drain();
    CHECK(press(1u, 1u, f, 3u, 20u));
    CHECK(arp.phrase_count == 1u && arp.phrases[96].ordinal == 1u && drain() == 0u);
    CHECK(phrase_has(96u, 60u) && phrase_has(96u, 72u));
    thy_arp_clear(&arp, &core); drain();
    config.grouping = THY_ARP_EACH;
    CHECK(thy_arp_configure(&arp, &core, &config, 20u, 1u, 1) == THY_OK);
    CHECK(press(48u, 0u, c, 1u, 30u));
    CHECK(press(49u, 0u, f, 1u, 40u));
    CHECK(arp.phrase_count == 2u && arp.phrases[48].active && arp.phrases[49].active);
    CHECK(arp.output_count == 2u);
    return 1;
}

static int hold_retains_phase_and_optional_restart(void)
{
    CHECK(initialize());
    const int16_t c[] = {60, 64, 67}, d[] = {62};
    CHECK(press(0u, 1u, c, 3u, 0u)); drain();
    CHECK(press(48u, 0u, d, 1u, 100u));
    CHECK(arp.phrases[0].anchor == 0u && arp.phrases[0].ordinal == 1u);
    CHECK(advance(240u)); drain();
    CHECK(release(48u, 250u));
    CHECK(arp.phrases[0].anchor == 0u && arp.phrases[0].ordinal == 2u);
    config.restart_on_change = 1u;
    CHECK(thy_arp_configure(&arp, &core, &config, 250u, 1u, 1) == THY_OK);
    CHECK(press(48u, 0u, d, 1u, 300u));
    CHECK(arp.phrases[0].anchor == 300u && arp.phrases[0].ordinal == 1u);
    CHECK(arp.output_count == 1u);
    return 1;
}

static int patterns_and_as_played(void)
{
    static const uint8_t expected[4][5] = {
        {60,64,67,60,64}, {67,64,60,67,64}, {60,64,67,64,60}, {67,64,60,64,67}
    };
    const int16_t chord[] = {60, 64, 67};
    for (unsigned pattern = 0u; pattern < 4u; ++pattern) {
        CHECK(initialize());
        config.pattern = (uint8_t)pattern;
        CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
        CHECK(press(0u, 1u, chord, 3u, 0u));
        CHECK(drain() == 2u && events[0].data1 == expected[pattern][0]);
        for (unsigned step = 1u; step < 5u; ++step) {
            CHECK(advance(step * 240u));
            CHECK(drain() == 4u && events[2].data1 == expected[pattern][step]);
        }
    }
    CHECK(initialize()); config.pattern = THY_ARP_AS_PLAYED;
    CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
    const int16_t g[] = {67}, c[] = {60}, e[] = {64};
    CHECK(press(48u, 0u, g, 1u, 0u)); drain();
    CHECK(press(49u, 0u, c, 1u, 10u));
    CHECK(press(50u, 0u, e, 1u, 20u));
    CHECK(advance(240u) && drain() == 4u && events[2].data1 == 60u);
    CHECK(advance(480u) && drain() == 4u && events[2].data1 == 64u);
    return 1;
}

static int grid_swing_and_rests(void)
{
    CHECK(initialize()); config.start = THY_ARP_GRID; config.swing_percent = 50u;
    config.gate_percent = 100u; config.step_mask = 5u;
    CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
    const int16_t chord[] = {60, 64, 67};
    CHECK(press(0u, 1u, chord, 3u, 10u));
    CHECK(arp.phrases[0].anchor == 240u && drain() == 0u);
    CHECK(advance(239u) && drain() == 0u);
    CHECK(advance(240u) && drain() == 2u && events[0].data1 == 60u);
    CHECK(advance(599u) && drain() == 0u);
    CHECK(advance(600u) && drain() == 2u && events[0].status == 0x82u);
    CHECK(advance(720u) && drain() == 2u && events[0].data1 == 67u);
    CHECK(arp.phrases[0].ordinal == 3u && arp.skipped == 1u);
    return 1;
}

static int full_gate_releases_before_retrigger(void)
{
    CHECK(initialize()); config.gate_percent = 100u;
    CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
    const int16_t c[] = {60};
    CHECK(press(0u, 1u, c, 1u, 0u)); drain();
    CHECK(advance(240u));
    CHECK(drain() == 4u);
    CHECK(events[0].status == 0x82u && events[2].status == 0x92u);
    CHECK(events[0].data1 == 60u && events[2].data1 == 60u);
    CHECK(arp.output_count == 1u && core.refs[thy_core_key(0u, 2u, 60u)] == 1u);
    return 1;
}

static int late_service_skips_without_catchup(void)
{
    CHECK(initialize()); const int16_t chord[] = {60,64,67};
    CHECK(press(0u, 1u, chord, 3u, 0u)); drain();
    CHECK(advance(10000u) && drain() == 2u && arp.output_count == 0u);
    CHECK(arp.phrases[0].ordinal == 42u && arp.attacks == 1u && arp.skipped == 41u);
    CHECK(advance(10080u) && drain() == 2u && events[0].data1 == 60u);
    CHECK(arp.attacks == 2u);
    CHECK(advance(UINT64_C(1000000000000)));
    CHECK(drain() <= 4u && arp.output_count <= 1u);
    CHECK(arp.phrases[0].ordinal == UINT64_C(1000000000000) / 240u + 1u);
    return 1;
}

static int chord_offsets_preserve_voicing_and_bounds(void)
{
    CHECK(initialize()); config.type = THY_ARP_CHORD;
    config.offsets[1] = -1;
    CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
    const int16_t chord[] = {0,61,127};
    CHECK(press(0u, 1u, chord, 3u, 0u));
    CHECK(arp.output_count == 3u && drain() == 6u);
    CHECK(events[0].data1 == 0u && events[2].data1 == 61u && events[4].data1 == 127u);
    CHECK(advance(240u));
    CHECK(arp.output_count == 2u && drain() == 10u);
    CHECK(events[6].data1 == 60u && events[8].data1 == 125u);
    return 1;
}

static int independent_arps_and_manual_same_source_owner(void)
{
    CHECK(initialize());
    thy_arp_init(&second, 2u);
    CHECK(thy_arp_configure(&second, &core, &config, 0u, 1u, 1) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 1u, 1u, 30u, 2u, 5u, 96u, &manual[0]) == THY_OK);
    const int16_t c[] = {60};
    CHECK(press(0u, 1u, c, 1u, 0u));
    CHECK(thy_arp_press(&second, &core, 0u, 1u, c, 1u, 96u, 0u, 1u, 1) == THY_OK);
    CHECK(core.generated_count == 3u && drain() == 4u);
    thy_arp_clear(&arp, &core);
    CHECK(core.generated_count == 2u && drain() == 0u);
    CHECK(thy_core_generated_find(&core, manual[0]) != 0);
    CHECK(thy_arp_release(&second, &core, 0u, 10u, 1u, 1) == THY_OK);
    CHECK(core.generated_count == 1u && drain() == 2u);
    CHECK(thy_core_generated_find(&core, manual[0]) != 0);
    return 1;
}

static int disable_clear_loss_and_generation(void)
{
    CHECK(initialize()); const int16_t c[] = {60};
    CHECK(press(0u, 1u, c, 1u, 0u)); drain();
    CHECK(thy_arp_advance(&arp, &core, 10u, 2u, 1) == THY_OK);
    CHECK(arp.held_count == 0u && arp.phrase_count == 0u && arp.output_count == 0u && drain() == 2u);
    CHECK(thy_arp_advance(&arp, &core, 240u, 2u, 1) == THY_OK && drain() == 0u);
    saved_arp = arp;
    CHECK(thy_arp_press(&arp, &core, 0u, 1u, c, 1u, 96u, 240u, 1u, 1) == THY_ARP_STALE_RUN);
    CHECK(memcmp(&arp, &saved_arp, sizeof arp) == 0);
    CHECK(thy_arp_press(&arp, &core, 0u, 1u, c, 1u, 96u, 240u, 2u, 1) == THY_OK);
    drain();
    CHECK(thy_arp_advance(&arp, &core, 250u, 2u, 0) == THY_OK);
    CHECK(arp.held_count == 0u && arp.output_count == 0u && drain() == 2u);
    CHECK(thy_arp_advance(&arp, &core, 480u, 2u, 1) == THY_OK && drain() == 0u);
    CHECK(thy_arp_release(&arp, &core, 0u, 480u, 2u, 1) == THY_OK);
    CHECK(thy_arp_press(&arp, &core, 0u, 1u, c, 1u, 96u, 480u, 2u, 1) == THY_OK);
    config.enabled = 0u;
    CHECK(thy_arp_configure(&arp, &core, &config, 490u, 2u, 1) == THY_OK);
    CHECK(arp.held_count == 0u && arp.output_count == 0u);
    config.enabled = 1u;
    CHECK(thy_arp_configure(&arp, &core, &config, 500u, 2u, 1) == THY_OK);
    drain();
    CHECK(thy_arp_advance(&arp, &core, 1000u, 2u, 1) == THY_OK && drain() == 0u);
    return 1;
}

static int pool_exhaustion_is_atomic_and_not_retried(void)
{
    CHECK(initialize()); config.type = THY_ARP_CHORD;
    CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
    for (unsigned n = 0u; n < 127u; ++n)
        CHECK(thy_core_generated_acquire(&core, 64u, 1u, 30u, 0u, 1u, 96u, &manual[n]) == THY_OK);
    drain();
    const int16_t chord[] = {60,64,67};
    uint32_t handle = core.generated_last_handle;
    CHECK(press(0u, 1u, chord, 3u, 0u));
    CHECK(arp.failures == 1u && arp.last_error == THY_OWNER_LIMIT);
    CHECK(core.generated_count == 127u && core.generated_last_handle == handle && drain() == 0u);
    CHECK(arp.output_count == 0u && arp.phrases[0].ordinal == 1u);
    for (unsigned n = 0u; n < 127u; ++n) CHECK(thy_core_generated_release(&core, manual[n]) == THY_OK);
    drain();
    CHECK(advance(0u) && drain() == 0u && arp.output_count == 0u);
    CHECK(advance(240u) && drain() == 6u && arp.output_count == 3u);
    return 1;
}

static int queue_exhaustion_is_atomic(void)
{
    CHECK(initialize()); config.type = THY_ARP_CHORD;
    CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
    for (unsigned n = 0u; n < 1013u; ++n)
        CHECK(thy_core_cc(&core, 1u, 0u, 1u, 1u) == THY_OK);
    const int16_t chord[] = {60,64,67};
    CHECK(press(0u, 1u, chord, 3u, 0u));
    CHECK(arp.failures == 1u && arp.last_error == THY_QUEUE_FULL);
    CHECK(core.generated_count == 0u && core.generated_last_handle == 0u && core.queue_count == 1013u);
    drain();
    CHECK(advance(240u) && drain() == 6u && arp.output_count == 3u);
    return 1;
}

static int handle_exhaustion_and_max_order(void)
{
    CHECK(initialize()); config.type = THY_ARP_CHORD;
    CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
    core.generated_last_handle = UINT32_MAX - 2u;
    const int16_t chord[] = {60,64,67};
    CHECK(press(0u, 1u, chord, 3u, 0u));
    CHECK(arp.failures == 1u && arp.last_error == THY_HANDLE_EXHAUSTED);
    CHECK(core.generated_count == 0u && core.generated_last_handle == UINT32_MAX - 2u && drain() == 0u);
    CHECK(initialize()); config.pattern = THY_ARP_AS_PLAYED;
    CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
    arp.press_order = UINT64_MAX - 1u;
    CHECK(press(0u, 1u, chord, 3u, 0u));
    CHECK(drain() == 2u && events[0].data1 == 60u);
    CHECK(thy_arp_press(&arp, &core, 1u, 1u, chord, 3u, 96u, 10u, 1u, 1) == THY_OWNER_LIMIT);
    CHECK(arp.held_count == 1u);
    return 1;
}

static int validation_and_backward_are_atomic(void)
{
    CHECK(initialize()); saved_arp = arp; saved_core = core;
    config.scale[2] = 2u;
    CHECK(thy_arp_configure(&arp, &core, &config, 10u, 1u, 1) == THY_INVALID_ARGUMENT);
    CHECK(memcmp(&arp, &saved_arp, sizeof arp) == 0 && memcmp(&core, &saved_core, sizeof core) == 0);
    const int16_t invalid[] = {128}, valid[] = {0};
    CHECK(thy_arp_press(&arp, &core, 0u, 1u, invalid, 1u, 96u, 0u, 1u, 1) == THY_INVALID_ARGUMENT);
    CHECK(thy_arp_press(&arp, &core, 96u, 1u, valid, 1u, 96u, 0u, 1u, 1) == THY_INVALID_ARGUMENT);
    CHECK(advance(10u)); saved_arp = arp; saved_core = core;
    CHECK(thy_arp_advance(&arp, &core, 9u, 1u, 1) == THY_ARP_TIME_BACKWARD);
    CHECK(memcmp(&arp, &saved_arp, sizeof arp) == 0 && memcmp(&core, &saved_core, sizeof core) == 0);
    return 1;
}

static int full_union_sustain_and_external_cleanup(void)
{
    CHECK(initialize()); config.grouping = THY_ARP_COMBINED; config.type = THY_ARP_CHORD;
    CHECK(thy_arp_configure(&arp, &core, &config, 0u, 1u, 1) == THY_OK);
    int16_t notes[64];
    for (unsigned n = 0u; n < 64u; ++n) notes[n] = (int16_t)n;
    CHECK(press(0u, 1u, notes, 64u, 0u)); drain();
    for (unsigned n = 0u; n < 64u; ++n) notes[n] = (int16_t)(n + 64u);
    CHECK(press(1u, 1u, notes, 64u, 10u));
    CHECK(advance(240u));
    CHECK(arp.output_count == 128u && core.generated_count == 128u);
    drain();
    CHECK(thy_core_sustain(&core, 5u, 2u, 1) == THY_OK); drain();
    thy_arp_clear(&arp, &core);
    CHECK(core.generated_count == 0u && core.sounding_count == 256u && drain() == 0u);
    CHECK(thy_core_sustain(&core, 5u, 2u, 0) == THY_OK && drain() == 258u);
    CHECK(initialize()); const int16_t c[] = {60};
    CHECK(press(0u, 1u, c, 1u, 0u)); drain();
    thy_core_panic(&core); drain();
    CHECK(advance(100u) && arp.output_count == 0u && drain() == 0u);
    return 1;
}

int main(void)
{
    static int (*const tests[])(void) = {
        notes_and_gates, hybrid_duplicate_contributions_and_fallback,
        final_chord_release_keeps_single_pool_deadline,
        combined_and_each_grouping, hold_retains_phase_and_optional_restart,
        patterns_and_as_played, grid_swing_and_rests,
        full_gate_releases_before_retrigger, late_service_skips_without_catchup,
        chord_offsets_preserve_voicing_and_bounds, independent_arps_and_manual_same_source_owner,
        disable_clear_loss_and_generation, pool_exhaustion_is_atomic_and_not_retried,
        queue_exhaustion_is_atomic, handle_exhaustion_and_max_order,
        validation_and_backward_are_atomic, full_union_sustain_and_external_cleanup
    };
    unsigned count = sizeof tests / sizeof tests[0];
    for (unsigned n = 0u; n < count; ++n) if (!tests[n]()) return 1;
    printf("Arp C tests passed: %u scenarios, %u assertions, %u bytes/instance\n",
        count, assertions, (unsigned)sizeof(thy_arp));
    return 0;
}
