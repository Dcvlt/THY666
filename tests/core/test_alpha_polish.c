/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_engine.h"
#include "../../core/thy_lighting.h"

/* These are host integration tests: the same planner and menu adapters run in
 * WebAssembly and the instrument. No physical sensor timing is inferred. */
static thy_core core;
static thy_instrument instrument;
static thy_engine engine;
static thy_instrument_pad pads[THY_PAD_COUNT], original[THY_PAD_COUNT];
static unsigned assertions;
static const int16_t degrees[] = {0, 2, 4, 5, 7, 9, 11};

#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 0; \
} } while (0)

static int setup(void)
{
    memset(pads, 0, sizeof pads);
    for (unsigned n = 0u; n < 48u; ++n)
        pads[n] = (thy_instrument_pad){{57, 62, 65}, 7u, 2u}; /* Dm/A, degree II. */
    for (unsigned n = 0u; n < 48u; ++n)
        pads[48u + n] = (thy_instrument_pad){{(int16_t)(60 + (int)(n / 4u) - 12 * (int)(n % 4u))}, 1u, 0u};
    /* Degree references are archive metadata, independent of generated modes. */
    for (unsigned n = 0u; n < 7u; ++n)
        pads[48u + 4u * n].notes[0] = (int16_t)(60 + degrees[n]);
    memcpy(original, pads, sizeof pads);
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    CHECK(thy_instrument_set_layout(&instrument, pads, 60, degrees, 7u) == THY_OK);
    CHECK(thy_instrument_set_route(&instrument, 0u, 1u, THY_ROUTE_USB0 | THY_ROUTE_DIN, 0) == THY_OK);
    CHECK(thy_instrument_set_route(&instrument, 1u, 1u, THY_ROUTE_USB0 | THY_ROUTE_DIN, 0) == THY_OK);
    thy_engine_init(&engine, &instrument, 0u, 70u);
    return 1;
}

static void drain(void)
{
    thy_midi_event event;
    while (thy_core_poll(&core, &event)) { }
}

static int one_pitch(unsigned logical, int expected)
{
    int16_t notes[THY_MAX_NOTES];
    size_t count = 0u;
    CHECK(thy_instrument_preview_pad(&instrument, (uint8_t)logical, notes, &count) == THY_OK);
    CHECK(count == 1u && notes[0] == expected);
    return 1;
}

static int32_t value(uint16_t row)
{
    int32_t result = INT32_MIN;
    (void)thy_state_get(&engine.state, row, &result);
    return result;
}

static int test_generated_layouts_are_playable_and_preserve_the_archive(void)
{
    static const int16_t tuning[] = {40, 45, 50, 55, 59, 64};
    CHECK(setup());
    instrument.performance.melody_layout = THY_LAYOUT_GUITAR;
    for (unsigned window = 0u; window < 3u; ++window) {
        instrument.performance.guitar_window = (uint8_t)window;
        for (unsigned column = 0u; column < 12u; ++column)
            for (unsigned row = 0u; row < 4u; ++row)
                CHECK(one_pitch(48u + column * 4u + row, tuning[window + row] + (int)column));
    }
    /* Standard tuning is absolute. A changed musical root/anchor does not
     * retune its strings; the explicit transpose and octave controls do. */
    instrument.performance.root = 7u;
    CHECK(thy_instrument_set_layout(&instrument, pads, 67, degrees, 7u) == THY_OK);
    CHECK(one_pitch(48u, 50));
    CHECK(thy_instrument_set_route(&instrument, 1u, 1u, THY_ROUTE_DIN, 2) == THY_OK);
    instrument.performance.melody_octave = 1;
    CHECK(one_pitch(48u, 64));
    CHECK(one_pitch(95u, 89));
    CHECK(thy_instrument_set_route(&instrument, 1u, 1u, THY_ROUTE_DIN, 0) == THY_OK);
    instrument.performance.melody_octave = 0;
    CHECK(thy_instrument_set_layout(&instrument, pads, 60, degrees, 7u) == THY_OK);
    instrument.performance.melody_layout = THY_LAYOUT_CONDENSED;
    for (unsigned column = 0u; column < 12u; ++column)
        for (unsigned row = 0u; row < 4u; ++row)
            CHECK(one_pitch(48u + column * 4u + row,
                60 + degrees[column % 7u] + 12 * (int)(column / 7u) - 12 * (int)row));
    /* Illumination-only policies retain chromatic pitch geometry. Piano has
     * two white/black row pairs, including actual non-playing gaps. */
    for (unsigned mode = THY_LAYOUT_IN_KEY_LIT; mode <= THY_LAYOUT_ALL_LIT; ++mode) {
        instrument.performance.melody_layout = (uint8_t)mode;
        for (unsigned column = 0u; column < 12u; ++column)
            for (unsigned row = 0u; row < 4u; ++row)
                CHECK(one_pitch(48u + column * 4u + row, 60 + (int)column - 12 * (int)row));
    }
    static const int16_t keyboard[4][12] = {
        {-1,61,-1,63,-1,-1,66,-1,68,-1,70,-1},
        {60,-1,62,-1,64,65,-1,67,-1,69,-1,71},
        {-1,49,-1,51,-1,-1,54,-1,56,-1,58,-1},
        {48,-1,50,-1,52,53,-1,55,-1,57,-1,59}
    };
    instrument.performance.melody_layout = THY_LAYOUT_PIANO;
    for (unsigned column = 0u; column < 12u; ++column)
        for (unsigned row = 0u; row < 4u; ++row) {
            const uint8_t logical = (uint8_t)(48u + column * 4u + row);
            if (keyboard[row][column] >= 0) CHECK(one_pitch(logical, keyboard[row][column]));
            else {
                int16_t notes[THY_MAX_NOTES]; size_t count = 99u;
                CHECK(thy_instrument_preview_pad(&instrument, logical, notes, &count) == THY_OK);
                CHECK(count == 0u);
            }
        }
    CHECK(memcmp(pads, original, sizeof pads) == 0);
    CHECK(thy_core_queued(&core) == 0u && thy_core_sounding(&core) == 0u);
    return 1;
}

