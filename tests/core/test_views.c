/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_engine.h"
#include "../../core/thy_studio_ui.h"

static thy_core core, core_before;
static thy_instrument instrument, instrument_before;
static thy_engine engine, engine_before;
static thy_studio_ui ui;
static uint8_t frames[3][THY_UI_FRAME_BYTES];
static unsigned assertions;
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 0; \
} } while (0)

static void initialize(void)
{
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&engine, &instrument, 0u, 70u, undo_workspace_1);
    thy_studio_ui_init(&ui);
}

static void render(unsigned now, uint8_t *out)
{
    thy_studio_ui_render(&ui, &engine.state, "MAJOR C", 1, 1, now, out);
}

/* Dense display fixtures must retain the real ownership/count invariant.
 * Generated notes keep the physical-pad count at zero. */
static int generated_range(unsigned first, unsigned end)
{
    thy_midi_event event;
    CHECK(thy_core_generated_release_all(&core) == THY_OK);
    while(thy_core_poll(&core, &event)) {}
    for(unsigned pitch=first; pitch<end; ++pitch) {
        thy_generated_handle handle;
        CHECK(thy_core_generated_acquire(&core, 100u, 1u, (uint8_t)pitch,
            0u, THY_ROUTE_DIN, 96u, &handle) == THY_OK);
        while(thy_core_poll(&core, &event)) {}
    }
    CHECK(core.sounding_count == end-first);
    return 1;
}

static int pixel(const uint8_t *frame, unsigned x, unsigned y)
{
    return !!(frame[y / 8u * THY_UI_WIDTH + x] & (1u << (y % 8u)));
}

static int region_equal(const uint8_t *a, const uint8_t *b, unsigned top, unsigned bottom)
{
    for (unsigned y = top; y <= bottom; ++y)
        for (unsigned x = 0u; x < THY_UI_WIDTH; ++x)
            if (pixel(a, x, y) != pixel(b, x, y)) return 0;
    return 1;
}

static int text_at(const uint8_t *actual, unsigned y, const char *text)
{
    uint8_t expected[THY_UI_FRAME_BYTES] = {0};
    unsigned start = y == 1u ? 12u : 2u;
    unsigned cells = y == 1u ? 19u : 21u;
    thy_ui_draw_text(expected, start, y, text, cells, 0);
    /* Check glyph columns and internal/trailing spaces. The final spacer at
     * x106, the frame at x107 and the status rail are independent content. */
    for (unsigned dy = y; dy <= y + 4u; ++dy)
        for (unsigned x = start; x < start + cells * 5u - 1u; ++x)
            if (pixel(actual, x, dy) != pixel(expected, x, dy)) return 0;
    return 1;
}

static int test_stable_identities_and_independent_defaults(void)
{
    initialize();
    CHECK(sizeof(thy_view_config) == 10u);
    CHECK(THY_VIEW_DETAILED == 0 && THY_VIEW_PIANO == 1 && THY_VIEW_SIMPLE == 4 &&
        THY_VIEW_CHLADNI == 5 && THY_VIEW_ORBIT == 6 && THY_VIEW_WAVES == 7 &&
        THY_VIEW_FIFTHS == 8 && THY_VIEW_COUNT == 9);
    CHECK(!thy_view_available(2u) && !thy_view_available(3u) && !thy_view_available(255u));
    for (unsigned view = 0u; view < THY_VIEW_COUNT; ++view) {
        CHECK(thy_view_config_valid(&instrument.display_config[view]));
        CHECK(instrument.display_config[view].slots[0] == THY_VIEW_INFO_MUSICAL_NAMES);
        CHECK(instrument.display_config[view].slots[1] == THY_VIEW_INFO_COUNTS);
    }
    instrument.display_config[THY_VIEW_PIANO].slots[1] = THY_VIEW_INFO_CLOCK;
    CHECK(instrument.display_config[THY_VIEW_SIMPLE].slots[1] == THY_VIEW_INFO_COUNTS);
    thy_view_config invalid = instrument.display_config[0];
    invalid.detail_rows[5] = THY_VIEW_INFO_COUNT;
    CHECK(!thy_view_config_valid(&invalid));
    invalid = instrument.display_config[0]; invalid.slots[0] = 255u;
    CHECK(!thy_view_config_valid(&invalid));
    invalid = instrument.display_config[0]; invalid.header = 2u;
    CHECK(!thy_view_config_valid(&invalid));
    invalid = instrument.display_config[0]; invalid.motion = 2u;
    CHECK(!thy_view_config_valid(&invalid) && !thy_view_config_valid(NULL));
    return 1;
}

