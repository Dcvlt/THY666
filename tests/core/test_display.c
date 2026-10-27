/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_display.h"

static unsigned assertions;
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 0; \
} } while (0)

static thy_display display;
static uint8_t frame[THY_DISPLAY_FRAME_BYTES], panel[THY_DISPLAY_FRAME_BYTES];

static void initialize(void)
{
    thy_display_init(&display);
    memset(frame, 0, sizeof frame);
    memset(panel, 0xa5, sizeof panel); /* Unknown panel contents after boot. */
}

static int commit_to_panel(uint32_t now)
{
    const thy_display_transfer *transfer = thy_display_prepare(&display, now);
    CHECK(transfer != NULL);
    CHECK(transfer->count > 0u && transfer->count <= 16u);
    CHECK(transfer->offset < sizeof panel);
    CHECK(transfer->offset + transfer->count <= sizeof panel);
    CHECK(transfer->offset / 128u ==
        (transfer->offset + transfer->count - 1u) / 128u);
    CHECK(transfer->command[0] == (0xb0u | transfer->offset / 128u));
    CHECK(transfer->command[1] == (transfer->offset & 0x0fu));
    CHECK(transfer->command[2] == (0x10u | ((transfer->offset % 128u) >> 4u)));
    memcpy(panel + transfer->offset, transfer->data, transfer->count);
    thy_display_commit(&display, 1, now);
    CHECK(memcmp(panel, display.shadow, sizeof panel) == 0 ||
        !display.report.initial_complete);
    return 1;
}

static int drain(uint32_t now)
{
    unsigned guard = 0;
    while (display.report.active && guard++ < 1024u)
        CHECK(commit_to_panel(now++));
    CHECK(guard < 1024u);
    CHECK(display.report.dirty_bytes == 0u && !display.prepared);
    CHECK(memcmp(panel, frame, sizeof panel) == 0);
    return 1;
}

static int clean_panel(void)
{
    initialize();
    CHECK(thy_display_submit(&display, frame, 0u));
    CHECK(drain(0u));
    return 1;
}

static int test_initial_zero_frame_must_reach_every_panel_byte(void)
{
    initialize();
    CHECK(thy_display_due(&display, 0u));
    CHECK(thy_display_submit(&display, frame, 0u));
    CHECK(display.report.dirty_bytes == 1024u);
    CHECK(!display.report.initial_complete && display.report.valid_bytes == 0u);
    CHECK(display.report.frames == 0u && display.report.submitted == 1u);
    unsigned seen_pages = 0u;
    for (unsigned chunk = 0; chunk < 64u; ++chunk) {
        const thy_display_transfer *transfer = thy_display_prepare(&display, chunk);
        CHECK(transfer != NULL && transfer->count == 16u);
        seen_pages |= 1u << (transfer->offset / 128u);
        if (chunk == 3u) CHECK(transfer->offset / 128u == 0u);
        CHECK(commit_to_panel(chunk));
        CHECK(display.report.frames == (chunk == 63u ? 1u : 0u));
    }
    CHECK(seen_pages == 0xffu);
    CHECK(display.report.initial_complete && display.report.valid_bytes == 1024u);
    CHECK(display.report.data_bytes == 1024u && display.report.command_bytes == 192u);
    CHECK(!display.report.active && thy_display_prepare(&display, 64u) == NULL);
    CHECK(memcmp(panel, frame, sizeof frame) == 0);
    return 1;
}

static int test_sparse_bytes_use_explicit_nonzero_column_addresses(void)
{
    CHECK(clean_panel());
    frame[6u * 128u + 47u] = 0x77u;
    CHECK(thy_display_submit(&display, frame, 100u));
    const thy_display_transfer *transfer = thy_display_prepare(&display, 100u);
    CHECK(transfer != NULL && transfer->count == 1u && transfer->offset == 815u);
    CHECK(transfer->command[0] == 0xb6u && transfer->command[1] == 0x0fu &&
        transfer->command[2] == 0x12u);
    CHECK(display.shadow[815] == 0u && display.report.frames == 1u);
    CHECK(commit_to_panel(101u));
    CHECK(display.report.data_bytes == 1025u && display.report.frames == 2u);
    CHECK(!thy_display_submit(&display, frame, 116u));
    CHECK(display.report.unchanged == 1u && display.report.submitted == 3u);
    CHECK(!display.report.active && display.report.frames == 2u);
    CHECK(thy_display_prepare(&display, 116u) == NULL);
    return 1;
}