static int test_layout_edits_cannot_retarget_held_midi_owners(void)
{
    CHECK(setup());
    instrument.performance.melody_layout = THY_LAYOUT_GUITAR;
    CHECK(thy_instrument_press_pad(&instrument, 54u) == THY_OK); /* D string, fret 1: Eb3. */
    CHECK(core.owners[54].held && core.owners[54].count == 2u);
    const thy_pad_owner held = core.owners[54];
    CHECK(thy_core_sounding(&core) == 2u);
    drain();
    instrument.performance.melody_layout = THY_LAYOUT_PIANO;
    instrument.performance.guitar_window = 2u;
    CHECK(thy_instrument_set_layout(&instrument, pads, 72, degrees, 7u) == THY_OK);
    CHECK(thy_instrument_set_route(&instrument, 1u, 9u, THY_ROUTE_USB1, 5) == THY_OK);
    CHECK(one_pitch(54u, 66)); /* Lower black row: C#4 plus route transpose 5. */
    CHECK(thy_instrument_press_pad(&instrument, 54u) == THY_OK); /* Idempotent. */
    CHECK(memcmp(&held, &core.owners[54], sizeof held) == 0);
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_instrument_release_pad(&instrument, 54u) == THY_OK);
    thy_midi_event event;
    unsigned offs = 0u;
    while (thy_core_poll(&core, &event)) {
        CHECK(event.status == 0x81u && event.data1 == 51u && event.data2 == 0u);
        CHECK(event.destination == 0u || event.destination == 2u);
        ++offs;
    }
    CHECK(offs == 2u && thy_core_sounding(&core) == 0u && thy_core_reserved(&core) == 0u);
    CHECK(thy_instrument_press_pad(&instrument, 54u) == THY_OK);
    CHECK(thy_core_poll(&core, &event));
    CHECK(event.status == 0x99u && event.data1 == 66u && event.destination == 1u);
    CHECK(!thy_core_poll(&core, &event));
    CHECK(thy_instrument_release_pad(&instrument, 54u) == THY_OK);
    return 1;
}