static int check_line(unsigned kind, const char *expected)
{
    char line[32];
    thy_studio_ui_view_line(&engine.state, kind, "MAJOR C", 1, 1, line);
    CHECK(strcmp(line, expected) == 0);
    CHECK(strlen(line) <= 25u);
    return 1;
}

static int test_information_uses_routes_physical_pressure_and_actual_ownership(void)
{
    initialize();
    CHECK(thy_instrument_set_route_velocity(&instrument, 1u, 94u) == THY_OK);
    CHECK(thy_instrument_set_route_velocity(&instrument, 2u, 98u) == THY_OK);
    CHECK(thy_instrument_set_route(&instrument, 0u, 2u, THY_ROUTE_DIN | THY_ROUTE_USB0, 0) == THY_OK);
    CHECK(thy_instrument_set_route(&instrument, 1u, 3u, THY_ROUTE_USB1, 0) == THY_OK);
    int16_t chord[] = {60,64,67};
    CHECK(thy_core_press(&core, 0u, chord, 3u, 0, 2u, THY_ROUTE_DIN | THY_ROUTE_USB0, 94u) == THY_OK);
    CHECK(thy_core_press(&core, 48u, chord, 3u, 0, 3u, THY_ROUTE_USB1, 98u) == THY_OK);
    thy_generated_handle generated;
    CHECK(thy_core_generated_acquire(&core, 64u, 1u, 72u, 3u, THY_ROUTE_DIN, 96u, &generated) == THY_OK);
    instrument.sensors[0].held = 1u; instrument.sensors[0].pressure = 45u;
    instrument.sensors[48].held = 1u; instrument.sensors[48].pressure = 87u;
    instrument.performance.chord_octave = -1; instrument.performance.melody_octave = 2;
    instrument.bass_octave = -2;
    CHECK(check_line(THY_VIEW_INFO_OFF, ""));
    CHECK(check_line(THY_VIEW_INFO_VELOCITY, "VEL L94 R98"));
    CHECK(check_line(THY_VIEW_INFO_COUNTS, "PADS 2  NOTES 4"));
    CHECK(check_line(THY_VIEW_INFO_NOTES, "NOTES C4 E4 G4 C5"));
    CHECK(check_line(THY_VIEW_INFO_ROOT_SCALE, "SCALE MAJOR C"));
    CHECK(check_line(THY_VIEW_INFO_CHANNELS, "CH L3 R4"));
    CHECK(check_line(THY_VIEW_INFO_OUTPUTS, "OUT L:U1D R:U2"));
    CHECK(check_line(THY_VIEW_INFO_MODIFIERS, "INV0 SPR0 STR OFF"));
    CHECK(check_line(THY_VIEW_INFO_PRESSURE, "PRESS L45 R87"));
    CHECK(check_line(THY_VIEW_INFO_OCTAVES, "OCT L-1 R+2 B-2"));
    CHECK(check_line(THY_VIEW_INFO_CLOCK, "INT 120 BPM 4/4"));
    CHECK(check_line(THY_VIEW_INFO_ARPS, "ARP 1:OFF 2:OFF"));
    CHECK(thy_instrument_set_routing_profile(&instrument, THY_ROUTING_ZONES) == THY_OK);
    for (unsigned target = 3u; target < 9u; ++target)
        CHECK(thy_instrument_set_route_velocity(&instrument, (uint8_t)target, 127u) == THY_OK);
    CHECK(check_line(THY_VIEW_INFO_VELOCITY, "V127/127/127|127/127/127"));
    CHECK(thy_core_generated_release(&core, generated) == THY_OK);
    CHECK(check_line(THY_VIEW_INFO_COUNTS, "PADS 2  NOTES 3"));
    thy_studio_ui_view_line(NULL, THY_VIEW_INFO_CLOCK, NULL, 0, 0, NULL);
    return 1;
}