static int test_latest_target_never_overwrites_a_prepared_payload(void)
{
    CHECK(clean_panel());
    frame[300] = 1u;
    CHECK(thy_display_submit(&display, frame, 100u));
    const thy_display_transfer *transfer = thy_display_prepare(&display, 101u);
    CHECK(transfer != NULL && transfer->offset == 300u && transfer->data[0] == 1u);
    thy_display_transfer frozen = *transfer;
    frame[300] = 2u;
    frame[301] = 3u;
    CHECK(thy_display_submit(&display, frame, 116u));
    CHECK(memcmp(transfer, &frozen, sizeof frozen) == 0);
    CHECK(thy_display_prepare(&display, 117u) == transfer);
    CHECK(display.report.coalesced == 1u && display.report.generation == 3u);
    CHECK(commit_to_panel(120u));
    CHECK(display.shadow[300] == 1u && display.report.dirty_bytes == 2u);
    CHECK(display.report.last_generation_age_ms == 20u);
    CHECK(drain(121u));
    CHECK(display.shadow[300] == 2u && display.shadow[301] == 3u);
    CHECK(display.report.frames == 2u); /* Coalescing did not complete frame 2. */
    return 1;
}

static int test_target_can_cancel_dirty_then_old_commit_redirties_it(void)
{
    CHECK(clean_panel());
    frame[300] = 9u;
    CHECK(thy_display_submit(&display, frame, 100u));
    CHECK(thy_display_prepare(&display, 101u) != NULL);
    frame[300] = 0u;
    CHECK(thy_display_submit(&display, frame, 116u));
    CHECK(display.report.dirty_bytes == 0u && display.report.active);
    CHECK(commit_to_panel(120u));
    CHECK(display.shadow[300] == 9u && display.report.dirty_bytes == 1u);
    CHECK(drain(121u));
    CHECK(display.shadow[300] == 0u && display.report.frames == 2u);
    return 1;
}

static int test_failed_commit_and_deferral_preserve_panel_shadow(void)
{
    initialize();
    memset(frame, 0x5a, sizeof frame);
    CHECK(thy_display_submit(&display, frame, 0u));
    const thy_display_transfer *transfer = thy_display_prepare(&display, 0u);
    CHECK(transfer != NULL);
    thy_display_transfer frozen = *transfer;
    thy_display_defer(&display);
    thy_display_commit(&display, 0, 1u);
    CHECK(display.report.failed_chunks == 1u && display.report.busy_deferrals == 1u);
    CHECK(display.report.data_bytes == 0u && display.report.valid_bytes == 0u);
    CHECK(display.report.dirty_bytes == 1024u && display.report.chunks == 0u);
    CHECK(memcmp(thy_display_prepare(&display, 2u), &frozen, sizeof frozen) == 0);
    CHECK(drain(3u));
    CHECK(memcmp(display.shadow, frame, sizeof frame) == 0);
    return 1;
}

static int test_render_due_wrap_fault_and_mode_isolation(void)
{
    initialize();
    CHECK(thy_display_submit(&display, frame, 0xfffffff0u));
    CHECK(!thy_display_due(&display, 0xfffffff1u));
    CHECK(thy_display_due(&display, 0u)); /* Active transfer does not stop rendering. */
    frame[256] = 1u;
    CHECK(thy_display_submit(&display, frame, 0u));
    CHECK(display.report.last_render_gap_ms == 16u);
    CHECK(display.report.max_render_gap_ms == 16u);
    const thy_display_transfer *transfer = thy_display_prepare(&display, 0u);
    CHECK(transfer != NULL);
    thy_display_begin(&display, 100u); /* Cannot switch API modes mid-flight. */
    CHECK(thy_display_prepare(&display, 100u) == transfer);
    CHECK(commit_to_panel(3u));
    CHECK(display.report.last_generation_age_ms == 3u);
    thy_display_fault(&display);
    CHECK(!display.report.active && display.report.faulted && !display.prepared);
    CHECK(!thy_display_due(&display, 100u));
    CHECK(!thy_display_submit(&display, frame, 100u));
    CHECK(thy_display_prepare(&display, 100u) == NULL);
    unsigned chunks = display.report.chunks;
    thy_display_commit(&display, 1, 100u);
    CHECK(display.report.chunks == chunks);
    thy_display_init(&display);
    CHECK(thy_display_due(&display, 100u) && !display.report.initial_complete);
    thy_display_begin(&display, 100u);
    CHECK(!thy_display_submit(&display, frame, 200u));
    return 1;
}