static int test_context_controls_use_mapping_and_never_own_notes(void)
{
    static const uint8_t logicals[] = {32u, 36u, 40u, 44u, 88u, 92u};
    uint8_t mapping[THY_PAD_COUNT];
    CHECK(setup());
    for (unsigned n = 0u; n < THY_PAD_COUNT; ++n) mapping[n] = (uint8_t)n;
    mapping[32] = 3u;
    mapping[3] = 32u;
    CHECK(thy_instrument_set_mapping(&instrument, mapping) == THY_OK);
    CHECK(thy_instrument_set_context_controls(&instrument, 1u) == THY_OK);
    for (unsigned n = 0u; n < 6u; ++n) {
        unsigned logical = logicals[n], electrical = logical == 32u ? 3u : logical;
        int16_t notes[THY_MAX_NOTES];
        size_t count = 99u;
        CHECK(thy_instrument_context_pad(&instrument, (uint8_t)logical) == (int)n + 1);
        CHECK(thy_instrument_preview_pad(&instrument, (uint8_t)logical, notes, &count) == THY_OK && count == 0u);
        CHECK(thy_instrument_press_pad(&instrument, (uint8_t)electrical) == THY_OK);
        const thy_instrument_performance after = instrument.performance;
        CHECK(instrument.context_held[electrical] && !core.owners[electrical].held);
        CHECK(thy_instrument_press_pad(&instrument, (uint8_t)electrical) == THY_OK);
        CHECK(memcmp(&after, &instrument.performance, sizeof after) == 0);
        CHECK(thy_instrument_set_context_controls(&instrument, 0u) != THY_OK);
        CHECK(instrument.context_controls == 1u);
        CHECK(thy_core_queued(&core) == 0u && thy_core_sounding(&core) == 0u);
        CHECK(thy_instrument_release_pad(&instrument, (uint8_t)electrical) == THY_OK);
        CHECK(!instrument.context_held[electrical]);
    }
    CHECK(instrument.performance.chord_octave == 0 && instrument.performance.melody_octave == 0);
    CHECK(instrument.performance.inversion == 1u && !instrument.performance.spread && instrument.bass_octave == -1);
    CHECK(thy_instrument_context_pad(&instrument, 3u) == 0);
    CHECK(thy_instrument_press_pad(&instrument, 32u) == THY_OK); /* Mapped musical chord. */
    CHECK(core.owners[32].held && thy_core_sounding(&core) != 0u);
    CHECK(thy_instrument_set_context_controls(&instrument, 0u) != THY_OK);
    CHECK(thy_instrument_release_pad(&instrument, 32u) == THY_OK);
    drain();
    CHECK(thy_instrument_set_context_controls(&instrument, 0u) == THY_OK);
    CHECK(thy_instrument_context_pad(&instrument, 3u) == 0);
    CHECK(thy_core_sounding(&core) == 0u);
    return 1;
}

static int test_inverted_chords_capture_harmonic_color_without_revoicing(void)
{
    int16_t root = -1;
    CHECK(setup());
    CHECK(thy_instrument_chord_root(&instrument, 0u, &root) == THY_OK && root == 62);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(instrument.held_color_valid[0] && instrument.held_color_root[0] == 62u);
    CHECK(instrument.held_color_degree[0] == 1u);
    CHECK((thy_core_owner_key(&core, (uint8_t)(0), (uint8_t)(0)) & 127u) == 57u); /* Original A bass, not rotated to D. */
    const thy_pad_owner owner = core.owners[0];
    drain();
    CHECK(thy_instrument_set_route(&instrument, 0u, 2u, THY_ROUTE_DIN, 3) == THY_OK);
    instrument.performance.inversion = 1u;
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(instrument.held_color_root[0] == 62u && instrument.held_color_degree[0] == 1u);
    CHECK(memcmp(&owner, &core.owners[0], sizeof owner) == 0);
    CHECK(memcmp(pads, original, sizeof pads) == 0);
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    CHECK(!instrument.held_color_valid[0]);
    drain();
    /* Unknown archive tokens must not masquerade as a scale degree. */
    pads[0].color = 0u;
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    CHECK(instrument.held_color_valid[0] && instrument.held_color_degree[0] == UINT8_MAX);
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    drain();
    /* Panic clears transient lighting/control ownership as well as notes. */
    CHECK(thy_instrument_set_context_controls(&instrument, 1u) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 32u) == THY_OK);
    CHECK(instrument.context_held[32]);
    CHECK(thy_instrument_press_pad(&instrument, 3u) == THY_OK);
    thy_instrument_panic(&instrument);
    CHECK(!instrument.context_held[32] && !instrument.held_color_valid[3]);
    CHECK(!core.owners[3].held && thy_core_sounding(&core) == 0u);
    return 1;
}

