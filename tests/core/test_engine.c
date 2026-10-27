/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_engine.h"
#include "../../core/thy_settings.h"

static thy_core core;
static thy_instrument instrument;
static thy_engine engine;
static thy_mpe expression;
static thy_instrument_pad layout[THY_PAD_COUNT];
static unsigned assertions, action_count;
static uint32_t now;

#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 0; \
} } while (0)

static int set(uint16_t row, int32_t value) { return thy_state_set(&engine.state, row, value); }
static int32_t get(uint16_t row)
{
    int32_t value = INT32_MIN;
    (void)thy_state_get(&engine.state, row, &value);
    return value;
}
static void drain(void) { thy_midi_event event; while (thy_core_poll(&core, &event)) { } }
static void action(void *context, const thy_instrument_control_event *event)
{
    (void)context;
    (void)event;
    ++action_count;
}
static int initialize(void)
{
    static const int16_t degrees[] = {0, 2, 4, 5, 7, 9, 11};
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    for (unsigned pad = 0; pad < THY_PAD_COUNT; ++pad)
        layout[pad] = (thy_instrument_pad){{60, 64, 67, 0, 0, 0, 0}, 7, 1};
    CHECK(thy_instrument_set_layout(&instrument, layout, 60, degrees, 7) == THY_OK);
    CHECK(thy_instrument_set_detector_profile(&instrument, THY_DETECTOR_FACTORY_LINEAR) == THY_OK);
    thy_engine_init(&engine, &instrument, 21, 70);
    thy_engine_bind_expression(&engine, &expression);
    thy_instrument_set_action_handler(&instrument, action, NULL);
    now = 0;
    action_count = 0;
    return 1;
}
static int sample(uint16_t raw, unsigned count)
{
    for (unsigned n = 0; n < count; ++n)
        CHECK(thy_instrument_sample(&instrument, 0, raw, now++) == THY_OK);
    return 1;
}
static int control(unsigned index)
{
    CHECK(thy_instrument_control(&instrument, 0, (uint8_t)index, 1) == THY_OK);
    CHECK(thy_instrument_control(&instrument, 0, (uint8_t)index, 0) == THY_OK);
    return 1;
}

static int test_all_live_rows_use_real_instrument_values(void)
{
    static const struct { uint16_t row; int32_t value; } edits[] = {
        {THY_ROW_PERFORMANCE_SCALE, 0}, {THY_ROW_PERFORMANCE_ROOT, 2},
        {THY_ROW_PERFORMANCE_TRANSPOSE, 7}, {THY_ROW_PERFORMANCE_CHORDOCTAVE, 1},
        {THY_ROW_PERFORMANCE_MELODYOCTAVE, -1}, {THY_ROW_PERFORMANCE_VELOCITY, 88},
        {THY_ROW_PERFORMANCE_AFTERTOUCH, 1}, {THY_ROW_CHORDS_INVERSION, 2},
        {THY_ROW_CHORDS_SPREAD, 1}, {THY_ROW_CHORDS_OCTAVESTACKS, 1},
        {THY_ROW_CHORDS_STRUM, 1}, {THY_ROW_CHORDS_STRUMDELAY, 30},
        {THY_ROW_CHORDS_STRUMDIRECTION, 2}, {THY_ROW_SENSITIVITY_ATTACK, 1393},
        {THY_ROW_SENSITIVITY_RELEASE, 1360}, {THY_ROW_SENSITIVITY_FLOOR, 1240},
        {THY_ROW_SENSITIVITY_CEILING, 4000}, {THY_ROW_SENSITIVITY_CURVE, 1},
        {THY_ROW_LIGHTS_ENABLED, 0}, {THY_ROW_LIGHTS_IDLE, 3},
        {THY_ROW_LIGHTS_HELD, 6}, {THY_ROW_LIGHTS_FADE, 200},
        {THY_ROW_PERFORMANCE_MELODYLAYOUT, 5}, {THY_ROW_PERFORMANCE_GUITARWINDOW, 2},
        {THY_ROW_PERFORMANCE_PADOVERLAYS, 0}, {THY_ROW_SENSITIVITY_CENTERRESPONSE, 10},
        {THY_ROW_LIGHTS_CHORDCOLOR, 2}, {THY_ROW_LIGHTS_NOTECOLORS, 1},
        {THY_ROW_LIGHTS_KEYCONTRAST, 0}, {THY_ROW_LIGHTS_HELDSTYLE, 1},
        {THY_ROW_LIGHTS_HELDFEEDBACK, 0}, {THY_ROW_LIGHTS_VIEW, 5},
        {THY_ROW_LIGHTS_ANIMATIONS, 0},
        {THY_ROW_PERFORMANCE_CONTEXTLOCATION, 1},
        {THY_ROW_PERFORMANCE_CONTEXTCHORDOCTAVES, 0},
        {THY_ROW_PERFORMANCE_CONTEXTMELODYOCTAVES, 0},
        {THY_ROW_PERFORMANCE_CONTEXTBASS, 0},
        {THY_ROW_PERFORMANCE_CONTEXTINVERSION, 0},
        {THY_ROW_PERFORMANCE_BASSOCTAVE, -2},
        {THY_ROW_LIGHTS_KEYDIM, 23}, {THY_ROW_LIGHTS_CENTER, 4},
        {THY_ROW_VIEWS_SLOT2, 0}, {THY_ROW_VIEWS_MOTION, 0},
        {THY_ROW_CONTROLS_LEFT, THY_PLAY_CHORDS},
        {THY_ROW_CONTROLS_UP, THY_PLAY_SENSITIVITY},
        {THY_ROW_CONTROLS_DOWN, THY_PLAY_LIGHTS},
        {THY_ROW_CONTROLS_RIGHT, THY_PLAY_VIEWS},
        {THY_ROW_CONTROLS_LOWERMODE, THY_LOWER_STORED_NOTES},
        {THY_ROW_CONTROLS_SLOTSCOPE, THY_SLOTS_SELECTED_PAD},
        {THY_ROW_MIDI_ROUTINGPROFILE, 1}, {THY_ROW_MIDI_ROUTETARGET, 1},
        {THY_ROW_MIDI_CHANNEL, 4}, {THY_ROW_MIDI_DESTINATIONS, 0},
        {THY_ROW_MIDI_VELOCITY, 80}, {THY_ROW_MIDI_PRESSURE, 1},
        {THY_ROW_MIDI_PRESSUREBEND, 1}, {THY_ROW_MIDI_BENDRANGE, 12},
        {THY_ROW_SENSITIVITY_PROFILEENABLED, 1}, {THY_ROW_SENSITIVITY_PROFILESPLIT, 60},
        {THY_ROW_SENSITIVITY_PRESSURECURVE, 1}, {THY_ROW_SENSITIVITY_AFTERTOUCHCURVE, 2},
        {THY_ROW_EXPRESSION_ENABLED, 0}, {THY_ROW_EXPRESSION_ZONE, 1},
        {THY_ROW_EXPRESSION_MEMBERS, 7}, {THY_ROW_EXPRESSION_BENDRANGE, 24},
        {THY_ROW_EXPRESSION_GLIDE, 1}, {THY_ROW_EXPRESSION_PADWINDOW, 2},
        {THY_ROW_EXPRESSION_INTERVAL, 3}, {THY_ROW_EXPRESSION_MANAGERRANGE, 12}
    };
    CHECK(initialize());
    unsigned live_count = 0;
    for (unsigned row = 0; row < THY_MENU_ROW_COUNT; ++row)
        if (thy_menu_rows[row].status == THY_MENU_LIVE &&
            thy_menu_rows[row].type != THY_MENU_ACTION && thy_menu_rows[row].type != THY_MENU_INFO &&
            row != THY_ROW_SEQUENCER_STEPLENGTH && row != THY_ROW_SEQUENCER_STEPVELOCITY) ++live_count;
    for (unsigned n = 0; n < sizeof edits / sizeof edits[0]; ++n) {
        CHECK(set(edits[n].row, edits[n].value) == THY_STATE_OK);
        CHECK(get(edits[n].row) == edits[n].value);
        CHECK(thy_core_queued(&core) == 0);
    }
    /* Newly live Arp/clock rows have dedicated behavior tests. Every scalar
     * still needs a real adapter, not an unavailable or draft-only binding. */
    for (uint16_t row = 0u; row < THY_MENU_ROW_COUNT; ++row) {
        if (thy_menu_rows[row].status != THY_MENU_LIVE ||
            thy_menu_rows[row].type == THY_MENU_ACTION || thy_menu_rows[row].type == THY_MENU_INFO ||
            row == THY_ROW_SEQUENCER_STEPLENGTH || row == THY_ROW_SEQUENCER_STEPVELOCITY) continue;
        unsigned covered = 0u;
        for (unsigned n = 0u; n < sizeof edits / sizeof edits[0]; ++n)
            if (edits[n].row == row) covered = 1u;
        if (!covered) {
            int32_t value = get(row);
            int result = value == INT32_MIN ? THY_STATE_ADAPTER_ERROR : set(row,value);
            if (result != THY_STATE_OK) fprintf(stderr,"Live adapter %s value %ld returned %d\n",
                thy_menu_rows[row].id,(long)value,result);
            CHECK(value != INT32_MIN && result == THY_STATE_OK);
            CHECK(get(row) == value);
        }
    }
    CHECK(engine.scale == 0 && instrument.performance.root == 2);
    CHECK(instrument.transpose[0] == 7 && instrument.transpose[1] == 7);
    CHECK(instrument.performance.chord_octave == 1 && instrument.performance.melody_octave == -1);
    CHECK(instrument.fixed_velocity == 88 && instrument.pressure_enabled == 1);
    CHECK(instrument.performance.inversion == 2 && instrument.performance.spread == 1);
    CHECK(instrument.performance.upper_octaves == 1 && instrument.performance.strum == 1);
    CHECK(instrument.performance.strum_ms == 30 && instrument.performance.strum_order == 2);
    CHECK(instrument.cluster_response[0].attack_raw == 1393 && instrument.cluster_response[0].release_raw == 1360);
    CHECK(instrument.cluster_response[0].pressure_floor == 1240 && instrument.cluster_response[0].pressure_full_scale == 4000);
    CHECK(instrument.cluster_response[0].pressure_curve == 1 && instrument.lights_enabled == 0);
    CHECK(instrument.response.attack_raw==1377 && instrument.response.release_raw==1354);
    CHECK(instrument.cluster_response[1].attack_raw==1377 && instrument.cluster_response[1].release_raw==1354);
    CHECK(instrument.cluster_response[1].pressure_floor==1353 && instrument.cluster_response[1].pressure_full_scale==4095);
    CHECK(instrument.light_idle == 3 && instrument.light_held == 6 && instrument.light_fade_ms == 200);
    CHECK(instrument.context_location == 1u && instrument.context_mask == 0u);
    CHECK(instrument.bass_octave == -2 && instrument.slot_scope == THY_SLOTS_SELECTED_PAD);
    CHECK(instrument.light_key_contrast_percent == 23u && instrument.center_brightness == 4u);
    CHECK(instrument.display_view == THY_VIEW_CHLADNI && instrument.display_config[THY_VIEW_SIMPLE].slots[1] == THY_VIEW_INFO_COUNTS);
    CHECK(instrument.display_config[THY_VIEW_CHLADNI].slots[1] == THY_VIEW_INFO_OFF && !instrument.display_config[THY_VIEW_CHLADNI].motion);
    CHECK(instrument.play_nav[0] == THY_PLAY_CHORDS && instrument.play_nav[1] == THY_PLAY_SENSITIVITY);
    CHECK(instrument.play_nav[2] == THY_PLAY_LIGHTS && instrument.play_nav[3] == THY_PLAY_VIEWS);
    CHECK(thy_instrument_set_expression(&instrument, 91, 0) == THY_OK);
    CHECK(get(THY_ROW_PERFORMANCE_VELOCITY) == 91 && get(THY_ROW_PERFORMANCE_AFTERTOUCH) == 0);
    CHECK(engine.state.revision == live_count);
    unsigned actions = 0u, cleanup_revisions = 0u;
    for (uint16_t row = 0u; row < THY_MENU_ROW_COUNT; ++row)
        if (thy_menu_rows[row].status == THY_MENU_LIVE && thy_menu_rows[row].type == THY_MENU_ACTION) {
            if(row==THY_ROW_EXPRESSION_SETUP) {
                /* Receiver configuration is an explicit enabled-MPE action,
                 * not a generic no-output menu operation. Its successful RPN
                 * batch has a dedicated expression fixture. */
                uint32_t revision=engine.state.revision;
                CHECK(!engine.expression_config.enabled);
                CHECK(thy_state_begin_edit(&engine.state,row)==THY_STATE_BLOCKED);
                CHECK(!engine.state.editing && !thy_core_queued(&core));
                CHECK(engine.state.revision==revision);
                continue;
            }
            /* Transport, destructive edits and history require dedicated
             * sequencer fixtures; never blindly execute the whole menu. */
            if (thy_menu_rows[row].group == thy_menu_rows[THY_ROW_SEQUENCER_TRACK].group ||
                row == THY_ROW_ENGINES_ARP || row == THY_ROW_ENGINES_SEQUENCER ||
                /* Ordinary receiver setup emits an explicit RPN batch and
                 * is covered independently below. */
                row == THY_ROW_MIDI_BENDSETUP ||
                /* Scoped sensitivity reset and muted acquisition have their
                 * own fixtures; their actions must not run blindly here. */
                row == THY_ROW_SENSITIVITY_PICK || row == THY_ROW_SENSITIVITY_RESET ||
                row == THY_ROW_SENSITIVITY_INHERIT || row==THY_ROW_SENSITIVITY_CALIBRATE) continue;
            CHECK(thy_state_begin_edit(&engine.state, row) == THY_STATE_OK);
            CHECK(!engine.state.editing && thy_core_queued(&core) == 0u);
            if (row == THY_ROW_PERFORMANCE_GUITARSHORTCUT)
                CHECK(instrument.performance.melody_layout == THY_LAYOUT_GUITAR && instrument.display_view == THY_VIEW_CHLADNI);
            else if (row == THY_ROW_SYSTEM_ABOUT) {
                CHECK(engine.state.about_open);
                CHECK(thy_engine_navigate(&engine, THY_STATE_LEFT) == THY_STATE_OK);
                CHECK(!engine.state.about_open);
            } else if (row == THY_ROW_ARPS_OPEN) {
                CHECK(engine.arp_quick_open);
                CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT) == THY_STATE_OK);
                CHECK(!engine.arp_quick_open);
            } else if (row == THY_ROW_ARPS_CLEAR) {
                CHECK(!engine.arps[engine.arp_target].held_count);
            } else if (row == THY_ROW_ENGINES_PLAY) {
                CHECK(instrument.engine_kind == THY_PLAY_ENGINE_PLAY);
            } else if (row >= THY_ROW_CHORDS_EDITSCALES && row <= THY_ROW_CHORDS_EDITSIX) {
                const thy_library saved = instrument.library;
                CHECK(instrument.library_editor.open);
                CHECK(instrument.library_editor.kind == row - THY_ROW_CHORDS_EDITSCALES);
                CHECK(instrument.library_editor.slot == 0u);
                CHECK(instrument.library_editor.page == THY_LIBRARY_UI_BROWSE && !instrument.library_editor.editing);
                CHECK(thy_engine_navigate(&engine, THY_STATE_LEFT) == THY_STATE_OK);
                CHECK(!instrument.library_editor.open);
                CHECK(memcmp(&instrument.library, &saved, sizeof saved) == 0);
                CHECK(thy_core_queued(&core) == 0u);
                ++cleanup_revisions; /* Closing an editor has its own UI revision. */
            } else CHECK(0 && "Uncovered live action");
            ++actions;
        }
    CHECK(actions == 8u && cleanup_revisions == 3u);
    CHECK(engine.state.revision == live_count + actions + cleanup_revisions);
    return 1;
}

