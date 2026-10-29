/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_studio_ui.h"
#include "../../core/thy_display.h"
#include "../../core/thy_engine.h"
#include "../../core/thy_settings.h"
#include "../../core/thy_calibration.h"
#include "../../core/thy_brand_logo.h"

static thy_core musical_core;
static thy_instrument instrument;
static thy_engine engine;
static thy_state before_state;
static void set_capture_state(thy_sequencer *seq, uint8_t value)
{ seq->capture.state = value; }
/* Visual-only ready/pending/applied fixtures; not calibration data tests. */
static void set_calibration_state(thy_calibration *calibration, uint8_t value)
{ calibration->state = value; }
#define state engine.state
static thy_studio_ui ui, still;
static uint8_t frame[THY_UI_FRAME_BYTES], before[THY_UI_FRAME_BYTES];
static uint8_t target[THY_UI_FRAME_BYTES], interrupted[THY_UI_FRAME_BYTES];
static uint8_t destinations[7][THY_UI_FRAME_BYTES];
static unsigned assertions;
static int matches_text(const uint8_t *actual, unsigned x, unsigned y,
    const char *text, unsigned limit);

#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 0; \
} } while (0)

static void initialize(void)
{
    thy_core_init(&musical_core);
    thy_instrument_init(&instrument, &musical_core);
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&engine, &instrument, 0u, 70u, undo_workspace_1);
    thy_studio_ui_init(&ui);
    thy_studio_ui_init(&still);
    still.animations_enabled = 0u;
}

static void render(thy_studio_ui *renderer, uint32_t now, uint8_t *out)
{
    thy_studio_ui_render(renderer, &state, "MAJOR C", 1, 1, now, out);
}

/* Advance with ordinary foreground render cadence. Separate stall assertions
 * below deliberately call render directly with large gaps. */
static void advance(thy_studio_ui *renderer, uint32_t now, uint8_t *out)
{
    while (now - renderer->last_render_ms > 35u)
        render(renderer, renderer->last_render_ms + 35u, out);
    render(renderer, now, out);
}

static int pixel_at(const uint8_t *pixels, unsigned x, unsigned y)
{
    return !!(pixels[y / 8u * THY_UI_WIDTH + x] & (1u << (y % 8u)));
}

static int test_geometry_catalog_and_render_is_read_only(void)
{
    struct { uint8_t prefix[32], pixels[THY_UI_FRAME_BYTES], suffix[32]; } guarded;
    initialize();
    instrument.display_view = 5u; /* All applicable per-view rows visible. */
    memset(&guarded, 0xa5, sizeof guarded);
    thy_state_open(&state);
    before_state = state;
    thy_studio_ui_render(&still, &state, "MAJOR C", 1, 1, 0, guarded.pixels);
    CHECK(memcmp(&state, &before_state, sizeof state) == 0);
    /* Original independently drawn S glyph, sampled at its actual OLED page
     * coordinates. The output is page-addressed 1-bit, not a browser bitmap. */
    static const uint8_t s[] = {0x12, 0x15, 0x15, 0x09};
    for (unsigned x = 0; x < 4u; ++x)
        for (unsigned y = 0; y < 5u; ++y)
            CHECK(pixel_at(guarded.pixels, x + 12u, y + 1u) == !!(s[x] & (1u << y)));
    for (unsigned x = 0; x < THY_UI_WIDTH; ++x) {
        CHECK(pixel_at(guarded.pixels,x,8)+pixel_at(guarded.pixels,x,9)+pixel_at(guarded.pixels,x,10)==1);
        CHECK(pixel_at(guarded.pixels,x,53)+pixel_at(guarded.pixels,x,54)+pixel_at(guarded.pixels,x,55)==1);
    }
    for (unsigned g = 0; g < THY_MENU_GROUP_COUNT; ++g) {
        const thy_menu_group *group = &thy_menu_groups[g];
        for (unsigned row = 0; row < group->row_count; ++row) {
            if (!thy_state_row_visible(&state, (uint16_t)(group->first_row + row))) continue;
            CHECK(thy_state_focus(&state, (uint8_t)g,
                (uint16_t)(group->first_row + row)) == THY_STATE_OK);
            before_state = state;
            thy_studio_ui_render(&still, &state,
                "A SCALE LABEL LONGER THAN THE DISPLAY WIDTH", 1, 1, row, guarded.pixels);
            CHECK(memcmp(&state, &before_state, sizeof state) == 0);
            for (unsigned i = 0; i < 32u; ++i) {
                CHECK(guarded.prefix[i] == 0xa5u);
                CHECK(guarded.suffix[i] == 0xa5u);
            }
        }
    }
    /* All seven destination sets remain distinguishable in the eight-glyph
     * value column, including USB 1 versus USB 1 + DIN. */
    uint16_t route = thy_state_find_row("midi.destinations");
    CHECK(route != THY_STATE_NO_ROW);
    CHECK(thy_state_focus(&state, thy_menu_rows[route].group, route) == THY_STATE_OK);
    CHECK(thy_menu_rows[route].option_count == 7u);
    for (unsigned option = 0; option < 7u; ++option) {
        CHECK(thy_state_set(&state, route, (int32_t)option) == THY_STATE_OK);
        render(&still, option, destinations[option]);
        for (unsigned previous = 0; previous < option; ++previous)
            CHECK(memcmp(destinations[previous], destinations[option], THY_UI_FRAME_BYTES) != 0);
    }
    thy_studio_ui_render(&still, NULL, NULL, 0, 0, 0, guarded.pixels);
    CHECK(!still.initialized && !still.transitioning);
    for (unsigned i = 0; i < 32u; ++i) {
        CHECK(guarded.prefix[i] == 0xa5u);
        CHECK(guarded.suffix[i] == 0xa5u);
    }
    thy_studio_ui_init(NULL);
    thy_studio_ui_render(NULL, &state, NULL, 0, 0, 0, guarded.pixels);
    thy_studio_ui_render(&still, &state, NULL, 0, 0, 0, NULL);
    return 1;
}

static int check_half_slide(const uint8_t *old, const uint8_t *next,
    const uint8_t *actual, int direction)
{
    for (unsigned page = 0; page < THY_UI_HEIGHT / 8u; ++page) {
        unsigned base = page * THY_UI_WIDTH;
        for (unsigned x = 0; x < THY_UI_WIDTH; ++x) {
            uint8_t expected = direction < 0 ?
                (x < 64u ? old[base + x + 64u] : next[base + x - 64u]) :
                (x < 64u ? next[base + x + 64u] : old[base + x - 64u]);
            CHECK(actual[base + x] == expected);
        }
    }
    return 1;
}

static int test_play_projection_and_opposite_hierarchy_slides(void)
{
    initialize();
    const int16_t note = 60;
    CHECK(thy_core_press(&musical_core, 0, &note, 1, 0, 0,
        THY_ROUTE_DIN, 96) == THY_OK);
    render(&ui, 0, before);
    render(&still, 0, target);
    CHECK(memcmp(before, target, sizeof target) == 0);
    CHECK(musical_core.owners[0].held);
    thy_state_open(&state);
    render(&still, 1, target);
    render(&ui, 100, frame);
    CHECK(ui.transitioning && ui.transition_direction == -1);
    CHECK(memcmp(before, frame, sizeof frame) == 0);
    advance(&ui, 100u + THY_STUDIO_UI_TRANSITION_MS / 2u, frame);
    CHECK(check_half_slide(before, target, frame, -1));
    advance(&ui, 100u + THY_STUDIO_UI_TRANSITION_MS, frame);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    memcpy(before, frame, sizeof before);
    CHECK(thy_state_navigate(&state, THY_STATE_LEFT) == THY_STATE_OK);
    render(&still, 281, target);
    render(&ui, 281, frame);
    CHECK(ui.transitioning && ui.transition_direction == 1);
    CHECK(memcmp(before, frame, sizeof frame) == 0);
    advance(&ui, 281u + THY_STUDIO_UI_TRANSITION_MS / 2u, frame);
    CHECK(check_half_slide(before, target, frame, 1));
    advance(&ui, 281u + THY_STUDIO_UI_TRANSITION_MS, frame);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    CHECK(musical_core.owners[0].held);
    CHECK(thy_core_release(&musical_core, 0) == THY_OK);
    return 1;
}

static int test_cursor_and_editor_changes_do_not_slide(void)
{
    initialize();
    thy_state_open(&state);
    render(&ui, 0, before);
    uint32_t groups_revision = state.transition_revision;
    CHECK(thy_state_navigate(&state, THY_STATE_UP) == THY_STATE_OK);
    render(&ui, 1, frame);
    CHECK(!ui.transitioning && state.transition_revision == groups_revision);
    CHECK(memcmp(before, frame, sizeof frame) != 0);
    CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
    render(&ui, 2, frame);
    CHECK(!ui.transitioning && state.transition_revision == groups_revision);

    initialize();
    uint16_t row = thy_state_find_row("arps.swing");
    CHECK(row != THY_STATE_NO_ROW);
    CHECK(thy_state_focus(&state, thy_menu_rows[row].group, row) == THY_STATE_OK);
    render(&ui, 0, before); /* Initial view is immediate. */
    uint32_t revision = state.transition_revision;
    CHECK(!ui.transitioning);
    CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
    render(&ui, 1, frame);
    CHECK(!ui.transitioning && state.transition_revision == revision);
    CHECK(memcmp(before, frame, sizeof frame) != 0);
    CHECK(thy_state_focus(&state, thy_menu_rows[row].group, row) == THY_STATE_OK);
    render(&ui, 2, before);
    CHECK(!ui.transitioning && state.transition_revision == revision);
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(state.editing);
    render(&ui, 3, frame);
    CHECK(!ui.transitioning && state.transition_revision == revision);
    CHECK(memcmp(before, frame, sizeof frame) != 0);
    CHECK(thy_state_navigate(&state, THY_STATE_UP) == THY_STATE_OK);
    render(&ui, 4, frame);
    CHECK(!ui.transitioning && state.transition_revision == revision);
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(!state.editing);
    render(&ui, 5, frame);
    CHECK(!ui.transitioning && state.transition_revision == revision);
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(thy_state_navigate(&state, THY_STATE_LEFT) == THY_STATE_OK);
    render(&ui, 6, frame);
    CHECK(!state.editing && !ui.transitioning && state.transition_revision == revision);
    /* Jumping to a different category changes hierarchy once. Cursor motion
     * during that slide updates selection without restarting its clock. */
    CHECK(thy_state_focus(&state, 1, thy_menu_groups[1].first_row) == THY_STATE_OK);
    render(&ui, 10, frame);
    CHECK(ui.transitioning && ui.transition_direction == -1);
    CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
    render(&ui, 20, frame);
    CHECK(ui.transitioning && ui.transition_started_ms == 10u);
    advance(&ui, 290, frame);
    CHECK(!ui.transitioning);
    return 1;
}

static int test_wrap_reduced_motion_and_interruption(void)
{
    initialize();
    render(&ui, 0xffffffe0u, before);
    thy_state_open(&state);
    render(&ui, 0xfffffff0u, frame);
    CHECK(ui.transitioning && memcmp(frame, before, sizeof frame) == 0);
    render(&still, 0x4au, target);
    advance(&ui, 0x4au, frame); /* Half of 180 ms across unsigned wrap. */
    CHECK(ui.transitioning && check_half_slide(before, target, frame, -1));
    memcpy(interrupted, frame, sizeof interrupted);
    CHECK(thy_state_navigate(&state, THY_STATE_LEFT) == THY_STATE_OK);
    render(&ui, 0x4bu, frame);
    CHECK(ui.transitioning && ui.transition_direction == 1);
    CHECK(memcmp(frame, interrupted, sizeof frame) == 0);
    ui.animations_enabled = 0u;
    render(&ui, 0x4cu, frame);
    render(&still, 0x4cu, target);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    ui.animations_enabled = 1u;
    render(&ui, 0x4du, frame);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);

    thy_state_open(&state);
    render(&ui, 0xfffffff0u, frame);
    CHECK(ui.transitioning);
    advance(&ui, 0xa4u, frame); /* 180 ms elapsed across unsigned wrap. */
    render(&still, 0xa4u, target);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    ui.animations_enabled = 0u;
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    render(&ui, 0x109u, frame);
    render(&still, 0x109u, target);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    /* The menu's animation switch is shared C configuration, in addition to
     * the caller's reduced-motion override. It cancels an in-flight slide. */
    ui.animations_enabled = 1u;
    CHECK(thy_state_navigate(&state, THY_STATE_LEFT) == THY_STATE_OK);
    render(&ui, 0x110u, frame);
    CHECK(ui.transitioning);
    CHECK(thy_state_set(&state, THY_ROW_LIGHTS_ANIMATIONS, 0) == THY_STATE_OK);
    render(&ui, 0x111u, frame);
    render(&still, 0x111u, target);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    return 1;
}


static int test_frozen_transfer_budget_completion(void)
{
    thy_display transfer;
    thy_display_init(&transfer);
    CHECK(thy_display_due(&transfer, 0xfffffff0u));
    thy_display_begin(&transfer, 0xfffffff0u);
    CHECK(!thy_display_due(&transfer, 0xfffffff1u));
    for (unsigned chunk = 0; chunk < 64u; ++chunk) {
        uint8_t command[3] = {0xff, 0xff, 0xff};
        uint16_t offset = 9999u, count = 0u;
        unsigned commands = thy_display_chunk(&transfer, command, &offset, &count);
        CHECK(commands == (chunk % 8u == 0u ? 3u : 0u));
        CHECK(count == 16u && offset == chunk * 16u);
        CHECK(commands + count <= 19u);
        if (commands) {
            CHECK(command[0] == (0xb0u | chunk / 8u));
            CHECK(command[1] == 0u && command[2] == 0x10u);
        }
        if (chunk == 17u) {
            thy_display_defer(&transfer);
            thy_display_defer(&transfer);
            CHECK(transfer.report.page == 2u && transfer.report.column == 16u);
            CHECK(!thy_display_due(&transfer, 1000u));
            /* Requests cannot replace an in-flight framebuffer. */
            thy_display_begin(&transfer, 1000u);
            CHECK(transfer.report.page == 2u && transfer.report.column == 16u);
        }
        thy_display_complete_chunk(&transfer, 0xfffffff0u + chunk);
    }
    CHECK(transfer.report.frames == 1u && transfer.report.chunks == 64u);
    CHECK(transfer.report.command_bytes == 24u && transfer.report.data_bytes == 1024u);
    CHECK(transfer.report.last_duration_ms == 63u && transfer.report.max_duration_ms == 63u);
    CHECK(transfer.report.busy_deferrals == 2u && !transfer.report.active);
    CHECK(thy_display_due(&transfer, 0xfffffff0u + 63u));
    thy_display_begin(&transfer, 100u);
    CHECK(transfer.report.last_render_gap_ms == 116u);
    CHECK(transfer.report.max_render_gap_ms == 116u);
    thy_display_fault(&transfer);
    CHECK(transfer.report.faulted && !thy_display_due(&transfer, 200u));
    thy_display_complete_chunk(&transfer, 200u);
    CHECK(transfer.report.frames == 1u);

    return 1;
}

static int test_hierarchy_slides_finish_at_wall_clock_deadline_after_stalls(void)
{
    initialize();
    const int16_t note = 60;
    CHECK(thy_core_press(&musical_core, 0u, &note, 1u, 0, 0,
        THY_ROUTE_DIN, 96) == THY_OK);
    render(&ui, 0u, before);
    thy_state_open(&state);
    render(&ui, 1u, frame);
    CHECK(ui.transitioning && ui.transition_elapsed_ms == 0u);
    render(&still, 1u, target);
    /* The previous device took about 115 ms per full transfer. Its next
     * rendered position must reflect that wall time, not a capped 42 ms. */
    render(&ui, 116u, frame);
    CHECK(ui.transitioning && ui.transition_elapsed_ms == 115u);
    CHECK(memcmp(frame, before, sizeof frame) != 0);
    CHECK(memcmp(frame, target, sizeof frame) != 0);
    render(&ui, 1u + THY_STUDIO_UI_TRANSITION_MS, frame);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    CHECK(ui.transition_elapsed_ms == THY_STUDIO_UI_TRANSITION_MS);
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    render(&ui, 200u, frame);
    CHECK(ui.transitioning);
    render(&still, 5000u, target);
    render(&ui, 5000u, frame);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    CHECK(ui.transition_elapsed_ms == THY_STUDIO_UI_TRANSITION_MS);
    render(&ui, 5016u, frame);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    /* Presentation deadlines never touch musical owners. */
    CHECK(musical_core.owners[0].held);
    CHECK(thy_core_release(&musical_core, 0u) == THY_OK);
    return 1;
}

