/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_state.h"

static struct { unsigned char before[32]; thy_state state; unsigned char after[32]; } guarded;
static unsigned assertions, writes;
static int reject_write;
static uint16_t last_write;
static uint8_t last_instance;
static int32_t last_value;
static int32_t live_values[THY_MENU_ROW_COUNT][98];

#define S (&guarded.state)
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 0; \
} } while (0)

static uint16_t row(const char *id) { return thy_state_find_row(id); }
static int32_t get(const char *id)
{
    int32_t value = INT32_MIN;
    if (thy_state_get(S, row(id), &value) != THY_STATE_OK) return INT32_MIN;
    return value;
}
static int set(const char *id, int32_t value) { return thy_state_set(S, row(id), value); }
static int read_live(void *context, uint16_t id, uint8_t instance, int32_t *value)
{
    (void)context;
    *value = live_values[id][instance];
    return THY_STATE_OK;
}
static int apply(void *context, uint16_t id, uint8_t instance, int32_t value)
{
    (void)context;
    ++writes;
    last_write = id;
    last_instance = instance;
    last_value = value;
    if (!reject_write) {
        live_values[id][instance] = value;
        if (id == THY_ROW_MIDI_ROUTINGPROFILE) {
            int32_t *target = &live_values[THY_ROW_MIDI_ROUTETARGET][0];
            if (!(value == 0 ? *target == 0 : value == 1 ? *target == 1 || *target == 2 : *target >= 3 && *target <= 8))
                *target = value == 0 ? 0 : value == 1 ? 1 : 3;
        }
    }
    return reject_write ? THY_STATE_BLOCKED : THY_STATE_OK;
}
static void initialize(void)
{
    const thy_state_callbacks callbacks = { .read = read_live, .write = apply };
    for (unsigned i = 0; i < THY_MENU_ROW_COUNT; ++i)
        for (unsigned instance = 0; instance < 98; ++instance)
            live_values[i][instance] = thy_menu_rows[i].default_value;
    memset(&guarded, 0xa5, sizeof guarded);
    thy_state_init(S, &callbacks, NULL);
    (void)thy_state_set_scale_count(S, 70);
    writes = 0;
    reject_write = 0;
}