static int test_nonpriority_pages_cannot_starve_during_note_band_churn(void)
{
    CHECK(clean_panel());
    memset(frame, 1, sizeof frame);
    unsigned pages = 0u, regular_bytes = 0u;
    for (unsigned chunk = 0; chunk < 160u; ++chunk) {
        /* A new note-band image every service call deliberately exceeds the
         * rendering cadence. Other pages remain unchanged but still dirty. */
        memset(frame + 256u, (int)(2u + chunk % 200u), 384u);
        CHECK(thy_display_submit(&display, frame, 100u + chunk));
        const thy_display_transfer *transfer = thy_display_prepare(&display, 100u + chunk);
        CHECK(transfer != NULL);
        unsigned page = transfer->offset / 128u;
        pages |= 1u << page;
        if (page < 2u || page > 4u) regular_bytes += transfer->count;
        CHECK(commit_to_panel(100u + chunk));
    }
    CHECK(pages == 0xffu && regular_bytes == 640u);
    for (unsigned offset = 0; offset < sizeof frame; ++offset)
        if (offset < 256u || offset >= 640u) CHECK(display.shadow[offset] == 1u);
    CHECK(drain(300u));
    CHECK(display.report.coalesced >= 159u);
    return 1;
}

static int test_dense_full_screen_churn_covers_all_pages_in_sixty_four_chunks(void)
{
    CHECK(clean_panel());
    uint8_t seen[sizeof frame] = {0};
    unsigned covered = 0u;
    for (unsigned chunk = 0; chunk < 64u; ++chunk) {
        memset(frame, (int)(chunk + 1u), sizeof frame);
        CHECK(thy_display_submit(&display, frame, 100u + chunk));
        const thy_display_transfer *transfer = thy_display_prepare(&display, 100u + chunk);
        CHECK(transfer != NULL && transfer->count == 16u);
        for (unsigned byte = 0; byte < transfer->count; ++byte) {
            unsigned offset = transfer->offset + byte;
            CHECK(!seen[offset]);
            seen[offset] = 1u;
            ++covered;
        }
        CHECK(commit_to_panel(100u + chunk));
    }
    CHECK(covered == 1024u);
    for (unsigned offset = 0; offset < sizeof frame; ++offset) CHECK(seen[offset]);
    CHECK(drain(200u));
    return 1;
}

static int test_sparse_note_band_priority_and_fragmented_glyph_packing(void)
{
    CHECK(clean_panel());
    frame[256] = frame[384] = frame[512] = frame[0] = 1u;
    CHECK(thy_display_submit(&display, frame, 100u));
    for (unsigned chunk = 0; chunk < 4u; ++chunk) {
        const thy_display_transfer *transfer = thy_display_prepare(&display, 100u + chunk);
        CHECK(transfer != NULL && transfer->count == 1u);
        CHECK(transfer->offset == (chunk < 3u ? (chunk + 2u) * 128u : 0u));
        CHECK(commit_to_panel(100u + chunk));
    }
    CHECK(!display.report.active);
    CHECK(clean_panel());
    for (unsigned offset = 0; offset < sizeof frame; ++offset)
        if ((offset % 128u) % 3u == 0u) frame[offset] = 0xffu;
    CHECK(thy_display_submit(&display, frame, 100u));
    uint32_t chunks_before = display.report.chunks;
    uint32_t bytes_before = display.report.data_bytes;
    CHECK(drain(100u));
    CHECK(display.report.chunks - chunks_before <= 64u);
    CHECK(display.report.data_bytes - bytes_before > 344u); /* Gaps were sent. */
    CHECK(display.report.data_bytes - bytes_before <= 1024u);
    return 1;
}