static int test_six_detailed_rows_and_two_compact_slots_are_independent(void)
{
    initialize();
    thy_view_config *detail = &instrument.display_config[THY_VIEW_DETAILED];
    instrument.display_view = THY_VIEW_DETAILED;
    detail->header = 0u;
    memset(detail->detail_rows, 0, sizeof detail->detail_rows);
    render(0u, frames[0]);
    uint8_t blank[THY_UI_FRAME_BYTES] = {0};
    for (unsigned y = 0u; y < 64u; ++y)
        for (unsigned x = 0u; x < 105u; ++x) CHECK(!pixel(frames[0], x, y));
    CHECK(memcmp(frames[0], blank, sizeof blank) != 0); /* Output side rail. */
    for (unsigned row = 0u; row < THY_VIEW_DETAIL_ROWS; ++row) {
        detail->detail_rows[row] = THY_VIEW_INFO_COUNTS;
        render(row + 1u, frames[0]);
        CHECK(text_at(frames[0], row + 1u == THY_VIEW_DETAIL_ROWS ? 58u : 12u + row * 8u,
            "PADS 0  NOTES 0"));
        detail->detail_rows[row] = THY_VIEW_INFO_OFF;
    }
    detail->header = 1u; detail->detail_rows[5] = THY_VIEW_INFO_ARPS;
    render(10u, frames[0]);
    CHECK(text_at(frames[0], 58u, "ARP 1:OFF 2:OFF"));
    CHECK(text_at(frames[0], 1u, "MAJOR C"));
    for (unsigned view = 0u; view < THY_VIEW_COUNT; ++view) {
        if (!thy_view_available(view) || view == THY_VIEW_DETAILED) continue;
        instrument.display_view = (uint8_t)view;
        thy_view_config *config = &instrument.display_config[view];
        config->motion = 0u;
        config->slots[0] = THY_VIEW_INFO_OFF; config->slots[1] = THY_VIEW_INFO_OFF;
        render(20u, frames[0]);
        CHECK(text_at(frames[0], 48u, "") && text_at(frames[0], 58u, ""));
        config->slots[0] = THY_VIEW_INFO_CHANNELS;
        render(21u, frames[1]);
        CHECK(text_at(frames[1], 48u, "CH L1 R2"));
        CHECK(region_equal(frames[0], frames[1], 58u, 62u));
        config->slots[1] = THY_VIEW_INFO_CLOCK;
        render(22u, frames[2]);
        CHECK(text_at(frames[2], 58u, "INT 120 BPM 4/4"));
        CHECK(region_equal(frames[1], frames[2], 0u, 52u));
    }
    return 1;
}