static int test_hierarchy_slide_uses_current_time_at_fast_or_irregular_cadence(void)
{
    initialize();
    render(&ui, 0u, before);
    thy_state_open(&state);
    render(&ui, 10u, frame);
    render(&still, 10u, target);
    for (uint32_t elapsed = 16u; elapsed < THY_STUDIO_UI_TRANSITION_MS; elapsed += 16u) {
        render(&ui, 10u + elapsed, frame);
        CHECK(ui.transitioning && ui.transition_elapsed_ms == elapsed);
        CHECK(memcmp(frame, target, sizeof frame) != 0);
    }
    /* A repeated call at the same timestamp cannot advance the slide. */
    memcpy(interrupted, frame, sizeof interrupted);
    render(&ui, ui.last_render_ms, frame);
    CHECK(memcmp(frame, interrupted, sizeof frame) == 0);
    render(&ui, 10u + THY_STUDIO_UI_TRANSITION_MS, frame);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    /* An interrupted outgoing slide begins from the currently presented
     * pixels but still receives only its own short wall-clock lifetime. */
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    render(&ui, 201u, frame);
    render(&ui, 250u, frame);
    memcpy(interrupted, frame, sizeof interrupted);
    CHECK(thy_state_navigate(&state, THY_STATE_LEFT) == THY_STATE_OK);
    render(&ui, 251u, frame);
    CHECK(ui.transitioning && ui.transition_started_ms == 251u);
    CHECK(memcmp(frame, interrupted, sizeof frame) == 0);
    render(&still, 251u + THY_STUDIO_UI_TRANSITION_MS, target);
    render(&ui, 251u + THY_STUDIO_UI_TRANSITION_MS, frame);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    return 1;
}

static int test_navigation_motion_policy_returns_exact_destination_and_can_restore_slides(void)
{
    static thy_core saved_core;
    initialize();
    CHECK(ui.animations_enabled && ui.navigation_animations_enabled);
    render(&ui, 0u, before);
    thy_state_open(&state);
    render(&ui, 1u, frame);
    render(&ui, 61u, frame);
    CHECK(ui.transitioning);
    saved_core = musical_core;
    before_state = state;
    thy_studio_ui_set_navigation_animations(&ui, 0);
    CHECK(!ui.transitioning && !ui.navigation_animations_enabled && ui.animations_enabled);
    render(&ui, 62u, frame);
    render(&still, 62u, target);
    CHECK(memcmp(frame, target, sizeof frame) == 0);
    CHECK(memcmp(&musical_core, &saved_core, sizeof musical_core) == 0);
    CHECK(memcmp(&state, &before_state, sizeof state) == 0);
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    render(&ui, 63u, frame);
    render(&still, 63u, target);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    memcpy(before, frame, sizeof before);
    thy_studio_ui_set_navigation_animations(&ui, 1);
    CHECK(ui.navigation_animations_enabled && ui.animations_enabled);
    CHECK(thy_state_navigate(&state, THY_STATE_LEFT) == THY_STATE_OK);
    render(&ui, 64u, frame);
    CHECK(ui.transitioning && memcmp(frame, before, sizeof frame) == 0);
    render(&ui, 64u + THY_STUDIO_UI_TRANSITION_MS, frame);
    render(&still, 64u + THY_STUDIO_UI_TRANSITION_MS, target);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);
    thy_studio_ui_set_navigation_animations(NULL, 0);
    return 1;
}

static int test_navigation_motion_policy_preserves_play_graphics_and_global_reduced_motion(void)
{
    static thy_core saved_core;
    initialize();
    instrument.display_view = THY_VIEW_CHLADNI;
    thy_studio_ui_set_navigation_animations(&ui, 0);
    const int16_t note = 60;
    CHECK(thy_core_press(&musical_core, 0u, &note, 1u, 0, 0,
        THY_ROUTE_DIN, 96u) == THY_OK);
    saved_core = musical_core;
    before_state = state;
    render(&ui, 85u, before);
    render(&ui, 255u, frame);
    CHECK(memcmp(before, frame, sizeof frame) != 0);
    CHECK(!ui.transitioning && !ui.navigation_animations_enabled && ui.animations_enabled);
    CHECK(memcmp(&musical_core, &saved_core, sizeof musical_core) == 0);
    CHECK(memcmp(&state, &before_state, sizeof state) == 0);
    ui.animations_enabled = 0u;
    render(&ui, 300u, before);
    render(&ui, 4000u, frame);
    CHECK(memcmp(before, frame, sizeof frame) == 0);
    CHECK(thy_core_release(&musical_core, 0u) == THY_OK);
    return 1;
}

static int test_navigation_motion_policy_preserves_startup_reveal_and_handoff(void)
{
    initialize();
    thy_studio_ui_set_navigation_animations(&ui, 0);
    before_state = state;
    thy_studio_ui_boot_begin(&ui, 0u);
    CHECK(!ui.navigation_animations_enabled && ui.animations_enabled);
    render(&ui, 0u, before);
    advance(&ui, 210u, frame);
    CHECK(memcmp(before, frame, sizeof frame) != 0);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_REVEAL && ui.boot_elapsed_ms == 210u);
    thy_studio_ui_boot_ready(&ui);
    advance(&ui, THY_STUDIO_UI_BOOT_REVEAL_MS + THY_STUDIO_UI_BOOT_HANDOFF_MS / 2u, frame);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_HANDOFF);
    CHECK(ui.boot_handoff_elapsed_ms == THY_STUDIO_UI_BOOT_HANDOFF_MS / 2u);
    advance(&ui, THY_STUDIO_UI_BOOT_REVEAL_MS + THY_STUDIO_UI_BOOT_HANDOFF_MS, frame);
    render(&still, ui.last_render_ms, target);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_DONE && memcmp(frame, target, sizeof frame) == 0);
    CHECK(memcmp(&state, &before_state, sizeof state) == 0);
    CHECK(!ui.transitioning && !ui.navigation_animations_enabled && ui.animations_enabled);
    return 1;
}

static int read_scene_key_without_mutation(uint32_t *key)
{
    static thy_core saved_core;
    static thy_engine saved_engine;
    static thy_instrument saved_instrument;
    saved_core = musical_core;
    saved_engine = engine;
    saved_instrument = instrument;
    *key = thy_studio_ui_scene_key(&state);
    CHECK(memcmp(&musical_core, &saved_core, sizeof musical_core) == 0);
    CHECK(memcmp(&engine, &saved_engine, sizeof engine) == 0);
    CHECK(memcmp(&instrument, &saved_instrument, sizeof instrument) == 0);
    return 1;
}

static int test_scene_keys_distinguish_page_groups_but_ignore_cursor_motion(void)
{
    uint32_t play, groups, key, row_keys[THY_MENU_GROUP_COUNT];
    initialize();
    CHECK(read_scene_key_without_mutation(&play));
    CHECK(thy_studio_ui_scene_key(NULL) != play);
    thy_state_open(&state);
    CHECK(read_scene_key_without_mutation(&groups));
    CHECK(groups != play);
    for (unsigned step = 0u; step < THY_MENU_GROUP_COUNT; ++step) {
        CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
        CHECK(read_scene_key_without_mutation(&key));
        CHECK(key == groups); /* Selecting another category stays in Settings. */
    }
    for (uint8_t group = 0u; group < THY_MENU_GROUP_COUNT; ++group) {
        CHECK(thy_state_focus(&state, group, THY_STATE_NO_ROW) == THY_STATE_OK);
        CHECK(read_scene_key_without_mutation(&row_keys[group]));
        CHECK(row_keys[group] != play && row_keys[group] != groups);
        for (unsigned earlier = 0u; earlier < group; ++earlier)
            CHECK(row_keys[group] != row_keys[earlier]);
        CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
        CHECK(read_scene_key_without_mutation(&key));
        CHECK(key == row_keys[group]);
    }
    thy_state_play(&state);
    CHECK(read_scene_key_without_mutation(&key));
    CHECK(key == play); /* A remembered category does not change Play identity. */
    return 1;
}

static int test_scene_keys_ignore_live_owners_editor_drafts_and_committed_values(void)
{
    uint32_t play, rows, key;
    initialize();
    CHECK(read_scene_key_without_mutation(&play));
    const int16_t notes[] = {60,64,67};
    CHECK(thy_core_press(&musical_core, 0u, notes, 3u, 0, 0,
        THY_ROUTE_DIN | THY_ROUTE_USB0, 96u) == THY_OK);
    CHECK(read_scene_key_without_mutation(&key));
    CHECK(key == play);
    CHECK(thy_core_release(&musical_core, 0u) == THY_OK);
    CHECK(read_scene_key_without_mutation(&key));
    CHECK(key == play);
    uint16_t root = THY_ROW_PERFORMANCE_ROOT;
    CHECK(thy_state_focus(&state, thy_menu_rows[root].group, root) == THY_STATE_OK);
    CHECK(read_scene_key_without_mutation(&rows));
    CHECK(thy_state_begin_edit(&state, root) == THY_STATE_OK);
    CHECK(thy_state_edit_set(&state, 1) == THY_STATE_OK);
    CHECK(read_scene_key_without_mutation(&key));
    CHECK(key == rows);
    CHECK(thy_state_cancel(&state) == THY_STATE_OK);
    CHECK(read_scene_key_without_mutation(&key));
    CHECK(key == rows);
    CHECK(thy_state_begin_edit(&state, root) == THY_STATE_OK);
    CHECK(thy_state_edit_set(&state, 2) == THY_STATE_OK);
    CHECK(thy_state_commit(&state) == THY_STATE_OK);
    CHECK(read_scene_key_without_mutation(&key));
    CHECK(key == rows);
    CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
    CHECK(read_scene_key_without_mutation(&key));
    CHECK(key == rows);
    thy_state_play(&state);
    CHECK(read_scene_key_without_mutation(&key));
    CHECK(key == play);
    return 1;
}