static int test_one_point_four_ms_service_observes_latest_note_without_full_frame(void)
{
    CHECK(clean_panel());
    display.report.max_generation_age_ms = 0u;
    uint32_t max_age = 0u;
    unsigned generations_seen = 0u, last_seen = 0u;
    /* Representative 1/16 @120 BPM: change note every125ms, gatehalf62.5ms.
     * Render at16ms; every foreground service is separated by1400us. There is
     * no scheduler work that waits for a1024-byte transfer to finish. */
    for (uint32_t us = 100000u; us < 1100000u; us += 1400u) {
        uint32_t now = us / 1000u;
        unsigned note = (us - 100000u) / 125000u + 1u;
        unsigned gate = ((us - 100000u) % 125000u) < 62500u;
        if (thy_display_due(&display, now)) {
            memset(frame + 384u + 20u, gate ? (int)note : 0, 8u);
            thy_display_submit(&display, frame, now);
        }
        if (thy_display_prepare(&display, now)) CHECK(commit_to_panel(now));
        if (gate && panel[404] == note && last_seen != note) {
            uint32_t age = us - (100000u + (note - 1u) * 125000u);
            if (age > max_age) max_age = age;
            last_seen = note;
            ++generations_seen;
        }
    }
    CHECK(generations_seen == 8u);
    CHECK(max_age < 18000u && max_age < 62500u);
    CHECK(display.report.data_bytes <= 1024u + 16u * 8u);
    CHECK(display.report.max_generation_age_ms <= 1u);
    CHECK(drain(1100u));
    return 1;
}

static int test_latest_frame_under_coalescing_failures_and_sparse_churn(void)
{
    initialize();
    CHECK(thy_display_submit(&display, frame, 0u));
    uint32_t random = 0x76a52f19u;
    for (unsigned step = 0; step < 2000u; ++step) {
        const thy_display_transfer *transfer = thy_display_prepare(&display, step);
        thy_display_transfer frozen = {0};
        if (transfer) frozen = *transfer;
        if (step % 2u == 0u) {
            for (unsigned byte = 0; byte < 32u; ++byte) {
                random = random * 1664525u + 1013904223u;
                frame[(random >> 12u) % sizeof frame] = (uint8_t)(random >> 24u);
            }
            thy_display_submit(&display, frame, step);
        }
        if (transfer) {
            CHECK(memcmp(transfer, &frozen, sizeof frozen) == 0);
            if (step % 17u == 0u) {
                uint8_t before_shadow[sizeof panel];
                memcpy(before_shadow, display.shadow, sizeof before_shadow);
                thy_display_commit(&display, 0, step);
                CHECK(memcmp(before_shadow, display.shadow, sizeof before_shadow) == 0);
                CHECK(memcmp(thy_display_prepare(&display, step), &frozen, sizeof frozen) == 0);
            } else CHECK(commit_to_panel(step));
        }
    }
    CHECK(display.report.failed_chunks > 100u);
    CHECK(display.report.coalesced > 900u);
    CHECK(drain(2000u));
    CHECK(display.report.initial_complete && display.report.valid_bytes == 1024u);
    CHECK(memcmp(panel, frame, sizeof panel) == 0);
    CHECK(memcmp(display.shadow, frame, sizeof frame) == 0);
    return 1;
}

static int test_null_arguments_and_legacy_frozen_frame_remain_safe(void)
{
    thy_display_init(NULL);
    CHECK(!thy_display_due(NULL, 0u));
    CHECK(!thy_display_submit(NULL, frame, 0u));
    CHECK(thy_display_prepare(NULL, 0u) == NULL);
    thy_display_commit(NULL, 1, 0u);
    thy_display_defer(NULL);
    thy_display_fault(NULL);
    initialize();
    CHECK(!thy_display_submit(&display, NULL, 0u));
    thy_display_begin(&display, 0u);
    for (unsigned chunk = 0; chunk < 64u; ++chunk) {
        uint8_t command[3];
        uint16_t offset, count;
        unsigned commands = thy_display_chunk(&display, command, &offset, &count);
        CHECK(commands == (chunk % 8u == 0u ? 3u : 0u));
        CHECK(offset == chunk * 16u && count == 16u);
        thy_display_complete_chunk(&display, chunk / 4u);
    }
    CHECK(display.report.frames == 1u && display.report.command_bytes == 24u);
    CHECK(!thy_display_due(&display, 41u) && thy_display_due(&display, 42u));
    return 1;
}

static unsigned simulated_visible;