static int test_new_menu_bindings_reach_the_shared_instrument(void)
{
    static const uint8_t layouts[] = {0u, 4u, 7u, 1u, 8u, 5u, 2u, 3u, 6u};
    static const struct { uint16_t row; int32_t edit; } edits[] = {
        {THY_ROW_PERFORMANCE_GUITARWINDOW, 2}, {THY_ROW_SENSITIVITY_CENTERRESPONSE, 24},
        {THY_ROW_LIGHTS_CHORDCOLOR, 2}, {THY_ROW_LIGHTS_NOTECOLORS, 1},
        {THY_ROW_LIGHTS_KEYCONTRAST, 0}, {THY_ROW_LIGHTS_HELDSTYLE, 1},
        {THY_ROW_LIGHTS_HELDFEEDBACK, 0}, {THY_ROW_LIGHTS_VIEW, THY_VIEW_ORBIT},
        {THY_ROW_LIGHTS_ANIMATIONS, 0}
    };
    CHECK(setup());
    CHECK(thy_state_set_design_mode(&engine.state, 0u) == THY_STATE_OK);
    for (unsigned n = 0u; n < sizeof layouts / sizeof layouts[0]; ++n) {
        CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_MELODYLAYOUT, (int32_t)n) == THY_STATE_OK);
        CHECK(instrument.performance.melody_layout == layouts[n]);
        CHECK(value(THY_ROW_PERFORMANCE_MELODYLAYOUT) == (int32_t)n);
    }
    for (unsigned n = 0u; n < sizeof edits / sizeof edits[0]; ++n) {
        CHECK(thy_menu_rows[edits[n].row].status == THY_MENU_LIVE);
        CHECK(thy_state_set(&engine.state, edits[n].row, edits[n].edit) == THY_STATE_OK);
        CHECK(value(edits[n].row) == edits[n].edit);
    }
    CHECK(instrument.performance.guitar_window == 2u && instrument.center_poll_ms == 24u);
    CHECK(instrument.light_chord_mode == 2u && instrument.light_palette == 1u);
    CHECK(!instrument.light_key_contrast && instrument.light_held_style == 1u);
    CHECK(!instrument.light_held_feedback && instrument.display_view == THY_VIEW_ORBIT && !instrument.animations_enabled);
    CHECK(thy_state_set(&engine.state, THY_ROW_SENSITIVITY_CENTERRESPONSE, 7) == THY_STATE_INVALID);
    CHECK(thy_state_set(&engine.state, THY_ROW_SENSITIVITY_CENTERRESPONSE, 81) == THY_STATE_INVALID);
    CHECK(instrument.center_poll_ms == 24u);
    CHECK(thy_state_option_allowed(&engine.state, THY_ROW_LIGHTS_VIEW, 4));
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_VIEW, 4) == THY_STATE_OK);
    CHECK(instrument.display_view == 4u);
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_PADOVERLAYS, 1) == THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument, 88u) == THY_OK);
    const uint32_t revision = engine.state.revision;
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_PADOVERLAYS, 0) != THY_STATE_OK);
    CHECK(engine.state.revision == revision && instrument.context_controls);
    CHECK(thy_instrument_release_pad(&instrument, 88u) == THY_OK);
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_PADOVERLAYS, 0) == THY_STATE_OK);
    char label[32];
    CHECK(thy_state_format(&engine.state, THY_ROW_PERFORMANCE_GUITARSHORTCUT, 0, label, sizeof label) == THY_STATE_OK);
    CHECK(strcmp(label, "Run") == 0);
    const uint32_t action_revision = engine.state.revision;
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_PERFORMANCE_GUITARSHORTCUT) == THY_STATE_OK);
    CHECK(!engine.state.editing && engine.state.revision == action_revision + 1u);
    CHECK(instrument.performance.melody_layout == THY_LAYOUT_GUITAR && instrument.display_view == THY_VIEW_SIMPLE);
    CHECK(engine.state.notice == THY_NOTICE_APPLIED && thy_core_queued(&core) == 0u);
    return 1;
}

static int test_future_design_controls_require_opt_in_and_cannot_start_engines(void)
{
    CHECK(setup());
    CHECK(thy_menu_rows[THY_ROW_ARPS_KEEPKEY].status == THY_MENU_DESIGN);
    CHECK(thy_menu_rows[THY_ROW_ARPS_ENABLED].status == THY_MENU_LIVE);
    CHECK(thy_state_set_design_mode(&engine.state, 0u) == THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_ARPS_KEEPKEY) == THY_STATE_UNAVAILABLE);
    CHECK(!engine.state.editing && value(THY_ROW_ARPS_KEEPKEY) == 0);
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_ARPS_ENABLED) == THY_STATE_OK);
    CHECK(thy_state_edit_set(&engine.state, 1) == THY_STATE_OK);
    CHECK(thy_state_commit(&engine.state) == THY_STATE_OK && engine.arps[0].config.enabled);
    const thy_arp_config live_config = engine.arps[0].config;
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_LIGHTS_NOTECOLORS) == THY_STATE_OK);
    CHECK(thy_state_edit_set(&engine.state, 1) == THY_STATE_OK);
    CHECK(thy_state_commit(&engine.state) == THY_STATE_OK && instrument.light_palette == 1u);
    CHECK(thy_state_set_design_mode(&engine.state, 1u) == THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_ARPS_KEEPKEY) == THY_STATE_OK);
    CHECK(thy_state_edit_set(&engine.state, 1) == THY_STATE_OK);
    CHECK(thy_state_commit(&engine.state) == THY_STATE_OK && value(THY_ROW_ARPS_KEEPKEY) == 1);
    CHECK(memcmp(&engine.arps[0].config, &live_config, sizeof live_config) == 0);
    CHECK(engine.session.transport == THY_ENGINE_STOPPED && engine.session.capture == THY_ENGINE_CAPTURE_OFF);
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_ARPS_KEEPKEY) == THY_STATE_OK);
    CHECK(thy_state_edit_set(&engine.state, 0) == THY_STATE_OK);
    CHECK(thy_state_set_design_mode(&engine.state, 0u) == THY_STATE_OK);
    CHECK(!engine.state.editing && value(THY_ROW_ARPS_KEEPKEY) == 1); /* Draft discarded. */
    CHECK(memcmp(&engine.arps[0].config, &live_config, sizeof live_config) == 0);
    CHECK(thy_state_set_design_mode(&engine.state, 2u) == THY_STATE_INVALID);
    CHECK(!engine.state.design_editing);
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_SENSITIVITY_TOUCHCONFIG) == THY_STATE_UNAVAILABLE);
    CHECK(thy_core_queued(&core) == 0u && thy_core_sounding(&core) == 0u);
    return 1;
}