static int test_ordinary_bend_setup_uses_selected_route_and_requires_quiet_non_mpe(void)
{
    static const uint8_t controllers[] = {101u, 100u, 6u, 38u, 101u, 100u};
    static const uint8_t values[] = {0u, 0u, 12u, 0u, 127u, 127u};
    CHECK(initialize());
    CHECK(set(THY_ROW_MIDI_ROUTINGPROFILE, THY_ROUTING_SPLIT) == THY_STATE_OK);
    CHECK(set(THY_ROW_MIDI_ROUTETARGET, 2) == THY_STATE_OK);
    CHECK(set(THY_ROW_MIDI_CHANNEL, 12) == THY_STATE_OK);
    CHECK(set(THY_ROW_MIDI_DESTINATIONS, 5) == THY_STATE_OK); /* USB1 + DIN */
    CHECK(set(THY_ROW_MIDI_BENDRANGE, 12) == THY_STATE_OK);
    CHECK(engine.channel_routes[1].range == 2u && engine.channel_routes[2].range == 12u);
    CHECK(thy_core_queued(&core) == 0u);
    uint32_t revision = engine.state.revision;
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_MIDI_BENDSETUP) == THY_STATE_OK);
    CHECK(!engine.state.editing && engine.state.revision == revision + 1u);
    thy_midi_event event;
    unsigned count = 0u;
    while (thy_core_poll(&core, &event)) {
        CHECK(count < 12u);
        CHECK(event.destination == (count < 6u ? 1u : 2u));
        CHECK(event.status == 0xbbu);
        CHECK(event.data1 == controllers[count % 6u] && event.data2 == values[count % 6u]);
        ++count;
    }
    CHECK(count == 12u && !thy_core_sounding(&core) && !thy_core_reserved(&core));

    CHECK(thy_instrument_press_pad(&instrument, 48u) == THY_OK);
    CHECK(thy_core_pad_held(&core, 48u));
    drain();
    const thy_pad_owner owner = core.owners[48];
    revision = engine.state.revision;
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_MIDI_BENDSETUP) == THY_STATE_BLOCKED);
    CHECK(!engine.state.editing && engine.state.revision == revision);
    CHECK(memcmp(&owner, &core.owners[48], sizeof owner) == 0 && !thy_core_queued(&core));
    CHECK(thy_instrument_release_pad(&instrument, 48u) == THY_OK);
    drain();

    CHECK(set(THY_ROW_EXPRESSION_ENABLED, 1) == THY_STATE_OK);
    revision = engine.state.revision;
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_MIDI_BENDSETUP) == THY_STATE_BLOCKED);
    CHECK(!engine.state.editing && engine.state.revision == revision && !thy_core_queued(&core));
    CHECK(set(THY_ROW_EXPRESSION_ENABLED, 0) == THY_STATE_OK);
    CHECK(initialize());
    CHECK(!engine.expression_config.enabled && engine.channel_routes[2].range == 2u);
    CHECK(!thy_core_queued(&core));
    return 1;
}

static int test_response_commit_rejection_keeps_the_real_owner(void)
{
    CHECK(initialize());
    CHECK(sample(100, 8));
    CHECK(sample(2000, 2));
    CHECK(thy_core_pad_held(&core, 0));
    drain();
    const thy_pad_owner owner = core.owners[0];
    const thy_instrument_response response = instrument.cluster_response[0];
    const int32_t original = get(THY_ROW_SENSITIVITY_ATTACK);
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_SENSITIVITY_ATTACK) == THY_STATE_OK);
    CHECK(thy_state_edit_set(&engine.state, original + 16) == THY_STATE_OK);
    const uint32_t before = engine.state.revision;
    CHECK(thy_state_commit(&engine.state) != THY_STATE_OK);
    CHECK(engine.state.editing && engine.state.edit_value == original + 16);
    CHECK(engine.state.revision == before && get(THY_ROW_SENSITIVITY_ATTACK) == original);
    CHECK(memcmp(&response, &instrument.cluster_response[0], sizeof response) == 0);
    CHECK(memcmp(&owner, &core.owners[0], sizeof owner) == 0);
    CHECK(thy_core_queued(&core) == 0);
    CHECK(sample(100, 2));
    drain();
    CHECK(thy_state_commit(&engine.state) == THY_STATE_OK);
    CHECK(instrument.cluster_response[0].attack_raw == original + 16);
    CHECK(instrument.response.attack_raw==1377 && instrument.cluster_response[1].attack_raw==1377);
    CHECK(instrument.sensors[0].warmup_count == 0);
    return 1;
}