static int test_three_graphical_views_are_distinct_note_driven_and_reduced_motion_safe(void)
{
    initialize();
    static const unsigned views[] = {THY_VIEW_CHLADNI, THY_VIEW_ORBIT, THY_VIEW_WAVES};
    int16_t chord[] = {60,64,67};
    CHECK(thy_core_press(&core, 0u, chord, 3u, 0, 0u, THY_ROUTE_DIN | THY_ROUTE_USB0, 96u) == THY_OK);
    core_before = core; instrument_before = instrument; engine_before = engine;
    for (unsigned v = 0u; v < 3u; ++v) {
        instrument.display_view = (uint8_t)views[v];
        render(100u, frames[v]);
        uint8_t later[THY_UI_FRAME_BYTES];
        render(575u, later);
        CHECK(!region_equal(frames[v], later, 12u, 41u));
        instrument.display_config[views[v]].motion = 0u;
        render(100u, frames[v]); render(45000u, later);
        CHECK(memcmp(frames[v], later, sizeof later) == 0);
        instrument.display_config[views[v]].motion = 1u;
        ui.animations_enabled = 0u;
        render(100u, frames[v]); render(45000u, later);
        CHECK(memcmp(frames[v], later, sizeof later) == 0);
        ui.animations_enabled = 1u;
    }
    CHECK(!region_equal(frames[0], frames[1], 12u, 41u));
    CHECK(!region_equal(frames[1], frames[2], 12u, 41u));
    CHECK(!region_equal(frames[0], frames[2], 12u, 41u));
    CHECK(memcmp(&core, &core_before, sizeof core) == 0);
    CHECK(memcmp(&engine, &engine_before, sizeof engine) == 0);
    instrument.display_view = instrument_before.display_view;
    CHECK(memcmp(&instrument, &instrument_before, sizeof instrument) == 0);
    /* Changing pitch changes the static drawing; this is MIDI state rather
     * than an unrelated screensaver or a fabricated audio oscilloscope. */
    instrument.display_view = THY_VIEW_WAVES;
    instrument.display_config[THY_VIEW_WAVES].motion = 0u;
    render(0u, frames[0]);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    const int16_t other = 62;
    CHECK(thy_core_press(&core, 1u, &other, 1u, 0, 0u, THY_ROUTE_DIN, 96u) == THY_OK);
    render(0u, frames[1]);
    CHECK(!region_equal(frames[0], frames[1], 12u, 41u));
    return 1;
}

static int test_retired_views_fall_back_and_all_renderers_keep_frame_bounds(void)
{
    initialize();
    instrument.display_view = THY_VIEW_DETAILED;
    render(0u, frames[0]);
    for (unsigned id = 2u; id <= 3u; ++id) {
        instrument.display_view = (uint8_t)id;
        render(0u, frames[1]);
        CHECK(memcmp(frames[0], frames[1], THY_UI_FRAME_BYTES) == 0);
    }
    struct { uint8_t before[32], pixels[THY_UI_FRAME_BYTES], after[32]; } guarded;
    CHECK(generated_range(0u, 128u));
    for (unsigned view = 0u; view <= THY_VIEW_COUNT; ++view) {
        instrument.display_view = (uint8_t)(view == THY_VIEW_COUNT ? 255u : view);
        memset(&guarded, 0xa5, sizeof guarded);
        thy_studio_ui_render(&ui, &engine.state,
            "A SCALE LABEL LONGER THAN THIS DISPLAY", 0, 0, 0xffffffffu, guarded.pixels);
        for (unsigned n = 0u; n < 32u; ++n)
            CHECK(guarded.before[n] == 0xa5u && guarded.after[n] == 0xa5u);
    }
    return 1;
}