static int tap_function(uint8_t index)
{
    CHECK(thy_instrument_control(&instrument, 1u, index, 1) == THY_OK);
    CHECK(thy_instrument_control(&instrument, 1u, index, 0) == THY_OK);
    return 1;
}

static int test_global_slot_switches_and_optional_selected_chord_scope(void)
{
    int16_t notes[THY_MAX_NOTES]; size_t count = 0u;
    CHECK(setup());
    CHECK(thy_instrument_set_lower_mode(&instrument, THY_LOWER_STORED_NOTES) == THY_OK);
    CHECK(instrument.slot_scope == THY_SLOTS_LEFT_SIDE);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    const thy_pad_owner held = core.owners[0];
    CHECK(tap_function(7u));
    CHECK(instrument.global_excluded_slots == 1u && !instrument.excluded_slots[0]);
    CHECK(memcmp(&held, &core.owners[0], sizeof held) == 0);
    for (unsigned n = 0u; n < 48u; ++n) {
        CHECK(thy_instrument_preview_pad(&instrument, (uint8_t)n, notes, &count) == THY_OK);
        CHECK(count == 2u && notes[0] == 62 && notes[1] == 65);
    }
    CHECK(thy_instrument_preview_pad(&instrument, 48u, notes, &count) == THY_OK && count == 1u);
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    drain();
    CHECK(thy_instrument_set_layout(&instrument, pads, 60, degrees, 7u) == THY_OK);
    CHECK(instrument.global_excluded_slots == 1u);
    CHECK(thy_state_set(&engine.state, THY_ROW_CONTROLS_SLOTSCOPE, THY_SLOTS_SELECTED_PAD) == THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument, 12u) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument, 12u) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument, 48u) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument, 48u) == THY_OK);
    CHECK(instrument.selected_pad == 48u && instrument.selected_chord == 12u);
    CHECK(tap_function(8u));
    CHECK(instrument.excluded_slots[12] == 2u && !instrument.excluded_slots[0]);
    CHECK(thy_instrument_preview_pad(&instrument, 12u, notes, &count) == THY_OK && count == 2u);
    CHECK(notes[0] == 57 && notes[1] == 65);
    CHECK(thy_instrument_preview_pad(&instrument, 0u, notes, &count) == THY_OK && count == 3u);
    CHECK(thy_state_set(&engine.state, THY_ROW_CONTROLS_SLOTSCOPE, THY_SLOTS_LEFT_SIDE) == THY_STATE_OK);
    CHECK(thy_instrument_preview_pad(&instrument, 0u, notes, &count) == THY_OK && count == 2u);
    CHECK(instrument.global_excluded_slots == 1u && instrument.excluded_slots[12] == 2u);
    CHECK(memcmp(pads, original, sizeof pads) == 0);
    return 1;
}