static int test_live_edits_cannot_retarget_an_acquired_release(void)
{
    CHECK(initialize());
    CHECK(thy_instrument_set_route(&instrument, 0, 3, THY_ROUTE_USB0 | THY_ROUTE_DIN, 0) == THY_OK);
    CHECK(sample(100, 8));
    CHECK(sample(2000, 2));
    CHECK(thy_core_queued(&core) == 6);
    drain();
    const thy_pad_owner owner = core.owners[0];
    CHECK(set(THY_ROW_PERFORMANCE_ROOT, 2) == THY_STATE_OK);
    CHECK(set(THY_ROW_PERFORMANCE_TRANSPOSE, 12) == THY_STATE_OK);
    CHECK(set(THY_ROW_CHORDS_INVERSION, 1) == THY_STATE_OK);
    CHECK(set(THY_ROW_PERFORMANCE_VELOCITY, 70) == THY_STATE_OK);
    CHECK(memcmp(&owner, &core.owners[0], sizeof owner) == 0);
    CHECK(thy_core_queued(&core) == 0);
    CHECK(sample(100, 2));
    thy_midi_event event;
    unsigned releases = 0;
    while (thy_core_poll(&core, &event)) {
        CHECK(event.status == 0x83 && event.data2 == 0);
        CHECK(event.destination == 0 || event.destination == 2);
        CHECK(event.data1 == 60 || event.data1 == 64 || event.data1 == 67);
        ++releases;
    }
    CHECK(releases == 6 && thy_core_sounding(&core) == 0 && thy_core_reserved(&core) == 0);
    return 1;
}

static int test_control_binding_selects_one_navigation_owner(void)
{
    CHECK(initialize());
    CHECK(control(2));
    CHECK(instrument.menu_open && engine.state.level == THY_STATE_PLAY);
    CHECK(control(0));
    CHECK(!instrument.menu_open);
    thy_engine_bind_controls(&engine);
    CHECK(control(2));
    CHECK(engine.state.level == THY_STATE_GROUPS && !instrument.menu_open);
    CHECK(control(3)); /* First group is Engines; pass Performance to Chords. */
    CHECK(control(3));
    CHECK(control(4));
    CHECK(engine.state.level == THY_STATE_ROWS && !instrument.menu_open);
    CHECK(thy_state_current_row(&engine.state) == THY_ROW_PERFORMANCE_ROOT);
    CHECK(control(4));
    CHECK(engine.state.editing);
    CHECK(control(1));
    CHECK(engine.state.edit_value == 1 && get(THY_ROW_PERFORMANCE_ROOT) == 0);
    CHECK(control(4));
    CHECK(!engine.state.editing && instrument.performance.root == 1);
    CHECK(instrument.menu_open == 0 && instrument.menu_editing == 0);
    CHECK(action_count == 18); /* Original application action observation survives binding. */
    CHECK(thy_core_queued(&core) == 0);
    return 1;
}

static int test_center_exit_discards_draft_without_changing_music_or_saved_parameters(void)
{
    static uint8_t before_settings[THY_SETTINGS_PAYLOAD_MAX], after_settings[THY_SETTINGS_PAYLOAD_MAX];
    size_t before_bytes = 0u, after_bytes = 0u;
    CHECK(initialize());
    thy_engine_bind_controls(&engine);
    CHECK(set(THY_ROW_PERFORMANCE_ROOT, 2) == THY_STATE_OK);
    CHECK(sample(100, 8) && sample(2000, 2));
    CHECK(thy_core_pad_held(&core, 0u));
    drain();
    const thy_pad_owner owner = core.owners[0];
    const uint32_t sounding = thy_core_sounding(&core), reserved = thy_core_reserved(&core);
    engine.session.transport = THY_ENGINE_PLAYING;
    engine.session.capture = THY_ENGINE_CAPTURE_ARMED;
    engine.session.clock = THY_ENGINE_LOCKED;
    engine.session.tracks[0].playhead = 1234u;
    const thy_engine_session session = engine.session;
    CHECK(thy_settings_encode(&engine, before_settings, sizeof before_settings, &before_bytes));
    for (unsigned blocked = 0u; blocked < 2u; ++blocked) {
        const uint16_t row = blocked ? THY_ROW_SENSITIVITY_ATTACK : THY_ROW_PERFORMANCE_ROOT;
        CHECK(thy_state_focus(&engine.state, thy_menu_rows[row].group, row) == THY_STATE_OK);
        CHECK(control(4));
        CHECK(engine.state.editing && engine.state.edit_row == row);
        CHECK(control(1));
        if (blocked) {
            CHECK(thy_engine_navigate(&engine, THY_STATE_RIGHT) == THY_STATE_BLOCKED);
            CHECK(engine.state.editing);
        }
        CHECK(control(2));
        CHECK(engine.state.level == THY_STATE_PLAY && !engine.state.editing);
        CHECK(instrument.performance.root == 2 && instrument.cluster_response[0].attack_raw == 1377u);
        CHECK(memcmp(&owner, &core.owners[0], sizeof owner) == 0);
        CHECK(thy_core_queued(&core) == 0u && thy_core_sounding(&core) == sounding &&
            thy_core_reserved(&core) == reserved);
        CHECK(memcmp(&session, &engine.session, sizeof session) == 0);
        CHECK(thy_settings_encode(&engine, after_settings, sizeof after_settings, &after_bytes));
        CHECK(after_bytes == before_bytes && memcmp(before_settings, after_settings, before_bytes) == 0);
    }
    CHECK(sample(100, 2));
    unsigned releases = 0u;
    thy_midi_event event;
    while (thy_core_poll(&core, &event)) {
        CHECK((event.status & 0xf0u) == 0x80u);
        ++releases;
    }
    CHECK(releases == 3u && thy_core_sounding(&core) == 0u);
    return 1;
}

static int test_right_opens_about_and_center_returns_directly_to_play(void)
{
    uint16_t levels[THY_INSTRUMENT_CENTER_LIGHTS];
    CHECK(initialize());
    thy_engine_bind_controls(&engine);
    CHECK(thy_state_focus(&engine.state, thy_menu_rows[THY_ROW_SYSTEM_ABOUT].group,
        THY_ROW_SYSTEM_ABOUT) == THY_STATE_OK);
    CHECK(control(4));
    CHECK(engine.state.about_open && !engine.state.editing);
    thy_engine_center_levels(&engine, levels);
    CHECK(levels[THY_STATE_SELECT] && levels[THY_STATE_LEFT]);
    CHECK(!levels[THY_STATE_RIGHT] && !levels[THY_STATE_UP] && !levels[THY_STATE_DOWN]);
    CHECK(control(2));
    CHECK(engine.state.level == THY_STATE_PLAY && !engine.state.about_open);
    CHECK(control(2));
    CHECK(engine.state.level == THY_STATE_GROUPS);
    CHECK(control(4));
    CHECK(thy_state_current_row(&engine.state) == THY_ROW_SYSTEM_ABOUT);
    CHECK(control(2));
    CHECK(engine.state.level == THY_STATE_PLAY && !engine.state.about_open);
    CHECK(thy_core_queued(&core) == 0u);
    return 1;
}

static int test_catalog_replacement_is_atomic_and_future_engines_remain_stopped(void)
{
    CHECK(initialize());
    CHECK(engine.session.epoch == 1 && engine.session.system == THY_ENGINE_CONDITIONING);
    CHECK(thy_engine_catalog(&engine, 0, 2) == THY_STATE_OK);
    CHECK(engine.scale == 0 && engine.state.scale_count == 2 && engine.session.system == THY_ENGINE_READY);
    CHECK(set(THY_ROW_SEQUENCER_SCALE, 1) == THY_STATE_OK);
    const uint32_t before = engine.state.revision;
    CHECK(thy_engine_catalog(&engine, 0, 1) == THY_STATE_INVALID);
    CHECK(engine.scale == 0 && engine.state.scale_count == 2);
    CHECK(get(THY_ROW_SEQUENCER_SCALE) == 1 && engine.state.revision == before);
    CHECK(set(THY_ROW_ARPS_ENABLED, 1) == THY_STATE_OK);
    CHECK(set(THY_ROW_MIDI_REMOTERECORD, 1) == THY_STATE_OK);
    CHECK(engine.session.transport == THY_ENGINE_STOPPED && engine.session.capture == THY_ENGINE_CAPTURE_OFF);
    for (unsigned track = 0; track < 21; ++track) {
        CHECK(engine.session.tracks[track].generation == 0);
        CHECK(engine.session.tracks[track].recording == 0);
    }
    CHECK(thy_core_queued(&core) == 0 && thy_core_sounding(&core) == 0);
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_SEQUENCER_OPEN) == THY_STATE_OK);
    CHECK(engine.sequencer_ui.open && engine.rhythm_transport.state == THY_TRANSPORT_STOPPED);
    CHECK(engine.sequencer.capture.state == THY_SEQ_CAPTURE_OFF);
    return 1;
}