static int scene_commit(uint32_t now)
{
    const thy_display_transfer *transfer = thy_display_prepare(&display, now);
    CHECK(transfer != NULL);
    if (transfer->kind == THY_DISPLAY_TRANSFER_DATA) {
        CHECK(transfer->command_count == 3u);
        CHECK(transfer->count > 0u && transfer->count <= 16u);
        CHECK(!simulated_visible); /* Scene uploads are hidden, not black fills. */
        memcpy(panel + transfer->offset, transfer->data, transfer->count);
    } else {
        CHECK(transfer->command_count == 1u && transfer->count == 0u);
        CHECK(transfer->command[0] ==
            (transfer->kind == THY_DISPLAY_TRANSFER_OFF ? 0xaeu : 0xafu));
        if (transfer->kind == THY_DISPLAY_TRANSFER_ON) {
            CHECK(display.report.initial_complete && display.report.valid_bytes == 1024u);
            CHECK(display.report.dirty_bytes == 0u);
            CHECK(memcmp(panel, display.target, sizeof panel) == 0);
            simulated_visible = 1u;
        } else simulated_visible = 0u;
    }
    thy_display_commit(&display, 1, now);
    CHECK(thy_display_panel_visible(&display) == (int)simulated_visible);
    return 1;
}

static int scene_drain(uint32_t now)
{
    unsigned guard = 0u;
    while (display.report.active && guard++ < 1024u) {
        if (thy_display_prepare(&display, now)) CHECK(scene_commit(now));
        ++now;
    }
    CHECK(guard < 1024u);
    CHECK(!thy_display_scene_active(&display));
    CHECK(thy_display_panel_visible(&display));
    CHECK(memcmp(panel, frame, sizeof frame) == 0);
    return 1;
}

static int scene_clean_panel(void)
{
    initialize();
    simulated_visible = 0u; /* Board bringup already issued AE. */
    thy_display_enable_scene_control(&display, 1);
    CHECK(!thy_display_scene_active(&display) && !thy_display_panel_visible(&display));
    CHECK(thy_display_submit(&display, frame, 0u));
    CHECK(scene_drain(0u));
    CHECK(display.report.scene_completed == 1u);
    return 1;
}

static int test_scene_initial_fence_covers_all_bytes_before_first_reveal(void)
{
    initialize();
    simulated_visible = 0u;
    thy_display_enable_scene_control(&display, 1);
    CHECK(thy_display_due(&display, 0xfffffff0u));
    CHECK(thy_display_submit(&display, frame, 0xfffffff0u));
    CHECK(thy_display_scene_active(&display) && !thy_display_panel_visible(&display));
    for (unsigned chunk = 0; chunk < 64u; ++chunk) {
        const thy_display_transfer *transfer = thy_display_prepare(&display, chunk);
        CHECK(transfer != NULL && transfer->kind == THY_DISPLAY_TRANSFER_DATA);
        CHECK(scene_commit(chunk));
        CHECK(!thy_display_panel_visible(&display));
        CHECK(display.report.frames == 0u);
    }
    CHECK(display.report.valid_bytes == 1024u && display.report.dirty_bytes == 0u);
    CHECK(display.report.active && thy_display_scene_active(&display));
    CHECK(thy_display_prepare(&display, 64u)->kind == THY_DISPLAY_TRANSFER_ON);
    CHECK(scene_commit(64u));
    CHECK(display.report.frames == 1u && display.report.scene_completed == 1u);
    CHECK(display.report.data_bytes == 1024u && display.report.command_bytes == 193u);
    CHECK(!display.report.active && !thy_display_scene_active(&display));
    CHECK(thy_display_due(&display, 64u));
    return 1;
}

static int test_scene_off_then_pinned_data_then_on_with_failed_commands(void)
{
    CHECK(scene_clean_panel());
    memset(frame, 0x55, sizeof frame);
    CHECK(thy_display_submit_scene(&display, frame, 100u));
    const thy_display_transfer *transfer = thy_display_prepare(&display, 100u);
    CHECK(transfer != NULL && transfer->kind == THY_DISPLAY_TRANSFER_OFF);
    thy_display_transfer frozen = *transfer;
    uint32_t bytes_before = display.report.data_bytes;
    thy_display_commit(&display, 0, 101u);
    CHECK(thy_display_panel_visible(&display) && display.report.data_bytes == bytes_before);
    CHECK(memcmp(thy_display_prepare(&display, 102u), &frozen, sizeof frozen) == 0);
    CHECK(scene_commit(103u));
    CHECK(!thy_display_panel_visible(&display));
    unsigned guard = 0u;
    while ((transfer = thy_display_prepare(&display, 104u + guard))->kind ==
        THY_DISPLAY_TRANSFER_DATA && guard++ < 128u) CHECK(scene_commit(104u + guard));
    CHECK(guard == 64u && transfer->kind == THY_DISPLAY_TRANSFER_ON);
    frozen = *transfer;
    thy_display_commit(&display, 0, 200u);
    CHECK(!thy_display_panel_visible(&display) && thy_display_scene_active(&display));
    CHECK(display.report.dirty_bytes == 0u);
    CHECK(memcmp(thy_display_prepare(&display, 201u), &frozen, sizeof frozen) == 0);
    CHECK(scene_commit(202u));
    CHECK(thy_display_panel_visible(&display) && !thy_display_scene_active(&display));
    CHECK(display.report.failed_chunks == 2u);
    CHECK(display.report.data_bytes - bytes_before == 1024u);
    CHECK(display.report.scene_requests == 1u && display.report.scene_completed == 2u);
    return 1;
}