static int test_scene_keys_identify_each_info_page_credits_quick_page_and_play_view(void)
{
    uint32_t information[THY_MENU_ROW_COUNT], count = 0u;
    uint32_t play, rows, key, credits, quick;
    initialize();
    CHECK(read_scene_key_without_mutation(&play));
    for (uint16_t row = 0u; row < THY_MENU_ROW_COUNT; ++row) {
        if (thy_menu_rows[row].type != THY_MENU_INFO || !thy_state_row_visible(&state, row)) continue;
        CHECK(thy_state_focus(&state, thy_menu_rows[row].group, row) == THY_STATE_OK);
        CHECK(read_scene_key_without_mutation(&rows));
        CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
        CHECK(state.info_open);
        CHECK(read_scene_key_without_mutation(&key));
        CHECK(key != rows && key != play);
        for (unsigned earlier = 0u; earlier < count; ++earlier)
            CHECK(key != information[earlier]);
        information[count++] = key;
        CHECK(thy_state_navigate(&state, THY_STATE_UP) == THY_STATE_OK);
        CHECK(thy_studio_ui_scene_key(&state) == key);
        CHECK(thy_state_navigate(&state, THY_STATE_LEFT) == THY_STATE_OK);
        CHECK(!state.info_open && thy_studio_ui_scene_key(&state) == rows);
    }
    CHECK(count >= 5u);
    uint16_t about = THY_ROW_SYSTEM_ABOUT;
    CHECK(thy_state_focus(&state, thy_menu_rows[about].group, about) == THY_STATE_OK);
    CHECK(read_scene_key_without_mutation(&rows));
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(state.about_open);
    CHECK(read_scene_key_without_mutation(&credits));
    CHECK(credits != rows && credits != play);
    for (unsigned earlier = 0u; earlier < count; ++earlier)
        CHECK(credits != information[earlier]);
    CHECK(thy_state_navigate(&state, THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(thy_studio_ui_scene_key(&state) == rows);
    CHECK(thy_engine_arp_quick_open(&engine) == THY_STATE_OK);
    CHECK(read_scene_key_without_mutation(&quick));
    CHECK(quick != play && quick != credits && quick != rows);
    CHECK(thy_state_set(&state, THY_ROW_ARPS_TARGET, 1) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine, THY_STATE_DOWN) == THY_STATE_OK);
    CHECK(read_scene_key_without_mutation(&key));
    CHECK(key == quick); /* Arp target and its quick cursor share one page. */
    CHECK(thy_engine_arp_quick_close(&engine) == THY_STATE_OK);
    CHECK(thy_studio_ui_scene_key(&state) == play);
    uint32_t views[THY_VIEW_COUNT], views_seen = 0u;
    for (uint8_t view = 0u; view < THY_VIEW_COUNT; ++view) {
        if (!thy_view_available(view)) continue;
        instrument.display_view = view;
        CHECK(read_scene_key_without_mutation(&key));
        CHECK(key != quick && key != credits && key != rows);
        for (unsigned earlier = 0u; earlier < views_seen; ++earlier)
            CHECK(key != views[earlier]);
        views[views_seen++] = key;
    }
    CHECK(views_seen == 7u);
    return 1;
}

static int test_scene_key_quick_page_precedence_matches_the_renderer(void)
{
    uint32_t quick, key;
    initialize();
    CHECK(thy_engine_arp_quick_open(&engine) == THY_STATE_OK);
    CHECK(read_scene_key_without_mutation(&quick));
    /* The action API can set About without first changing the quick page's
     * level. Both helpers must use the same visible-page precedence. */
    CHECK(thy_state_begin_edit(&state, THY_ROW_SYSTEM_ABOUT) == THY_STATE_OK);
    CHECK(state.about_open && engine.arp_quick_open && state.level == THY_STATE_PLAY);
    CHECK(read_scene_key_without_mutation(&key));
    CHECK(key == quick);
    render(&still, 0u, before);
    state.about_open = 0u;
    render(&still, 0u, frame);
    CHECK(memcmp(before, frame, sizeof frame) == 0);
    return 1;
}

static int test_live_play_views_follow_held_notes_routes_and_layout(void)
{
    initialize();
    instrument.display_view = 0u;
    render(&still, 0u, before);
    instrument.display_view = 1u;
    render(&still, 1u, frame);
    CHECK(memcmp(frame, before, sizeof frame) != 0);
    memcpy(before, frame, sizeof before);
    int16_t notes[] = {60, 64, 67, 73};
    CHECK(thy_core_press(&musical_core, 0u, notes, 4u, 0, 0,
        THY_ROUTE_DIN, 96) == THY_OK);
    render(&still, 2u, frame);
    CHECK(memcmp(frame, before, sizeof frame) != 0);
    CHECK(pixel_at(frame, 5u, 38u)); /* Actual sounding C has bright interior. */
    CHECK(pixel_at(frame, 16u, 25u)); /* Held C# black key lights. */
    CHECK(thy_core_release(&musical_core, 0u) == THY_OK);
    render(&still, 3u, frame);
    CHECK(memcmp(frame, before, sizeof frame) == 0);
    instrument.display_view = THY_VIEW_RETIRED_LAYOUTS;
    render(&still, 4u, before);
    instrument.performance.melody_layout = 5u;
    render(&still, 5u, frame);
    CHECK(memcmp(frame, before, sizeof frame) == 0); /* Retired IDs fall back. */
    instrument.display_view = THY_VIEW_RETIRED_ROUTES;
    render(&still, 6u, before);
    instrument.display_config[THY_VIEW_DETAILED].detail_rows[0] = THY_VIEW_INFO_CHANNELS;
    instrument.channels[0] = 15u;
    instrument.destinations[1] = THY_ROUTE_USB1 | THY_ROUTE_DIN;
    render(&still, 7u, frame);
    CHECK(memcmp(frame, before, sizeof frame) != 0);
    CHECK(!still.transitioning);
    return 1;
}

static int test_live_navigation_and_oled_share_visible_canonical_rows(void)
{
    initialize();
    instrument.display_view = THY_VIEW_CHLADNI; /* Slots and Motion visible. */
    CHECK(thy_state_set_design_mode(&state, 0u) == THY_STATE_OK);
    CHECK(!thy_state_row_visible(&state, THY_STATE_NO_ROW));
    unsigned empty_groups = 0u;
    for (uint8_t group_index = 0u; group_index < THY_MENU_GROUP_COUNT; ++group_index) {
        const thy_menu_group *group = &thy_menu_groups[group_index];
        uint16_t rows[THY_MENU_ROW_COUNT];
        unsigned count = 0u;
        for (unsigned i = 0u; i < group->row_count; ++i) {
            uint16_t row = (uint16_t)(group->first_row + i);
            int expected = (thy_menu_rows[row].status == THY_MENU_LIVE ||
                (group_index == thy_menu_rows[THY_ROW_ENGINES_PLAY].group &&
                thy_menu_rows[row].type == THY_MENU_INFO)) &&
                row != THY_ROW_CHORDS_OCTAVESTACKS;
            if (row == THY_ROW_SEQUENCER_STEPLENGTH || row == THY_ROW_SEQUENCER_STEPVELOCITY) expected = 0;
            if (group_index == thy_menu_rows[THY_ROW_SEQUENCER_OPEN].group) {
                /* The global page is a small hub, not a selected-track editor.
                 * Track options appear only from an explicit track canvas. */
                expected = row == THY_ROW_SEQUENCER_OPEN || row == THY_ROW_SEQUENCER_HELP ||
                    row == THY_ROW_SEQUENCER_TEMPO || row == THY_ROW_SEQUENCER_LAUNCH ||
                    row == THY_ROW_SEQUENCER_CLEARSONG || row == THY_ROW_SEQUENCER_SAVE ||
                    row == THY_ROW_SEQUENCER_RECALL || row == THY_ROW_SEQUENCER_UNDO ||
                    row == THY_ROW_SEQUENCER_REDO || row==THY_ROW_SEQUENCER_RESETOPTIONS;
            }
            if (row >= THY_ROW_VIEWS_DETAIL1 && row <= THY_ROW_VIEWS_DETAIL6) expected = 0;
            if (row == THY_ROW_SENSITIVITY_INHERIT) expected = 0;
            if (row == THY_ROW_PERFORMANCE_AFTERTOUCH ||
                row == THY_ROW_PERFORMANCE_VELOCITYSOURCE) expected = 0;
            /* Harmony now owns exploration; the former Chords entry remains
             * an internal identity rather than a second visible path. */
            if (row == THY_ROW_CHORDS_GUIDANCE) expected = 0;
            if (row == THY_ROW_HARMONY_OPEN || row == THY_ROW_HARMONY_PATTERN ||
                row == THY_ROW_HARMONY_SUGGESTIONS || row == THY_ROW_HARMONY_DISCOVERY ||
                row == THY_ROW_HARMONY_PREVIEW || row == THY_ROW_HARMONY_MANUAL ||
                row == THY_ROW_HARMONY_BANK || row == THY_ROW_HARMONY_OCTAVETARGET) expected = 0;
            /* The factory Classic/prefix-up context has no quality selector
             * and no drop switches. Their live availability is tested below. */
            if (row == THY_ROW_CHORDS_QUALITY ||
                (row >= THY_ROW_CHORDS_DROP2 && row <= THY_ROW_CHORDS_DROP4)) expected = 0;
            CHECK(thy_state_row_visible(&state, row) == expected);
            if (expected) rows[count++] = row;
        }
        CHECK(thy_state_focus(&state, group_index, THY_STATE_NO_ROW) == THY_STATE_OK);
        render(&still, group_index, frame);
        if (!count) {
            ++empty_groups;
            uint8_t stored = state.cursors[group_index];
            CHECK(thy_state_current_row(&state) == THY_STATE_NO_ROW);
            CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
            CHECK(!state.editing);
            CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
            CHECK(state.cursors[group_index] == stored);
            memset(target, 0, sizeof target);
            thy_ui_draw_text(target, 2, 22, "FUTURE ENGINE PENDING", 25, 0);
            for (unsigned y = 22u; y < 27u; ++y)
                for (unsigned x = 2u; x < 102u; ++x)
                    CHECK(pixel_at(frame, x, y) == pixel_at(target, x, y));
            continue;
        }
        CHECK(thy_state_current_row(&state) == rows[0]);
        for (unsigned i = 0u; i < count; ++i) {
            CHECK(thy_state_current_row(&state) == rows[i]);
            CHECK(state.cursors[group_index] == rows[i] - group->first_row);
            CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
        }
        CHECK(thy_state_current_row(&state) == rows[0]);
        CHECK(thy_state_navigate(&state, THY_STATE_UP) == THY_STATE_OK);
        CHECK(thy_state_current_row(&state) == rows[count - 1u]);
        CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
        /* The five visible labels are packed without holes left by future
         * rows. Font primitives are the independent pixel oracle here. */
        unsigned section_offset = 0u;
        for (unsigned i = 0u; i < count && i < 5u; ++i) {
            if (i && thy_menu_rows[rows[i]].advanced && !thy_menu_rows[rows[i - 1u]].advanced)
                section_offset = 2u;
            unsigned y = 13u + i * 8u + section_offset;
            memset(target, 0, sizeof target);
            unsigned label_x = thy_state_reset_row(rows[i]) ||
                (thy_menu_rows[rows[i]].group == thy_menu_rows[THY_ROW_ENGINES_PLAY].group) ? 19u : 8u;
            thy_ui_draw_text(target, label_x, y, thy_menu_rows[rows[i]].short_label,
                label_x == 19u ? 13u : 15u, 0);
            for (unsigned dy = y; dy < y + 5u; ++dy)
                for (unsigned x = label_x; x < 83u; ++x)
                    CHECK(pixel_at(frame, x, dy) == pixel_at(target, x, dy));
        }
    }
    CHECK(empty_groups == 0u); /* Every settings group now ends in its reset action. */
    uint16_t hidden = thy_state_find_row("arps.releaseTail");
    CHECK(thy_state_focus(&state, thy_menu_rows[THY_ROW_PERFORMANCE_VELOCITY].group,
        THY_ROW_PERFORMANCE_VELOCITY) == THY_STATE_OK);
    CHECK(thy_state_focus(&state, thy_menu_rows[hidden].group, hidden) == THY_STATE_UNAVAILABLE);
    CHECK(thy_state_current_row(&state) == THY_ROW_PERFORMANCE_VELOCITY);
    CHECK(thy_state_set_design_mode(&state, 1u) == THY_STATE_OK);
    CHECK(thy_state_row_visible(&state, hidden));
    CHECK(thy_state_focus(&state, thy_menu_rows[hidden].group, hidden) == THY_STATE_OK);
    CHECK(thy_state_current_row(&state) == hidden);
    return 1;
}

static int test_chord_controls_follow_layout_and_voicing_context(void)
{
    initialize();
    CHECK(thy_state_set_design_mode(&state, 0u) == THY_STATE_OK);
    CHECK(!state.design_editing && instrument.chord_layout == THY_CHORD_LAYOUT_CLASSIC);
    CHECK(!thy_state_row_visible(&state, THY_ROW_CHORDS_QUALITY));
    CHECK(thy_state_row_visible(&state, THY_ROW_CHORDS_INVERSION));
    for (uint16_t row = THY_ROW_CHORDS_DROP2; row <= THY_ROW_CHORDS_DROP4; ++row)
        CHECK(!thy_state_row_visible(&state, row));
    CHECK(thy_state_set(&state, THY_ROW_CHORDS_LAYOUT, THY_CHORD_LAYOUT_CHROMATIC) == THY_STATE_OK);
    CHECK(thy_state_row_visible(&state, THY_ROW_CHORDS_QUALITY));
    CHECK(thy_state_focus(&state, thy_menu_rows[THY_ROW_CHORDS_LAYOUT].group,
        THY_ROW_CHORDS_LAYOUT) == THY_STATE_OK);
    CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
    CHECK(thy_state_current_row(&state) == THY_ROW_CHORDS_QUALITY);
    CHECK(thy_state_set(&state, THY_ROW_CHORDS_LAYOUT, THY_CHORD_LAYOUT_DIATONIC) == THY_STATE_OK);
    CHECK(!thy_state_row_visible(&state, THY_ROW_CHORDS_QUALITY));
    CHECK(thy_state_set(&state, THY_ROW_CHORDS_VOICINGMODE, 1) == THY_STATE_OK);
    CHECK(!thy_state_row_visible(&state, THY_ROW_CHORDS_INVERSION));
    for (uint16_t row = THY_ROW_CHORDS_DROP2; row <= THY_ROW_CHORDS_DROP4; ++row) {
        CHECK(thy_state_row_visible(&state, row));
        CHECK(thy_state_focus(&state, thy_menu_rows[row].group, row) == THY_STATE_OK);
        CHECK(thy_state_current_row(&state) == row);
        render(&still, (uint32_t)row, frame);
        unsigned label_visible = 0u;
        for (unsigned y = 13u; y <= 55u; ++y) {
            memset(target, 0, sizeof target);
            thy_ui_draw_text(target, 8u, y, thy_menu_rows[row].short_label, 15u, 0);
            unsigned match = 1u;
            for (unsigned dy = y; dy < y + 5u; ++dy)
                for (unsigned x = 8u; x < 83u; ++x)
                    if (pixel_at(frame, x, dy) != pixel_at(target, x, dy)) match = 0u;
            if (match) label_visible = 1u;
        }
        CHECK(label_visible);
    }
    CHECK(thy_state_set(&state, THY_ROW_CHORDS_VOICINGMODE, 0) == THY_STATE_OK);
    CHECK(thy_state_row_visible(&state, THY_ROW_CHORDS_INVERSION));
    for (uint16_t row = THY_ROW_CHORDS_DROP2; row <= THY_ROW_CHORDS_DROP4; ++row)
        CHECK(!thy_state_row_visible(&state, row));
    CHECK(thy_state_set_design_mode(&state, 1u) == THY_STATE_OK);
    CHECK(thy_state_row_visible(&state, THY_ROW_CHORDS_QUALITY));
    CHECK(thy_state_row_visible(&state, THY_ROW_CHORDS_INVERSION));
    for (uint16_t row = THY_ROW_CHORDS_DROP2; row <= THY_ROW_CHORDS_DROP4; ++row)
        CHECK(thy_state_row_visible(&state, row));
    CHECK(thy_core_queued(&musical_core) == 0u && thy_core_sounding(&musical_core) == 0u);
    return 1;
}

static int test_information_rows_show_read_only_text_without_service_errors(void)
{
    static thy_core held_core;
    static thy_instrument held_instrument;
    unsigned checked[2] = {0u, 0u}, all_information = 0u;
    for (uint16_t row = 0u; row < THY_MENU_ROW_COUNT; ++row)
        if (thy_menu_rows[row].type == THY_MENU_INFO) ++all_information;
    for (uint8_t design = 0u; design <= 1u; ++design) {
        initialize();
        CHECK(thy_state_set_design_mode(&state, design) == THY_STATE_OK);
        const int16_t notes[] = {60, 64, 67};
        CHECK(thy_core_press(&musical_core, 0u, notes, 3u, 0u, 0u,
            THY_ROUTE_DIN | THY_ROUTE_USB0, 96u) == THY_OK);
        held_core = musical_core;
        held_instrument = instrument;
        for (uint16_t row = 0u; row < THY_MENU_ROW_COUNT; ++row) {
            if (thy_menu_rows[row].type != THY_MENU_INFO) continue;
            if (!thy_state_row_visible(&state, row)) {
                CHECK(!design);
                CHECK(thy_state_focus(&state, thy_menu_rows[row].group, row) == THY_STATE_UNAVAILABLE);
                continue;
            }
            CHECK(thy_state_focus(&state, thy_menu_rows[row].group, row) == THY_STATE_OK);
            CHECK(!state.info_open);
            const uint32_t revision = state.revision;
            const uint8_t history = state.history_count;
            before_state = state;
            render(&still, 0u, before);
            CHECK(memcmp(&state, &before_state, sizeof state) == 0);
            CHECK(matches_text(before, 2u, 57u, thy_menu_rows[row].group ==
                thy_menu_rows[THY_ROW_ENGINES_PLAY].group ? "> OPEN  O EXIT  < BACK" : "> INFO O EXIT < BACK", 25u));
            char value[32];
            thy_studio_ui_format(&state, row, 0, value);
            CHECK(strcmp(value, thy_menu_rows[row].group ==
                thy_menu_rows[THY_ROW_ENGINES_PLAY].group ? "Open" : "INFO") == 0);
            CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
            CHECK(state.info_open && !state.editing && state.notice == THY_NOTICE_NONE);
            before_state = state;
            render(&still, 1u, frame);
            CHECK(memcmp(&state, &before_state, sizeof state) == 0);
            CHECK(memcmp(before, frame, sizeof frame) != 0);
            if(row==THY_ROW_SENSITIVITY_CUSTOMCURVE) CHECK(matches_text(frame,2u,13u,"AFTERTOUCH",12u));
            else if(row==THY_ROW_SENSITIVITY_SCOPE) CHECK(matches_text(frame,2u,13u,"LEFT: CLUSTER DEFAULTS",25u));
            else if(row==THY_ROW_SYSTEM_RESOURCES) CHECK(matches_text(frame,2u,20u,"DEVICE METRICS UNAVAILABLE",25u));
            else if(row==THY_ROW_SYSTEM_STORAGE) CHECK(matches_text(frame,2u,13u,"STATE ",6u));
            else CHECK(matches_text(frame, 2u, 13u, thy_menu_rows[row].short_label, 25u));
            if(row==THY_ROW_SYSTEM_RESOURCES){
                CHECK(matches_text(frame,12u,1u,"RESOURCES",9u));
                CHECK(matches_text(frame,2u,58u,"O EXIT  < BACK",25u));
            }else CHECK(matches_text(frame, 2u, 57u, "READ ONLY O EXIT < BACK", 25u));
            if (row == thy_state_find_row("midi.remoteEcho"))
                CHECK(matches_text(frame, 2u, 25u, "No automatic echo / THRU", 25u));
            memcpy(target, frame, sizeof target);
            const uint8_t ignored[] = {THY_STATE_UP, THY_STATE_DOWN, THY_STATE_RIGHT};
            for (unsigned action = 0u; action < sizeof ignored / sizeof ignored[0]; ++action) {
                CHECK(thy_state_navigate(&state, ignored[action]) == THY_STATE_OK);
                CHECK(state.info_open && thy_state_current_row(&state) == row);
                render(&still, 2u, frame);
                CHECK(memcmp(target, frame, sizeof frame) == 0);
            }
            CHECK(thy_state_navigate(&state, THY_STATE_LEFT) == THY_STATE_OK);
            CHECK(!state.info_open && thy_state_current_row(&state) == row);
            render(&still, 3u, frame);
            CHECK(memcmp(before, frame, sizeof frame) == 0);
            CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
            CHECK(thy_state_navigate(&state, THY_STATE_SELECT) == THY_STATE_OK);
            CHECK(state.level == THY_STATE_PLAY && !state.info_open);
            CHECK(state.revision == revision && state.history_count == history);
            CHECK(memcmp(&musical_core, &held_core, sizeof musical_core) == 0);
            CHECK(memcmp(&instrument, &held_instrument, sizeof instrument) == 0);
            ++checked[design];
        }
    }
    CHECK(checked[0] >= 4u && checked[0] < all_information);
    CHECK(checked[1] == all_information);
    return 1;
}

static int test_route_overview_follows_all_live_routing_profiles(void)
{
    initialize();
    instrument.display_view = THY_VIEW_DETAILED;
    instrument.display_config[THY_VIEW_DETAILED].detail_rows[0] = THY_VIEW_INFO_CHANNELS;
    instrument.display_config[THY_VIEW_DETAILED].detail_rows[1] = THY_VIEW_INFO_OUTPUTS;
    CHECK(thy_instrument_set_pad_route(&instrument, 0u, 15u, THY_ROUTE_DIN) == THY_OK);
    CHECK(thy_instrument_set_routing_profile(&instrument, THY_ROUTING_WHOLE) == THY_OK);
    render(&still, 0u, frame);
    char line[32];
    thy_studio_ui_view_line(&state, THY_VIEW_INFO_CHANNELS, "MAJOR C", 1, 1, line);
    CHECK(strcmp(line, "CH 16") == 0);
    CHECK(matches_text(frame, 2u, 12u, line, 21u));
    thy_studio_ui_view_line(&state, THY_VIEW_INFO_OUTPUTS, "MAJOR C", 1, 1, line);
    CHECK(strcmp(line, "OUT L:D R:D") == 0);
    CHECK(matches_text(frame, 2u, 20u, line, 21u));
    CHECK(thy_instrument_set_routing_profile(&instrument, THY_ROUTING_ZONES) == THY_OK);
    for (uint8_t zone = 0u; zone < 6u; ++zone)
        CHECK(thy_instrument_set_pad_route(&instrument, (uint8_t)(3u + zone), zone,
            THY_ROUTE_USB0 | THY_ROUTE_DIN) == THY_OK);
    render(&still, 1u, frame);
    thy_studio_ui_view_line(&state, THY_VIEW_INFO_CHANNELS, "MAJOR C", 1, 1, line);
    CHECK(strcmp(line, "CH L1/2/3 R4/5/6") == 0);
    CHECK(matches_text(frame, 2u, 12u, line, 21u));
    thy_studio_ui_view_line(&state, THY_VIEW_INFO_OUTPUTS, "MAJOR C", 1, 1, line);
    CHECK(strcmp(line, "OUT* L:U1D R:U1D") == 0);
    CHECK(matches_text(frame, 2u, 20u, line, 21u));
    return 1;
}

static int test_boot_before_state_has_real_milestones_and_ready_gate(void)
{
    initialize();
    struct { uint8_t prefix[16], pixels[THY_UI_FRAME_BYTES], suffix[16]; } guarded;
    memset(&guarded, 0xa5, sizeof guarded);
    thy_studio_ui_boot_begin(&ui, 0u);
    CHECK(thy_studio_ui_boot_active(&ui));
    CHECK(ui.boot_stage == THY_STUDIO_BOOT_START && !ui.boot_ready);
    thy_studio_ui_render(&ui, NULL, NULL, 0, 0, 0u, before);
    thy_studio_ui_boot_progress(&ui, THY_STUDIO_BOOT_DISPLAY);
    thy_studio_ui_render(&ui, NULL, NULL, 0, 0, 35u, frame);
    CHECK(memcmp(before, frame, sizeof frame) != 0);
    CHECK(ui.boot_stage == THY_STUDIO_BOOT_DISPLAY);
    thy_studio_ui_boot_progress(&ui, THY_STUDIO_BOOT_START); /* Cannot regress. */
    thy_studio_ui_boot_progress(&ui, 255u); /* Invalid input cannot index prose. */
    CHECK(ui.boot_stage == THY_STUDIO_BOOT_DISPLAY);
    thy_studio_ui_boot_progress(&ui, THY_STUDIO_BOOT_MIDI);
    for (uint32_t now = 70u; now <= 1400u; now += 35u)
        thy_studio_ui_render(&ui, NULL, NULL, 0, 0, now, guarded.pixels);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_WAIT && !ui.boot_ready);
    CHECK(ui.boot_elapsed_ms == THY_STUDIO_UI_BOOT_REVEAL_MS);
    /* Completed pixels are stable while hardware has not reported ready. */
    memcpy(before, guarded.pixels, sizeof before);
    thy_studio_ui_render(&ui, NULL, NULL, 0, 0, 1435u, guarded.pixels);
    CHECK(memcmp(before, guarded.pixels, sizeof before) == 0);
    thy_studio_ui_boot_ready(&ui);
    for (uint32_t now = 1470u; now <= 2030u; now += 35u)
        thy_studio_ui_render(&ui, NULL, NULL, 0, 0, now, guarded.pixels);
    CHECK(ui.boot_ready && ui.boot_phase == THY_STUDIO_BOOT_WAIT);
    CHECK(thy_studio_ui_boot_active(&ui)); /* No Play state, no completion. */
    for (unsigned i = 0u; i < 16u; ++i) {
        CHECK(guarded.prefix[i] == 0xa5u && guarded.suffix[i] == 0xa5u);
    }
    /* Every supplied pixel, including blank separation around the three
     * central sixes, survives unchanged at its native 48x40 resolution. */
    CHECK(sizeof thy_brand_logo_bits==240u);
    for(unsigned y=0u;y<THY_BRAND_LOGO_HEIGHT;++y)
        for(unsigned x=0u;x<THY_BRAND_LOGO_WIDTH;++x)
            CHECK(pixel_at(guarded.pixels,x+4u,y+12u)==
                !!(thy_brand_logo_bits[y*THY_BRAND_LOGO_STRIDE+x/8u]&(1u<<(7u-x%8u))));
    CHECK(pixel_at(guarded.pixels, 70u, 13u));
    CHECK(!pixel_at(guarded.pixels, 70u, 15u));
    for (unsigned glyph = 0u; glyph < 2u; ++glyph)
        for (unsigned y = 13u; y < 20u; ++y)
            for (unsigned x = 94u; x < 99u; ++x)
                CHECK(pixel_at(guarded.pixels, x, y) ==
                    pixel_at(guarded.pixels, x + 8u * (glyph + 1u), y));
    thy_studio_ui_boot_begin(NULL, 0u);
    thy_studio_ui_boot_progress(NULL, 255u);
    thy_studio_ui_boot_ready(NULL);
    thy_studio_ui_boot_skip(NULL);
    CHECK(!thy_studio_ui_boot_active(NULL));
    return 1;
}