static int test_engine_hub_library_and_shortcuts_keep_held_ownership(void)
{
    CHECK(initialize()); thy_engine_bind_controls(&engine);
    CHECK(thy_menu_rows[THY_ROW_ENGINES_PLAY].group == 0u);
    CHECK(set(THY_ROW_PERFORMANCE_MELODYLAYOUT, 3) == THY_STATE_OK);
    CHECK(thy_engine_workspace(&engine, THY_PLAY_ENGINE_PLAY) == THY_STATE_OK);
    int16_t preview[THY_MAX_NOTES]; size_t count = 0u;
    CHECK(thy_instrument_preview_pad(&instrument, 52u, preview, &count) == THY_OK);
    CHECK(count == 1u && preview[0] == 61);
    CHECK(thy_instrument_press_pad(&instrument, 52u) == THY_OK); drain();
    thy_pad_owner owner = core.owners[52];
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_ENGINES_PLAY) == THY_STATE_OK);
    CHECK(instrument.engine_kind == THY_PLAY_ENGINE_PLAY);
    CHECK(thy_instrument_preview_pad(&instrument, 52u, preview, &count) == THY_OK);
    CHECK(count == 1u && preview[0] == 61); /* Entering Play preserves layout. */
    CHECK(set(THY_ROW_PERFORMANCE_MELODYLAYOUT, 1) == THY_STATE_OK);
    CHECK(instrument.engine_kind == THY_PLAY_ENGINE_PLAY);
    CHECK(thy_instrument_preview_pad(&instrument, 52u, preview, &count) == THY_OK);
    CHECK(count == 1u && preview[0] == 62); /* Scale degrees are a layout. */
    CHECK(memcmp(&owner,&core.owners[52],sizeof owner) == 0);
    CHECK(thy_engine_workspace(&engine, THY_PLAY_ENGINE_SEQUENCER) == THY_STATE_OK);
    CHECK(instrument.engine_kind == THY_PLAY_ENGINE_SEQUENCER && memcmp(&owner,&core.owners[52],sizeof owner) == 0);
    CHECK(thy_state_row_visible(&engine.state, THY_ROW_ENGINES_SEQUENCER));
    CHECK(thy_state_begin_edit(&engine.state, THY_ROW_ENGINES_SEQUENCER) == THY_STATE_OK);
    CHECK(!engine.state.editing && instrument.engine_kind == THY_PLAY_ENGINE_SEQUENCER && engine.sequencer_ui.open);
    CHECK(thy_engine_workspace(&engine, THY_PLAY_ENGINE_PLAY) == THY_STATE_OK);
    CHECK(thy_instrument_release_pad(&instrument,52u) == THY_OK);
    thy_midi_event event; unsigned releases=0u;
    while(thy_core_poll(&core,&event)) { CHECK(event.data1==61 && (event.status&0xf0u)==0x80u); ++releases; }
    CHECK(releases>0u && !thy_core_sounding(&core));
    for(int32_t scale=0;scale<70;++scale) {
        CHECK(set(THY_ROW_ENGINES_SCALE,scale)==THY_STATE_OK);
        CHECK(get(THY_ROW_PERFORMANCE_SCALE)==scale);
    }
    CHECK(set(THY_ROW_ENGINES_SHORTCUT,11)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_ENGINES_ASSIGNMENT)==THY_STATE_OK);
    CHECK(engine.state.edit_instance==11u);
    CHECK(thy_state_edit_set(&engine.state,14)==THY_STATE_OK);
    CHECK(set(THY_ROW_ENGINES_SHORTCUT,9)==THY_STATE_OK);
    CHECK(thy_state_commit(&engine.state)==THY_STATE_OK);
    CHECK(instrument.scale_shortcuts[11]==14u && instrument.scale_shortcuts[9]==14u);
    CHECK(thy_instrument_set_scale_shortcut(&instrument,10u,35u)==THY_OK);
    CHECK(thy_instrument_control(&instrument,3u,10u,1)==THY_OK);
    CHECK(thy_instrument_control(&instrument,3u,10u,0)==THY_OK);
    CHECK(engine.scale==35u);
    return 1;
}

static int test_default_engines_shortcut_and_arp_entry_are_processor_access(void)
{
    CHECK(initialize()); thy_engine_bind_controls(&engine);
    CHECK(instrument.play_nav[0] == THY_PLAY_ENGINES);
    engine.arp_target = 1u;
    uint8_t layout = instrument.performance.melody_layout;
    CHECK(control(THY_STATE_LEFT));
    CHECK(engine.state.level == THY_STATE_ROWS && thy_state_current_row(&engine.state) == THY_ROW_ENGINES_PLAY);
    CHECK(control(THY_STATE_DOWN));
    CHECK(thy_state_current_row(&engine.state) == THY_ROW_ENGINES_ARP);
    CHECK(control(THY_STATE_RIGHT));
    CHECK(engine.arp_quick_open && !engine.sequencer_ui.open);
    CHECK(!engine.arps[0].config.enabled && engine.arps[1].config.enabled);
    CHECK(instrument.engine_kind == THY_PLAY_ENGINE_PLAY && instrument.performance.melody_layout == layout);
    CHECK(thy_instrument_control(&instrument,3u,2u,1) == THY_OK);
    CHECK(thy_instrument_control(&instrument,3u,2u,0) == THY_OK);
    CHECK(engine.arps[0].config.enabled);
    CHECK(control(THY_STATE_SELECT));
    CHECK(!engine.arp_quick_open && engine.state.level == THY_STATE_PLAY);
    CHECK(engine.arps[0].config.enabled && engine.arps[1].config.enabled && instrument.engine_kind == THY_PLAY_ENGINE_PLAY);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_ENGINES_ARP) == THY_STATE_OK);
    CHECK(engine.arps[1].config.enabled && engine.arps[0].config.enabled); /* Idempotent activation. */
    CHECK(control(THY_STATE_SELECT));
    CHECK(instrument.performance.melody_layout == layout && !thy_core_sounding(&core));
    return 1;
}
static int test_sequencer_actions_target_selection_and_clear_song_is_undoable(void)
{
    CHECK(initialize());
    thy_seq_event first = {0u,120u,0u,0u,0u,THY_SEQ_NOTE,60u,96u};
    thy_seq_event second = {0u,120u,0u,0u,1u,THY_SEQ_NOTE,64u,96u};
    CHECK(thy_sequencer_add(&engine.sequencer,0u,&first,1u) == THY_OK);
    CHECK(thy_sequencer_add(&engine.sequencer,1u,&second,1u) == THY_OK);
    CHECK(set(THY_ROW_SEQUENCER_TRACK,2) == THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SEQUENCER_CLEARTRACK) == THY_STATE_OK);
    CHECK(thy_sequencer_track_count(&engine.sequencer,0u) == 1u);
    CHECK(!thy_sequencer_track_count(&engine.sequencer,1u));
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SEQUENCER_UNDO) == THY_STATE_OK);
    CHECK(thy_sequencer_track_count(&engine.sequencer,1u) == 1u);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SEQUENCER_DUPLICATE) == THY_STATE_OK);
    CHECK(thy_sequencer_track_count(&engine.sequencer,0u) == 1u);
    CHECK(thy_sequencer_track_count(&engine.sequencer,1u) == 2u);
    CHECK(engine.sequencer.tracks[1].loop_end == 7680u);
    CHECK(set(THY_ROW_SEQUENCER_LOOPEND,1920) == THY_STATE_OK);
    CHECK(thy_sequencer_track_count(&engine.sequencer,1u) == 2u); /* Bounds keep content. */
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SEQUENCER_TRIM) == THY_STATE_OK);
    CHECK(thy_sequencer_track_count(&engine.sequencer,0u) == 1u);
    CHECK(thy_sequencer_track_count(&engine.sequencer,1u) == 1u);
    CHECK(set(THY_ROW_SEQUENCER_CHANNEL,9) == THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SEQUENCER_CLEARSONG) == THY_STATE_OK);
    CHECK(!engine.sequencer.count && engine.sequencer.selected_track == 1u);
    CHECK(engine.sequencer.tracks[1].channel == 8u);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SEQUENCER_UNDO) == THY_STATE_OK);
    CHECK(engine.sequencer.count == 2u && engine.sequencer.tracks[1].channel == 8u);
    CHECK(thy_sequencer_track_count(&engine.sequencer,0u) == 1u && thy_sequencer_track_count(&engine.sequencer,1u) == 1u);
    return 1;
}
static int test_sequencer_scalar_target_is_frozen_and_rejection_is_atomic(void)
{
    CHECK(initialize());
    CHECK(set(THY_ROW_SEQUENCER_TRACK,2) == THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SEQUENCER_CHANNEL) == THY_STATE_OK);
    CHECK(engine.state.edit_instance == 1u);
    CHECK(thy_state_edit_set(&engine.state,9) == THY_STATE_OK);
    CHECK(set(THY_ROW_SEQUENCER_TRACK,3) == THY_STATE_OK);
    CHECK(thy_state_commit(&engine.state) == THY_STATE_OK);
    CHECK(engine.sequencer.tracks[1].channel == 8u && engine.sequencer.tracks[2].channel == 2u);
    CHECK(set(THY_ROW_SEQUENCER_TRACK,2) == THY_STATE_OK);
    CHECK(set(THY_ROW_SEQUENCER_ACTIVE,1) == THY_STATE_OK);
    engine.sequencer.capture.state = THY_SEQ_RECORDING;
    engine.sequencer.capture.track = 1u;
    const thy_transport before_transport = engine.rhythm_transport;
    const thy_seq_track before_track = engine.sequencer.tracks[1];
    CHECK(set(THY_ROW_SEQUENCER_ACTIVE,0) != THY_STATE_OK);
    CHECK(!memcmp(&engine.rhythm_transport,&before_transport,sizeof before_transport));
    CHECK(!memcmp(&engine.sequencer.tracks[1],&before_track,sizeof before_track));
    CHECK(engine.sequencer.capture.state == THY_SEQ_RECORDING && engine.sequencer.capture.track == 1u);
    return 1;
}
static int test_sequencer_navigation_reports_success_and_opens_selected_note(void)
{
    CHECK(initialize());
    CHECK(thy_sequencer_create(&engine.sequencer,0u) == THY_OK);
    CHECK(set(THY_ROW_PERFORMANCE_MELODYLAYOUT,THY_LAYOUT_CHROMATIC) == THY_STATE_OK);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_UP) == THY_STATE_OK);
    CHECK(engine.sequencer_ui.selected_step == 47u);
    CHECK(thy_sequencer_ui_pad(&engine.sequencer_ui,0u,1));
    CHECK(thy_sequencer_ui_pad(&engine.sequencer_ui,0u,0));
    CHECK(engine.sequencer.count == 0u && engine.sequencer_ui.selected_step == 0u);
    CHECK(thy_instrument_press_pad(&instrument,48u) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,48u) == THY_OK);
    drain();CHECK(engine.sequencer.count == 1u);
    CHECK(thy_sequencer_ui_pad(&engine.sequencer_ui,0u,1));
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(engine.state.level == THY_STATE_ROWS && thy_state_current_row(&engine.state) == THY_ROW_SEQUENCER_LENGTH);
    CHECK(engine.sequencer_ui.options_open && engine.sequencer_ui.options_track == 0u);
    CHECK(thy_sequencer_ui_pad(&engine.sequencer_ui,0u,0));
    CHECK(engine.sequencer.count == 1u); /* Selection release never changes content. */
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(!engine.sequencer_ui.open && engine.state.level == THY_STATE_PLAY);
    return 1;
}