static int test_scene_ignores_continuous_animated_submissions_while_hidden(void)
{
    CHECK(scene_clean_panel());
    memset(frame, 0x39, sizeof frame);
    CHECK(thy_display_submit_scene(&display, frame, 100u));
    CHECK(scene_commit(100u)); /* AE. */
    uint8_t animated[sizeof frame];
    unsigned guard = 0u;
    while (display.report.active && guard++ < 100u) {
        memset(animated, (int)guard, sizeof animated);
        CHECK(!thy_display_due(&display, 100u + guard * 16u));
        CHECK(!thy_display_submit(&display, animated, 100u + guard * 16u));
        CHECK(memcmp(display.target, frame, sizeof frame) == 0);
        CHECK(scene_commit(100u + guard * 16u));
    }
    CHECK(guard == 65u); /* 64 data slices plus AF; churn cannot extend it. */
    CHECK(display.report.scene_deferred == 65u);
    CHECK(thy_display_panel_visible(&display) && !thy_display_scene_active(&display));
    CHECK(memcmp(panel, frame, sizeof frame) == 0);
    CHECK(thy_display_due(&display, 2000u));
    /* Ordinary live Arp updates resume without AE/AF after the coherent reveal. */
    frame[404] ^= 0xffu;
    CHECK(thy_display_submit(&display, frame, 2000u));
    const thy_display_transfer *transfer = thy_display_prepare(&display, 2000u);
    CHECK(transfer != NULL && transfer->kind == THY_DISPLAY_TRANSFER_DATA);
    CHECK(transfer->count == 1u && transfer->command_count == 3u);
    CHECK(commit_to_panel(2001u));
    CHECK(thy_display_panel_visible(&display) && !thy_display_scene_active(&display));
    return 1;
}

static int test_new_scenes_coalesce_while_black_without_mutating_prepared_data(void)
{
    CHECK(scene_clean_panel());
    memset(frame, 0x11, sizeof frame);
    CHECK(thy_display_submit_scene(&display, frame, 100u));
    CHECK(scene_commit(100u));
    const thy_display_transfer *transfer = thy_display_prepare(&display, 101u);
    CHECK(transfer != NULL && transfer->kind == THY_DISPLAY_TRANSFER_DATA);
    thy_display_transfer frozen = *transfer;
    memset(frame, 0x22, sizeof frame);
    CHECK(thy_display_submit_scene(&display, frame, 102u));
    memset(frame, 0x33, sizeof frame);
    CHECK(thy_display_submit_scene(&display, frame, 103u));
    CHECK(memcmp(thy_display_prepare(&display, 104u), &frozen, sizeof frozen) == 0);
    CHECK(scene_commit(104u));
    CHECK(!thy_display_panel_visible(&display));
    CHECK(scene_drain(105u));
    CHECK(display.report.scene_requests == 3u && display.report.scene_coalesced == 2u);
    CHECK(display.report.scene_completed == 2u);
    CHECK(memcmp(panel, frame, sizeof frame) == 0);
    return 1;
}