static int test_boot_handoff_has_exact_endpoints_and_current_play(void)
{
    initialize();
    before_state = state;
    thy_studio_ui_boot_begin(&ui, 0u);
    thy_studio_ui_boot_progress(&ui, THY_STUDIO_BOOT_MIDI);
    thy_studio_ui_boot_ready(&ui);
    advance(&ui, THY_STUDIO_UI_BOOT_REVEAL_MS - 35u, before);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_REVEAL);
    render(&ui, THY_STUDIO_UI_BOOT_REVEAL_MS, frame);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_HANDOFF);
    CHECK(memcmp(before, frame, sizeof before) == 0); /* No first-frame jump. */
    render(&still, THY_STUDIO_UI_BOOT_REVEAL_MS, target);
    advance(&ui, THY_STUDIO_UI_BOOT_REVEAL_MS + THY_STUDIO_UI_BOOT_HANDOFF_MS / 2u, frame);
    CHECK(ui.boot_handoff_elapsed_ms == THY_STUDIO_UI_BOOT_HANDOFF_MS / 2u);
    for (unsigned y = 0u; y < THY_UI_HEIGHT; ++y)
        for (unsigned x = 0u; x < THY_UI_WIDTH; ++x)
            CHECK(pixel_at(frame, x, y) == (y < 32u ?
                pixel_at(before, x, y + 32u) : pixel_at(target, x, y - 32u)));
    /* Handoff targets current state, not a cached initial Play screenshot. */
    instrument.display_view = 3u;
    advance(&ui, THY_STUDIO_UI_BOOT_REVEAL_MS + THY_STUDIO_UI_BOOT_HANDOFF_MS, frame);
    render(&still, ui.last_render_ms, target);
    CHECK(!thy_studio_ui_boot_active(&ui) && ui.boot_phase == THY_STUDIO_BOOT_DONE);
    CHECK(memcmp(frame, target, sizeof frame) == 0 && !ui.transitioning);
    CHECK(memcmp(&state, &before_state, sizeof state) == 0);
    render(&ui, ui.last_render_ms + 35u, frame);
    CHECK(memcmp(frame, target, sizeof frame) == 0); /* Never restarts itself. */
    CHECK(musical_core.queue_count == 0u && musical_core.sounding_count == 0u);
    return 1;
}

static int test_boot_reduced_motion_and_missing_state_are_safe(void)
{
    initialize();
    ui.animations_enabled = 0u;
    thy_studio_ui_boot_begin(&ui, 0u);
    CHECK(!ui.animations_enabled); /* Replay preserves caller preference. */
    render(&ui, 0u, before);
    render(&ui, 35u, frame);
    CHECK(memcmp(before, frame, sizeof frame) == 0); /* Static brand. */
    thy_studio_ui_boot_ready(&ui);
    render(&ui, 36u, frame);
    render(&still, 36u, target);
    CHECK(!thy_studio_ui_boot_active(&ui) && ui.boot_phase == THY_STUDIO_BOOT_DONE);
    CHECK(memcmp(frame, target, sizeof frame) == 0);

    ui.animations_enabled = 1u;
    thy_studio_ui_boot_begin(&ui, 40u);
    thy_studio_ui_boot_ready(&ui);
    advance(&ui, 40u + THY_STUDIO_UI_BOOT_REVEAL_MS + 70u, frame);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_HANDOFF);
    CHECK(thy_state_set(&state, THY_ROW_LIGHTS_ANIMATIONS, 0) == THY_STATE_OK);
    render(&ui, ui.last_render_ms + 1u, frame);
    render(&still, ui.last_render_ms, target);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_DONE && memcmp(frame, target, sizeof frame) == 0);

    CHECK(thy_state_set(&state, THY_ROW_LIGHTS_ANIMATIONS, 1) == THY_STATE_OK);
    thy_studio_ui_boot_begin(&ui, 2000u);
    thy_studio_ui_boot_ready(&ui);
    advance(&ui, 2000u + THY_STUDIO_UI_BOOT_REVEAL_MS + 35u, frame);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_HANDOFF);
    thy_studio_ui_render(&ui, NULL, NULL, 0, 0, ui.last_render_ms + 35u, frame);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_WAIT && thy_studio_ui_boot_active(&ui));
    CHECK(ui.boot_handoff_elapsed_ms == 0u);
    return 1;
}

static int test_boot_input_never_delays_menus_or_note_ownership(void)
{
    initialize();
    thy_studio_ui_boot_begin(&ui, 0u);
    render(&ui, 0u, before);
    thy_state_open(&state);
    render(&ui, 1u, frame);
    render(&still, 1u, target);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_SKIPPED && !ui.transitioning);
    CHECK(memcmp(frame, target, sizeof frame) == 0);
    CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
    render(&ui, 2u, frame);
    render(&still, 2u, target);
    CHECK(memcmp(frame, target, sizeof frame) == 0 && !ui.transitioning);

    initialize();
    thy_studio_ui_boot_begin(&ui, 0u);
    thy_studio_ui_boot_ready(&ui);
    advance(&ui, THY_STUDIO_UI_BOOT_REVEAL_MS + 35u, frame);
    const int16_t note = 60;
    CHECK(thy_core_press(&musical_core, 95u, &note, 1u, 0, 0,
        THY_ROUTE_DIN | THY_ROUTE_USB0, 96u) == THY_OK);
    uint16_t queued = musical_core.queue_count;
    uint16_t sounding = musical_core.sounding_count;
    render(&ui, ui.last_render_ms + 1u, frame);
    render(&still, ui.last_render_ms, target);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_SKIPPED);
    CHECK(memcmp(frame, target, sizeof frame) == 0);
    CHECK(musical_core.owners[95].held && musical_core.queue_count == queued &&
        musical_core.sounding_count == sounding);
    CHECK(thy_core_release(&musical_core, 95u) == THY_OK);
    CHECK(!musical_core.owners[95].held && musical_core.sounding_count == 0u);

    thy_studio_ui_boot_begin(&ui, 2000u);
    thy_studio_ui_boot_skip(&ui); /* Early raw control, silent pad or fault. */
    thy_studio_ui_boot_ready(&ui);
    render(&ui, 2001u, frame);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_SKIPPED && !thy_studio_ui_boot_active(&ui));
    return 1;
}

static int test_boot_wrap_and_stalls_advance_one_completed_frame(void)
{
    initialize();
    thy_studio_ui_boot_begin(&ui, 0xffffffe0u);
    render(&ui, 0xffffffe0u, before);
    render(&ui, 0x03u, frame); /* 35 ms across the wrap, not a huge gap. */
    CHECK(ui.boot_elapsed_ms == 35u);
    uint32_t now = 0x03u;
    unsigned calls = 0u;
    while (ui.boot_elapsed_ms < THY_STUDIO_UI_BOOT_REVEAL_MS) {
        uint32_t previous_elapsed = ui.boot_elapsed_ms;
        now += 1000u;
        render(&ui, now, frame);
        CHECK(ui.boot_elapsed_ms - previous_elapsed <= THY_STUDIO_UI_MAX_STEP_MS);
        CHECK(++calls <= 21u);
    }
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_WAIT);
    thy_studio_ui_boot_ready(&ui);
    memcpy(before, frame, sizeof before);
    now += 1000u;
    render(&ui, now, frame);
    CHECK(ui.boot_phase == THY_STUDIO_BOOT_HANDOFF && ui.boot_handoff_elapsed_ms == 0u);
    CHECK(memcmp(before, frame, sizeof before) == 0);
    for (unsigned i = 1u; i <= 7u; ++i) {
        now += 1000u;
        render(&ui, now, frame);
        CHECK(ui.boot_handoff_elapsed_ms == (i < 7u ? i * THY_STUDIO_UI_MAX_STEP_MS :
            THY_STUDIO_UI_BOOT_HANDOFF_MS));
        CHECK(thy_studio_ui_boot_active(&ui) == (i < 7u));
    }
    render(&still, now, target);
    CHECK(memcmp(frame, target, sizeof frame) == 0);
    return 1;
}

static int matches_text(const uint8_t *actual, unsigned x, unsigned y,
    const char *text, unsigned limit)
{
    memset(target, 0, sizeof target);
    thy_ui_draw_text(target, x, y, text, limit, 0);
    for (unsigned row = y; row < y + 5u; ++row)
        for (unsigned col = x; col < x + limit * 5u && col < THY_UI_WIDTH; ++col)
            CHECK(pixel_at(actual, col, row) == pixel_at(target, col, row));
    return 1;
}