static int test_track_options_have_their_own_scope_and_return_path(void)
{
    static const uint16_t track_rows[] = {
        THY_ROW_SEQUENCER_LENGTH, THY_ROW_SEQUENCER_INPUT, THY_ROW_SEQUENCER_GRID,
        THY_ROW_SEQUENCER_SOURCE, THY_ROW_SEQUENCER_PROCESSOR, THY_ROW_SEQUENCER_MUTE,
        THY_ROW_SEQUENCER_CHANNEL, THY_ROW_SEQUENCER_DESTINATIONS,
        THY_ROW_SEQUENCER_QUANTIZE, THY_ROW_SEQUENCER_RECORDCONTROLLERS,
        THY_ROW_SEQUENCER_SENDPRESSURE, THY_ROW_SEQUENCER_CLEARTRACK,
        THY_ROW_SEQUENCER_DUPLICATE, THY_ROW_SEQUENCER_TRIM
    };
    static const uint16_t global_rows[] = {
        THY_ROW_SEQUENCER_OPEN, THY_ROW_SEQUENCER_HELP, THY_ROW_SEQUENCER_TEMPO,
        THY_ROW_SEQUENCER_LAUNCH, THY_ROW_SEQUENCER_SAVE, THY_ROW_SEQUENCER_RECALL,
        THY_ROW_SEQUENCER_CLEARSONG, THY_ROW_SEQUENCER_UNDO, THY_ROW_SEQUENCER_REDO
    };
    CHECK(initialize());
    CHECK(thy_sequencer_create(&engine.sequencer,0u) == THY_OK);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS) == THY_STATE_OK);
    CHECK(thy_sequencer_ui_configure(&engine.sequencer_ui,THY_SEQ_UI_SELECTED_STEP,9u) == THY_OK);
    const uint8_t group = thy_menu_rows[THY_ROW_SEQUENCER_OPEN].group;
    const uint8_t global_cursor = (uint8_t)(THY_ROW_SEQUENCER_TEMPO - thy_menu_groups[group].first_row);
    engine.state.cursors[group] = global_cursor;
    /* Merely leaving a track open must not leak its controls into Settings. */
    for (unsigned n = 0u; n < sizeof track_rows / sizeof track_rows[0]; ++n)
        CHECK(!thy_state_row_visible(&engine.state,track_rows[n]));
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(engine.sequencer_ui.options_open && engine.sequencer_ui.options_track == 0u);
    CHECK(engine.state.level == THY_STATE_ROWS);
    for (unsigned n = 0u; n < sizeof track_rows / sizeof track_rows[0]; ++n)
        CHECK(thy_state_row_visible(&engine.state,track_rows[n]));
    for (unsigned n = 0u; n < sizeof global_rows / sizeof global_rows[0]; ++n)
        CHECK(!thy_state_row_visible(&engine.state,global_rows[n]));
    CHECK(!thy_state_row_visible(&engine.state,THY_ROW_SEQUENCER_STEPLENGTH));
    CHECK(!thy_state_row_visible(&engine.state,THY_ROW_SEQUENCER_STEPVELOCITY));
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[THY_ROW_SEQUENCER_CHANNEL].group,
        THY_ROW_SEQUENCER_CHANNEL) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(engine.state.editing && engine.state.edit_instance == 0u);
    CHECK(thy_engine_navigate(&engine,THY_STATE_UP) == THY_STATE_OK);
    CHECK(engine.state.edit_value == 2 && get(THY_ROW_SEQUENCER_CHANNEL) == 1);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(!engine.state.editing && engine.sequencer_ui.options_open);
    CHECK(engine.state.level == THY_STATE_ROWS && get(THY_ROW_SEQUENCER_CHANNEL) == 1);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(!engine.sequencer_ui.options_open && engine.sequencer_ui.open);
    CHECK(engine.sequencer_ui.view == THY_SEQ_VIEW_STEPS && engine.sequencer_ui.selected_step == 9u);
    CHECK(engine.state.level == THY_STATE_PLAY);
    CHECK(engine.state.cursors[group] == global_cursor);
    /* Direct close uses the same bounded return, never the Settings root. */
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(thy_state_current_row(&engine.state) == THY_ROW_SEQUENCER_CHANNEL);
    CHECK(thy_engine_sequencer_options_close(&engine) == THY_STATE_OK);
    CHECK(!engine.sequencer_ui.options_open && engine.sequencer_ui.open &&
        engine.state.level == THY_STATE_PLAY && engine.sequencer_ui.selected_step == 9u);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[THY_ROW_SEQUENCER_CHANNEL].group,
        THY_ROW_SEQUENCER_CHANNEL) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_UP) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(!engine.sequencer_ui.options_open && !engine.sequencer_ui.open && !engine.state.editing);
    CHECK(engine.state.level == THY_STATE_PLAY && engine.sequencer.tracks[0].channel == 0u);
    for (unsigned n = 0u; n < sizeof track_rows / sizeof track_rows[0]; ++n)
        CHECK(!thy_state_row_visible(&engine.state,track_rows[n]));
    CHECK(thy_core_queued(&core) == 0u);
    return 1;
}

static int test_selected_note_draft_cannot_follow_a_new_step_or_deleted_event(void)
{
    CHECK(initialize());
    thy_seq_event notes[] = {
        {0u,120u,0u,0u,0u,THY_SEQ_NOTE,60u,90u},
        {240u,120u,0u,0u,0u,THY_SEQ_NOTE,64u,80u}
    };
    CHECK(thy_sequencer_add(&engine.sequencer,0u,notes,2u) == THY_OK);
    const uint16_t first_id = engine.sequencer.events[0].id;
    const uint16_t second_id = engine.sequencer.events[1].id;
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS) == THY_STATE_OK);
    CHECK(engine.sequencer_ui.selected_event == first_id);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[THY_ROW_SEQUENCER_STEPVELOCITY].group,
        THY_ROW_SEQUENCER_STEPVELOCITY) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(engine.state.edit_event == first_id);
    CHECK(thy_state_edit_set(&engine.state,111) == THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument,4u) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,4u) == THY_OK);
    CHECK(engine.sequencer_ui.selected_event == second_id && engine.state.editing);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(thy_sequencer_find(&engine.sequencer,first_id)->value == 111u);
    CHECK(thy_sequencer_find(&engine.sequencer,second_id)->value == 80u);
    /* Duration edits use the same captured note identity as velocity. */
    CHECK(thy_instrument_press_pad(&instrument,0u) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,0u) == THY_OK);
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[THY_ROW_SEQUENCER_STEPLENGTH].group,
        THY_ROW_SEQUENCER_STEPLENGTH) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(engine.state.edit_event == first_id);
    CHECK(thy_state_edit_set(&engine.state,321) == THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument,4u) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,4u) == THY_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(thy_sequencer_find(&engine.sequencer,first_id)->duration == 321u);
    CHECK(thy_sequencer_find(&engine.sequencer,second_id)->duration == 120u);
    CHECK(thy_instrument_press_pad(&instrument,0u) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,0u) == THY_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(engine.state.edit_event == first_id);
    CHECK(thy_state_edit_set(&engine.state,640) == THY_STATE_OK);
    CHECK(thy_sequencer_remove(&engine.sequencer,first_id) == THY_OK);
    CHECK(thy_instrument_press_pad(&instrument,4u) == THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,4u) == THY_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) != THY_STATE_OK);
    CHECK(engine.state.editing && engine.sequencer_ui.options_open && engine.sequencer.count == 1u);
    CHECK(thy_sequencer_find(&engine.sequencer,second_id)->duration == 120u);
    CHECK(thy_sequencer_find(&engine.sequencer,second_id)->value == 80u);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(!engine.state.editing && engine.sequencer_ui.options_open);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(engine.sequencer_ui.open && !engine.sequencer_ui.options_open && !thy_core_queued(&core));
    return 1;
}

static int test_track_options_freeze_the_track_for_values_and_actions(void)
{
    CHECK(initialize());
    thy_seq_event first = {0u,120u,0u,0u,1u,THY_SEQ_NOTE,64u,96u};
    thy_seq_event second = {0u,120u,0u,0u,2u,THY_SEQ_NOTE,67u,96u};
    CHECK(thy_sequencer_add(&engine.sequencer,1u,&first,1u) == THY_OK);
    CHECK(thy_sequencer_add(&engine.sequencer,2u,&second,1u) == THY_OK);
    CHECK(thy_sequencer_select(&engine.sequencer,1u) == THY_OK);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(engine.sequencer_ui.options_track == 1u);
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[THY_ROW_SEQUENCER_CHANNEL].group,
        THY_ROW_SEQUENCER_CHANNEL) == THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(engine.state.edit_instance == 1u);
    CHECK(thy_state_edit_set(&engine.state,7) == THY_STATE_OK);
    CHECK(set(THY_ROW_SEQUENCER_TRACK,3) != THY_STATE_OK);
    CHECK(engine.sequencer.selected_track == 1u && engine.sequencer_ui.options_track == 1u);
    /* A lower-level host selection cannot retarget an already open screen. */
    CHECK(thy_sequencer_select(&engine.sequencer,2u) == THY_OK);
    CHECK(get(THY_ROW_SEQUENCER_CHANNEL) == 2);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(engine.sequencer.tracks[1].channel == 6u && engine.sequencer.tracks[2].channel == 2u);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SEQUENCER_CLEARTRACK) == THY_STATE_OK);
    CHECK(!thy_sequencer_track_count(&engine.sequencer,1u));
    CHECK(thy_sequencer_track_count(&engine.sequencer,2u) == 1u);
    CHECK(thy_engine_sequencer_options_close(&engine) == THY_STATE_OK);
    CHECK(engine.sequencer.selected_track == 1u && engine.sequencer_ui.view == THY_SEQ_VIEW_STEPS);
    return 1;
}