static int test_new_scene_during_prepared_on_queues_latest_after_coherent_reveal(void)
{
    CHECK(scene_clean_panel());
    frame[404] = 1u;
    CHECK(thy_display_submit_scene(&display, frame, 100u));
    CHECK(scene_commit(100u)); /* AE. */
    CHECK(scene_commit(101u)); /* The one changed pixel byte. */
    const thy_display_transfer *transfer = thy_display_prepare(&display, 102u);
    CHECK(transfer != NULL && transfer->kind == THY_DISPLAY_TRANSFER_ON);
    thy_display_transfer frozen = *transfer;
    uint8_t original[sizeof frame];
    memcpy(original, frame, sizeof original);
    memset(frame, 0x22, sizeof frame);
    CHECK(thy_display_submit_scene(&display, frame, 103u));
    memset(frame, 0x33, sizeof frame);
    CHECK(thy_display_submit_scene(&display, frame, 104u));
    CHECK(memcmp(display.target, original, sizeof original) == 0);
    CHECK(memcmp(thy_display_prepare(&display, 105u), &frozen, sizeof frozen) == 0);
    CHECK(scene_commit(106u)); /* Only complete old scene may become visible. */
    CHECK(thy_display_panel_visible(&display) && thy_display_scene_active(&display));
    CHECK(memcmp(panel, original, sizeof original) == 0);
    CHECK(memcmp(display.target, frame, sizeof frame) == 0);
    transfer = thy_display_prepare(&display, 107u);
    CHECK(transfer != NULL && transfer->kind == THY_DISPLAY_TRANSFER_OFF);
    CHECK(scene_commit(107u));
    CHECK(!thy_display_panel_visible(&display));
    CHECK(scene_drain(108u));
    CHECK(display.report.scene_completed == 3u);
    CHECK(!display.queued_scene && memcmp(panel, frame, sizeof frame) == 0);
    return 1;
}

static int test_scene_failure_never_reveals_an_incomplete_target(void)
{
    CHECK(scene_clean_panel());
    memset(frame, 0x5a, sizeof frame);
    CHECK(thy_display_submit_scene(&display, frame, 100u));
    CHECK(scene_commit(100u));
    const thy_display_transfer *transfer = thy_display_prepare(&display, 101u);
    CHECK(transfer != NULL && transfer->kind == THY_DISPLAY_TRANSFER_DATA);
    thy_display_transfer frozen = *transfer;
    uint8_t old_shadow[sizeof panel];
    memcpy(old_shadow, display.shadow, sizeof old_shadow);
    thy_display_commit(&display, 0, 102u);
    CHECK(!thy_display_panel_visible(&display) && thy_display_scene_active(&display));
    CHECK(memcmp(display.shadow, old_shadow, sizeof old_shadow) == 0);
    CHECK(memcmp(thy_display_prepare(&display, 103u), &frozen, sizeof frozen) == 0);
    thy_display_fault(&display);
    CHECK(!thy_display_panel_visible(&display) && display.report.faulted);
    CHECK(thy_display_prepare(&display, 104u) == NULL);
    CHECK(!thy_display_submit_scene(&display, frame, 104u));
    CHECK(!thy_display_due(&display, 104u));
    CHECK(display.report.scene_completed == 1u);
    return 1;
}

static int test_scene_request_preserves_an_existing_ordinary_inflight_slice(void)
{
    CHECK(scene_clean_panel());
    frame[404] = 1u;
    CHECK(thy_display_submit(&display, frame, 100u));
    const thy_display_transfer *transfer = thy_display_prepare(&display, 100u);
    CHECK(transfer != NULL && transfer->kind == THY_DISPLAY_TRANSFER_DATA);
    thy_display_transfer frozen = *transfer;
    memset(frame, 0x77, sizeof frame);
    CHECK(thy_display_submit_scene(&display, frame, 101u));
    CHECK(memcmp(thy_display_prepare(&display, 102u), &frozen, sizeof frozen) == 0);
    CHECK(commit_to_panel(102u)); /* Old normal slice is immutable and still visible. */
    CHECK(thy_display_panel_visible(&display));
    CHECK(thy_display_prepare(&display, 103u)->kind == THY_DISPLAY_TRANSFER_OFF);
    CHECK(scene_commit(103u));
    CHECK(scene_drain(104u));
    CHECK(memcmp(panel, frame, sizeof frame) == 0);
    return 1;
}