static int test_navigation_and_transaction(void)
{
    initialize();
    CHECK(S->level == THY_STATE_PLAY);
    CHECK(thy_state_navigate(S, THY_STATE_SELECT) == THY_STATE_OK);
    CHECK(S->level == THY_STATE_GROUPS);
    const uint32_t opened = S->transition_revision;
    CHECK(thy_state_navigate(S, THY_STATE_DOWN) == THY_STATE_OK);
    CHECK(thy_state_navigate(S, THY_STATE_DOWN) == THY_STATE_OK);
    CHECK(S->group_index == thy_menu_rows[row("chords.inversion")].group);
    CHECK(S->transition_revision == opened); /* Moving a cursor must not start a slide. */
    CHECK(thy_state_navigate(S, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(S->level == THY_STATE_ROWS);
    const uint32_t entered = S->transition_revision;
    CHECK(entered != opened);
    CHECK(thy_state_navigate(S, THY_STATE_DOWN) == THY_STATE_OK);
    CHECK(thy_state_current_row(S) == row("performance.scale")); /* Musical setup is grouped in Chords. */
    for(unsigned n=0u;n<THY_MENU_ROW_COUNT && thy_state_current_row(S)!=row("chords.inversion");++n)
        CHECK(thy_state_navigate(S,THY_STATE_DOWN)==THY_STATE_OK);
    const uint16_t selected = thy_state_current_row(S);
    CHECK(selected == row("chords.inversion"));
    CHECK(S->transition_revision == entered);
    CHECK(thy_state_navigate(S, THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(S->level == THY_STATE_GROUPS);
    CHECK(thy_state_navigate(S, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(thy_state_current_row(S) == selected);
    CHECK(thy_state_navigate(S, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(S->editing);
    const int32_t original = get("chords.inversion");
    const uint32_t editing = S->transition_revision;
    CHECK(thy_state_navigate(S, THY_STATE_UP) == THY_STATE_OK);
    CHECK(get("chords.inversion") == original);
    CHECK(S->edit_value == original + 1);
    CHECK(S->transition_revision == editing);
    CHECK(writes == 0u);
    CHECK(thy_state_navigate(S, THY_STATE_LEFT) == THY_STATE_OK);
    CHECK(!S->editing);
    CHECK(get("chords.inversion") == original);
    CHECK(writes == 0u);
    CHECK(thy_state_begin_edit(S, selected) == THY_STATE_OK);
    CHECK(thy_state_edit_set(S, original + 1) == THY_STATE_OK);
    CHECK(thy_state_navigate(S, THY_STATE_RIGHT) == THY_STATE_OK);
    CHECK(get("chords.inversion") == original + 1);
    CHECK(writes == 1u && last_write == selected && last_value == original + 1);
    CHECK(!S->editing);
    CHECK(thy_state_commit(S) == THY_STATE_NO_EDIT);
    CHECK(writes == 1u);
    return 1;
}

static int test_center_returns_to_play_from_every_settings_context(void)
{
    for (unsigned context = 0u; context < 6u; ++context) {
        initialize();
        CHECK(set("performance.root", 2) == THY_STATE_OK);
        CHECK(thy_state_navigate(S, THY_STATE_SELECT) == THY_STATE_OK);
        CHECK(S->level == THY_STATE_GROUPS);
        char saved_name[THY_STATE_TEXT_BYTES];
        memcpy(saved_name, thy_state_get_text(S, row("storage.profileName")), sizeof saved_name);
        uint16_t selected = context == 3u ? row("storage.profileName") :
            context == 4u ? row("sensitivity.attack") : row("performance.root");
        if (context == 3u) CHECK(thy_state_set_design_mode(S, 1u) == THY_STATE_OK);
        CHECK(thy_state_focus(S, thy_menu_rows[selected].group, selected) == THY_STATE_OK);
        if (context == 0u) {
            CHECK(thy_state_navigate(S, THY_STATE_LEFT) == THY_STATE_OK);
            CHECK(S->level == THY_STATE_GROUPS);
        } else if (context >= 2u && context <= 4u) {
            CHECK(thy_state_navigate(S, THY_STATE_RIGHT) == THY_STATE_OK);
            CHECK(S->editing);
            if (context == 3u)
                CHECK(thy_state_edit_text(S, "Unapplied name") == THY_STATE_OK);
            else CHECK(thy_state_navigate(S, THY_STATE_UP) == THY_STATE_OK);
            if (context == 4u) {
                reject_write = 1;
                CHECK(thy_state_navigate(S, THY_STATE_RIGHT) == THY_STATE_BLOCKED);
                CHECK(S->editing);
            }
        } else if (context == 5u) S->about_open = 1u;
        const uint8_t group = S->group_index;
        uint8_t cursors[THY_MENU_GROUP_COUNT];
        memcpy(cursors, S->cursors, sizeof cursors);
        const unsigned before_writes = writes;
        const uint32_t before = S->revision;
        const int was_editing = S->editing;
        CHECK(thy_state_navigate(S, THY_STATE_SELECT) == THY_STATE_OK);
        CHECK(S->level == THY_STATE_PLAY && !S->editing && !S->about_open);
        CHECK(S->edit_row == THY_STATE_NO_ROW);
        CHECK(get("performance.root") == 2);
        CHECK(strcmp(thy_state_get_text(S, row("storage.profileName")), saved_name) == 0);
        CHECK(writes == before_writes && S->group_index == group);
        CHECK(memcmp(cursors, S->cursors, sizeof cursors) == 0);
        CHECK(S->revision == before + (unsigned)was_editing);
        if (was_editing) {
            thy_state_event event;
            CHECK(thy_state_get_event(S, S->revision, &event));
            CHECK(event.kind == THY_STATE_EDIT_CANCELLED && event.row == selected);
        }
        CHECK(thy_state_navigate(S, THY_STATE_SELECT) == THY_STATE_OK);
        CHECK(S->level == THY_STATE_GROUPS && S->group_index == group);
        CHECK(thy_state_navigate(S, THY_STATE_RIGHT) == THY_STATE_OK);
        CHECK(thy_state_current_row(S) == selected);
    }
    return 1;
}

static int test_rejected_live_commit_is_atomic(void)
{
    initialize();
    const uint16_t trigger = row("sensitivity.attack");
    const int32_t original = get("sensitivity.attack");
    CHECK(thy_state_begin_edit(S, trigger) == THY_STATE_OK);
    CHECK(thy_state_edit_set(S, original + 1) == THY_STATE_OK);
    const uint32_t before = S->revision;
    reject_write = 1;
    CHECK(thy_state_commit(S) != THY_STATE_OK);
    CHECK(S->editing && S->edit_row == trigger && S->edit_value == original + 1);
    CHECK(get("sensitivity.attack") == original);
    CHECK(S->revision == before && writes == 1u);
    CHECK(thy_state_notice_text(S)[0] != '\0');
    reject_write = 0;
    CHECK(thy_state_commit(S) == THY_STATE_OK);
    CHECK(get("sensitivity.attack") == original + 1);
    CHECK(S->revision == before + 1u && writes == 2u);
    return 1;
}

static int test_independent_processor_and_track_instances(void)
{
    initialize();
    CHECK(set("arps.target", 1) == THY_STATE_OK); /* Option 1 is Arp 2. */
    CHECK(set("arps.enabled", 1) == THY_STATE_OK);
    CHECK(set("arps.channel", 9) == THY_STATE_OK);
    CHECK(set("arps.target", 0) == THY_STATE_OK);
    CHECK(get("arps.enabled") == 0 && get("arps.channel") == 3);
    CHECK(set("arps.channel", 3) == THY_STATE_OK);
    CHECK(set("arps.target", 1) == THY_STATE_OK);
    CHECK(get("arps.enabled") == 1 && get("arps.channel") == 9);
    CHECK(set("chords.strummerTarget", 0) == THY_STATE_OK);
    CHECK(set("chords.continuous", 1) == THY_STATE_OK);
    CHECK(set("chords.channel", 4) == THY_STATE_OK);
    CHECK(set("chords.strummerTarget", 1) == THY_STATE_OK);
    CHECK(get("chords.continuous") == 0 && get("chords.channel") == 1);
    CHECK(set("chords.channel", 8) == THY_STATE_OK);
    CHECK(set("chords.strummerTarget", 0) == THY_STATE_OK);
    CHECK(get("chords.continuous") == 1 && get("chords.channel") == 4);
    for (int32_t track = 1; track <= 21; ++track) {
        char name[32];
        (void)snprintf(name, sizeof name, "Track owner %ld", (long)track);
        CHECK(set("sequencer.track", track) == THY_STATE_OK);
        CHECK(set("sequencer.channel", (track % 16) + 1) == THY_STATE_OK);
        CHECK(thy_state_set_text(S, row("sequencer.name"), name) == THY_STATE_OK);
    }
    for (int32_t track = 21; track >= 1; --track) {
        char name[32];
        (void)snprintf(name, sizeof name, "Track owner %ld", (long)track);
        CHECK(set("sequencer.track", track) == THY_STATE_OK);
        CHECK(get("sequencer.channel") == (track % 16) + 1);
        CHECK(strcmp(thy_state_get_text(S, row("sequencer.name")), name) == 0);
    }
    CHECK(writes == 6u + 21u * 3u); /* Arp and track adapters bind; names remain text. */
    return 1;
}

static int test_edit_context_cannot_be_retargeted(void)
{
    initialize();
    CHECK(set("arps.target", 0) == THY_STATE_OK);
    CHECK(thy_state_begin_edit(S, row("arps.enabled")) == THY_STATE_OK);
    CHECK(thy_state_edit_set(S, 1) == THY_STATE_OK);
    CHECK(set("arps.target", 1) == THY_STATE_OK);
    CHECK(thy_state_commit(S) == THY_STATE_OK);
    CHECK(get("arps.enabled") == 0); /* Selected Arp 2 stays unchanged. */
    CHECK(set("arps.target", 0) == THY_STATE_OK);
    CHECK(get("arps.enabled") == 1);
    CHECK(set("sequencer.track", 1) == THY_STATE_OK);
    CHECK(thy_state_begin_edit(S, row("sequencer.name")) == THY_STATE_OK);
    CHECK(thy_state_edit_text(S, "Captured track") == THY_STATE_OK);
    CHECK(set("sequencer.track", 21) == THY_STATE_OK);
    CHECK(thy_state_commit(S) == THY_STATE_OK);
    CHECK(strcmp(thy_state_get_text(S, row("sequencer.name")), "Captured track") != 0);
    CHECK(set("sequencer.track", 1) == THY_STATE_OK);
    CHECK(strcmp(thy_state_get_text(S, row("sequencer.name")), "Captured track") == 0);
    return 1;
}

static int test_route_profiles_keep_their_targets(void)
{
    initialize();
    /* Route target options: whole, left, right, left1..3, right1..3. */
    CHECK(get("midi.routingProfile") == 1);
    CHECK(set("midi.routeTarget", 1) == THY_STATE_OK);
    CHECK(set("midi.channel", 3) == THY_STATE_OK);
    CHECK(set("midi.routeTarget", 2) == THY_STATE_OK);
    CHECK(set("midi.channel", 5) == THY_STATE_OK);
    CHECK(set("midi.routingProfile", 2) == THY_STATE_OK);
    for (int32_t target = 3; target <= 8; ++target) {
        CHECK(thy_state_option_allowed(S, row("midi.routeTarget"), target));
        CHECK(set("midi.routeTarget", target) == THY_STATE_OK);
        CHECK(set("midi.channel", target + 1) == THY_STATE_OK);
    }
    const uint32_t before = S->revision;
    CHECK(set("midi.routeTarget", 1) == THY_STATE_INVALID);
    CHECK(S->revision == before && get("midi.routeTarget") == 8);
    CHECK(set("midi.routingProfile", 0) == THY_STATE_OK);
    CHECK(get("midi.routeTarget") == 0);
    CHECK(set("midi.channel", 16) == THY_STATE_OK);
    CHECK(set("midi.routingProfile", 1) == THY_STATE_OK);
    CHECK(set("midi.routeTarget", 1) == THY_STATE_OK);
    CHECK(get("midi.channel") == 3);
    CHECK(set("midi.routeTarget", 2) == THY_STATE_OK);
    CHECK(get("midi.channel") == 5);
    CHECK(set("midi.routingProfile", 2) == THY_STATE_OK);
    for (int32_t target = 3; target <= 8; ++target) {
        CHECK(set("midi.routeTarget", target) == THY_STATE_OK);
        CHECK(get("midi.channel") == target + 1);
    }
    CHECK(set("midi.routingProfile", 0) == THY_STATE_OK);
    CHECK(get("midi.channel") == 16);
    CHECK(writes > 0u); /* These routes are now real adapter operations. */
    return 1;
}

static int test_cross_field_validation_and_unavailable_actions(void)
{
    initialize();
    const uint32_t before = S->revision;
    CHECK(set("midi.remoteLow", 100) == THY_STATE_INVALID);
    CHECK(get("midi.remoteLow") == 36 && get("midi.remoteHigh") == 67);
    CHECK(S->revision == before);
    CHECK(set("midi.remoteHigh", 110) == THY_STATE_OK);
    CHECK(set("midi.remoteLow", 100) == THY_STATE_OK);
    const uint32_t valid = S->revision;
    CHECK(set("midi.remoteHigh", 99) == THY_STATE_INVALID);
    CHECK(get("midi.remoteHigh") == 110 && S->revision == valid);
    CHECK(set("midi.assignmentTrigger", 1) == THY_STATE_OK);
    CHECK(set("midi.assignmentKind", 1) == THY_STATE_INVALID);
    CHECK(get("midi.assignmentKind") == 0);
    CHECK(set("midi.assignmentTrigger", 0) == THY_STATE_OK);
    CHECK(set("midi.assignmentKind", 1) == THY_STATE_OK);
    const uint32_t pc = S->revision;
    CHECK(set("midi.assignmentTrigger", 2) == THY_STATE_INVALID);
    CHECK(get("midi.assignmentTrigger") == 0 && S->revision == pc);
    CHECK(thy_state_begin_edit(S, row("storage.usbDisk")) == THY_STATE_UNAVAILABLE);
    CHECK(set("sequencer.play", 1) == THY_STATE_UNAVAILABLE);
    CHECK(set("system.installation", 1) == THY_STATE_UNAVAILABLE);
    CHECK(S->revision == pc && writes == 0u);
    return 1;
}

static int test_lower_button_mode_is_a_live_transaction(void)
{
    initialize();
    const uint16_t mode = row("controls.lowerMode");
    CHECK(row("chords.noteSlots") == THY_STATE_NO_ROW);
    CHECK(mode != THY_STATE_NO_ROW);
    CHECK(thy_menu_rows[mode].type == THY_MENU_SELECT);
    CHECK(thy_menu_rows[mode].status == THY_MENU_LIVE);
    CHECK(thy_menu_rows[mode].option_count == 2u);
    CHECK(get("controls.lowerMode") == 1); /* Octave layers is the default. */
    CHECK(get("controls.slotScope") == 0); /* Whole left side remains default. */
    const uint32_t before = S->revision;
    CHECK(set("controls.lowerMode", -1) == THY_STATE_INVALID);
    CHECK(set("controls.lowerMode", 2) == THY_STATE_INVALID);
    CHECK(thy_state_set_text(S, mode, "1111111") == THY_STATE_UNAVAILABLE);
    CHECK(S->revision == before && writes == 0u);
    CHECK(thy_state_begin_edit(S, mode) == THY_STATE_OK);
    CHECK(thy_state_edit_set(S, 0) == THY_STATE_OK);
    CHECK(get("controls.lowerMode") == 1 && writes == 0u);
    CHECK(thy_state_cancel(S) == THY_STATE_OK);
    CHECK(get("controls.lowerMode") == 1 && writes == 0u);
    CHECK(thy_state_begin_edit(S, mode) == THY_STATE_OK);
    CHECK(thy_state_edit_set(S, 0) == THY_STATE_OK);
    reject_write = 1;
    const uint32_t draft = S->revision;
    CHECK(thy_state_commit(S) == THY_STATE_BLOCKED);
    CHECK(S->revision == draft && S->editing && S->edit_value == 0);
    CHECK(get("controls.lowerMode") == 1 && writes == 1u);
    reject_write = 0;
    CHECK(thy_state_commit(S) == THY_STATE_OK);
    CHECK(get("controls.lowerMode") == 0 && !S->editing);
    CHECK(S->revision == draft + 1u && writes == 2u);
    CHECK(last_write == mode && last_value == 0);
    /* Live reads must follow the adapter, not an obsolete planned text mask. */
    live_values[mode][0] = 1;
    CHECK(get("controls.lowerMode") == 1);
    CHECK(S->revision == draft + 1u && writes == 2u);
    return 1;
}

static int test_invalid_inputs_and_bounded_storage(void)
{
    initialize();
    const uint32_t before = S->revision;
    CHECK(row("does.not.exist") == THY_STATE_NO_ROW);
    CHECK(thy_state_set(S, THY_STATE_NO_ROW, 0) == THY_STATE_INVALID);
    CHECK(set("arps.enabled", 2) == THY_STATE_INVALID);
    CHECK(set("sequencer.track", 0) == THY_STATE_INVALID);
    CHECK(set("sequencer.track", 22) == THY_STATE_INVALID);
    CHECK(set("midi.channel", 17) == THY_STATE_INVALID);
    CHECK(set("performance.root", 12) == THY_STATE_INVALID);
    CHECK(set("performance.scale", 70) == THY_STATE_INVALID);
    CHECK(S->revision == before && writes == 0u);
    char max_name[49], too_long[50];
    memset(max_name, 'N', sizeof max_name - 1u); max_name[48] = '\0';
    memset(too_long, 'X', sizeof too_long - 1u); too_long[49] = '\0';
    CHECK(thy_state_set_text(S, row("storage.profileName"), max_name) == THY_STATE_OK);
    const uint32_t named = S->revision;
    CHECK(thy_state_set_text(S, row("storage.profileName"), too_long) == THY_STATE_INVALID);
    CHECK(strcmp(thy_state_get_text(S, row("storage.profileName")), max_name) == 0);
    CHECK(S->revision == named);
    const char *invalid_text[] = { "\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x28\xa1", "\x7f", "\n" };
    for (unsigned i = 0; i < sizeof invalid_text / sizeof invalid_text[0]; ++i) {
        CHECK(thy_state_set_text(S, row("storage.profileName"), invalid_text[i]) == THY_STATE_INVALID);
        CHECK(strcmp(thy_state_get_text(S, row("storage.profileName")), max_name) == 0);
        CHECK(S->revision == named);
    }
    CHECK(thy_state_set_text(S, row("storage.profileName"), "Caf\xc3\xa9") == THY_STATE_OK);
    CHECK(strcmp(thy_state_get_text(S, row("storage.profileName")), "Caf\xc3\xa9") == 0);
    CHECK(thy_state_set_scale_count(S, 0) == THY_STATE_INVALID);
    CHECK(S->scale_count == 70);
    for (unsigned i = 0; i < 1000; ++i) CHECK(set("arps.enabled", (int32_t)(i & 1u)) == THY_STATE_OK);
    CHECK(S->history_count == THY_STATE_HISTORY);
    thy_state_event event;
    CHECK(thy_state_get_event(S, S->revision, &event));
    CHECK(event.sequence == S->revision && event.row == row("arps.enabled"));
    CHECK(!thy_state_get_event(S, S->revision - THY_STATE_HISTORY, &event));
    CHECK(sizeof(thy_state) <= 8192u); /* Keep schema growth visible in the host budget. */
    for (unsigned i = 0; i < sizeof guarded.before; ++i) CHECK(guarded.before[i] == 0xa5u && guarded.after[i] == 0xa5u);
    char clipped[5] = { 'X', 'X', 'X', 'X', 'Y' };
    CHECK(thy_state_format(S, row("storage.profileName"), 0, clipped, 4u) == THY_STATE_OK);
    CHECK(clipped[3] == '\0' && clipped[4] == 'Y');
    return 1;
}

static int test_information_descriptions_require_explicit_entry(void)
{
    static const char *const engines[] = {
        "engines.arp", "engines.sequencer", "engines.drums", "engines.lightboard", "engines.user"
    };
    initialize();
    CHECK(thy_state_set_design_mode(S, 0u) == THY_STATE_OK);
    const uint8_t group = thy_menu_rows[row(engines[0])].group;
    CHECK(thy_state_focus(S, group, row("engines.play")) == THY_STATE_OK);
    const uint32_t initial_transition = S->transition_revision;
    for (unsigned index = 0u; index < sizeof engines / sizeof engines[0]; ++index) {
        CHECK(thy_state_navigate(S, THY_STATE_DOWN) == THY_STATE_OK);
        CHECK(thy_state_current_row(S) == row(engines[index]));
        CHECK(!S->info_open && !S->editing && !S->about_open);
        CHECK(S->transition_revision == initial_transition); /* Highlighting stays in the list. */
    }
    unsigned checked[2] = {0u, 0u}, all_information = 0u;
    for (uint16_t selected = 0u; selected < THY_MENU_ROW_COUNT; ++selected)
        if (thy_menu_rows[selected].type == THY_MENU_INFO) ++all_information;
    for (uint8_t design = 0u; design <= 1u; ++design) {
        initialize();
        CHECK(thy_state_set_design_mode(S, design) == THY_STATE_OK);
        for (uint16_t selected = 0u; selected < THY_MENU_ROW_COUNT; ++selected) {
            const thy_menu_row *definition = &thy_menu_rows[selected];
            if (definition->type != THY_MENU_INFO) continue;
            int expected_visible = design || definition->status == THY_MENU_LIVE ||
                definition->group == group;
            CHECK(thy_state_row_visible(S, selected) == expected_visible);
            if (!expected_visible) {
                CHECK(thy_state_focus(S, definition->group, selected) == THY_STATE_UNAVAILABLE);
                CHECK(thy_state_begin_edit(S, selected) == THY_STATE_UNAVAILABLE);
                CHECK(!S->info_open && !S->editing);
                continue;
            }
            CHECK(thy_state_focus(S, definition->group, selected) == THY_STATE_OK);
            CHECK(!S->info_open && !S->editing && !S->about_open);
            const uint32_t before = S->transition_revision;
            const uint32_t revision = S->revision;
            const uint8_t history = S->history_count;
            CHECK(thy_state_navigate(S, THY_STATE_DOWN) == THY_STATE_OK);
            CHECK(!S->info_open);
            CHECK(thy_state_navigate(S, THY_STATE_UP) == THY_STATE_OK);
            CHECK(thy_state_current_row(S) == selected && !S->info_open);
            CHECK(S->transition_revision == before); /* Merely focusing never slides. */
            CHECK(thy_state_navigate(S, THY_STATE_RIGHT) == THY_STATE_OK);
            CHECK(S->info_open && !S->editing && S->level == THY_STATE_ROWS);
            CHECK(S->transition_revision != before && S->transition_direction == -1);
            CHECK(thy_state_current_row(S) == selected);
            CHECK(thy_state_get_text(S, selected) != NULL);
            const uint32_t entered = S->transition_revision;
            CHECK(thy_state_navigate(S, THY_STATE_UP) == THY_STATE_OK);
            CHECK(thy_state_navigate(S, THY_STATE_DOWN) == THY_STATE_OK);
            CHECK(thy_state_navigate(S, THY_STATE_RIGHT) == THY_STATE_OK);
            CHECK(S->info_open && thy_state_current_row(S) == selected);
            CHECK(S->transition_revision == entered);
            CHECK(thy_state_navigate(S, THY_STATE_LEFT) == THY_STATE_OK);
            CHECK(!S->info_open && S->level == THY_STATE_ROWS);
            CHECK(thy_state_current_row(S) == selected);
            CHECK(S->transition_revision != entered && S->transition_direction == 1);
            CHECK(thy_state_navigate(S, THY_STATE_RIGHT) == THY_STATE_OK);
            CHECK(S->info_open);
            CHECK(thy_state_navigate(S, THY_STATE_SELECT) == THY_STATE_OK);
            CHECK(S->level == THY_STATE_PLAY && !S->info_open);
            CHECK(thy_state_navigate(S, THY_STATE_SELECT) == THY_STATE_OK);
            CHECK(thy_state_navigate(S, THY_STATE_RIGHT) == THY_STATE_OK);
            CHECK(thy_state_current_row(S) == selected && !S->info_open);
            CHECK(thy_state_begin_edit(S, selected) == THY_STATE_OK);
            CHECK(S->info_open);
            CHECK(thy_state_focus(S, definition->group, selected) == THY_STATE_OK);
            CHECK(!S->info_open);
            CHECK(thy_state_begin_edit(S, selected) == THY_STATE_OK);
            thy_state_open(S);
            CHECK(!S->info_open && S->level == THY_STATE_GROUPS);
            CHECK(thy_state_focus(S, definition->group, selected) == THY_STATE_OK);
            CHECK(thy_state_begin_edit(S, selected) == THY_STATE_OK);
            thy_state_play(S);
            CHECK(!S->info_open && S->level == THY_STATE_PLAY);
            CHECK(S->revision == revision && S->history_count == history && writes == 0u);
            ++checked[design];
        }
    }
    CHECK(checked[0] >= 4u && checked[0] < all_information);
    CHECK(checked[1] == all_information);
    return 1;
}

static int test_pad_editor_freezes_instance_and_cross_field_checks(void)
{
    initialize();
    uint16_t trigger=row("sensitivity.attack");
    CHECK(thy_state_instance_count(trigger)==98u);
    CHECK(set("sensitivity.target",1)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(S,trigger)==THY_STATE_OK && S->edit_instance==0u);
    CHECK(set("sensitivity.target",2)==THY_STATE_OK);
    live_values[row("sensitivity.release")][1]=1395;
    live_values[trigger][1]=1450;
    CHECK(thy_state_edit_set(S,1390)==THY_STATE_OK);
    CHECK(thy_state_commit(S)==THY_STATE_OK);
    CHECK(last_write==trigger && last_instance==0u && live_values[trigger][0]==1390);
    CHECK(live_values[trigger][1]==1450);
    /* Velocity editing does not accidentally depend on pressure gates just
     * because menu grouping places its row between two pressure settings. */
    live_values[trigger][1]=100;
    CHECK(set("sensitivity.velocityMode",1)==THY_STATE_OK);
    live_values[trigger][1]=1450;
    CHECK(thy_state_begin_edit(S,row("sensitivity.softSpeed"))==THY_STATE_OK && S->edit_instance==1u);
    CHECK(thy_state_edit_set(S,128)==THY_STATE_OK);
    CHECK(thy_state_commit(S)==THY_STATE_INVALID && S->editing);
    CHECK(live_values[row("sensitivity.softSpeed")][1]==2);
    CHECK(thy_state_cancel(S)==THY_STATE_OK);
    CHECK(set("sensitivity.minVelocity",127)==THY_STATE_OK);
    CHECK(set("sensitivity.maxVelocity",126)==THY_STATE_INVALID);
    CHECK(set("sensitivity.target",0)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(S,trigger)==THY_STATE_OK && S->edit_instance==96u);
    CHECK(thy_state_edit_set(S,1393)==THY_STATE_OK);
    CHECK(thy_state_commit(S)==THY_STATE_OK && last_instance==96u);
    CHECK(!thy_state_row_visible(S,row("sensitivity.inherit")));
    CHECK(thy_state_row_visible(S,row("sensitivity.reset")));
    CHECK(!thy_state_row_visible(S,row("sensitivity.velocityPreset")));
    CHECK(set("sensitivity.target",96)==THY_STATE_OK);
    char text[20];
    CHECK(thy_state_format(S,row("sensitivity.target"),0,text,sizeof text)==THY_STATE_OK);
    CHECK(!strcmp(text,"R4 C12"));
    CHECK(thy_state_row_visible(S,row("sensitivity.inherit")));
    CHECK(!thy_state_row_visible(S,row("sensitivity.reset")));
    return 1;
}

static int test_scoped_bounds_and_direct_return_to_cluster(void)
{
    initialize();int32_t lo,hi;
    CHECK(set("sensitivity.target",96)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(S,row("sensitivity.target"))==THY_STATE_OK);
    CHECK(thy_state_adjust(S,1)==THY_STATE_OK && S->edit_value==97);
    CHECK(get("sensitivity.target")==96); /* UP changes only the draft. */
    CHECK(thy_state_adjust(S,1)==THY_STATE_OK && S->edit_value==0);
    CHECK(thy_state_adjust(S,1)==THY_STATE_OK && S->edit_value==97);
    CHECK(thy_state_adjust(S,-1)==THY_STATE_OK && S->edit_value==49);
    CHECK(thy_state_adjust(S,-1)==THY_STATE_OK && S->edit_value==50);
    CHECK(thy_state_adjust(S,-1)==THY_STATE_OK && S->edit_value==51);
    CHECK(thy_state_cancel(S)==THY_STATE_OK && get("sensitivity.target")==96);
    CHECK(set("sensitivity.target",1)==THY_STATE_OK);
    CHECK(set("sensitivity.linkedRelease",0)==THY_STATE_OK);
    CHECK(thy_state_edit_bounds(S,row("sensitivity.attack"),0u,&lo,&hi)==THY_STATE_OK && lo==1355 && hi==4095);
    CHECK(thy_state_begin_edit(S,row("sensitivity.attack"))==THY_STATE_OK);
    CHECK(set("sensitivity.target",2)==THY_STATE_OK);
    live_values[row("sensitivity.release")][1]=2000;
    CHECK(thy_state_adjust(S,-10000)==THY_STATE_OK && S->edit_value==1355);
    CHECK(thy_state_commit(S)==THY_STATE_OK && last_instance==0u);
    CHECK(thy_state_edit_bounds(S,row("sensitivity.floor"),0u,&lo,&hi)==THY_STATE_OK && lo==0 && hi==4094);
    CHECK(thy_state_edit_bounds(S,row("sensitivity.softSpeed"),0u,&lo,&hi)==THY_STATE_OK && hi==127);
    CHECK(thy_state_edit_bounds(S,row("sensitivity.release"),0u,&lo,&hi)==THY_STATE_OK && hi==1354);
    CHECK(thy_state_set_instance(S,row("sensitivity.linkedRelease"),0u,1)==THY_STATE_OK);
    CHECK(thy_state_edit_bounds(S,row("sensitivity.attack"),0u,&lo,&hi)==THY_STATE_OK && lo==1);
    CHECK(thy_state_set_instance(S,row("sensitivity.attack"),0u,1300)==THY_STATE_OK);
    CHECK(thy_state_set_instance(S,row("sensitivity.ceiling"),0u,1200)==THY_STATE_INVALID); /* Must exceed AT floor, not Trigger. */
    CHECK(thy_state_set_instance(S,row("sensitivity.floor"),0u,20)==THY_STATE_OK);
    CHECK(thy_state_set_instance(S,row("sensitivity.ceiling"),0u,1200)==THY_STATE_OK);
    return 1;
}
static int test_left_and_right_cluster_instances_are_independent_and_frozen(void)
{
    initialize();int32_t lo,hi,value;
    const uint16_t trigger=row("sensitivity.attack"),amount=row("sensitivity.amount");
    CHECK(thy_state_instance_count(amount)==98u);
    CHECK(set("sensitivity.target",0)==THY_STATE_OK);
    char text[20];
    CHECK(thy_state_format(S,row("sensitivity.target"),0,text,sizeof text)==THY_STATE_OK && !strcmp(text,"Left"));
    CHECK(thy_state_begin_edit(S,row("sensitivity.target"))==THY_STATE_OK);
    CHECK(thy_state_adjust(S,1)==THY_STATE_OK && S->edit_value==97);
    CHECK(thy_state_commit(S)==THY_STATE_OK && get("sensitivity.target")==97);
    CHECK(thy_state_format(S,row("sensitivity.target"),0,text,sizeof text)==THY_STATE_OK && !strcmp(text,"Right"));
    CHECK(thy_state_current_instance(S,trigger)==97u);
    CHECK(thy_state_begin_edit(S,trigger)==THY_STATE_OK && S->edit_instance==97u);
    CHECK(set("sensitivity.target",0)==THY_STATE_OK);
    CHECK(thy_state_edit_set(S,1500)==THY_STATE_OK);
    CHECK(thy_state_commit(S)==THY_STATE_OK && last_instance==97u);
    CHECK(live_values[trigger][97]==1500 && live_values[trigger][96]==1377);
    CHECK(thy_state_edit_bounds(S,amount,96u,&lo,&hi)==THY_STATE_OK && lo==0 && hi==100);
    CHECK(thy_state_edit_bounds(S,amount,97u,&lo,&hi)==THY_STATE_OK && lo==0 && hi==100);
    CHECK(thy_state_set_instance(S,amount,96u,100)==THY_STATE_OK);
    CHECK(thy_state_set_instance(S,amount,97u,0)==THY_STATE_OK);
    CHECK(thy_state_get_instance(S,amount,96u,&value)==THY_STATE_OK && value==100);
    CHECK(thy_state_get_instance(S,amount,97u,&value)==THY_STATE_OK && value==0);
    CHECK(thy_state_set_instance(S,amount,98u,50)==THY_STATE_INVALID);
    CHECK(set("sensitivity.target",48)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(S,row("sensitivity.target"))==THY_STATE_OK);
    CHECK(thy_state_adjust(S,1)==THY_STATE_OK && S->edit_value==0);
    CHECK(thy_state_cancel(S)==THY_STATE_OK && get("sensitivity.target")==48);
    CHECK(set("sensitivity.target",49)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(S,row("sensitivity.target"))==THY_STATE_OK);
    CHECK(thy_state_adjust(S,1)==THY_STATE_OK && S->edit_value==97);
    CHECK(thy_state_cancel(S)==THY_STATE_OK && get("sensitivity.target")==49);
    return 1;
}

int main(void)
{
    if (!test_navigation_and_transaction() || !test_center_returns_to_play_from_every_settings_context() ||
        !test_rejected_live_commit_is_atomic() ||
        !test_independent_processor_and_track_instances() || !test_edit_context_cannot_be_retargeted() ||
        !test_route_profiles_keep_their_targets() || !test_cross_field_validation_and_unavailable_actions() ||
        !test_lower_button_mode_is_a_live_transaction() ||
        !test_information_descriptions_require_explicit_entry() ||
        !test_invalid_inputs_and_bounded_storage() ||
        !test_pad_editor_freezes_instance_and_cross_field_checks() ||
        !test_scoped_bounds_and_direct_return_to_cluster() ||
        !test_left_and_right_cluster_instances_are_independent_and_frozen()) return 1;
    printf("13 shared state scenarios passed (%u assertions); state %zu bytes.\n", assertions, sizeof(thy_state));
    return 0;
}