static int test_sensitivity_pad_scope_mapping_and_atomic_reset(void)
{
    CHECK(initialize());
    instrument.mapping[0]=95u;instrument.mapping[95]=0u;
    CHECK(thy_engine_sensitivity_electrical(&engine,0u)==95);
    CHECK(thy_engine_sensitivity_electrical(&engine,96u)==-1);
    uint32_t revision=instrument.revision;
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_INHERIT)!=THY_STATE_OK);
    CHECK(instrument.revision==revision && instrument.response.attack_raw==1377);
    CHECK(set(THY_ROW_SENSITIVITY_TARGET,1)==THY_STATE_OK);
    revision=instrument.revision;
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_RESET)!=THY_STATE_OK);
    CHECK(instrument.revision==revision && !instrument.sensors[95].response_overridden);
    CHECK(set(THY_ROW_SENSITIVITY_ATTACK,1400)==THY_STATE_OK);
    thy_instrument_velocity pad_velocity;thy_instrument_velocity_defaults(&pad_velocity);pad_velocity.mode=1u;
    CHECK(thy_instrument_set_pad_velocity(&instrument,95u,&pad_velocity)==THY_OK);
    CHECK(instrument.sensors[95].response.attack_raw==1400 && instrument.sensors[95].response_overridden);
    CHECK(instrument.velocity_overridden[95] && instrument.pad_velocity[95].mode==1u);
    CHECK(instrument.sensors[0].response.attack_raw==1377 && !instrument.velocity_overridden[0]);
    CHECK(set(THY_ROW_SENSITIVITY_TARGET,2)==THY_STATE_OK);
    CHECK(set(THY_ROW_SENSITIVITY_ATTACK,1450)==THY_STATE_OK);
    CHECK(set(THY_ROW_SENSITIVITY_RELEASE,1395)==THY_STATE_OK);
    CHECK(set(THY_ROW_SENSITIVITY_TARGET,1)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_ATTACK)==THY_STATE_OK);
    CHECK(engine.state.edit_instance==0u);
    CHECK(set(THY_ROW_SENSITIVITY_TARGET,2)==THY_STATE_OK);
    CHECK(thy_state_edit_set(&engine.state,1390)==THY_STATE_OK);
    CHECK(thy_state_commit(&engine.state)==THY_STATE_OK);
    CHECK(instrument.sensors[95].response.attack_raw==1390 && instrument.sensors[1].response.attack_raw==1450);
    CHECK(set(THY_ROW_SENSITIVITY_TARGET,0)==THY_STATE_OK);
    CHECK(set(THY_ROW_SENSITIVITY_ATTACK,1393)==THY_STATE_OK);
    CHECK(instrument.cluster_response[0].attack_raw==1393 && instrument.sensors[95].response.attack_raw==1390);
    CHECK(instrument.response.attack_raw==1377 && instrument.cluster_response[1].attack_raw==1377);
    CHECK(instrument.velocity_overridden[95]);
    CHECK(set(THY_ROW_SENSITIVITY_TARGET,1)==THY_STATE_OK);
    instrument.sensors[95].held=1u;
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_INHERIT)!=THY_STATE_OK);
    CHECK(instrument.sensors[95].response_overridden && instrument.velocity_overridden[95]);
    instrument.sensors[95].held=0u;
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_INHERIT)==THY_STATE_OK);
    CHECK(!instrument.sensors[95].response_overridden && !instrument.velocity_overridden[95]);
    CHECK(instrument.sensors[95].response.attack_raw==1393);
    CHECK(set(THY_ROW_SENSITIVITY_TARGET,0)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_RESET)==THY_STATE_OK);
    CHECK(instrument.cluster_response[0].attack_raw==1377 && instrument.cluster_velocity[0].mode==THY_VELOCITY_FIXED);
    CHECK(instrument.response.attack_raw==1377 && instrument.cluster_response[1].attack_raw==1377);
    CHECK(instrument.sensors[1].response_overridden && instrument.sensors[1].response.attack_raw==1450);
    CHECK(!thy_core_queued(&core));
    return 1;
}

static int test_pick_pad_mutes_selection_and_retains_release_after_exit(void)
{
    CHECK(initialize());
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[THY_ROW_SENSITIVITY_PICK].group,THY_ROW_SENSITIVITY_PICK)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_PICK)==THY_STATE_OK);
    CHECK(engine.sensitivity_pick);
    int result=-1;
    CHECK(thy_engine_sensitivity_pad(&engine,52u,0,&result)==0); /* Earlier musical release is not swallowed. */
    CHECK(thy_engine_sensitivity_pad(&engine,52u,1,&result)==1 && result==THY_OK);
    CHECK(engine.sensitivity_target==instrument.mapping[52]+1u);
    uint8_t target=engine.sensitivity_target;
    CHECK(thy_engine_sensitivity_pad(&engine,53u,1,&result)==1);
    CHECK(engine.sensitivity_target==target && !thy_core_queued(&core));
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK);
    CHECK(engine.state.level==THY_STATE_PLAY && !engine.sensitivity_pick);
    CHECK(thy_engine_sensitivity_pad(&engine,52u,0,&result)==1 && result==THY_OK);
    CHECK(thy_engine_sensitivity_pad(&engine,53u,0,&result)==1 && result==THY_OK);
    CHECK(!thy_core_queued(&core));
    CHECK(thy_engine_sensitivity_pad(&engine,52u,1,&result)==0);
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[THY_ROW_SENSITIVITY_PICK].group,THY_ROW_SENSITIVITY_PICK)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_PICK)==THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(!engine.sensitivity_pick && engine.state.level==THY_STATE_ROWS);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_ATTACK)==THY_STATE_OK);
    CHECK(thy_engine_sensitivity_pad(&engine,3u,1,&result)==0);
    CHECK(engine.state.edit_instance==(uint8_t)(target-1u));
    return 1;
}

static int begin_repeat_edit(uint16_t row)
{
    thy_engine_bind_controls(&engine);
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[row].group,row)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,row)==THY_STATE_OK);
    return 1;
}

static int test_numeric_hold_repeats_without_catchup_or_committing_the_draft(void)
{
    CHECK(initialize());
    CHECK(thy_engine_advance(&engine,0u)==THY_OK);
    CHECK(begin_repeat_edit(THY_ROW_SENSITIVITY_ATTACK));
    int32_t initial=engine.state.edit_value;
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,1)==THY_OK);
    CHECK(engine.state.edit_value==initial+1 && engine.navigation_repeat.action==THY_STATE_UP);
    CHECK(thy_engine_advance(&engine,399u)==THY_OK && engine.state.edit_value==initial+1);
    CHECK(thy_engine_advance(&engine,400u)==THY_OK && engine.state.edit_value==initial+2);
    CHECK(thy_engine_advance(&engine,459u)==THY_OK && engine.state.edit_value==initial+2);
    CHECK(thy_engine_advance(&engine,460u)==THY_OK && engine.state.edit_value==initial+3);
    CHECK(thy_engine_advance(&engine,2000u)==THY_OK && engine.state.edit_value==initial+4);
    CHECK(thy_engine_advance(&engine,2000u)==THY_OK && engine.state.edit_value==initial+4);
    CHECK(thy_engine_advance(&engine,2029u)==THY_OK && engine.state.edit_value==initial+4);
    CHECK(thy_engine_advance(&engine,2030u)==THY_OK && engine.state.edit_value==initial+5);
    CHECK(instrument.cluster_response[0].attack_raw==initial && !thy_core_queued(&core));
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,0)==THY_OK);
    CHECK(thy_engine_advance(&engine,5000u)==THY_OK && engine.state.edit_value==initial+5);
    CHECK(!engine.navigation_repeat.action);
    CHECK(thy_state_cancel(&engine.state)==THY_STATE_OK);
    CHECK(instrument.cluster_response[0].attack_raw==initial);
    CHECK(begin_repeat_edit(THY_ROW_SENSITIVITY_ATTACK));
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_DOWN,1)==THY_OK);
    CHECK(engine.state.edit_value==initial-1);
    CHECK(thy_engine_advance(&engine,5400u)==THY_OK && engine.state.edit_value==initial-2);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_DOWN,0)==THY_OK);
    CHECK(thy_state_cancel(&engine.state)==THY_STATE_OK);
    return 1;
}

static int test_hold_cancels_on_opposed_controls_context_change_and_panic(void)
{
    CHECK(initialize());CHECK(thy_engine_advance(&engine,0u)==THY_OK);
    CHECK(begin_repeat_edit(THY_ROW_SENSITIVITY_ATTACK));
    int32_t initial=engine.state.edit_value;
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,1)==THY_OK);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_DOWN,1)==THY_OK);
    CHECK(!engine.navigation_repeat.action);
    int32_t opposed=engine.state.edit_value;
    CHECK(thy_engine_advance(&engine,600u)==THY_OK && engine.state.edit_value==opposed);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_DOWN,0)==THY_OK);
    CHECK(thy_engine_advance(&engine,1000u)==THY_OK && engine.state.edit_value==opposed);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,0)==THY_OK);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,1)==THY_OK);
    CHECK(engine.navigation_repeat.action==THY_STATE_UP);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(!engine.state.editing && !engine.navigation_repeat.action);
    CHECK(begin_repeat_edit(THY_ROW_SENSITIVITY_ATTACK));
    CHECK(thy_engine_advance(&engine,1500u)==THY_OK && engine.state.edit_value==initial);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,0)==THY_OK);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,1)==THY_OK);
    CHECK(thy_state_set(&engine.state,THY_ROW_SENSITIVITY_TARGET,1)==THY_STATE_OK);
    int32_t captured=engine.state.edit_value;
    CHECK(thy_engine_advance(&engine,2000u)==THY_OK && engine.state.edit_value==captured);
    CHECK(!engine.navigation_repeat.action);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,0)==THY_OK);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,1)==THY_OK);
    thy_engine_panic(&engine);
    captured=engine.state.edit_value;
    CHECK(thy_engine_advance(&engine,2500u)==THY_OK && engine.state.edit_value==captured);
    CHECK(!engine.navigation_repeat.action);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,0)==THY_OK);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,1)==THY_OK);
    CHECK(thy_engine_usb_reset(&engine,2501u)==THY_OK);
    CHECK(!engine.navigation_repeat.action);
    return 1;
}

static int test_hold_ignores_selectors_small_values_and_pad_picker_and_handles_clock_wrap(void)
{
    CHECK(initialize());CHECK(thy_engine_advance(&engine,0u)==THY_OK);
    const uint16_t excluded[]={THY_ROW_SENSITIVITY_TARGET,THY_ROW_PERFORMANCE_ROOT,
        THY_ROW_PERFORMANCE_MPE,THY_ROW_LIGHTS_IDLE,THY_ROW_CHORDS_INVERSION};
    for(unsigned n=0;n<sizeof excluded/sizeof excluded[0];++n){
        CHECK(begin_repeat_edit(excluded[n]));
        CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,1)==THY_OK);
        int32_t value=engine.state.edit_value;
        CHECK(!engine.navigation_repeat.action);
        CHECK(thy_engine_advance(&engine,(n+1u)*1000u)==THY_OK && engine.state.edit_value==value);
        CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,0)==THY_OK);
        CHECK(thy_state_cancel(&engine.state)==THY_STATE_OK);
    }
    CHECK(initialize());CHECK(thy_engine_advance(&engine,UINT32_MAX-300u)==THY_OK);
    CHECK(begin_repeat_edit(THY_ROW_SENSITIVITY_ATTACK));
    int32_t initial=engine.state.edit_value;
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,1)==THY_OK);
    CHECK(thy_engine_advance(&engine,98u)==THY_OK && engine.state.edit_value==initial+1);
    CHECK(thy_engine_advance(&engine,99u)==THY_OK && engine.state.edit_value==initial+2);
    CHECK(thy_instrument_control(&instrument,0u,THY_STATE_UP,0)==THY_OK);
    return 1;
}