static int test_navigation_legends_and_center_escape_from_editor(void)
{
    initialize();
    thy_state_open(&state);
    render(&still, 0u, frame);
    CHECK(matches_text(frame, 2u, 57u, "> OPEN  O EXIT", 25u));

    uint16_t row = THY_ROW_PERFORMANCE_ROOT;
    CHECK(thy_state_focus(&state, thy_menu_rows[row].group, row) == THY_STATE_OK);
    render(&ui, 0u, before);
    CHECK(matches_text(before, 2u, 57u, "> EDIT  O EXIT  < BACK", 25u));
    int32_t original;
    CHECK(thy_state_get(&state, row, &original) == THY_STATE_OK);
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(thy_state_navigate(&state, THY_STATE_UP) == THY_STATE_OK);
    render(&ui, 1u, frame);
    CHECK(!ui.transitioning);
    CHECK(matches_text(frame, 2u, 57u, "> APPLY O EXIT < CANCEL", 25u));
    CHECK(state.edit_value != original);
    /* CENTER is an immediate escape, even with an uncommitted value. It
     * cancels that editor and starts the ordinary outward hierarchy slide. */
    CHECK(thy_state_navigate(&state, THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(state.level == THY_STATE_PLAY && !state.editing);
    int32_t current;
    CHECK(thy_state_get(&state, row, &current) == THY_STATE_OK);
    CHECK(current == original);
    render(&ui, 2u, frame);
    CHECK(ui.transitioning && ui.transition_direction == 1);
    advance(&ui, 2u + THY_STUDIO_UI_TRANSITION_MS, frame);
    render(&still, ui.last_render_ms, target);
    CHECK(!ui.transitioning && memcmp(frame, target, sizeof frame) == 0);

    CHECK(thy_state_navigate(&state, THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(state.level == THY_STATE_GROUPS);
    CHECK(thy_state_focus(&state, thy_menu_rows[row].group, row) == THY_STATE_OK);
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(thy_state_navigate(&state, THY_STATE_UP) == THY_STATE_OK);
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(!state.editing && state.notice == THY_NOTICE_APPLIED);
    render(&still, 500u, frame);
    CHECK(matches_text(frame, 2u, 57u, "APPLIED  O EXIT", 25u));

    /* Sticky feedback must never hide the one-touch Play escape. The
     * independent strings also verify the whole legend fits 25 glyphs. */
    static const struct { uint8_t notice; const char *text; } notices[] = {
        {THY_NOTICE_DRAFT, "CONFIG ONLY  O EXIT"},
        {THY_NOTICE_INVALID, "INVALID  O EXIT"},
        {THY_NOTICE_UNAVAILABLE, "UNAVAILABLE  O EXIT"},
        {THY_NOTICE_ADAPTER, "RELEASE PADS RETRY O EXIT"},
        {THY_NOTICE_CANCELLED, "CANCELLED  O EXIT"},
        {THY_NOTICE_RANGE, "OUT OF RANGE  O EXIT"},
        {THY_NOTICE_REMOTE_RANGE, "LOW NOTE > HIGH  O EXIT"},
        {THY_NOTICE_PROGRAM_TRIGGER, "PC NEEDS PRESS  O EXIT"},
        {THY_NOTICE_SENSITIVITY, "CHECK LIMITS  O EXIT"},
        {THY_NOTICE_TEXT, "INVALID TEXT  O EXIT"},
        {THY_NOTICE_RELEASE, "RELEASE ALL PADS  O EXIT"}
    };
    for (unsigned i = 0u; i < sizeof notices / sizeof notices[0]; ++i) {
        state.notice = notices[i].notice;
        render(&still, 501u + i, frame);
        CHECK(strlen(notices[i].text) <= 25u);
        CHECK(matches_text(frame, 2u, 57u, notices[i].text, 25u));
    }
    return 1;
}

static int test_compact_views_show_effective_routes_and_optional_counts(void)
{
    initialize();
    CHECK(thy_instrument_set_route_velocity(&instrument, 1u, 94u) == THY_OK);
    CHECK(thy_instrument_set_route_velocity(&instrument, 2u, 98u) == THY_OK);
    instrument.display_view = 4u;
    instrument.display_config[THY_VIEW_SIMPLE].slots[0] = THY_VIEW_INFO_VELOCITY;
    render(&still, 0u, frame);
    CHECK(matches_text(frame, 2u, 48u, "VEL L94 R98", 25u));
    CHECK(matches_text(frame, 2u, 58u, "PADS 0  NOTES 0", 25u));
    CHECK(thy_instrument_set_routing_profile(&instrument, THY_ROUTING_WHOLE) == THY_OK);
    CHECK(thy_instrument_set_route_velocity(&instrument, 0u, 112u) == THY_OK);
    render(&still, 1u, frame);
    CHECK(matches_text(frame, 2u, 48u, "VEL 112", 25u));
    CHECK(thy_instrument_set_routing_profile(&instrument, THY_ROUTING_ZONES) == THY_OK);
    for (uint8_t route = 3u; route < 9u; ++route)
        CHECK(thy_instrument_set_route_velocity(&instrument, route,
            (uint8_t)(91u + route)) == THY_OK);
    render(&still, 2u, frame);
    CHECK(matches_text(frame, 2u, 48u, "V94/95/96|97/98/99", 25u));
    const int16_t chord[] = {60,64,67};
    CHECK(thy_core_press(&musical_core, 0u, chord, 3u, 0, 0,
        THY_ROUTE_DIN | THY_ROUTE_USB0, 96u) == THY_OK);
    render(&still, 3u, frame);
    CHECK(matches_text(frame, 2u, 58u, "PADS 1  NOTES 3", 25u));
    instrument.display_config[THY_VIEW_SIMPLE].slots[1] = THY_VIEW_INFO_OFF;
    render(&still, 4u, frame);
    CHECK(matches_text(frame, 2u, 58u, "", 22u));
    instrument.display_view = 5u;
    render(&still, 4u, frame);
    CHECK(matches_text(frame, 2u, 58u, "PADS 1  NOTES 3", 25u));
    instrument.display_config[THY_VIEW_CHLADNI].slots[1] = THY_VIEW_INFO_OFF;
    render(&still, 4u, frame);
    CHECK(matches_text(frame, 2u, 58u, "", 22u));
    /* Detailed has its own six rows regardless of compact-view preferences. */
    instrument.display_view = 0u;
    render(&still, 5u, frame);
    CHECK(matches_text(frame, 2u, 52u, "PADS 1  NOTES 3", 25u));
    return 1;
}

static void expected_note_label(uint8_t *out, const char *label,
    unsigned x, unsigned y, unsigned scale)
{
    uint8_t glyphs[THY_UI_FRAME_BYTES] = {0};
    unsigned length = (unsigned)strlen(label);
    thy_ui_draw_text(glyphs, 0u, 0u, label, length, 0);
    for (unsigned gy = 0u; gy < 5u; ++gy)
        for (unsigned gx = 0u; gx < length * 5u; ++gx)
            if (pixel_at(glyphs, gx, gy))
                thy_ui_draw_rectangle(out, x + gx * scale, y + gy * scale,
                    scale, scale, 1);
}

static int test_simple_five_notes_use_four_large_corners_and_large_center(void)
{
    static const struct {
        int16_t notes[5];
        const char *names[5];
        uint8_t x[5];
    } chords[] = {
        {{60,64,67,70,74}, {"C4","E4","G4","A#4","D5"}, {2,86,2,76,44}},
        {{0,1,10,126,127}, {"C-1","C#-1","A#-1","F#9","G9"}, {2,66,2,76,44}},
        {{1,3,6,8,10}, {"C#-1","D#-1","F#-1","G#-1","A#-1"}, {2,66,2,66,34}}
    };
    static const uint8_t y[5] = {12,12,36,36,24};
    static thy_core saved_core;
    for (unsigned chord = 0u; chord < sizeof chords / sizeof chords[0]; ++chord) {
        initialize();
        instrument.display_view = THY_VIEW_SIMPLE;
        CHECK(thy_core_press(&musical_core, 0u, chords[chord].notes, 5u, 0, 0,
            THY_ROUTE_DIN | THY_ROUTE_USB0, 96u) == THY_OK);
        saved_core = musical_core;
        before_state = state;
        render(&still, chord, frame);
        CHECK(memcmp(&musical_core, &saved_core, sizeof musical_core) == 0);
        CHECK(memcmp(&state, &before_state, sizeof state) == 0);
        memset(before, 0, sizeof before);
        for (unsigned note = 0u; note < 5u; ++note) {
            char label[6];
            thy_ui_note_name(chords[chord].notes[note], label);
            CHECK(strcmp(label, chords[chord].names[note]) == 0);
            /* Independently placed 2x glyphs require every octave label,
             * all five large glyph heights and blank gaps between rows. */
            expected_note_label(before, chords[chord].names[note],
                chords[chord].x[note], y[note], 2u);
        }
        for (unsigned py = 11u; py < 47u; ++py)
            for (unsigned px = 0u; px < 106u; ++px)
                CHECK(pixel_at(frame, px, py) == pixel_at(before, px, py));
        CHECK(pixel_at(frame,106u,16u) && pixel_at(frame,107u,22u));
        char musical_name[32];
        thy_studio_ui_view_line(&state,THY_VIEW_INFO_MUSICAL_NAMES,"MAJOR C",1,1,musical_name);
        CHECK(matches_text(frame,strlen(musical_name)<=19u?12u:2u,1u,musical_name,strlen(musical_name)<=19u?19u:21u));
        CHECK(matches_text(frame, 2u, 48u, musical_name, 21u));
        CHECK(matches_text(frame, 2u, 58u, "PADS 1  NOTES 5", 25u));
        CHECK(thy_core_release(&musical_core, 0u) == THY_OK);
    }
    return 1;
}

static int test_simple_six_notes_begin_compact_wrapping_without_ownership_changes(void)
{
    static const int16_t notes[6] = {60,64,67,70,74,77};
    static const char *const labels[6] = {"C4","E4","G4","A#4","D5","F5"};
    static const uint8_t x[6] = {2,23,44,65,86,2};
    static const uint8_t y[6] = {13,13,13,13,13,20};
    static thy_core saved_core;
    initialize();
    instrument.display_view = THY_VIEW_SIMPLE;
    CHECK(thy_core_press(&musical_core, 0u, notes, 6u, 0, 0,
        THY_ROUTE_DIN | THY_ROUTE_USB0, 96u) == THY_OK);
    saved_core = musical_core;
    before_state = state;
    render(&still, 0u, frame);
    CHECK(memcmp(&musical_core, &saved_core, sizeof musical_core) == 0);
    CHECK(memcmp(&state, &before_state, sizeof state) == 0);
    memset(before, 0, sizeof before);
    for (unsigned note = 0u; note < 6u; ++note)
        expected_note_label(before, labels[note], x[note], y[note], 1u);
    for (unsigned py = 11u; py < 47u; ++py)
        for (unsigned px = 0u; px < 106u; ++px)
            CHECK(pixel_at(frame, px, py) == pixel_at(before, px, py));
    char musical_name[32];
        thy_studio_ui_view_line(&state,THY_VIEW_INFO_MUSICAL_NAMES,"MAJOR C",1,1,musical_name);
        CHECK(matches_text(frame,strlen(musical_name)<=19u?12u:2u,1u,musical_name,strlen(musical_name)<=19u?19u:21u));
    CHECK(matches_text(frame, 2u, 48u, musical_name, 21u));
    CHECK(matches_text(frame, 2u, 58u, "PADS 1  NOTES 6", 25u));
    CHECK(thy_core_release(&musical_core, 0u) == THY_OK);
    return 1;
}

static int test_pretty_is_note_driven_bounded_and_respects_reduced_motion(void)
{
    struct { uint8_t prefix[16], pixels[THY_UI_FRAME_BYTES], suffix[16]; } guarded;
    initialize();
    instrument.display_view = 5u;
    const int16_t note = 60;
    render(&ui, 0u, before);
    CHECK(thy_core_press(&musical_core, 0u, &note, 1u, 0, 0,
        THY_ROUTE_DIN, 94u) == THY_OK);
    before_state = state;
    unsigned queued = musical_core.queue_count;
    memset(&guarded, 0xa5, sizeof guarded);
    thy_studio_ui_render(&ui, &state, "MAJOR C", 1, 0, 85u, guarded.pixels);
    CHECK(memcmp(before, guarded.pixels, sizeof before) != 0);
    memcpy(before, guarded.pixels, sizeof before);
    render(&ui, 255u, frame);
    CHECK(memcmp(before, frame, sizeof frame) != 0);
    CHECK(memcmp(&state, &before_state, sizeof state) == 0);
    CHECK(musical_core.queue_count == queued && musical_core.owners[0].held);
    for (unsigned i = 0u; i < 16u; ++i) {
        CHECK(guarded.prefix[i] == 0xa5u);
        CHECK(guarded.suffix[i] == 0xa5u);
    }
    instrument.display_config[THY_VIEW_CHLADNI].motion = 0u;
    render(&ui, 300u, before);
    render(&ui, 4000u, frame);
    CHECK(memcmp(before, frame, sizeof frame) == 0);
    instrument.display_config[THY_VIEW_CHLADNI].motion = 1u;
    render(&still, 300u, before);
    render(&still, 4000u, frame);
    CHECK(memcmp(before, frame, sizeof frame) == 0);
    instrument.animations_enabled = 0u;
    render(&ui, 300u, before);
    render(&ui, 4000u, frame);
    CHECK(memcmp(before, frame, sizeof frame) == 0);
    CHECK(thy_core_release(&musical_core, 0u) == THY_OK);
    return 1;
}

static int test_credits_share_the_boot_wordmark_and_real_optional_identity(void)
{
    initialize();
    thy_studio_ui_set_build_identity(&ui, "TEST BUILD 1234");
    thy_studio_ui_set_build_identity(NULL, NULL);
    thy_studio_ui_boot_begin(&ui, 0u);
    ui.boot_elapsed_ms = THY_STUDIO_UI_BOOT_REVEAL_MS;
    render(&ui, 0u, before);
    thy_studio_ui_boot_skip(&ui);
    state.about_open = 1u;
    render(&ui, 1u, frame);
    for (unsigned y = 0u; y < 7u; ++y)
        for (unsigned x = 70u; x < 116u; ++x)
            CHECK(pixel_at(frame, x, y + 5u) == pixel_at(before, x, y + 13u));
    for(unsigned y=0u;y<THY_BRAND_LOGO_HEIGHT;++y)
        for(unsigned x=0u;x<THY_BRAND_LOGO_WIDTH;++x)
            CHECK(pixel_at(frame,x+4u,y+4u)==pixel_at(before,x+4u,y+12u));
    CHECK(matches_text(frame, 2u, 50u, "TEST BUILD 1234", 25u));
    CHECK(matches_text(frame, 2u, 59u, "O EXIT  < BACK", 25u));
    thy_studio_ui_set_build_identity(&ui, NULL);
    render(&ui, 2u, frame);
    CHECK(matches_text(frame, 2u, 50u, "", 25u));
    return 1;
}

static int test_full_scale_names_remain_readable_without_mutating_state(void)
{
    initialize();
    uint16_t scale = THY_ROW_PERFORMANCE_SCALE;
    CHECK(thy_state_set(&state, scale, 14) == THY_STATE_OK);
    CHECK(thy_state_focus(&state, thy_menu_rows[scale].group, scale) == THY_STATE_OK);
    before_state = state;
    unsigned queued = musical_core.queue_count;
    render(&still, 0u, before);
    CHECK(matches_text(before, 2u, 57u, "Harmonic Minor", 25u));
    CHECK(memcmp(&state, &before_state, sizeof state) == 0);
    CHECK(thy_state_begin_edit(&state, scale) == THY_STATE_OK);
    CHECK(thy_state_edit_set(&state, 20) == THY_STATE_OK);
    before_state = state;
    render(&still, 1u, frame);
    CHECK(matches_text(frame, 2u, 57u, "Harmonic Min Mode 7", 25u));
    CHECK(memcmp(before, frame, sizeof frame) != 0);
    CHECK(memcmp(&state, &before_state, sizeof state) == 0);
    CHECK(musical_core.queue_count == queued);
    int32_t committed;
    CHECK(thy_state_get(&state, scale, &committed) == THY_STATE_OK && committed == 14);
    /* Every recovered library label fits in the full-width footer. */
    for (unsigned n = 0u; n < THY_MENU_DEFAULT_SCALE_COUNT; ++n) {
        CHECK(strlen(thy_menu_scale_names[n]) <= 25u);
        CHECK(thy_state_edit_set(&state, (int32_t)n) == THY_STATE_OK);
        render(&still, n + 2u, frame);
        CHECK(matches_text(frame, 2u, 57u, thy_menu_scale_names[n], 25u));
    }
    CHECK(thy_state_cancel(&state) == THY_STATE_OK);
    uint16_t assignment = THY_ROW_ENGINES_ASSIGNMENT;
    CHECK(thy_state_focus(&state, thy_menu_rows[assignment].group, assignment) == THY_STATE_OK);
    CHECK(thy_state_begin_edit(&state, assignment) == THY_STATE_OK);
    CHECK(thy_state_edit_set(&state, THY_SCALE_SHORTCUT_LAYOUT) == THY_STATE_OK);
    render(&still, 100u, frame);
    CHECK(matches_text(frame, 2u, 57u, "Cycle layout", 25u));
    CHECK(thy_state_edit_set(&state, 14) == THY_STATE_OK);
    render(&still, 101u, frame);
    CHECK(matches_text(frame, 2u, 57u, "Harmonic Minor", 25u));
    state.notice = THY_NOTICE_INVALID;
    render(&still, 102u, frame);
    CHECK(matches_text(frame, 2u, 57u, "INVALID  O EXIT", 25u));
    CHECK(thy_state_cancel(&state) == THY_STATE_OK);
    thy_state_open(&state);
    render(&still, 103u, frame);
    CHECK(matches_text(frame, 2u, 57u, "> OPEN  O EXIT", 25u));
    return 1;
}

static int test_engines_gear_is_distinct_and_descriptions_require_selection(void)
{
    initialize();
    thy_state_open(&state);
    uint8_t symbols[2][7][7];
    const char *ids[] = {"engines", "views"};
    for (unsigned symbol = 0u; symbol < 2u; ++symbol) {
        unsigned group = 0u;
        while (group < THY_MENU_GROUP_COUNT &&
            strcmp(thy_menu_groups[group].id, ids[symbol]) != 0) ++group;
        CHECK(group < THY_MENU_GROUP_COUNT);
        state.group_index = (uint8_t)group;
        render(&still, symbol, frame);
        unsigned selected = 0u, icon_y = 0u;
        for (unsigned line = 0u; line < 5u; ++line) {
            unsigned y = 13u + line * 8u, cursor_pixels = 0u;
            for (unsigned dy = 0u; dy < 5u; ++dy)
                for (unsigned x = 1u; x < 6u; ++x)
                    cursor_pixels += (unsigned)pixel_at(frame, x, y + dy);
            if (cursor_pixels) { ++selected; icon_y = y - 1u; }
        }
        CHECK(selected == 1u);
        for (unsigned y = 0u; y < 7u; ++y)
            for (unsigned x = 0u; x < 7u; ++x)
                symbols[symbol][y][x] = (uint8_t)pixel_at(frame, 8u + x, icon_y + y);
    }
    CHECK(memcmp(symbols[0], symbols[1], sizeof symbols[0]) != 0);
    /* The gear's shaft is hollow, with symmetric teeth around it. This
     * distinguishes a cog from the prior shared eye/constellation symbol. */
    for (unsigned y = 0u; y < 7u; ++y)
        for (unsigned x = 0u; x < 7u; ++x) {
            CHECK(symbols[0][y][x] == symbols[0][6u - y][x]);
            CHECK(symbols[0][y][x] == symbols[0][y][6u - x]);
            if (x >= 2u && x <= 4u && y >= 2u && y <= 4u)
                CHECK(!symbols[0][y][x]);
        }
    CHECK(symbols[0][0][2] && symbols[0][2][0]);
    CHECK(!symbols[0][0][0] && !symbols[0][3][0]);

    uint16_t row = THY_ROW_ENGINES_DRUMS;
    CHECK(thy_state_focus(&state, thy_menu_rows[row].group, row) == THY_STATE_OK);
    CHECK(!state.info_open);
    render(&still, 2u, before);
    char value[32];
    thy_studio_ui_format(&state, row, 0, value);
    CHECK(strcmp(value, "Open") == 0);
    CHECK(matches_text(before, 2u, 57u, "> OPEN  O EXIT  < BACK", 25u));
    CHECK(thy_state_navigate(&state, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(state.info_open);
    render(&still, 3u, frame);
    CHECK(matches_text(frame, 2u, 13u, thy_menu_rows[row].short_label, 25u));
    if(row==THY_ROW_SYSTEM_RESOURCES){
                CHECK(matches_text(frame,12u,1u,"RESOURCES",9u));
                CHECK(matches_text(frame,2u,58u,"O EXIT  < BACK",25u));
            }else CHECK(matches_text(frame, 2u, 57u, "READ ONLY O EXIT < BACK", 25u));
    CHECK(memcmp(before, frame, sizeof frame) != 0);
    CHECK(thy_state_navigate(&state, THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(!state.info_open && thy_state_current_row(&state) == row);
    render(&still, 4u, frame);
    CHECK(memcmp(before, frame, sizeof frame) == 0);
    return 1;
}

static int test_basic_advanced_divider_never_adds_a_navigation_target(void)
{
    initialize();
    thy_state_set_design_mode(&state, 1u);
    unsigned checked = 0u;
    for (unsigned g = 0u; g < THY_MENU_GROUP_COUNT; ++g) {
        const thy_menu_group *group = &thy_menu_groups[g];
        uint16_t visible[THY_MENU_ROW_COUNT];
        unsigned count = 0u, boundary = 0u;
        for (unsigned n = 0u; n < group->row_count; ++n) {
            uint16_t row = (uint16_t)(group->first_row + n);
            if (!thy_state_row_visible(&state, row)) continue;
            if (count && thy_menu_rows[row].advanced && !thy_menu_rows[visible[count - 1u]].advanced)
                boundary = count;
            visible[count++] = row;
        }
        if (!boundary) continue;
        /* Highlighted information rows remain in the list until RIGHT, so
         * information at a section boundary must retain the divider too. */
        CHECK(thy_state_focus(&state, (uint8_t)g, visible[boundary - 1u]) == THY_STATE_OK);
        CHECK(thy_state_navigate(&state, THY_STATE_DOWN) == THY_STATE_OK);
        CHECK(thy_state_current_row(&state) == visible[boundary]);
        before_state = state;
        render(&still, 0u, frame);
        CHECK(memcmp(&state, &before_state, sizeof state) == 0);
        unsigned start = boundary > 2u ? boundary - 2u : 0u;
        if (count > 5u && start > count - 5u) start = count - 5u;
        if (count <= 5u) start = 0u;
        unsigned line_y = 11u + (boundary - start) * 8u;
        for (unsigned x = 8u; x < 126u; ++x) CHECK(pixel_at(frame, x, line_y));
        CHECK(thy_state_navigate(&state, THY_STATE_UP) == THY_STATE_OK);
        CHECK(thy_state_current_row(&state) == visible[boundary - 1u]);
        ++checked;
    }
    CHECK(checked >= 8u);
    return 1;
}

static int test_sequencer_canvases_are_live_read_only_and_have_scene_keys(void)
{
    initialize();
    thy_state_play(&state);
    thy_sequencer_ui_init(&engine.sequencer_ui, &engine.sequencer,
        &engine.rhythm_transport, &engine.musical_clock, NULL, NULL);
    thy_sequencer_ui_open(&engine.sequencer_ui, THY_SEQ_VIEW_TRACKS);
    engine.sequencer_ui.track_new = 1u;
    CHECK(thy_studio_ui_scene_key(&state) == 0x60000u);
    uint32_t revision = engine.sequencer.revision;
    before_state = state;
    render(&still, 0u, frame);
    CHECK(matches_text(frame, 1u, 1u, "TRACKS STOP 120", 25u));
    CHECK(matches_text(frame, 1u, 12u, ">+ NEW TRACK", 25u));
    CHECK(matches_text(frame, 1u, 29u, "RIGHT CREATES FIRST TRACK", 25u));
    CHECK(matches_text(frame, 1u, 58u, "> CREATE O MENU", 25u));
    CHECK(!memcmp(&state, &before_state, sizeof state) && engine.sequencer.revision == revision);

    CHECK(thy_sequencer_create(&engine.sequencer, 0u) == THY_OK);
    CHECK(thy_sequencer_create(&engine.sequencer, 3u) == THY_OK);
    CHECK(thy_sequencer_create(&engine.sequencer, 16u) == THY_OK);
    CHECK(thy_sequencer_select(&engine.sequencer, 0u) == THY_OK);
    engine.sequencer_ui.track_new = 0u;
    thy_seq_event events[] = {
        {.tick=960u, .duration=240u, .kind=THY_SEQ_NOTE, .pitch=60u, .value=96u},
        {.tick=960u, .duration=240u, .kind=THY_SEQ_NOTE, .pitch=64u, .value=96u},
        {.tick=960u, .duration=240u, .kind=THY_SEQ_NOTE, .pitch=67u, .value=96u},
        {.tick=0u, .duration=1u, .kind=THY_SEQ_CC, .pitch=1u, .value=42u}
    };
    CHECK(thy_sequencer_add(&engine.sequencer, 0u, events, 4u) == THY_OK);
    engine.rhythm_transport.tracks[0].pending = THY_TRANSPORT_PENDING_START;
    engine.rhythm_transport.tracks[0].pending_position = 3840u;
    set_capture_state(&engine.sequencer, THY_SEQ_ARMED);
    engine.sequencer.capture.track = 3u;
    revision = engine.sequencer.revision;
    thy_sequencer_ui frozen = engine.sequencer_ui;
    render(&still, 1u, frame);
    CHECK(matches_text(frame, 1u, 12u, ">T1 N3 IN1BAR", 19u));
    CHECK(matches_text(frame, 1u, 19u, " T4 N0 ARM", 19u));
    CHECK(matches_text(frame, 1u, 26u, " T17 N0 STOP", 19u));
    CHECK(matches_text(frame, 1u, 33u, " + NEW TRACK", 25u));
    CHECK(!memcmp(&engine.sequencer_ui, &frozen, sizeof frozen));
    CHECK(engine.sequencer.revision == revision);
    engine.rhythm_transport.position = 1920u;
    render(&still, 2u, frame);
    CHECK(matches_text(frame, 1u, 12u, ">T1 N3 IN2BT", 19u));
    engine.sequencer_ui.delete_pending = 1u;
    render(&still, 3u, frame);
    CHECK(matches_text(frame, 1u, 18u, "DELETE TRACK 1?", 25u));
    CHECK(matches_text(frame, 1u, 58u, "> DELETE < CANCEL O MENU", 25u));
    CHECK(engine.sequencer.revision == revision);
    engine.sequencer_ui.delete_pending = 0u;
    set_capture_state(&engine.sequencer, THY_SEQ_CAPTURE_OFF);

    thy_sequencer_ui_open(&engine.sequencer_ui, THY_SEQ_VIEW_STEPS);
    engine.sequencer_ui.selected_step = 4u;
    CHECK(thy_studio_ui_scene_key(&state) == 0x60001u);
    render(&still, 10u, frame);
    CHECK(matches_text(frame, 1u, 1u, "T1 STEP 5/48 STOP", 25u));
    CHECK(matches_text(frame, 1u, 12u, "M4 NOTES / REPLACE", 25u));
    CHECK(matches_text(frame, 1u, 20u, "3 NOTES C4 E4 G4", 25u));
    CHECK(pixel_at(frame, 44u, 29u) && pixel_at(frame, 52u, 33u));
    engine.sequencer_ui.overdub = 1u;
    render(&still, 11u, frame);
    CHECK(matches_text(frame, 1u, 12u, "M4 NOTES / OVERDUB", 25u));
    CHECK(thy_studio_ui_scene_key(&state) == 0x60001u);
    engine.song_status=1u; render(&still,21u,frame);
    CHECK(matches_text(frame,1u,58u,"SONG SAVE PENDING",25u));
    engine.song_status=2u; render(&still,22u,frame);
    CHECK(matches_text(frame,1u,58u,"SONG SAVED",25u));
    engine.song_status=3u; render(&still,23u,frame);
    CHECK(matches_text(frame,1u,58u,"SONG SAVE FAILED",25u));
    engine.song_status=0u;
    CHECK(thy_state_focus(&state,thy_menu_rows[THY_ROW_SEQUENCER_TEMPO].group,
        THY_ROW_SEQUENCER_TEMPO) == THY_STATE_OK);
    CHECK(thy_studio_ui_scene_key(&state) != 0x60001u);
    return 1;
}

static int test_sequencer_help_is_paginated_short_and_read_only(void)
{
    initialize();
    thy_state_play(&state);
    thy_sequencer_ui_open(&engine.sequencer_ui, THY_SEQ_VIEW_TRACKS);
    engine.sequencer_ui.help_open = 1u;
    const char *titles[] = {"HELP TRACKS 1/5", "HELP ENTER NOTES 2/5", "HELP EDIT A STEP 3/5",
        "HELP PLAYBACK 4/5", "HELP RECORD + SAVE 5/5"};
    const char *first[] = {"UP/DOWN CHOOSE A TRACK", "TAP A LEFT STEP", "M2 CLEARS SELECTED STEP",
        "5 PLAYS / STOPS THE SONG", "12 STARTS / FINISHES"};
    for (unsigned page = 0u; page < 5u; ++page) {
        engine.sequencer_ui.help_page = (uint8_t)page;
        thy_sequencer_ui frozen = engine.sequencer_ui;
        uint32_t revision = engine.sequencer.revision;
        render(&still, page, frame);
        CHECK(matches_text(frame, 1u, 1u, titles[page], 25u));
        CHECK(matches_text(frame, 1u, 13u, first[page], 25u));
        CHECK(matches_text(frame, 1u, 58u, "UP/DN PAGE < BACK O MENU", 25u));
        CHECK(!memcmp(&engine.sequencer_ui, &frozen, sizeof frozen));
        CHECK(engine.sequencer.revision == revision);
    }
    return 1;
}

static int test_track_settings_have_a_dedicated_title_scene_and_return_legend(void)
{
    initialize();
    CHECK(thy_engine_sequencer_action(&engine, THY_SEQ_UI_CREATE_TRACK, 0u) == THY_OK);
    CHECK(thy_engine_sequencer_open(&engine, THY_SEQ_VIEW_STEPS) == THY_STATE_OK);
    CHECK(thy_sequencer_ui_configure(&engine.sequencer_ui, THY_SEQ_UI_SELECTED_STEP, 7u) == THY_OK);
    CHECK(thy_engine_sequencer_action(&engine, THY_SEQ_UI_TRACK_OPTIONS, 0u) == THY_OK);
    CHECK(engine.sequencer_ui.options_open && state.level == THY_STATE_ROWS);
    CHECK(thy_studio_ui_scene_key(&state) == 0x80000u);
    before_state = state;
    thy_sequencer_ui frozen = engine.sequencer_ui;
    uint32_t revision = engine.sequencer.revision;
    render(&still, 1u, frame);
    CHECK(matches_text(frame,12u,1u,"T1 SETTINGS",17u));
    CHECK(matches_text(frame, 2u, 57u, "< TRACK > EDIT O EXIT", 25u));
    CHECK(!memcmp(&state, &before_state, sizeof state));
    CHECK(!memcmp(&engine.sequencer_ui, &frozen, sizeof frozen));
    CHECK(engine.sequencer.revision == revision);
    CHECK(thy_engine_navigate(&engine, THY_STATE_RIGHT) == THY_STATE_OK);
    render(&still, 2u, frame);
    CHECK(matches_text(frame, 2u, 57u, "< CANCEL > APPLY O EXIT", 25u));
    CHECK(thy_studio_ui_scene_key(&state) == 0x80000u);
    CHECK(thy_engine_navigate(&engine, THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(engine.sequencer_ui.options_open && !state.editing);
    CHECK(thy_engine_navigate(&engine, THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(!engine.sequencer_ui.options_open && state.level == THY_STATE_PLAY);
    CHECK(engine.sequencer_ui.open && engine.sequencer_ui.view == THY_SEQ_VIEW_STEPS);
    CHECK(engine.sequencer_ui.selected_step == 7u);
    CHECK(thy_studio_ui_scene_key(&state) == 0x60001u);
    CHECK(thy_engine_sequencer_close(&engine) == THY_STATE_OK);
    CHECK(thy_state_focus(&state, thy_menu_rows[THY_ROW_SEQUENCER_OPEN].group,
        THY_ROW_SEQUENCER_OPEN) == THY_STATE_OK);
    render(&still, 3u, frame);
    CHECK(matches_text(frame,12u,1u,thy_menu_groups[thy_menu_rows[THY_ROW_SEQUENCER_OPEN].group].label,17u));
    CHECK(thy_studio_ui_scene_key(&state) != 0x80000u);
    CHECK(!thy_state_row_visible(&state, THY_ROW_SEQUENCER_CHANNEL));
    return 1;
}

static int test_left_navigation_and_confirmation_glyphs_are_visible(void)
{
    memset(frame, 0, sizeof frame);
    thy_ui_draw_text(frame, 0u, 0u, "<>", 2u, 0);
    for (unsigned col = 0u; col < 4u; ++col)
        for (unsigned row = 0u; row < 5u; ++row)
            CHECK(pixel_at(frame, col, row) == pixel_at(frame, 8u - col, row));
    CHECK(pixel_at(frame, 1u, 2u));
    CHECK(pixel_at(frame, 3u, 0u) && pixel_at(frame, 3u, 4u));
    memset(frame, 0, sizeof frame);
    thy_ui_draw_text(frame, 0u, 0u, "?", 1u, 0);
    static const uint8_t columns[] = {1u, 21u, 5u, 2u};
    for (unsigned col = 0u; col < 4u; ++col)
        for (unsigned row = 0u; row < 5u; ++row)
            CHECK(pixel_at(frame, col, row) == !!(columns[col] & (1u << row)));
    return 1;
}

static int test_scoped_sensitivity_graphs_and_pick_prompt_are_read_only(void)
{
    initialize();
    CHECK(thy_state_focus(&state,thy_menu_rows[THY_ROW_SENSITIVITY_CUSTOMCURVE].group,
        THY_ROW_SENSITIVITY_CUSTOMCURVE)==THY_STATE_OK);
    render(&still,0u,before);
    CHECK(!state.info_open && matches_text(before,12u,1u,"SENSE LEFT",17u));
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(state.info_open && !state.editing);
    before_state=state;
    static thy_instrument frozen;frozen=instrument;
    render(&still,1u,frame);
    CHECK(matches_text(frame,2u,13u,"AFTERTOUCH",12u));
    CHECK(matches_text(frame,68u,13u,"FIXED",11u));
    CHECK(matches_text(frame,2u,47u,"AT:LEFT",11u));
    CHECK(matches_text(frame,68u,47u,"V:LEFT",11u));
    CHECK(!memcmp(&state,&before_state,sizeof state) && !memcmp(&instrument,&frozen,sizeof instrument));
    thy_instrument_velocity velocity;
    CHECK(thy_instrument_get_cluster_velocity(&instrument,0u,&velocity)==THY_OK);
    velocity.mode=THY_VELOCITY_STRIKE;
    CHECK(thy_instrument_set_cluster_velocity(&instrument,0u,&velocity)==THY_OK);
    memcpy(before,frame,sizeof frame);render(&still,2u,frame);
    CHECK(matches_text(frame,68u,13u,"STRIKE",11u));
    CHECK(memcmp(frame,before,sizeof frame));
    CHECK(thy_state_set(&state,THY_ROW_SENSITIVITY_TARGET,97)==THY_STATE_OK);
    render(&still,3u,frame);
    CHECK(matches_text(frame,68u,13u,"FIXED",11u));
    CHECK(matches_text(frame,2u,47u,"AT:RIGHT",11u));
    CHECK(matches_text(frame,68u,47u,"V:RIGHT",11u));
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(thy_state_set(&state,THY_ROW_SENSITIVITY_TARGET,96)==THY_STATE_OK);
    CHECK(thy_state_focus(&state,thy_menu_rows[THY_ROW_SENSITIVITY_PICK].group,THY_ROW_SENSITIVITY_PICK)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&state,THY_ROW_SENSITIVITY_PICK)==THY_STATE_OK);
    render(&still,3u,frame);
    CHECK(matches_text(frame,12u,1u,"SENSE R4 C12",17u));
    CHECK(matches_text(frame,2u,57u,"TAP PAD / < CANCEL O EXIT",25u));
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(!engine.sensitivity_pick);
    return 1;
}

static int test_resource_clock_and_read_only_screen(void)
{
    thy_resources r={0};thy_resource_clock c={0};
    /* Both MCU counters cross their unsigned boundary in this window. */
    uint32_t start=UINT32_MAX-500000u, ms=UINT32_MAX-499u;
    thy_resources_tick(&r,&c,start,ms,1000000u);
    thy_resources_tick(&r,&c,start+250000u,ms+500u,1000000u);
    CHECK(!r.timing_valid);
    thy_resources_tick(&r,&c,start+500000u,ms+1000u,1000000u);
    CHECK(r.timing_valid && r.cpu_active==50u && r.loop_us==250000u && r.loop_peak_us==250000u);
    thy_resources_tick(&r,&c,start+1500000u,ms+2000u,1000000u);
    CHECK(r.cpu_active==100u && r.loop_us==1000000u);
    thy_resources_tick(&r,&c,start+7500000u,ms+8000u,1000000u);
    CHECK(!r.timing_valid);
    thy_resources_tick(&r,&c,start+8500000u,ms+9000u,1000000u);
    CHECK(r.timing_valid && r.cpu_active==100u);
    initialize();r.valid=r.stack_valid=1u;r.ram_used=100u*1024u;r.ram_total=128u*1024u;
    r.ccm_used=48u*1024u;r.ccm_total=64u*1024u;r.flash_used=400u*1024u;r.flash_total=512u*1024u;
    r.stack_used=3072u;r.stack_total=8192u;still.resources=&r;
    CHECK(thy_state_focus(&state,thy_menu_rows[THY_ROW_SYSTEM_RESOURCES].group,THY_ROW_SYSTEM_RESOURCES)==THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    before_state=state;render(&still,1u,frame);
    CHECK(matches_text(frame,3u,22u,"RAM 100/128K",24u));
    CHECK(matches_text(frame,3u,49u,"STACK 3/8K",24u));
    CHECK(!memcmp(&state,&before_state,sizeof state) && !thy_core_queued(&musical_core));
    r.stack_used=4096u;render(&still,2u,frame);
    CHECK(matches_text(frame,3u,49u,"STACK 4/8K",24u));
    still.resources=NULL;return 1;
}

static int test_storage_bars_count_real_record_and_song_usage_without_encoding(void)
{
    initialize();
    thy_seq_event events[3]={{0u,240u,0u,0u,0u,THY_SEQ_NOTE,60u,96u},
        {0u,240u,0u,0u,0u,THY_SEQ_NOTE,64u,96u},{0u,240u,0u,0u,0u,THY_SEQ_NOTE,67u,96u}};
    CHECK(thy_sequencer_add(&engine.sequencer,0u,events,3u)==THY_OK);
    CHECK(thy_state_focus(&state,thy_menu_rows[THY_ROW_SYSTEM_STORAGE].group,THY_ROW_SYSTEM_STORAGE)==THY_STATE_OK);
    render(&still,0u,before);
    CHECK(!state.info_open);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    before_state=state;
    uint32_t revision=engine.sequencer.revision;
    render(&still,1u,frame);
    CHECK(matches_text(frame,2u,26u,"SONG 3/512 EVENTS 0%",25u));
    CHECK(matches_text(frame,2u,39u,"JOURNAL 256KiB ROTATING",25u));
    CHECK(matches_text(frame,2u,47u,"COLLECTIONS NOT ALLOCATED",25u));
    CHECK(matches_text(frame,2u,57u,"READ ONLY O EXIT < BACK",25u));
    size_t used=THY_SETTINGS_HEADER_BYTES+THY_SETTINGS_V9_PARAMETER_BYTES+thy_sequencer_song_size(&engine.sequencer);
    char text[32];
    snprintf(text,sizeof text,"STATE %u/%uB %u%%",(unsigned)used,(unsigned)THY_SETTINGS_RECORD_BYTES,
        (unsigned)(100u*used/THY_SETTINGS_RECORD_BYTES));
    CHECK(matches_text(frame,2u,13u,text,25u));
    unsigned width=(unsigned)(122u*used/THY_SETTINGS_RECORD_BYTES);
    CHECK(pixel_at(frame,3u,21u) && pixel_at(frame,2u+width,21u));
    if(width<122u)CHECK(!pixel_at(frame,3u+width,21u));
    CHECK(!memcmp(&state,&before_state,sizeof state) && engine.sequencer.revision==revision);
    CHECK(engine.sequencer.count==3u && !thy_core_queued(&musical_core));
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK && !state.info_open);
    return 1;
}

static int test_pad_target_editor_explains_all_shortcut_without_applying(void)
{
    initialize();
    CHECK(thy_state_set(&state,THY_ROW_SENSITIVITY_TARGET,1)==THY_STATE_OK);
    CHECK(thy_state_focus(&state,thy_menu_rows[THY_ROW_SENSITIVITY_TARGET].group,
        THY_ROW_SENSITIVITY_TARGET)==THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(thy_state_edit_set(&state,65)==THY_STATE_OK);
    CHECK(engine.sensitivity_target==1u && state.editing);
    before_state=state;
    render(&still,1u,frame);
    CHECK(matches_text(frame,12u,1u,"SENSE L1 C1",17u));
    CHECK(matches_text(frame,2u,57u,"UP ALL DOWN PAD >APPLY",25u));
    CHECK(!memcmp(&state,&before_state,sizeof state) && engine.sensitivity_target==1u);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(!state.editing && engine.sensitivity_target==1u);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(thy_state_edit_set(&state,0)==THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK);
    CHECK(state.level==THY_STATE_PLAY && engine.sensitivity_target==1u);
    return 1;
}

static int test_sensitivity_edit_footer_uses_frozen_allowed_range_and_preserves_errors(void)
{
    initialize();
    CHECK(thy_state_set(&state,THY_ROW_SENSITIVITY_TARGET,1)==THY_STATE_OK);
    int electrical=thy_engine_sensitivity_electrical(&engine,0u);
    CHECK(electrical>=0);
    thy_instrument_response response=instrument.response;
    response.pressure_floor=1000u;response.pressure_full_scale=2200u;
    CHECK(thy_instrument_set_pad_response(&instrument,(uint8_t)electrical,&response)==THY_OK);
    CHECK(thy_state_focus(&state,thy_menu_rows[THY_ROW_SENSITIVITY_FLOOR].group,
        THY_ROW_SENSITIVITY_FLOOR)==THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(state.editing && state.edit_instance==0u);
    /* A separate cursor change cannot silently retarget this open editor. */
    engine.sensitivity_target=2u;
    static thy_instrument frozen;frozen=instrument;before_state=state;
    render(&still,1u,frame);
    CHECK(matches_text(frame,12u,1u,"SENSE L1 C1",17u));
    CHECK(matches_text(frame,2u,57u,"0..2199 >APPLY OEXIT",25u));
    CHECK(!memcmp(&state,&before_state,sizeof state) && !memcmp(&instrument,&frozen,sizeof instrument));
    state.notice=THY_NOTICE_RELEASE;before_state=state;
    render(&still,2u,frame);
    CHECK(matches_text(frame,2u,57u,"RELEASE ALL PADS  O EXIT",25u));
    CHECK(!memcmp(&state,&before_state,sizeof state) && !memcmp(&instrument,&frozen,sizeof instrument));
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(thy_state_set(&state,THY_ROW_SENSITIVITY_TARGET,0)==THY_STATE_OK);
    CHECK(thy_state_focus(&state,thy_menu_rows[THY_ROW_SENSITIVITY_SOFTSPEED].group,
        THY_ROW_SENSITIVITY_SOFTSPEED)==THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    render(&still,3u,frame);
    CHECK(matches_text(frame,2u,57u,"0..127 >APPLY OEXIT",25u));
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(thy_state_focus(&state,thy_menu_rows[THY_ROW_SENSITIVITY_MINVELOCITY].group,
        THY_ROW_SENSITIVITY_MINVELOCITY)==THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    render(&still,4u,frame);
    CHECK(matches_text(frame,2u,57u,"1..127 >APPLY OEXIT",25u));
    return 1;
}

static int test_calibration_wizard_progress_actions_and_preview_are_read_only(void)
{
    initialize();
    static thy_calibration calibration,frozen_calibration;
    static thy_instrument frozen_instrument;
    thy_calibration_init(&calibration);
    CHECK(thy_calibration_begin_scope(&calibration,0u,48u)==THY_CALIBRATION_OK);
    engine.calibration=&calibration;engine.calibration_open=1u;
    before_state=state;frozen_instrument=instrument;frozen_calibration=calibration;
    render(&still,0u,frame);
    CHECK(thy_studio_ui_scene_key(&state)==UINT32_C(0xA0000));
    CHECK(matches_text(frame,2u,1u,"CAL L R1 C1",19u));
    CHECK(matches_text(frame,108u,1u,"0/48",6u));
    CHECK(matches_text(frame,2u,25u,"1/4 SOFT 0/3",25u));
    CHECK(matches_text(frame,2u,33u,"STRIKE THEN RELEASE",25u));
    CHECK(matches_text(frame,68u,44u,"CANCEL",11u));
    CHECK(matches_text(frame,2u,58u,"UP/DN >DO OEXIT",25u));
    CHECK(pixel_at(frame,3u,11u) && !pixel_at(frame,4u,12u));
    CHECK(pixel_at(frame,48u,21u) && !pixel_at(frame,120u,21u)); /* Only the selected cluster is active. */
    CHECK(!memcmp(&state,&before_state,sizeof state) && !memcmp(&instrument,&frozen_instrument,sizeof instrument));
    CHECK(!memcmp(&calibration,&frozen_calibration,sizeof calibration));
    memcpy(before,frame,sizeof frame);
    calibration.stage=THY_CALIBRATION_MEDIUM;calibration.hits=2u;
    calibration.action_cursor=THY_CALIBRATION_AFTER;
    render(&still,1u,frame);
    CHECK(matches_text(frame,2u,25u,"2/4 MID 2/3",25u));
    CHECK(thy_studio_ui_scene_key(&state)==UINT32_C(0xA0000) && memcmp(frame,before,sizeof frame));
    calibration.stage=THY_CALIBRATION_PRESSURE;calibration.baseline=20u;calibration.live_raw=2000u;
    frozen_calibration=calibration;
    render(&still,2u,frame);
    CHECK(matches_text(frame,2u,25u,"4/4 PRESSURE",25u));
    CHECK(matches_text(frame,2u,58u,"HOLD SOFT 1S THEN FIRM",25u));
    CHECK(pixel_at(frame,4u,34u) && pixel_at(frame,60u,34u) && !pixel_at(frame,100u,34u));
    CHECK(!memcmp(&calibration,&frozen_calibration,sizeof calibration));
    engine.calibration_pick=1u;render(&still,3u,frame);
    CHECK(matches_text(frame,2u,58u,"PRESS PAD TO SELECT",25u));
    engine.calibration_pick=2u;render(&still,4u,frame);
    CHECK(matches_text(frame,2u,58u,"RELEASE SELECTED PAD",25u));
    engine.calibration_pick=0u;
    calibration.candidates[0]=(thy_calibration_candidate){1325u,1312u,1304u,2100u,8u,96u,
        {1400u,1650u,2100u},0u,1u};
    calibration.completed[0]=1u;calibration.completed_count=1u;calibration.selected=0u;
    calibration.live_raw=1700u;calibration.live_rate=32u;
    CHECK(thy_calibration_preview(&calibration,0)==THY_CALIBRATION_NOT_READY);
    for(unsigned p=0;p<48u;++p){
        calibration.candidates[p]=calibration.candidates[0];
        calibration.completed[p/8u]|=(uint8_t)(1u<<(p&7u));
    }
    calibration.candidates[47].pressure_full=1490u;calibration.candidates[47].hard_rate=48u;
    calibration.completed_count=48u;set_calibration_state(&calibration,THY_CALIBRATION_READY);
    CHECK(thy_calibration_review_prepare(&calibration)==THY_CALIBRATION_OK);
    calibration.preview_percent=80u;engine.calibration_review_cursor=2u;
    CHECK(thy_calibration_preview(&calibration,0)==THY_CALIBRATION_OK);
    frozen_calibration=calibration;render(&still,5u,frame);
    CHECK(matches_text(frame,2u,1u,"LEFT 48/48",15u));
    CHECK(matches_text(frame,86u,1u,"CHECK 1",8u));
    CHECK(matches_text(frame,10u,25u,"RESPONSE SAVED",23u));
    CHECK(matches_text(frame,10u,33u,"SENSITIVITY 80%",23u));
    CHECK(matches_text(frame,10u,49u,"DISCARD",23u));
    CHECK(pixel_at(frame,47u,20u) && !pixel_at(frame,48u,21u));
    CHECK(!memcmp(&calibration,&frozen_calibration,sizeof calibration));memcpy(before,frame,sizeof frame);
    CHECK(thy_calibration_preview(&calibration,1)==THY_CALIBRATION_OK);
    frozen_calibration=calibration;
    render(&still,6u,frame);
    CHECK(matches_text(frame,10u,25u,"RESPONSE CALIBRATED",23u));
    CHECK(memcmp(frame,before,sizeof frame) && !memcmp(&calibration,&frozen_calibration,sizeof calibration));
    CHECK(!memcmp(&state,&before_state,sizeof state) && !memcmp(&instrument,&frozen_instrument,sizeof instrument));
    CHECK(!thy_core_queued(&musical_core) && !musical_core.sounding_count);
    engine.calibration_review_cursor=1u;engine.calibration_review_editing=1u;engine.calibration_review_draft=85u;
    render(&still,7u,frame);CHECK(matches_text(frame,2u,58u,"UP/DN >SET <CANCEL",25u));
    CHECK(calibration.preview_percent==80u && !memcmp(&calibration,&frozen_calibration,sizeof calibration));
    CHECK(thy_calibration_begin_scope(&calibration,48u,48u)==THY_CALIBRATION_OK);
    render(&still,8u,frame);
    CHECK(matches_text(frame,2u,1u,"CAL R R1 C1",19u));
    CHECK(matches_text(frame,108u,1u,"0/48",6u));
    CHECK(!pixel_at(frame,4u,12u) && pixel_at(frame,75u,11u) && !pixel_at(frame,76u,12u));
    CHECK(pixel_at(frame,120u,21u));
    engine.calibration=NULL;
    render(&still,9u,frame);
    CHECK(thy_studio_ui_scene_key(&state)!=UINT32_C(0xA0000));
    return 1;
}

static void harmony_fixture(void)
{
    initialize();
    instrument.engine_kind = THY_PLAY_ENGINE_HARMONY;
    engine.harmony = (thy_harmony_workspace){
        .key_mask = 0x0ab5u, .input_mask = 0x0091u, .base_mask = 0x0091u,
        .target_mask = 0x0ad5u, .source_root = 0u, .current_root = 0u,
        .current_pad = 0u, .target_root = 7u, .target_family = 0u,
        .can_apply = 1u, .count = 2u, .cache_ready = 1u, .motion = 1u,
        .changed_ms = 1000u, .view = 1u, .octave_target = 1u, .high_note = 127u,
        .comparison = {.common = 0x0a95u, .added = 0x0040u, .removed = 0x0020u}
    };
    engine.harmony.choices[0] = (thy_harmony_choice){
        .mask = 0x0224u, .root = 2u, .shared = 0u, .pads = UINT64_C(2), .pad = 1u, .degree = 1u
    };
    engine.harmony.choices[1] = (thy_harmony_choice){
        .mask = 0x0221u, .root = 5u, .shared = 1u, .pads = UINT64_C(4), .pad = 2u, .degree = 3u
    };
    engine.harmony.scales[0] = (thy_scale_match){
        .mask = 0x0ab5u, .family = 0u, .root = 0u, .extra = 4u
    };
    engine.harmony.scale_count = 1u;
}

static int test_harmony_workspace_frames_are_live_read_only_and_preview_only(void)
{
    struct { uint8_t prefix[16], pixels[THY_UI_FRAME_BYTES], suffix[16]; } guarded;
    static thy_engine frozen_engine;
    static thy_instrument frozen_instrument;
    static thy_core frozen_core;
    harmony_fixture();
    frozen_engine = engine; frozen_instrument = instrument; frozen_core = musical_core;
    memset(&guarded, 0xa5, sizeof guarded);
    render(&still, 1000u, guarded.pixels);
    CHECK(thy_studio_ui_scene_key(&state) == UINT32_C(0xD0100));
    char key_label[32];thy_engine_harmony_key_label(&engine,0,key_label);
    CHECK(matches_text(guarded.pixels,strlen(key_label)<=15u?12u:2u,1u,key_label,strlen(key_label)<=15u?15u:17u));
    CHECK(matches_text(guarded.pixels, 93u, 1u, "AUTO", 4u));
    CHECK(matches_text(guarded.pixels, 68u, 13u, "NOW", 12u));
    CHECK(matches_text(guarded.pixels, 68u, 21u, "C MAJ", 12u));
    CHECK(matches_text(guarded.pixels, 68u, 35u, "NEXT 1/2", 12u));
    CHECK(matches_text(guarded.pixels, 68u, 43u, "D MIN", 12u));
    CHECK(pixel_at(guarded.pixels, 30u, 19u)); /* Current C: solid. */
    CHECK(!pixel_at(guarded.pixels, 47u, 27u)); /* Suggested D: open. */
    CHECK(matches_text(guarded.pixels, 2u, 58u, "SUG R+0 <BANK >SET O MENU", 25u));
    CHECK(!memcmp(&engine, &frozen_engine, sizeof engine));
    CHECK(!memcmp(&instrument, &frozen_instrument, sizeof instrument));
    CHECK(!memcmp(&musical_core, &frozen_core, sizeof musical_core));
    memcpy(before, guarded.pixels, sizeof before);
    engine.harmony.cursor = 1u;
    render(&still, 1001u, guarded.pixels);
    CHECK(thy_studio_ui_scene_key(&state) == UINT32_C(0xD0100));
    CHECK(matches_text(guarded.pixels, 68u, 43u, "F MAJ", 12u));
    CHECK(memcmp(before, guarded.pixels, sizeof before));
    engine.harmony.page = THY_HARMONY_SCALES; engine.harmony.cursor = 0u;
    render(&still, 1002u, guarded.pixels);
    CHECK(thy_studio_ui_scene_key(&state) == UINT32_C(0xD0001));
    CHECK(matches_text(guarded.pixels, 68u, 49u, "PREVIEW ONLY", 12u));
    engine.harmony.page = THY_HARMONY_KEY;
    frozen_engine = engine; frozen_instrument = instrument;
    render(&still, 1003u, guarded.pixels);
    CHECK(thy_studio_ui_scene_key(&state) == UINT32_C(0xD0002));
    CHECK(matches_text(guarded.pixels, 68u, 13u, "KEY PREVIEW", 12u));
    CHECK(matches_text(guarded.pixels, 68u, 49u, "APPLY: MENU", 12u));
    CHECK(!memcmp(&engine, &frozen_engine, sizeof engine));
    CHECK(!memcmp(&instrument, &frozen_instrument, sizeof instrument));
    CHECK(instrument.performance.root == 0u && !thy_core_queued(&musical_core));
    engine.harmony.can_apply = 0u;
    render(&still, 1004u, guarded.pixels);
    CHECK(matches_text(guarded.pixels, 68u, 49u, "ADVICE ONLY", 12u));
    for (unsigned n = 0u; n < 16u; ++n)
        CHECK(guarded.prefix[n] == 0xa5u && guarded.suffix[n] == 0xa5u);
    thy_state_open(&state);
    render(&still, 1005u, guarded.pixels);
    CHECK(thy_studio_ui_scene_key(&state) != UINT32_C(0xD0002));
    CHECK(matches_text(guarded.pixels,12u,1u,"SETTINGS",17u));
    return 1;
}

static int test_harmony_workspace_motion_is_bounded_optional_and_note_local(void)
{
    harmony_fixture();
    render(&ui, 1000u, before);
    render(&ui, 1180u, frame);
    CHECK(memcmp(before, frame, sizeof before));
    CHECK(matches_text(frame, 68u, 21u, "C MAJ", 12u));
    CHECK(!ui.transitioning);
    for (unsigned y = 0u; y < THY_UI_HEIGHT; ++y)
        for (unsigned x = 0u; x < THY_UI_WIDTH; ++x)
            if (x >= 63u || y < 12u || y >= 56u)
                CHECK(pixel_at(before, x, y) == pixel_at(frame, x, y));
    render(&ui, 1720u, before);
    render(&ui, 2500u, frame);
    CHECK(!memcmp(before, frame, sizeof before)); /* Idle animation terminates. */
    render(&still, 1000u, before);
    render(&still, 1180u, frame);
    CHECK(!memcmp(before, frame, sizeof before));
    engine.harmony.motion = 0u;
    render(&ui, 1000u, before);
    render(&ui, 1180u, frame);
    CHECK(!memcmp(before, frame, sizeof before));
    engine.harmony.motion = 1u; instrument.animations_enabled = 0u;
    render(&ui, 1000u, before);
    render(&ui, 1180u, frame);
    CHECK(!memcmp(before, frame, sizeof before));
    engine.harmony.input_mask = 0u; engine.harmony.base_mask = 0u; engine.harmony.count = 0u;
    render(&still, 2501u, frame);
    CHECK(matches_text(frame, 68u, 21u, "Play a chord", 12u));
    CHECK(matches_text(frame, 68u, 34u, "NO MATCH", 12u));
    engine.harmony.cache_ready = 0u;
    render(&still, 2502u, frame);
    CHECK(matches_text(frame, 68u, 34u, "MAPPING...", 12u));
    return 1;
}

static int test_harmony_focus_separates_base_chord_and_complete_melody_classes(void)
{
    harmony_fixture();engine.harmony.motion=0u;
    engine.harmony.right_mask=0x0044u;engine.harmony.input_mask=0x00d5u;
    static thy_engine frozen;frozen=engine;render(&still,1000u,frame);
    CHECK(matches_text(frame,68u,21u,"C MAJ",12u));
    CHECK(matches_text(frame,68u,43u,"D MIN",12u));
    CHECK(!memcmp(&engine,&frozen,sizeof engine));
    CHECK(pixel_at(frame,47u,27u)); /* Added D is solid; suggestions alone are open. */
    return 1;
}
static int test_harmony_four_views_status_and_details_follow_physical_clusters(void)
{
    harmony_fixture();render(&still,2000u,before);
    for(unsigned view=0;view<4u;++view){engine.harmony.view=(uint8_t)view;render(&still,2000u,frame);
        CHECK(!memcmp(frame,before,sizeof frame));CHECK(thy_studio_ui_scene_key(&state)==UINT32_C(0xD0100));}
    instrument.cluster_swap=1;engine.harmony.control_bank=1;instrument.performance.melody_octave=2;
    render(&still,2001u,frame);CHECK(matches_text(frame,2u,58u,"OCT L+2 <BANK >SET O MENU",25u));
    engine.harmony.octave_target=0;instrument.performance.chord_octave=-1;
    render(&still,2002u,frame);CHECK(matches_text(frame,2u,58u,"OCT R-1 <BANK >SET O MENU",25u));
    engine.harmony.choices[0].reason=THY_HARMONY_SECONDARY;render(&still,2003u,frame);
    CHECK(matches_text(frame,68u,51u,"Leads to G",12u));
    return 1;
}

static int test_harmony_key_and_scale_pages_override_chord_view_without_key_changes(void)
{
    harmony_fixture();engine.harmony.page=THY_HARMONY_KEY;
    static thy_instrument frozen;frozen=instrument;
    engine.harmony.view=0u;render(&still,3000u,before);
    for(unsigned view=1u;view<4u;++view) {
        engine.harmony.view=(uint8_t)view;render(&still,3000u,frame);
        CHECK(thy_studio_ui_scene_key(&state)==UINT32_C(0xD0002));
        CHECK(!memcmp(frame,before,sizeof frame));
    }
    CHECK(matches_text(frame,68u,13u,"KEY PREVIEW",12u));
    CHECK(matches_text(frame,2u,58u,"SUG R+0 KEY PREVIEW",25u));
    CHECK(!memcmp(&instrument,&frozen,sizeof instrument));
    engine.harmony.page=THY_HARMONY_SCALES;engine.harmony.view=0u;
    render(&still,3001u,before);
    for(unsigned view=1u;view<4u;++view) {
        engine.harmony.view=(uint8_t)view;render(&still,3001u,frame);
        CHECK(thy_studio_ui_scene_key(&state)==UINT32_C(0xD0001));
        CHECK(!memcmp(frame,before,sizeof frame));
    }
    CHECK(matches_text(frame,2u,58u,"SUG R+0 <BANK >SET O MENU",25u));
    CHECK(!memcmp(&instrument,&frozen,sizeof instrument));
    return 1;
}

int main(int argc,char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--harmony-fixtures")) {
        harmony_fixture();
        for(unsigned view=0u;view<4u;++view) {
            engine.harmony.view=(uint8_t)view;render(&still,1000u,frame);
            printf("HARMONY-VIEW%u ",view);
            for(unsigned n=0u;n<THY_UI_FRAME_BYTES;++n)printf("%02x",frame[n]);
            putchar('\n');
        }
        for (unsigned page = 0u; page <= THY_HARMONY_KEY; ++page) {
            engine.harmony.page = (uint8_t)page;
            render(&still, 1000u, frame);
            printf("HARMONY%u ", page);
            for (unsigned n = 0u; n < THY_UI_FRAME_BYTES; ++n) printf("%02x", frame[n]);
            putchar('\n');
        }
        return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--branding-fixtures")){
        initialize();thy_studio_ui_boot_begin(&ui,0u);ui.boot_elapsed_ms=THY_STUDIO_UI_BOOT_REVEAL_MS;
        render(&ui,0u,before);thy_studio_ui_boot_skip(&ui);state.about_open=1u;
        thy_studio_ui_set_build_identity(&ui,"BUILD 0123456789");render(&ui,1u,frame);
        const uint8_t *frames[]={before,frame};const char *names[]={"BOOT","CREDITS"};
        for(unsigned f=0;f<2u;++f){printf("%s ",names[f]);for(unsigned n=0;n<THY_UI_FRAME_BYTES;++n)printf("%02x",frames[f][n]);putchar('\n');}
        return 0;
    }
    if(argc!=1)return 1;
    if (!test_geometry_catalog_and_render_is_read_only() ||
        !test_play_projection_and_opposite_hierarchy_slides() ||
        !test_cursor_and_editor_changes_do_not_slide() ||
        !test_wrap_reduced_motion_and_interruption() ||
        !test_frozen_transfer_budget_completion() ||
        !test_hierarchy_slides_finish_at_wall_clock_deadline_after_stalls() ||
        !test_hierarchy_slide_uses_current_time_at_fast_or_irregular_cadence() ||
        !test_navigation_motion_policy_returns_exact_destination_and_can_restore_slides() ||
        !test_navigation_motion_policy_preserves_play_graphics_and_global_reduced_motion() ||
        !test_navigation_motion_policy_preserves_startup_reveal_and_handoff() ||
        !test_scene_keys_distinguish_page_groups_but_ignore_cursor_motion() ||
        !test_scene_keys_ignore_live_owners_editor_drafts_and_committed_values() ||
        !test_scene_keys_identify_each_info_page_credits_quick_page_and_play_view() ||
        !test_scene_key_quick_page_precedence_matches_the_renderer() ||
        !test_live_play_views_follow_held_notes_routes_and_layout() ||
        !test_live_navigation_and_oled_share_visible_canonical_rows() ||
        !test_chord_controls_follow_layout_and_voicing_context() ||
        !test_information_rows_show_read_only_text_without_service_errors() ||
        !test_route_overview_follows_all_live_routing_profiles() ||
        !test_boot_before_state_has_real_milestones_and_ready_gate() ||
        !test_boot_handoff_has_exact_endpoints_and_current_play() ||
        !test_boot_reduced_motion_and_missing_state_are_safe() ||
        !test_boot_input_never_delays_menus_or_note_ownership() ||
        !test_boot_wrap_and_stalls_advance_one_completed_frame() ||
        !test_navigation_legends_and_center_escape_from_editor() ||
        !test_compact_views_show_effective_routes_and_optional_counts() ||
        !test_simple_five_notes_use_four_large_corners_and_large_center() ||
        !test_simple_six_notes_begin_compact_wrapping_without_ownership_changes() ||
        !test_pretty_is_note_driven_bounded_and_respects_reduced_motion() ||
        !test_credits_share_the_boot_wordmark_and_real_optional_identity() ||
        !test_full_scale_names_remain_readable_without_mutating_state() ||
        !test_engines_gear_is_distinct_and_descriptions_require_selection() ||
        !test_basic_advanced_divider_never_adds_a_navigation_target() ||
        !test_sequencer_canvases_are_live_read_only_and_have_scene_keys() ||
        !test_sequencer_help_is_paginated_short_and_read_only() ||
        !test_track_settings_have_a_dedicated_title_scene_and_return_legend() ||
        !test_left_navigation_and_confirmation_glyphs_are_visible() ||
        !test_scoped_sensitivity_graphs_and_pick_prompt_are_read_only() ||
        !test_resource_clock_and_read_only_screen() ||
        !test_storage_bars_count_real_record_and_song_usage_without_encoding() ||
        !test_pad_target_editor_explains_all_shortcut_without_applying() ||
        !test_sensitivity_edit_footer_uses_frozen_allowed_range_and_preserves_errors() ||
        !test_calibration_wizard_progress_actions_and_preview_are_read_only() ||
        !test_harmony_workspace_frames_are_live_read_only_and_preview_only() ||
        !test_harmony_workspace_motion_is_bounded_optional_and_note_local() ||
        !test_harmony_focus_separates_base_chord_and_complete_melody_classes() ||
        !test_harmony_four_views_status_and_details_follow_physical_clusters() ||
        !test_harmony_key_and_scale_pages_override_chord_view_without_key_changes()) return 1;
    printf("46 shared studio UI/transfer scenarios passed (%u assertions)\n", assertions);
    return 0;
}