static int test_menu_edits_capture_the_view_and_skip_retired_choices(void)
{
    initialize();
    thy_state *state = &engine.state;
    CHECK(thy_state_set(state, THY_ROW_LIGHTS_VIEW, THY_VIEW_PIANO) == THY_STATE_OK);
    CHECK(thy_state_begin_edit(state, THY_ROW_VIEWS_SLOT1) == THY_STATE_OK);
    CHECK(thy_state_edit_set(state, THY_VIEW_INFO_CLOCK) == THY_STATE_OK);
    /* An unrelated selection change must not retarget an existing edit. */
    instrument.display_view = THY_VIEW_SIMPLE;
    CHECK(thy_state_commit(state) == THY_STATE_OK);
    CHECK(instrument.display_config[THY_VIEW_PIANO].slots[0] == THY_VIEW_INFO_CLOCK);
    CHECK(instrument.display_config[THY_VIEW_SIMPLE].slots[0] == THY_VIEW_INFO_MUSICAL_NAMES);
    CHECK(thy_state_set(state, THY_ROW_LIGHTS_VIEW, THY_VIEW_PIANO) == THY_STATE_OK);
    CHECK(thy_state_begin_edit(state, THY_ROW_LIGHTS_VIEW) == THY_STATE_OK);
    CHECK(thy_state_adjust(state, 1) == THY_STATE_OK && state->edit_value == THY_VIEW_SIMPLE);
    CHECK(thy_state_adjust(state, 1) == THY_STATE_OK && state->edit_value == THY_VIEW_CHLADNI);
    CHECK(thy_state_cancel(state) == THY_STATE_OK);
    CHECK(instrument.display_view == THY_VIEW_PIANO);
    CHECK(!thy_state_option_allowed(state, THY_ROW_LIGHTS_VIEW, THY_VIEW_RETIRED_LAYOUTS));
    CHECK(!thy_state_option_allowed(state, THY_ROW_LIGHTS_VIEW, THY_VIEW_RETIRED_ROUTES));
    CHECK(thy_state_row_visible(state, THY_ROW_VIEWS_SLOT1));
    CHECK(!thy_state_row_visible(state, THY_ROW_VIEWS_DETAIL1));
    CHECK(!thy_state_row_visible(state, THY_ROW_VIEWS_MOTION));
    CHECK(thy_state_set(state, THY_ROW_LIGHTS_VIEW, THY_VIEW_DETAILED) == THY_STATE_OK);
    CHECK(!thy_state_row_visible(state, THY_ROW_VIEWS_SLOT1));
    CHECK(thy_state_row_visible(state, THY_ROW_VIEWS_DETAIL1));
    CHECK(thy_state_set(state, THY_ROW_VIEWS_DETAIL6, THY_VIEW_INFO_PRESSURE) == THY_STATE_OK);
    CHECK(instrument.display_config[THY_VIEW_DETAILED].detail_rows[5] == THY_VIEW_INFO_PRESSURE);
    CHECK(instrument.display_config[THY_VIEW_PIANO].detail_rows[5] == THY_VIEW_INFO_COUNTS);
    CHECK(thy_core_generated_count(&core) == 0u && core.queue_count == 0u);
    return 1;
}

static int cell_text(const uint8_t *actual, unsigned x, unsigned y, const char *text)
{
    uint8_t expected[THY_UI_FRAME_BYTES] = {0};
    thy_ui_draw_text(expected, x, y, text, 4u, 0);
    for (unsigned dy = y; dy < y + 5u; ++dy)
        for (unsigned dx = x; dx < x + 20u; ++dx)
            CHECK(pixel(actual, dx, dy) == pixel(expected, dx, dy));
    return 1;
}

static unsigned lit_region(const uint8_t *actual, unsigned x, unsigned y,
    unsigned width, unsigned height)
{
    unsigned lit = 0u;
    for (unsigned dy = y; dy < y + height; ++dy)
        for (unsigned dx = x; dx < x + width; ++dx)
            lit += (unsigned)pixel(actual, dx, dy);
    return lit;
}

static int test_simple_shows_chords_octaves_and_explicit_dense_overflow(void)
{
    initialize();
    instrument.display_view = THY_VIEW_SIMPLE;
    CHECK(generated_range(48u, 73u));
    render(0u, frames[0]);
    for (unsigned n = 0u; n < 25u; ++n) {
        unsigned row = n / 5u;
        unsigned col = n % 5u;
        char name[6];
        thy_ui_note_name((int)(48u + n), name);
        CHECK(cell_text(frames[0], 2u + col * 21u, 13u + row * 7u, name));
    }
    CHECK(generated_range(48u, 74u));
    render(1u, frames[0]);
    CHECK(cell_text(frames[0], 86u, 41u, "+2"));
    CHECK(generated_range(0u, 128u));
    render(2u, frames[0]);
    CHECK(cell_text(frames[0], 2u, 13u, "C-1"));
    CHECK(cell_text(frames[0], 23u, 13u, "C#-1"));
    CHECK(cell_text(frames[0], 86u, 41u, "+104"));
    CHECK(text_at(frames[0], 58u, "PADS 0  NOTES 128"));
    return 1;
}