static int test_context_relocation_is_quiet_and_maps_both_grids(void)
{
    static const uint8_t lower[] = {3u, 7u, 11u, 15u, 51u, 55u};
    CHECK(setup());
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_PADOVERLAYS, 1) == THY_STATE_OK);
    CHECK(thy_instrument_context_pad(&instrument, 32u) == 1);
    for (unsigned logical = 0u; logical < THY_PAD_COUNT; ++logical) {
        unsigned local = logical % 48u, row = local % 4u, column = local / 4u;
        int expected = row == 0u ? (logical < 48u ?
            (column >= 8u ? (int)column - 7 : 0) :
            (column >= 10u ? (int)column - 5 : 0)) : 0;
        CHECK(thy_instrument_context_pad(&instrument, (uint8_t)logical) == expected);
    }
    CHECK(thy_instrument_press_pad(&instrument, 32u) == THY_OK);
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_CONTEXTLOCATION, 1) == THY_STATE_BLOCKED);
    CHECK(engine.state.notice == THY_NOTICE_RELEASE);
    CHECK(!instrument.context_location && instrument.context_held[32]);
    CHECK(thy_instrument_release_pad(&instrument, 32u) == THY_OK);
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_CONTEXTLOCATION, 1) == THY_STATE_OK);
    CHECK(thy_instrument_context_pad(&instrument, 32u) == 0);
    for (unsigned logical = 0u; logical < THY_PAD_COUNT; ++logical) {
        unsigned local = logical % 48u, row = local % 4u, column = local / 4u;
        int expected = row == 3u ? (logical < 48u ?
            (column < 4u ? (int)column + 1 : 0) :
            (column < 2u ? (int)column + 5 : 0)) : 0;
        CHECK(thy_instrument_context_pad(&instrument, (uint8_t)logical) == expected);
    }
    for (unsigned n = 0u; n < 6u; ++n) {
        CHECK(thy_instrument_context_pad(&instrument, lower[n]) == (int)n + 1);
        CHECK(thy_instrument_press_pad(&instrument, lower[n]) == THY_OK);
        CHECK(!core.owners[lower[n]].held && instrument.context_held[lower[n]]);
        CHECK(thy_instrument_release_pad(&instrument, lower[n]) == THY_OK);
    }
    CHECK(thy_core_queued(&core) == 0u && thy_core_sounding(&core) == 0u);
    CHECK(memcmp(pads, original, sizeof pads) == 0);
    return 1;
}