static int test_scene_off_payload_is_immutable_and_queued_scene_can_be_cancelled(void)
{
    CHECK(scene_clean_panel());
    CHECK(display.report.coalesced == 0u);
    frame[404] = 1u;
    CHECK(thy_display_submit_scene(&display, frame, 100u));
    const thy_display_transfer *transfer = thy_display_prepare(&display, 100u);
    CHECK(transfer != NULL && transfer->kind == THY_DISPLAY_TRANSFER_OFF);
    thy_display_transfer frozen = *transfer;
    frame[404] = 2u;
    CHECK(thy_display_submit_scene(&display, frame, 101u));
    CHECK(memcmp(thy_display_prepare(&display, 102u), &frozen, sizeof frozen) == 0);
    CHECK(scene_commit(102u));
    CHECK(scene_commit(103u));
    transfer = thy_display_prepare(&display, 104u);
    CHECK(transfer != NULL && transfer->kind == THY_DISPLAY_TRANSFER_ON);
    frozen = *transfer;
    uint8_t replacement[sizeof frame];
    memset(replacement, 0x99, sizeof replacement);
    CHECK(thy_display_submit_scene(&display, replacement, 105u));
    CHECK(display.queued_scene);
    CHECK(!thy_display_submit_scene(&display, frame, 106u)); /* Latest wants current scene. */
    CHECK(!display.queued_scene);
    CHECK(memcmp(thy_display_prepare(&display, 107u), &frozen, sizeof frozen) == 0);
    CHECK(scene_commit(107u));
    CHECK(thy_display_panel_visible(&display) && !thy_display_scene_active(&display));
    CHECK(!display.report.active && thy_display_prepare(&display, 108u) == NULL);
    CHECK(memcmp(panel, frame, sizeof frame) == 0);
    return 1;
}

static int test_initial_scene_failure_stays_hidden_and_default_api_is_unchanged(void)
{
    initialize();
    simulated_visible = 0u;
    thy_display_enable_scene_control(&display, 1);
    CHECK(thy_display_submit_scene(&display, frame, 0u));
    const thy_display_transfer *transfer = thy_display_prepare(&display, 0u);
    CHECK(transfer != NULL && transfer->kind == THY_DISPLAY_TRANSFER_DATA);
    thy_display_commit(&display, 0, 1u);
    CHECK(display.report.valid_bytes == 0u && display.report.scene_completed == 0u);
    thy_display_fault(&display);
    CHECK(!thy_display_panel_visible(&display) && !display.report.initial_complete);
    CHECK(thy_display_prepare(&display, 2u) == NULL);
    thy_display_enable_scene_control(NULL, 1);
    CHECK(!thy_display_scene_active(NULL) && !thy_display_panel_visible(NULL));
    CHECK(!thy_display_submit_scene(NULL, frame, 0u));
    initialize();
    CHECK(thy_display_panel_visible(&display)); /* Optional feature defaults off. */
    CHECK(thy_display_submit_scene(&display, frame, 0u));
    CHECK(!thy_display_scene_active(&display));
    CHECK(drain(0u));
    CHECK(display.report.scene_requests == 0u && display.report.scene_completed == 0u);
    thy_display_enable_scene_control(&display, 1); /* Cannot change an active API's contract. */
    CHECK(!display.scene_control && thy_display_panel_visible(&display));
    return 1;
}

int main(void)
{
    if (!test_initial_zero_frame_must_reach_every_panel_byte() ||
        !test_sparse_bytes_use_explicit_nonzero_column_addresses() ||
        !test_latest_target_never_overwrites_a_prepared_payload() ||
        !test_target_can_cancel_dirty_then_old_commit_redirties_it() ||
        !test_failed_commit_and_deferral_preserve_panel_shadow() ||
        !test_render_due_wrap_fault_and_mode_isolation() ||
        !test_nonpriority_pages_cannot_starve_during_note_band_churn() ||
        !test_dense_full_screen_churn_covers_all_pages_in_sixty_four_chunks() ||
        !test_sparse_note_band_priority_and_fragmented_glyph_packing() ||
        !test_one_point_four_ms_service_observes_latest_note_without_full_frame() ||
        !test_latest_frame_under_coalescing_failures_and_sparse_churn() ||
        !test_null_arguments_and_legacy_frozen_frame_remain_safe() ||
        !test_scene_initial_fence_covers_all_bytes_before_first_reveal() ||
        !test_scene_off_then_pinned_data_then_on_with_failed_commands() ||
        !test_scene_ignores_continuous_animated_submissions_while_hidden() ||
        !test_new_scenes_coalesce_while_black_without_mutating_prepared_data() ||
        !test_new_scene_during_prepared_on_queues_latest_after_coherent_reveal() ||
        !test_scene_failure_never_reveals_an_incomplete_target() ||
        !test_scene_request_preserves_an_existing_ordinary_inflight_slice() ||
        !test_scene_off_payload_is_immutable_and_queued_scene_can_be_cancelled() ||
        !test_initial_scene_failure_stays_hidden_and_default_api_is_unchanged()) return 1;
    printf("21 dirty OLED/scene transport scenarios passed (%u assertions)\n", assertions);
    return 0;
}