static int test_arp_pool_stays_dim_and_current_output_brightens_without_mutation(void)
{
    initialize();
    thy_arp *arp = &engine.arps[0];
    thy_arp_config config = arp->config;
    config.enabled = 1u;
    CHECK(thy_arp_configure(arp, &core, &config, 0u, 1u, 1) == THY_OK);
    const int16_t notes[] = {60,64,67};
    CHECK(thy_arp_press(arp, &core, 0u, 1u, notes, 3u, 96u, 0u, 1u, 1) == THY_OK);
    instrument.display_view = THY_VIEW_SIMPLE;
    core_before = core; engine_before = engine; instrument_before = instrument;
    render(0u, frames[0]);
    CHECK(lit_region(frames[0], 8u, 22u, 20u, 10u) > 0u);
    CHECK(lit_region(frames[0], 44u, 22u, 20u, 10u) > 0u);
    CHECK(lit_region(frames[0], 80u, 22u, 20u, 10u) > 0u);
    CHECK(memcmp(&core, &core_before, sizeof core) == 0);
    CHECK(memcmp(&engine, &engine_before, sizeof engine) == 0);
    CHECK(memcmp(&instrument, &instrument_before, sizeof instrument) == 0);
    CHECK(thy_arp_advance(arp, &core, 240u, 1u, 1) == THY_OK);
    render(1u, frames[1]);
    CHECK(lit_region(frames[1], 8u, 22u, 20u, 10u) < lit_region(frames[0], 8u, 22u, 20u, 10u));
    CHECK(lit_region(frames[1], 44u, 22u, 20u, 10u) > lit_region(frames[0], 44u, 22u, 20u, 10u));
    CHECK(lit_region(frames[1], 80u, 22u, 20u, 10u) == lit_region(frames[0], 80u, 22u, 20u, 10u));
    instrument.display_view = THY_VIEW_PIANO;
    render(2u, frames[0]);
    CHECK(lit_region(frames[0], 32u, 31u, 10u, 3u) == 30u); /* E bright. */
    CHECK(lit_region(frames[0], 2u, 31u, 10u, 3u) == 15u); /* C available, dim. */
    CHECK(thy_arp_advance(arp, &core, 360u, 1u, 1) == THY_OK);
    render(3u, frames[1]);
    CHECK(lit_region(frames[1], 32u, 31u, 10u, 3u) == 15u); /* Gate ended, pool remains. */
    CHECK(thy_arp_release(arp, &core, 0u, 361u, 1u, 1) == THY_OK);
    render(4u, frames[2]);
    CHECK(lit_region(frames[2], 32u, 31u, 10u, 3u) == 0u);
    CHECK(core.sounding_count == 0u && thy_core_generated_count(&core) == 0u);
    return 1;
}

static int test_fifths_order_root_chord_edges_and_motion_are_read_only(void)
{
    initialize();
    instrument.display_view = THY_VIEW_FIFTHS;
    instrument.display_config[THY_VIEW_FIFTHS].motion = 0u;
    static const uint8_t order[] = {0,7,2,9,4,11,6,1,8,3,10,5};
    static const uint8_t xs[] = {54,72,85,90,85,72,54,36,23,18,23,36};
    static const uint8_t ys[] = {18,19,23,28,33,36,38,36,33,28,23,19};
    for (unsigned step = 0u; step < 12u; ++step) {
        CHECK(generated_range(60u + order[step], 61u + order[step]));
        render(step, frames[0]);
        CHECK(pixel(frames[0], xs[step] - 1u, ys[step]));
        CHECK(pixel(frames[0], xs[step] + 1u, ys[step]));
        if (step) CHECK((order[step] + 12u - order[step - 1u]) % 12u == 7u);
    }
    CHECK(generated_range(60u, 61u));
    thy_generated_handle handle;
    CHECK(thy_core_generated_acquire(&core, 100u, 1u, 67u, 0u,
        THY_ROUTE_DIN, 96u, &handle) == THY_OK);
    core_before = core; instrument_before = instrument; engine_before = engine;
    render(0u, frames[0]); render(2000u, frames[1]);
    CHECK(memcmp(frames[0], frames[1], sizeof frames[0]) == 0);
    CHECK(pixel(frames[0], 63u, 18u)); /* C -> G adjacent fifth edge. */
    CHECK(pixel(frames[0], 51u, 15u)); /* Root C open-square corner. */
    CHECK(memcmp(&core, &core_before, sizeof core) == 0);
    CHECK(memcmp(&engine, &engine_before, sizeof engine) == 0);
    CHECK(memcmp(&instrument, &instrument_before, sizeof instrument) == 0);
    instrument.performance.root = 7u;
    render(0u, frames[1]);
    CHECK(!pixel(frames[1], 51u, 15u) && pixel(frames[1], 69u, 16u));
    instrument.display_config[THY_VIEW_FIFTHS].motion = 1u;
    render(0u, frames[0]); render(160u, frames[1]);
    CHECK(!region_equal(frames[0], frames[1], 12u, 41u));
    ui.animations_enabled = 0u;
    render(0u, frames[0]); render(160u, frames[1]);
    CHECK(memcmp(frames[0], frames[1], sizeof frames[0]) == 0);
    return 1;
}