static int test_simple_sensitivity_preserves_raw_precision_and_links_only_detection(void)
{
    CHECK(initialize());
    int32_t low,high;
    CHECK(thy_engine_sensitivity_raw_bounds(&engine,96u,&low,&high)==THY_STATE_OK && low==23 && high==4095);
    CHECK(get(THY_ROW_SENSITIVITY_AMOUNT)==67);
    CHECK(set(THY_ROW_SENSITIVITY_AMOUNT,67)==THY_STATE_OK);
    CHECK(instrument.cluster_response[0].attack_raw==1377 && instrument.cluster_response[0].release_raw==1354);
    CHECK(set(THY_ROW_SENSITIVITY_AMOUNT,66)==THY_STATE_OK);
    CHECK(instrument.cluster_response[0].attack_raw==1407 && instrument.cluster_response[0].release_raw==1384);
    CHECK(get(THY_ROW_SENSITIVITY_LINKEDRELEASE)==1 && get(THY_ROW_SENSITIVITY_RELEASEGAP)==23);
    CHECK(set(THY_ROW_SENSITIVITY_FLOOR,1550)==THY_STATE_OK);
    CHECK(instrument.cluster_response[0].pressure_floor==1550 && instrument.cluster_response[0].attack_raw==1407);
    CHECK(get(THY_ROW_SENSITIVITY_LINKEDRELEASE)==1);
    CHECK(set(THY_ROW_SENSITIVITY_RELEASE,1400)==THY_STATE_OK);
    CHECK(get(THY_ROW_SENSITIVITY_LINKEDRELEASE)==0);
    CHECK(set(THY_ROW_SENSITIVITY_ATTACK,1450)==THY_STATE_OK);
    CHECK(instrument.cluster_response[0].release_raw==1400);
    CHECK(set(THY_ROW_SENSITIVITY_LINKEDRELEASE,1)==THY_STATE_OK);
    CHECK(instrument.cluster_response[0].release_raw==1443); /* Manual edit retained its 7-ADC gap. */
    CHECK(set(THY_ROW_SENSITIVITY_RELEASEGAP,31)==THY_STATE_OK);
    CHECK(instrument.cluster_response[0].release_raw==1419);
    CHECK(instrument.cluster_response[1].attack_raw==1377 && instrument.cluster_response[1].release_raw==1354);
    CHECK(set(THY_ROW_SENSITIVITY_TARGET,96)==THY_STATE_OK);
    CHECK(set(THY_ROW_SENSITIVITY_LINKEDRELEASE,0)==THY_STATE_OK);
    CHECK(set(THY_ROW_SENSITIVITY_ATTACK,1470)==THY_STATE_OK);
    CHECK(instrument.sensors[95].response.attack_raw==1470);
    CHECK(instrument.sensors[95].response.release_raw==1354);
    CHECK(instrument.sensors[95].response_overridden);
    /* A manual RIGHT-pad gate cannot constrain the LEFT cluster slider. */
    CHECK(set(THY_ROW_SENSITIVITY_TARGET,0)==THY_STATE_OK);
    CHECK(thy_engine_sensitivity_raw_bounds(&engine,96u,&low,&high)==THY_STATE_OK && low==31 && high==4095);
    CHECK(set(THY_ROW_SENSITIVITY_AMOUNT,75)==THY_STATE_OK);
    CHECK(instrument.cluster_response[0].attack_raw==1047 && instrument.cluster_response[0].release_raw==1016);
    CHECK(instrument.cluster_response[0].pressure_floor==1550);
    CHECK(instrument.sensors[95].response.attack_raw==1470 && instrument.sensors[95].response.release_raw==1354);
    CHECK(instrument.response.attack_raw==1377 && instrument.cluster_response[1].attack_raw==1377);
    /* The entire percentage range remains usable on RIGHT by mapping into
     * its safe interval, retaining that pad's literal Manual Release. */
    CHECK(set(THY_ROW_SENSITIVITY_TARGET,97)==THY_STATE_OK);
    CHECK(thy_engine_sensitivity_raw_bounds(&engine,97u,&low,&high)==THY_STATE_OK);
    unsigned expected=(1470u*(uint32_t)low+1377u/2u)/1377u;
    CHECK(set(THY_ROW_SENSITIVITY_AMOUNT,100)==THY_STATE_OK);
    CHECK(instrument.cluster_response[1].attack_raw==low);
    CHECK(instrument.sensors[95].response.attack_raw==expected && expected>1354u);
    CHECK(instrument.sensors[95].response.release_raw==1354 && !instrument.pad_release_policy[95].linked);
    CHECK(instrument.cluster_response[0].attack_raw==1047 && instrument.cluster_response[0].release_raw==1016);
    CHECK(instrument.cluster_velocity[0].mode==THY_VELOCITY_FIXED && instrument.cluster_velocity[1].mode==THY_VELOCITY_FIXED);
    CHECK(initialize());CHECK(sample(100u,8u));
    CHECK(thy_state_edit_bounds(&engine.state,THY_ROW_SENSITIVITY_AMOUNT,96u,&low,&high)==THY_STATE_OK && low==0 && high==100);
    CHECK(set(THY_ROW_SENSITIVITY_AMOUNT,100)==THY_STATE_OK);
    CHECK(instrument.cluster_response[0].release_raw>100u); /* Quiet-baseline guard at maximum sensitivity. */
    CHECK(instrument.cluster_response[0].pressure_floor==1353u && instrument.cluster_response[1].attack_raw==1377u);
    CHECK(!thy_core_queued(&core));
    return 1;
}

static int test_performance_velocity_scope_is_atomic_and_preserves_calibration(void)
{
    CHECK(initialize());
    instrument.mapping[0]=95u;instrument.mapping[95]=0u;
    CHECK(set(THY_ROW_SENSITIVITY_TARGET,27)==THY_STATE_OK);
    CHECK(set(THY_ROW_MIDI_ROUTETARGET,2)==THY_STATE_OK);
    thy_instrument_velocity custom={4u,210u,THY_VELOCITY_FIXED,THY_PRESSURE_FIRM,12u,113u};
    CHECK(thy_instrument_set_pad_velocity(&instrument,95u,&custom)==THY_OK);
    CHECK(set(THY_ROW_PERFORMANCE_VELOCITYTARGET,1)==THY_STATE_OK);
    instrument.sensors[0].held=1u; /* Right cannot block a Left-only change. */
    CHECK(set(THY_ROW_SENSITIVITY_VELOCITYMODE,THY_VELOCITY_STRIKE)==THY_STATE_OK);
    custom.mode=THY_VELOCITY_STRIKE;
    CHECK(!memcmp(&instrument.pad_velocity[95],&custom,sizeof custom));
    CHECK(instrument.velocity_overridden[95] && !instrument.velocity_overridden[1]);
    CHECK(instrument.cluster_velocity[0].mode==THY_VELOCITY_STRIKE && instrument.cluster_velocity[1].mode==THY_VELOCITY_FIXED);
    CHECK(engine.sensitivity_target==27u && engine.route_target==2u);
    instrument.sensors[0].held=0u;
    CHECK(set(THY_ROW_PERFORMANCE_VELOCITYTARGET,0)==THY_STATE_OK);
    CHECK(get(THY_ROW_SENSITIVITY_VELOCITYMODE)==2);
    char text[16];CHECK(thy_state_format(&engine.state,THY_ROW_SENSITIVITY_VELOCITYMODE,0,text,sizeof text)==THY_STATE_OK);
    CHECK(!strcmp(text,"Mixed"));
    CHECK(!thy_state_option_allowed(&engine.state,THY_ROW_SENSITIVITY_VELOCITYMODE,2));
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_VELOCITYMODE)==THY_STATE_OK);
    CHECK(engine.state.edit_instance==0u && engine.state.edit_value==2);
    CHECK(thy_state_commit(&engine.state)==THY_STATE_INVALID && engine.state.editing);
    CHECK(thy_state_adjust(&engine.state,1)==THY_STATE_OK && engine.state.edit_value==THY_VELOCITY_FIXED);
    CHECK(thy_state_cancel(&engine.state)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_VELOCITYMODE)==THY_STATE_OK);
    CHECK(thy_state_adjust(&engine.state,-1)==THY_STATE_OK && engine.state.edit_value==THY_VELOCITY_STRIKE);
    CHECK(thy_state_cancel(&engine.state)==THY_STATE_OK);
    /* A late rejection on Right must not mutate Left or any pad override. */
    static thy_instrument before;
    instrument.sensors[0].strike_ready=1u;before=instrument;
    CHECK(set(THY_ROW_SENSITIVITY_VELOCITYMODE,THY_VELOCITY_FIXED)==THY_STATE_BLOCKED);
    CHECK(!memcmp(&instrument,&before,sizeof before));
    instrument.sensors[0].strike_ready=0u;
    CHECK(set(THY_ROW_SENSITIVITY_VELOCITYMODE,THY_VELOCITY_FIXED)==THY_STATE_OK);
    custom.mode=THY_VELOCITY_FIXED;
    CHECK(!memcmp(&instrument.pad_velocity[95],&custom,sizeof custom));
    CHECK(instrument.cluster_velocity[0].mode==THY_VELOCITY_FIXED && instrument.cluster_velocity[1].mode==THY_VELOCITY_FIXED);
    /* Same defaults, divergent override: the whole selected scope is Mixed. */
    custom.mode=THY_VELOCITY_STRIKE;CHECK(thy_instrument_set_pad_velocity(&instrument,95u,&custom)==THY_OK);
    CHECK(get(THY_ROW_SENSITIVITY_VELOCITYMODE)==2);
    CHECK(set(THY_ROW_PERFORMANCE_VELOCITYTARGET,2)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_SENSITIVITY_VELOCITYMODE)==THY_STATE_OK);
    CHECK(engine.state.edit_instance==2u);
    CHECK(set(THY_ROW_PERFORMANCE_VELOCITYTARGET,1)==THY_STATE_OK);
    CHECK(thy_state_edit_set(&engine.state,THY_VELOCITY_STRIKE)==THY_STATE_OK);
    CHECK(thy_state_commit(&engine.state)==THY_STATE_OK);
    CHECK(instrument.cluster_velocity[0].mode==THY_VELOCITY_FIXED && instrument.cluster_velocity[1].mode==THY_VELOCITY_STRIKE);
    CHECK(engine.sensitivity_target==27u && engine.route_target==2u && !thy_core_queued(&core));
    static uint8_t saved[THY_SETTINGS_PAYLOAD_MAX],restored[THY_SETTINGS_PAYLOAD_MAX];size_t bytes=0,restored_bytes=0;
    CHECK(thy_settings_encode(&engine,saved,sizeof saved,&bytes));
    engine.state.cursors[thy_menu_rows[THY_ROW_PERFORMANCE_VELOCITY].group]=255u;
    engine.state.cursors[thy_menu_rows[THY_ROW_SENSITIVITY_TARGET].group]=255u;
    instrument.input_enabled=0u;
    CHECK(thy_settings_apply(&engine,saved,bytes));
    CHECK(engine.velocity_target==0u && engine.state.cursors[thy_menu_rows[THY_ROW_PERFORMANCE_VELOCITY].group]==0u);
    CHECK(engine.state.cursors[thy_menu_rows[THY_ROW_SENSITIVITY_TARGET].group]==0u);
    CHECK(thy_settings_encode(&engine,restored,sizeof restored,&restored_bytes));
    CHECK(bytes==restored_bytes && !memcmp(saved,restored,bytes));
    return 1;
}