static int test_play_shortcuts_preserve_menu_navigation_and_hidden_arrows(void)
{
    uint16_t levels[THY_INSTRUMENT_CENTER_LIGHTS];
    CHECK(setup());
    thy_engine_bind_controls(&engine);
    CHECK(instrument.play_nav[0] == THY_PLAY_ENGINES && instrument.play_nav[1] == THY_PLAY_VIEWS);
    CHECK(thy_engine_navigate(&engine, THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(engine.state.level == THY_STATE_ROWS && thy_state_current_row(&engine.state) == THY_ROW_ENGINES_PLAY);
    CHECK(thy_engine_navigate(&engine, THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(thy_state_set(&engine.state,THY_ROW_CONTROLS_LEFT,THY_PLAY_HIDDEN) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine, THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(engine.state.level == THY_STATE_PLAY);
    CHECK(thy_instrument_control(&instrument, 0u, THY_STATE_LEFT, 1) == THY_OK);
    thy_engine_center_levels(&engine, levels);
    CHECK(!levels[0] && levels[1] && levels[2] && levels[3] && levels[4]);
    CHECK(thy_instrument_control(&instrument, 0u, THY_STATE_LEFT, 0) == THY_OK);
    CHECK(thy_engine_navigate(&engine, THY_STATE_UP) == THY_STATE_OK);
    CHECK(engine.state.level == THY_STATE_ROWS && thy_state_current_row(&engine.state) == THY_ROW_LIGHTS_VIEW);
    CHECK(thy_engine_navigate(&engine, THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(engine.state.level == THY_STATE_GROUPS);
    CHECK(thy_engine_navigate(&engine, THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(engine.state.level == THY_STATE_PLAY);
    CHECK(thy_engine_navigate(&engine, THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(engine.state.level == THY_STATE_GROUPS);
    CHECK(thy_engine_navigate(&engine, THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(engine.state.level == THY_STATE_PLAY);
    CHECK(thy_engine_navigate(&engine, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(engine.state.level == THY_STATE_ROWS &&
        thy_state_current_row(&engine.state) == THY_ROW_PERFORMANCE_ROOT);
    CHECK(thy_engine_navigate(&engine, THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(engine.state.level == THY_STATE_PLAY);
    CHECK(thy_state_set(&engine.state, THY_ROW_CONTROLS_LEFT, THY_PLAY_CHORDS) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine, THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(thy_state_current_row(&engine.state) == THY_ROW_PERFORMANCE_ROOT);
    CHECK(engine.session.transport == THY_ENGINE_STOPPED && engine.session.playing_mode == instrument.engine_kind);
    CHECK(thy_engine_navigate(&engine, 5u) == THY_STATE_INVALID);
    return 1;
}

static int test_center_lights_follow_committed_performance_and_brightness(void)
{
    uint16_t levels[THY_INSTRUMENT_CENTER_LIGHTS];
    CHECK(setup());
    CHECK(thy_instrument_set_lower_mode(&instrument, THY_LOWER_STORED_NOTES) == THY_OK);
    thy_engine_center_levels(&engine, levels);
    CHECK(levels[5] < levels[12] && levels[12] == 4095u);
    CHECK(tap_function(0u) && tap_function(5u) && tap_function(3u) && tap_function(7u));
    thy_engine_center_levels(&engine, levels);
    CHECK(levels[5] == 4095u && levels[6] == 256u);
    CHECK(levels[10] == 4095u && levels[9] == 256u && levels[11] == 256u);
    CHECK(levels[8] == 4095u && levels[12] == 256u && levels[13] == 4095u);
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_CENTER, 4) == THY_STATE_OK);
    thy_engine_center_levels(&engine, levels);
    CHECK(levels[5] == 2048u && levels[12] == 128u);
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_CENTER, 0) == THY_STATE_OK);
    thy_engine_center_levels(&engine, levels);
    for (unsigned n = 0u; n < THY_INSTRUMENT_CENTER_LIGHTS; ++n) CHECK(!levels[n]);
    CHECK(instrument.performance.inversion == 1u && instrument.performance.spread == 2u);
    CHECK(thy_core_queued(&core) == 0u);
    return 1;
}

static int test_bass_shift_and_optional_overlay_cells_preserve_notes_and_owners(void)
{
    int16_t notes[THY_MAX_NOTES]; size_t count = 0u;
    CHECK(setup());
    CHECK(thy_instrument_set_lower_mode(&instrument, THY_LOWER_STORED_NOTES) == THY_OK);
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_BASSOCTAVE, -1) == THY_STATE_OK);
    CHECK(thy_instrument_preview_pad(&instrument, 0u, notes, &count) == THY_OK && count == 3u);
    CHECK(notes[0] == 45 && notes[1] == 62 && notes[2] == 65);
    CHECK(thy_instrument_preview_pad(&instrument, 48u, notes, &count) == THY_OK && count == 1u && notes[0] == 60);
    CHECK(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    const thy_pad_owner owner = core.owners[0];
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_BASSOCTAVE, -2) == THY_STATE_OK);
    CHECK(memcmp(&owner, &core.owners[0], sizeof owner) == 0);
    CHECK(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    drain();
    instrument.bass_octave = -1;
    instrument.performance.inversion = 1u;
    CHECK(thy_instrument_preview_pad(&instrument, 0u, notes, &count) == THY_OK && count == 3u);
    CHECK(notes[0] == 69 && notes[1] == 50 && notes[2] == 65); /* Actual bass follows inversion. */
    instrument.global_excluded_slots = 2u;
    instrument.performance.upper_octaves = 1u;
    CHECK(thy_instrument_preview_pad(&instrument, 0u, notes, &count) == THY_OK && count == 4u);
    CHECK(notes[0] == 69 && notes[1] == 53 && notes[2] == 81 && notes[3] == 65);
    CHECK(memcmp(pads, original, sizeof pads) == 0);
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_PADOVERLAYS, 1) == THY_STATE_OK);
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_CONTEXTCHORDOCTAVES, 0) == THY_STATE_OK);
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_CONTEXTMELODYOCTAVES, 0) == THY_STATE_OK);
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_CONTEXTBASS, 0) == THY_STATE_OK);
    CHECK(!thy_instrument_context_pad(&instrument, 32u) && !thy_instrument_context_pad(&instrument, 36u));
    CHECK(!thy_instrument_context_pad(&instrument, 88u) && !thy_instrument_context_pad(&instrument, 92u));
    CHECK(!thy_instrument_context_pad(&instrument, 44u) && thy_instrument_context_pad(&instrument, 40u) == 3);
    CHECK(thy_instrument_press_pad(&instrument, 32u) == THY_OK && core.owners[32].held);
    CHECK(thy_state_set(&engine.state, THY_ROW_PERFORMANCE_CONTEXTCHORDOCTAVES, 1) != THY_STATE_OK);
    CHECK(thy_instrument_release_pad(&instrument, 32u) == THY_OK);
    drain();
    thy_shape shape = {0};
    shape.bass_octave = -2;
    const int16_t low[] = {12, 60, 64};
    notes[0] = -99; count = 99u;
    CHECK(thy_core_preview(&core, low, 3u, 0, &shape, notes, &count) == THY_INVALID_ARGUMENT);
    CHECK(notes[0] == -99 && count == 99u && thy_core_queued(&core) == 0u);
    const int16_t duplicates[] = {57, 57, 65};
    shape.bass_octave = -1;
    CHECK(thy_core_preview(&core, duplicates, 3u, 0, &shape, notes, &count) == THY_OK);
    CHECK(count == 2u && notes[0] == 45 && notes[1] == 65);
    return 1;
}

static int test_view_preferences_are_independent_and_only_show_applicable_options(void)
{
    CHECK(setup());
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_VIEW, THY_VIEW_DETAILED) == THY_STATE_OK);
    CHECK(!thy_state_row_visible(&engine.state, THY_ROW_VIEWS_SLOT2));
    CHECK(thy_state_row_visible(&engine.state, THY_ROW_VIEWS_DETAIL1));
    CHECK(!thy_state_row_visible(&engine.state, THY_ROW_VIEWS_MOTION));
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_VIEW, THY_VIEW_SIMPLE) == THY_STATE_OK);
    CHECK(thy_state_row_visible(&engine.state, THY_ROW_VIEWS_SLOT2));
    CHECK(!thy_state_row_visible(&engine.state, THY_ROW_VIEWS_DETAIL1));
    CHECK(thy_state_set(&engine.state, THY_ROW_VIEWS_SLOT2, THY_VIEW_INFO_OFF) == THY_STATE_OK);
    CHECK(instrument.display_config[THY_VIEW_SIMPLE].slots[1] == THY_VIEW_INFO_OFF);
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_VIEW, THY_VIEW_CHLADNI) == THY_STATE_OK);
    CHECK(value(THY_ROW_VIEWS_SLOT2) == THY_VIEW_INFO_COUNTS);
    CHECK(thy_state_row_visible(&engine.state, THY_ROW_VIEWS_MOTION));
    CHECK(thy_state_set(&engine.state, THY_ROW_VIEWS_MOTION, 0) == THY_STATE_OK);
    CHECK(!instrument.display_config[THY_VIEW_CHLADNI].motion);
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_VIEW, THY_VIEW_SIMPLE) == THY_STATE_OK);
    CHECK(instrument.display_config[THY_VIEW_SIMPLE].motion);
    CHECK(thy_state_instance_count(THY_ROW_VIEWS_SLOT2) == THY_VIEW_COUNT);
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_VIEWS_SLOT2) == THY_STATE_OK);
    CHECK(engine.state.edit_instance == THY_VIEW_SIMPLE);
    CHECK(thy_state_edit_set(&engine.state, THY_VIEW_INFO_CLOCK) == THY_STATE_OK);
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_VIEW, THY_VIEW_CHLADNI) == THY_STATE_OK);
    CHECK(thy_state_commit(&engine.state) == THY_STATE_OK);
    CHECK(instrument.display_config[THY_VIEW_SIMPLE].slots[1] == THY_VIEW_INFO_CLOCK);
    CHECK(instrument.display_config[THY_VIEW_CHLADNI].slots[1] == THY_VIEW_INFO_COUNTS);
    CHECK(!thy_state_option_allowed(&engine.state, THY_ROW_LIGHTS_VIEW, THY_VIEW_RETIRED_LAYOUTS));
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_VIEW, THY_VIEW_RETIRED_ROUTES) == THY_STATE_INVALID);
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_VIEW, THY_VIEW_ORBIT) == THY_STATE_OK);
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_VIEW, THY_VIEW_WAVES) == THY_STATE_OK);
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_KEYDIM, 17) == THY_STATE_OK);
    CHECK(instrument.light_key_contrast_percent == 17u);
    CHECK(thy_state_set(&engine.state, THY_ROW_LIGHTS_KEYDIM, 101) == THY_STATE_INVALID);
    return 1;
}

int main(void)
{
    if (!test_generated_layouts_are_playable_and_preserve_the_archive() ||
        !test_layout_edits_cannot_retarget_held_midi_owners() ||
        !test_context_controls_use_mapping_and_never_own_notes() ||
        !test_inverted_chords_capture_harmonic_color_without_revoicing() ||
        !test_new_menu_bindings_reach_the_shared_instrument() ||
        !test_future_design_controls_require_opt_in_and_cannot_start_engines() ||
        !test_global_slot_switches_and_optional_selected_chord_scope() ||
        !test_context_relocation_is_quiet_and_maps_both_grids() ||
        !test_play_shortcuts_preserve_menu_navigation_and_hidden_arrows() ||
        !test_center_lights_follow_committed_performance_and_brightness() ||
        !test_bass_shift_and_optional_overlay_cells_preserve_notes_and_owners() ||
        !test_view_preferences_are_independent_and_only_show_applicable_options()) return 1;
    printf("12 alpha polish integration scenarios passed (%u assertions).\n", assertions);
    return 0;
}