static int test_pad_count_deduplicates_arp_inputs_layers_and_physical_state(void)
{
    initialize();
    const int16_t c = 60, d = 62;
    for (unsigned a = 0u; a < 2u; ++a) {
        thy_arp_config config = engine.arps[a].config;
        config.enabled = 1u;
        CHECK(thy_arp_configure(&engine.arps[a], &core, &config, 0u, 1u, 1) == THY_OK);
        CHECK(thy_arp_press(&engine.arps[a], &core, 0u, 1u, &c, 1u, 96u, 0u, 1u, 1) == THY_OK);
        CHECK(check_line(THY_VIEW_INFO_COUNTS, "PADS 1  NOTES 1"));
    }
    CHECK(thy_core_press(&core, 0u, &c, 1u, 0, 0u, THY_ROUTE_DIN, 96u) == THY_OK);
    instrument.held_input_valid[0] = 1u;
    instrument.sensors[0].held = 1u;
    CHECK(check_line(THY_VIEW_INFO_COUNTS, "PADS 1  NOTES 1"));
    CHECK(thy_arp_press(&engine.arps[1], &core, 48u, 0u, &d, 1u, 96u, 0u, 1u, 1) == THY_OK);
    CHECK(check_line(THY_VIEW_INFO_COUNTS, "PADS 2  NOTES 1"));
    instrument.sensors[1].held = 1u;
    instrument.held_input_valid[2] = 1u;
    core_before = core; engine_before = engine; instrument_before = instrument;
    CHECK(check_line(THY_VIEW_INFO_COUNTS, "PADS 4  NOTES 1"));
    CHECK(memcmp(&core, &core_before, sizeof core) == 0);
    CHECK(memcmp(&engine, &engine_before, sizeof engine) == 0);
    CHECK(memcmp(&instrument, &instrument_before, sizeof instrument) == 0);
    for (unsigned a = 0u; a < 2u; ++a) thy_arp_clear(&engine.arps[a], &core);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    memset(instrument.held_input_valid, 0, sizeof instrument.held_input_valid);
    instrument.sensors[0].held = 0u; instrument.sensors[1].held = 0u;
    CHECK(check_line(THY_VIEW_INFO_COUNTS, "PADS 0  NOTES 0"));
    return 1;
}