static int test_performance_route_aliases_share_policies_and_capture_drafts(void)
{
    CHECK(initialize());
    const uint16_t target_rows[]={THY_ROW_MIDI_ROUTETARGET,THY_ROW_PERFORMANCE_ROUTETARGET};
    const uint16_t aliases[]={THY_ROW_PERFORMANCE_PRESSUREMODE,THY_ROW_PERFORMANCE_PRESSUREBEND,
        THY_ROW_PERFORMANCE_BENDRANGE};
    const uint16_t canonical[]={THY_ROW_MIDI_PRESSURE,THY_ROW_MIDI_PRESSUREBEND,THY_ROW_MIDI_BENDRANGE};
    for(unsigned n=0;n<3u;++n){
        CHECK(thy_state_instance_count(aliases[n])==9u);
        CHECK(thy_menu_rows[aliases[n]].instance_kind==THY_MENU_ROUTE);
    }
    for(unsigned profile=0;profile<3u;++profile){
        CHECK(set(THY_ROW_MIDI_ROUTINGPROFILE,(int32_t)profile)==THY_STATE_OK);
        for(unsigned target=0;target<9u;++target){
            int allowed=profile==THY_ROUTING_WHOLE?target==0u:
                profile==THY_ROUTING_SPLIT?target==1u || target==2u:target>=3u;
            for(unsigned n=0;n<2u;++n){
                CHECK(thy_state_option_allowed(&engine.state,target_rows[n],(int32_t)target)==allowed);
                uint8_t before=engine.route_target;
                CHECK(set(target_rows[n],(int32_t)target)==(allowed?THY_STATE_OK:THY_STATE_INVALID));
                CHECK(engine.route_target==(allowed?target:before));
            }
            if(!allowed)continue;
            CHECK(get(THY_ROW_MIDI_ROUTETARGET)==(int32_t)target && get(THY_ROW_PERFORMANCE_ROUTETARGET)==(int32_t)target);
            for(unsigned n=0;n<3u;++n){
                int32_t value=n==2u?(int32_t)target+1:(int32_t)(target%3u);
                CHECK(set(aliases[n],value)==THY_STATE_OK && get(canonical[n])==value);
                value=n==2u?24-(int32_t)target:(value+1)%3;
                CHECK(set(canonical[n],value)==THY_STATE_OK && get(aliases[n])==value);
                int32_t observed=-1;
                CHECK(thy_state_get_instance(&engine.state,aliases[n],(uint8_t)target,&observed)==THY_STATE_OK && observed==value);
            }
        }
    }
    CHECK(!thy_core_queued(&core)); /* No setup or musical output on policy edits. */
    CHECK(set(THY_ROW_MIDI_ROUTINGPROFILE,THY_ROUTING_SPLIT)==THY_STATE_OK);
    CHECK(set(THY_ROW_PERFORMANCE_ROUTETARGET,1)==THY_STATE_OK);
    CHECK(set(THY_ROW_MIDI_PRESSURE,THY_CHANNEL_PRESSURE_OFF)==THY_STATE_OK);
    CHECK(set(THY_ROW_PERFORMANCE_ROUTETARGET,2)==THY_STATE_OK);
    CHECK(set(THY_ROW_MIDI_PRESSURE,THY_CHANNEL_PRESSURE_POLY)==THY_STATE_OK);
    CHECK(set(THY_ROW_MIDI_ROUTETARGET,1)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_PERFORMANCE_PRESSUREMODE)==THY_STATE_OK);
    CHECK(engine.state.edit_instance==1u);
    CHECK(set(THY_ROW_PERFORMANCE_ROUTETARGET,2)==THY_STATE_OK);
    CHECK(thy_state_edit_set(&engine.state,THY_CHANNEL_PRESSURE_CHANNEL)==THY_STATE_OK);
    CHECK(thy_state_commit(&engine.state)==THY_STATE_OK);
    CHECK(get(THY_ROW_MIDI_PRESSURE)==THY_CHANNEL_PRESSURE_POLY);
    int32_t left=-1;
    CHECK(thy_state_get_instance(&engine.state,THY_ROW_MIDI_PRESSURE,1u,&left)==THY_STATE_OK && left==THY_CHANNEL_PRESSURE_CHANNEL);
    CHECK(set(THY_ROW_MIDI_BENDRANGE,7)==THY_STATE_OK);
    static uint8_t before[THY_SETTINGS_PAYLOAD_MAX],after[THY_SETTINGS_PAYLOAD_MAX];
    size_t before_bytes=0,after_bytes=0;
    CHECK(thy_settings_encode(&engine,before,sizeof before,&before_bytes));
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_PERFORMANCE_BENDRANGE)==THY_STATE_OK);
    CHECK(engine.state.edit_instance==2u);
    CHECK(thy_state_edit_set(&engine.state,21)==THY_STATE_OK);
    CHECK(set(THY_ROW_MIDI_ROUTETARGET,1)==THY_STATE_OK);
    CHECK(thy_state_cancel(&engine.state)==THY_STATE_OK);
    CHECK(thy_state_get_instance(&engine.state,THY_ROW_MIDI_BENDRANGE,2u,&left)==THY_STATE_OK && left==7);
    CHECK(thy_settings_encode(&engine,after,sizeof after,&after_bytes));
    CHECK(after_bytes==before_bytes && !memcmp(before,after,before_bytes));
    /* No alias state is serialized: the existing V13 policies restore once,
     * and both menus immediately project the same restored route values. */
    CHECK(set(THY_ROW_PERFORMANCE_PRESSUREMODE,THY_CHANNEL_PRESSURE_OFF)==THY_STATE_OK);
    instrument.input_enabled=0u;
    CHECK(thy_settings_apply(&engine,before,before_bytes));
    CHECK(set(THY_ROW_PERFORMANCE_ROUTETARGET,1)==THY_STATE_OK);
    CHECK(get(THY_ROW_PERFORMANCE_PRESSUREMODE)==THY_CHANNEL_PRESSURE_CHANNEL && get(THY_ROW_MIDI_PRESSURE)==THY_CHANNEL_PRESSURE_CHANNEL);
    CHECK(set(THY_ROW_MIDI_ROUTETARGET,2)==THY_STATE_OK);
    CHECK(get(THY_ROW_PERFORMANCE_BENDRANGE)==7 && get(THY_ROW_MIDI_BENDRANGE)==7);
    CHECK(thy_settings_encode(&engine,after,sizeof after,&after_bytes));
    CHECK(after_bytes==before_bytes && !memcmp(before,after,before_bytes) && !thy_core_queued(&core));
    return 1;
}

int main(void)
{
    if (!test_engine_hub_library_and_shortcuts_keep_held_ownership()) return 1;
    if (!test_all_live_rows_use_real_instrument_values() ||
        !test_ordinary_bend_setup_uses_selected_route_and_requires_quiet_non_mpe() ||
        !test_response_commit_rejection_keeps_the_real_owner() ||
        !test_live_edits_cannot_retarget_an_acquired_release() ||
        !test_control_binding_selects_one_navigation_owner() ||
        !test_center_exit_discards_draft_without_changing_music_or_saved_parameters() ||
        !test_right_opens_about_and_center_returns_directly_to_play() ||
        !test_catalog_replacement_is_atomic_and_future_engines_remain_stopped() ||
        !test_default_engines_shortcut_and_arp_entry_are_processor_access() ||
        !test_sequencer_actions_target_selection_and_clear_song_is_undoable() ||
        !test_sequencer_scalar_target_is_frozen_and_rejection_is_atomic() ||
        !test_sequencer_navigation_reports_success_and_opens_selected_note() ||
        !test_track_options_have_their_own_scope_and_return_path() ||
        !test_track_options_freeze_the_track_for_values_and_actions() ||
        !test_selected_note_draft_cannot_follow_a_new_step_or_deleted_event() ||
        !test_sensitivity_pad_scope_mapping_and_atomic_reset() ||
        !test_pick_pad_mutes_selection_and_retains_release_after_exit() ||
        !test_numeric_hold_repeats_without_catchup_or_committing_the_draft() ||
        !test_hold_cancels_on_opposed_controls_context_change_and_panic() ||
        !test_hold_ignores_selectors_small_values_and_pad_picker_and_handles_clock_wrap() ||
        !test_simple_sensitivity_preserves_raw_precision_and_links_only_detection() ||
        !test_performance_velocity_scope_is_atomic_and_preserves_calibration() ||
        !test_performance_route_aliases_share_policies_and_capture_drafts()) return 1;
    printf("24 shared engine integration scenarios passed (%u assertions).\n", assertions);
    return 0;
}