static int test_musical_identity_is_shared_and_never_replans_held_notes(void)
{
    initialize();
    CHECK(instrument.display_view == THY_VIEW_SIMPLE);
    CHECK(THY_VIEW_INFO_MUSICAL_NAMES == 12u && THY_VIEW_INFO_COUNT == 13u);
    CHECK(check_line(THY_VIEW_INFO_MUSICAL_NAMES, "READY"));
    const int16_t chord[] = {60,64,67};
    CHECK(thy_core_press(&core, 0u, chord, 3u, 0, 0u,
        THY_ROUTE_DIN | THY_ROUTE_USB0, 96u) == THY_OK);
    CHECK(check_line(THY_VIEW_INFO_MUSICAL_NAMES, "C MAJ"));
    instrument.performance.root = 2u;
    instrument.transpose[0] = 7;
    CHECK(check_line(THY_VIEW_INFO_MUSICAL_NAMES, "C MAJ"));
    for (unsigned view = 0u; view < THY_VIEW_COUNT; ++view) {
        if (!thy_view_available(view)) continue;
        instrument.display_view = (uint8_t)view;
        instrument.display_config[view].header = 0u;
        core_before=core; instrument_before=instrument; engine_before=engine;
        render(0u,frames[0]);
        CHECK(text_at(frames[0],1u,"C MAJ"));
        CHECK(!memcmp(&core,&core_before,sizeof core));
        CHECK(!memcmp(&instrument,&instrument_before,sizeof instrument));
        CHECK(!memcmp(&engine,&engine_before,sizeof engine));
    }
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    const int16_t inversion[] = {52,60,67};
    CHECK(thy_core_press(&core, 0u, inversion, 3u, 0, 0u, THY_ROUTE_DIN, 96u)==THY_OK);
    CHECK(check_line(THY_VIEW_INFO_MUSICAL_NAMES, "C MAJ/E"));
    CHECK(thy_core_release(&core,0u)==THY_OK);
    const int16_t single=61;
    CHECK(thy_core_press(&core,48u,&single,1u,0,0u,THY_ROUTE_DIN,96u)==THY_OK);
    CHECK(check_line(THY_VIEW_INFO_MUSICAL_NAMES,"C#4"));
    CHECK(thy_core_release(&core,48u)==THY_OK);
    CHECK(check_line(THY_VIEW_INFO_MUSICAL_NAMES,"READY"));
    return 1;
}

static int test_arp_name_stays_stable_between_gates_and_side_indicators_work(void)
{
    initialize();
    thy_arp_config config=engine.arps[0].config;config.enabled=1u;
    CHECK(thy_arp_configure(&engine.arps[0],&core,&config,0u,1u,1)==THY_OK);
    const int16_t chord[]={60,64,67};
    CHECK(thy_arp_press(&engine.arps[0],&core,0u,1u,chord,3u,96u,0u,1u,1)==THY_OK);
    CHECK(check_line(THY_VIEW_INFO_MUSICAL_NAMES,"C MAJ"));
    CHECK(thy_arp_advance(&engine.arps[0],&core,120u,1u,1)==THY_OK);
    CHECK(check_line(THY_VIEW_INFO_MUSICAL_NAMES,"C MAJ"));
    render(0u,frames[0]);
    engine.expression_config.enabled=1u;
    render(0u,frames[1]);
    CHECK(lit_region(frames[0],112u,30u,15u,5u)==0u);
    CHECK(lit_region(frames[1],112u,30u,15u,5u)>0u);
    CHECK(lit_region(frames[1],112u,42u,15u,12u)>0u);
    CHECK(thy_arp_release(&engine.arps[0],&core,0u,121u,1u,1)==THY_OK);
    CHECK(check_line(THY_VIEW_INFO_MUSICAL_NAMES,"READY"));
    return 1;
}

int main(void)
{
    if (!test_stable_identities_and_independent_defaults() ||
        !test_information_uses_routes_physical_pressure_and_actual_ownership() ||
        !test_six_detailed_rows_and_two_compact_slots_are_independent() ||
        !test_three_graphical_views_are_distinct_note_driven_and_reduced_motion_safe() ||
        !test_retired_views_fall_back_and_all_renderers_keep_frame_bounds() ||
        !test_menu_edits_capture_the_view_and_skip_retired_choices() ||
        !test_simple_shows_chords_octaves_and_explicit_dense_overflow() ||
        !test_arp_pool_stays_dim_and_current_output_brightens_without_mutation() ||
        !test_fifths_order_root_chord_edges_and_motion_are_read_only() ||
        !test_pad_count_deduplicates_arp_inputs_layers_and_physical_state() ||
        !test_musical_identity_is_shared_and_never_replans_held_notes() ||
        !test_arp_name_stays_stable_between_gates_and_side_indicators_work()) return 1;
    printf("12 configurable view tests passed (%u assertions)\n", assertions);
    return 0;
}
