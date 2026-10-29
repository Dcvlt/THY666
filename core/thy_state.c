/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_state.h"
#include "thy_instrument.h"
#include "thy_engine.h"

_Static_assert(sizeof(thy_state) <= 8192u, "Shared menu state exceeds its 8 KiB RAM budget");

static int equal(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void clear_bytes(void *pointer, size_t size) {
    uint8_t *bytes = (uint8_t *)pointer;
    while (size--) *bytes++ = 0;
}

static void copy_text(char *out, size_t size, const char *text) {
    size_t n = 0;
    if (!size) return;
    if (text) while (text[n] && n + 1u < size) { out[n] = text[n]; ++n; }
    out[n] = 0;
}

static void integer_text(int32_t value, char *out, size_t size) {
    char digits[12];
    size_t count = 0, at = 0;
    uint32_t magnitude = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    if (!size) return;
    do { digits[count++] = (char)('0' + magnitude % 10u); magnitude /= 10u; } while (magnitude);
    if (value < 0 && at + 1u < size) out[at++] = '-';
    while (count && at + 1u < size) out[at++] = digits[--count];
    out[at] = 0;
}

uint8_t thy_state_instance_count(uint16_t row) {
    static const uint8_t counts[] = {1u, 2u, 2u, 21u, 9u, 9u, 12u, 98u, 3u};
    return row < THY_MENU_ROW_COUNT ? counts[thy_menu_rows[row].instance_kind] : 0u;
}

uint8_t thy_state_current_instance(const thy_state *state, uint16_t row) {
    if(!state || row>=THY_MENU_ROW_COUNT)return UINT8_MAX;
    int32_t value = 0;
    const thy_menu_row *definition = &thy_menu_rows[row];
    switch (definition->instance_kind) {
    case THY_MENU_SIDE:
        (void)thy_state_get(state, THY_ROW_PERFORMANCE_VELOCITYTARGET, &value); break;
    case THY_MENU_PAD:
        (void)thy_state_get(state, THY_ROW_SENSITIVITY_TARGET, &value);
        return value==97?97u:value>0 && value<=96 ? (uint8_t)(value-1) : 96u;
    case THY_MENU_ARP:
        (void)thy_state_get(state, THY_ROW_ARPS_TARGET, &value); break;
    case THY_MENU_STRUMMER:
        (void)thy_state_get(state, THY_ROW_CHORDS_STRUMMERTARGET, &value); break;
    case THY_MENU_TRACK:
        if (state->engine && state->engine->sequencer_ui.options_open)
            return state->engine->sequencer_ui.options_track;
        (void)thy_state_get(state, THY_ROW_SEQUENCER_TRACK, &value); --value; break;
    case THY_MENU_ROUTE:
        (void)thy_state_get(state, THY_ROW_MIDI_ROUTETARGET, &value); break;
    case THY_MENU_VIEW:
        (void)thy_state_get(state, THY_ROW_LIGHTS_VIEW, &value);
        break;
    case THY_MENU_SHORTCUT:
        (void)thy_state_get(state, THY_ROW_ENGINES_SHORTCUT, &value); break;
    default: return 0;
    }
    return value >= 0 && value < thy_state_instance_count(row) ? (uint8_t)value : 0u;
}

static uint8_t current_instance(const thy_state *state,uint16_t row)
{return thy_state_current_instance(state,row);}

int thy_state_get_instance(const thy_state *state, uint16_t row, uint8_t instance, int32_t *value) {
    const thy_menu_row *definition;
    int32_t candidate;
    if (!state || !value || row >= THY_MENU_ROW_COUNT || instance >= thy_state_instance_count(row)) return THY_STATE_INVALID;
    definition = &thy_menu_rows[row];
    if (definition->type == THY_MENU_TEXT || definition->type == THY_MENU_INFO) return THY_STATE_TEXT_REQUIRED;
    if (definition->type == THY_MENU_ACTION) return THY_STATE_UNAVAILABLE;
    if (definition->status == THY_MENU_LIVE) {
        candidate = definition->default_value;
        if (state->callbacks.read && state->callbacks.read(state->context, row, instance, &candidate) != THY_STATE_OK) return THY_STATE_ADAPTER_ERROR;
    } else {
        if (definition->storage_offset == UINT16_MAX) return THY_STATE_UNAVAILABLE;
        candidate = state->values[definition->storage_offset + instance];
    }
    *value = candidate;
    return THY_STATE_OK;
}

int thy_state_get(const thy_state *state, uint16_t row, int32_t *value) {
    if (!state || row >= THY_MENU_ROW_COUNT) return THY_STATE_INVALID;
    return thy_state_get_instance(state, row, current_instance(state, row), value);
}

const char *thy_state_get_text_instance(const thy_state *state, uint16_t row, uint8_t instance) {
    const thy_menu_row *definition;
    if (!state || row >= THY_MENU_ROW_COUNT || instance >= thy_state_instance_count(row)) return NULL;
    definition = &thy_menu_rows[row];
    if (definition->type == THY_MENU_INFO) return definition->default_text;
    if (definition->type != THY_MENU_TEXT || definition->text_offset == UINT16_MAX) return NULL;
    return state->texts[definition->text_offset + instance];
}

const char *thy_state_get_text(const thy_state *state, uint16_t row) {
    if (!state || row >= THY_MENU_ROW_COUNT) return NULL;
    return thy_state_get_text_instance(state, row, current_instance(state, row));
}

uint16_t thy_state_find_row(const char *id) {
    uint16_t row;
    for (row = 0; row < THY_MENU_ROW_COUNT; ++row) if (equal(id, thy_menu_rows[row].id)) return row;
    return THY_STATE_NO_ROW;
}

static int32_t default_scalar(uint16_t row, uint8_t instance) {
    if (row == THY_ROW_MIDI_CHANNEL && (instance == 2u || instance >= 6u)) return 2;
    return thy_menu_rows[row].default_value;
}

static void default_text(uint16_t row, uint8_t instance, char out[THY_STATE_TEXT_BYTES]) {
    copy_text(out, THY_STATE_TEXT_BYTES, thy_menu_rows[row].default_text);
    if (row == THY_ROW_SEQUENCER_NAME) {
        copy_text(out, THY_STATE_TEXT_BYTES, "Track ");
        integer_text((int32_t)instance + 1, out + 6u, THY_STATE_TEXT_BYTES - 6u);
    }
}

void thy_state_init(thy_state *state, const thy_state_callbacks *callbacks, void *context) {
    uint16_t row;
    if (!state) return;
    clear_bytes(state, sizeof(*state));
    if (callbacks) state->callbacks = *callbacks;
    state->context = context;
    state->design_editing = 1u;
    state->scale_count = THY_MENU_DEFAULT_SCALE_COUNT;
    state->edit_row = THY_STATE_NO_ROW;
    state->reset_track = UINT8_MAX;
    for (row = 0; row < THY_MENU_ROW_COUNT; ++row) {
        const thy_menu_row *definition = &thy_menu_rows[row];
        uint8_t instance;
        for (instance = 0; instance < thy_state_instance_count(row); ++instance) {
            if (definition->storage_offset != UINT16_MAX) state->values[definition->storage_offset + instance] = default_scalar(row, instance);
            if (definition->text_offset != UINT16_MAX) default_text(row, instance, state->texts[definition->text_offset + instance]);
        }
    }
}

int thy_state_track_row(uint16_t row) {
    if(row>=THY_MENU_ROW_COUNT || thy_menu_rows[row].group!=thy_menu_rows[THY_ROW_SEQUENCER_OPEN].group)return 0;
    return row==THY_ROW_SEQUENCER_RESETTRACK || thy_menu_rows[row].instance_kind==THY_MENU_TRACK ||
        row==THY_ROW_SEQUENCER_INPUT || row==THY_ROW_SEQUENCER_CLEARTRACK ||
        row==THY_ROW_SEQUENCER_DUPLICATE || row==THY_ROW_SEQUENCER_TRIM ||
        row==THY_ROW_SEQUENCER_STEPLENGTH || row==THY_ROW_SEQUENCER_STEPVELOCITY;
}

int thy_state_group_visible(uint8_t group) {
    return group<THY_MENU_GROUP_COUNT && group!=thy_menu_rows[THY_ROW_HARMONY_VIEW].group &&
        group!=thy_menu_rows[THY_ROW_SEQUENCER_OPEN].group && group!=thy_menu_rows[THY_ROW_ENGINESETTINGS_HARMONY].group;
}
int thy_state_row_visible(const thy_state *state, uint16_t row) {
    int32_t view = 0;
    if (!state || row >= THY_MENU_ROW_COUNT) return 0;
    /* Old workflow rows keep their storage identities, but are never exposed
     * as competing control schemes, including in the web design preview. */
    switch(row) {
    case THY_ROW_HARMONY_OPEN:case THY_ROW_HARMONY_PATTERN:case THY_ROW_HARMONY_SUGGESTIONS:
    case THY_ROW_HARMONY_DISCOVERY:case THY_ROW_HARMONY_PREVIEW:case THY_ROW_HARMONY_MANUAL:
    case THY_ROW_HARMONY_BANK:case THY_ROW_HARMONY_OCTAVETARGET:
    case THY_ROW_CHORDS_GUIDANCE: /* Retired location; Harmony owns exploration. */
        return 0;
    case THY_ROW_PERFORMANCE_AFTERTOUCH:
    case THY_ROW_PERFORMANCE_VELOCITYSOURCE:
    case THY_ROW_SENSITIVITY_VELOCITYPRESET:
    case THY_ROW_SEQUENCER_STEPS: case THY_ROW_SEQUENCER_TRACK:
    case THY_ROW_SEQUENCER_ACTIVE: case THY_ROW_SEQUENCER_INTERACTION:
    case THY_ROW_SEQUENCER_LAYOUT: case THY_ROW_SEQUENCER_SWAP:
    case THY_ROW_SEQUENCER_PLAY: case THY_ROW_SEQUENCER_RECORD:
    case THY_ROW_SEQUENCER_STOP: case THY_ROW_SEQUENCER_LOOPSTART:
    case THY_ROW_SEQUENCER_LOOPEND: case THY_ROW_SEQUENCER_REGION:
    case THY_ROW_SEQUENCER_REGIONSTART: case THY_ROW_SEQUENCER_REGIONEND:
        return 0;
    default: break;
    }
    if(row==THY_ROW_SENSITIVITY_RESET || row==THY_ROW_SENSITIVITY_INHERIT) {
        int32_t target=0;
        (void)thy_state_get(state,THY_ROW_SENSITIVITY_TARGET,&target);
        if((row==THY_ROW_SENSITIVITY_RESET && target!=0 && target!=97) ||
           (row==THY_ROW_SENSITIVITY_INHERIT && (target==0 || target==97)))return 0;
    }
    if(!state->design_editing) {
        int32_t mode=0;
        if(row==THY_ROW_CHORDS_QUALITY) {
            (void)thy_state_get(state,THY_ROW_CHORDS_LAYOUT,&mode);
            if(mode!=THY_CHORD_LAYOUT_CHROMATIC)return 0;
        }
        if(row==THY_ROW_CHORDS_INVERSION || (row>=THY_ROW_CHORDS_DROP2&&row<=THY_ROW_CHORDS_DROP4)) {
            (void)thy_state_get(state,THY_ROW_CHORDS_VOICINGMODE,&mode);
            if((row==THY_ROW_CHORDS_INVERSION&&mode!=0) ||
               (row!=THY_ROW_CHORDS_INVERSION&&mode!=1))return 0;
        }
    }
    int track_scope=state->engine && state->engine->sequencer_ui.open &&
        state->engine->sequencer_ui.options_open;
    if(track_scope && (!thy_state_track_row(row) ||
        !thy_sequencer_track_exists(&state->engine->sequencer,state->engine->sequencer_ui.options_track)))return 0;
    if(!track_scope && thy_state_track_row(row))return 0;
    if ((row == THY_ROW_SEQUENCER_STEPLENGTH || row == THY_ROW_SEQUENCER_STEPVELOCITY) &&
        (!state->design_editing || track_scope)) {
        int selected = 0;
        if (state->engine) for (unsigned n = 0u; n < state->engine->sequencer.count; ++n)
            if (state->engine->sequencer.events[n].id == state->engine->sequencer_ui.selected_event)
                selected = 1;
        if (!selected) return 0;
    }
    if (thy_menu_rows[row].instance_kind == THY_MENU_VIEW) {
        (void)thy_state_get(state, THY_ROW_LIGHTS_VIEW, &view);
        if ((row == THY_ROW_VIEWS_SLOT1 || row == THY_ROW_VIEWS_SLOT2) && view == THY_VIEW_DETAILED) return 0;
        if (row >= THY_ROW_VIEWS_DETAIL1 && row <= THY_ROW_VIEWS_DETAIL6 && view != THY_VIEW_DETAILED) return 0;
        if (row == THY_ROW_VIEWS_MOTION && view != THY_VIEW_CHLADNI && view != THY_VIEW_ORBIT && view != THY_VIEW_WAVES && view != THY_VIEW_FIFTHS) return 0;
    }
    if (thy_menu_rows[row].group == thy_menu_rows[THY_ROW_ENGINES_PLAY].group &&
        thy_menu_rows[row].type == THY_MENU_INFO) return 1;
    if (row == THY_ROW_CHORDS_OCTAVESTACKS && !state->design_editing) {
        int32_t mode = 1;
        (void)thy_state_get(state, THY_ROW_CONTROLS_LOWERMODE, &mode);
        if (mode != 0) return 0;
    }
    return state && row < THY_MENU_ROW_COUNT && (state->design_editing ||
        thy_menu_rows[row].status == THY_MENU_LIVE);
}

static void normalize_cursor(thy_state *state, uint8_t group_index) {
    const thy_menu_group *group = &thy_menu_groups[group_index];
    unsigned cursor = state->cursors[group_index];
    if (cursor >= group->row_count) cursor = 0u;
    for (unsigned scanned = 0u; scanned < group->row_count; ++scanned) {
        unsigned candidate = (cursor + scanned) % group->row_count;
        if (thy_state_row_visible(state, (uint16_t)(group->first_row + candidate))) {
            state->cursors[group_index] = (uint8_t)candidate;
            return;
        }
    }
    /* Empty categories retain their stored canonical cursor, but current_row
     * returns NO_ROW so SELECT can never edit a hidden default. */
}

int thy_state_set_design_mode(thy_state *state, uint8_t enabled) {
    if (!state || enabled > 1u) return THY_STATE_INVALID;
    if (!enabled && state->editing && thy_menu_rows[state->edit_row].status == THY_MENU_DESIGN)
        (void)thy_state_cancel(state);
    state->design_editing = enabled;
    for (uint8_t group = 0u; group < THY_MENU_GROUP_COUNT; ++group)
        normalize_cursor(state, group);
    return THY_STATE_OK;
}

int thy_state_set_scale_count(thy_state *state, uint16_t count) {
    uint16_t row;
    if (!state || !count) return THY_STATE_INVALID;
    /* Changing the catalog never silently clips an existing configuration. */
    for (row = 0; row < THY_MENU_ROW_COUNT; ++row) if (thy_menu_rows[row].dynamic == 1u) {
        uint8_t instance;
        for (instance = 0; instance < thy_state_instance_count(row); ++instance) {
            int32_t value;
            if (thy_state_get_instance(state, row, instance, &value) != THY_STATE_OK || value < 0 || value >= count) return THY_STATE_INVALID;
        }
    }
    if (state->editing && thy_menu_rows[state->edit_row].dynamic == 1u && (state->edit_value < 0 || state->edit_value >= count)) return THY_STATE_INVALID;
    state->scale_count = count;
    return THY_STATE_OK;
}

int thy_state_option_allowed(const thy_state *state, uint16_t row, int32_t value) {
    const thy_menu_row *definition;
    int32_t profile;
    if (!state || row >= THY_MENU_ROW_COUNT) return 0;
    definition = &thy_menu_rows[row];
    if (definition->type != THY_MENU_SELECT || value < 0) return 0;
    if (definition->dynamic == 2u) return value <= (int32_t)THY_MENU_DEFAULT_SCALE_COUNT ||
        (value<=78 && state->instrument&&state->instrument->library.scales[value-71].used);
    if (definition->dynamic) return value < state->scale_count;
    if (value >= definition->option_count) return 0;
    if(row==THY_ROW_SENSITIVITY_VELOCITYMODE)return value<=1;
    if(row==THY_ROW_CHORDS_USERSCALE)return !value || (state->instrument&&state->instrument->library.scales[value-1].used);
    if(row==THY_ROW_CHORDS_SIXBANK)return state->instrument&&
        (state->instrument->chord_layout!=THY_CHORD_LAYOUT_SIX_USER||state->instrument->library.six[value].used);
    if(row==THY_ROW_CHORDS_LAYOUT && value==THY_CHORD_LAYOUT_SIX_USER)
        return state->instrument&&state->instrument->six_bank<3u&&state->instrument->library.six[state->instrument->six_bank].used;
    if (row == THY_ROW_LIGHTS_VIEW) return thy_view_available((unsigned)value);
    if (row != THY_ROW_MIDI_ROUTETARGET && row != THY_ROW_PERFORMANCE_ROUTETARGET) return 1;
    if (thy_state_get(state, THY_ROW_MIDI_ROUTINGPROFILE, &profile) != THY_STATE_OK) return 0;
    return profile == 0 ? value == 0 : profile == 1 ? value == 1 || value == 2 : value >= 3 && value <= 8;
}

static int fail(thy_state *state, int result, uint8_t notice) {
    if (state) state->notice = notice;
    return result;
}

static int validate_scalar(thy_state *state, uint16_t row, uint8_t instance, int32_t value) {
    const thy_menu_row *definition;
    int32_t other;
    if (!state || row >= THY_MENU_ROW_COUNT) return fail(state, THY_STATE_INVALID, THY_NOTICE_INVALID);
    definition = &thy_menu_rows[row];
    if (definition->type == THY_MENU_ACTION || definition->type == THY_MENU_INFO) return fail(state, THY_STATE_UNAVAILABLE, THY_NOTICE_UNAVAILABLE);
    if (definition->type == THY_MENU_TEXT) return fail(state, THY_STATE_TEXT_REQUIRED, THY_NOTICE_TEXT);
    if ((definition->type == THY_MENU_TOGGLE && value != 0 && value != 1) ||
        (definition->type == THY_MENU_NUMBER && (value < definition->min || value > definition->max)) ||
        (definition->type == THY_MENU_SELECT && !thy_state_option_allowed(state, row, value))) return fail(state, THY_STATE_INVALID, THY_NOTICE_RANGE);
    if (row == THY_ROW_MIDI_REMOTELOW || row == THY_ROW_MIDI_REMOTEHIGH) {
        if (thy_state_get(state, row == THY_ROW_MIDI_REMOTELOW ? THY_ROW_MIDI_REMOTEHIGH : THY_ROW_MIDI_REMOTELOW, &other) != THY_STATE_OK ||
            (row == THY_ROW_MIDI_REMOTELOW ? value > other : value < other)) return fail(state, THY_STATE_INVALID, THY_NOTICE_REMOTE_RANGE);
    }
    if (row == THY_ROW_MIDI_ASSIGNMENTKIND || row == THY_ROW_MIDI_ASSIGNMENTTRIGGER) {
        uint16_t other_row = row == THY_ROW_MIDI_ASSIGNMENTKIND ? THY_ROW_MIDI_ASSIGNMENTTRIGGER : THY_ROW_MIDI_ASSIGNMENTKIND;
        if (thy_state_get(state, other_row, &other) != THY_STATE_OK ||
            (row == THY_ROW_MIDI_ASSIGNMENTKIND ? value == 1 && other != 0 : other == 1 && value != 0)) return fail(state, THY_STATE_INVALID, THY_NOTICE_PROGRAM_TRIGGER);
    }
    if (row==THY_ROW_SENSITIVITY_ATTACK || row==THY_ROW_SENSITIVITY_RELEASE ||
        row==THY_ROW_SENSITIVITY_FLOOR || row==THY_ROW_SENSITIVITY_CEILING) {
        int32_t attack, release, floor, ceiling;
        if (thy_state_get_instance(state, THY_ROW_SENSITIVITY_ATTACK, instance, &attack) != THY_STATE_OK ||
            thy_state_get_instance(state, THY_ROW_SENSITIVITY_RELEASE, instance, &release) != THY_STATE_OK ||
            thy_state_get_instance(state, THY_ROW_SENSITIVITY_FLOOR, instance, &floor) != THY_STATE_OK ||
            thy_state_get_instance(state, THY_ROW_SENSITIVITY_CEILING, instance, &ceiling) != THY_STATE_OK) return fail(state, THY_STATE_ADAPTER_ERROR, THY_NOTICE_ADAPTER);
        if (row == THY_ROW_SENSITIVITY_ATTACK) attack = value;
        if (row == THY_ROW_SENSITIVITY_RELEASE) release = value;
        if (row == THY_ROW_SENSITIVITY_FLOOR) floor = value;
        if (row == THY_ROW_SENSITIVITY_CEILING) ceiling = value;
        int32_t linked=0;
        if(row==THY_ROW_SENSITIVITY_ATTACK &&
           thy_state_get_instance(state,THY_ROW_SENSITIVITY_LINKEDRELEASE,instance,&linked)!=THY_STATE_OK)
            return fail(state,THY_STATE_ADAPTER_ERROR,THY_NOTICE_ADAPTER);
        int detector=row==THY_ROW_SENSITIVITY_ATTACK || row==THY_ROW_SENSITIVITY_RELEASE;
        if (detector ? !(linked || release < attack) : !(floor < ceiling))
            return fail(state, THY_STATE_INVALID, THY_NOTICE_SENSITIVITY);
    }
    if(row==THY_ROW_SENSITIVITY_SOFTSPEED || row==THY_ROW_SENSITIVITY_HARDSPEED ||
       row==THY_ROW_SENSITIVITY_MINVELOCITY || row==THY_ROW_SENSITIVITY_MAXVELOCITY) {
        int32_t low,high;
        int speed=row==THY_ROW_SENSITIVITY_SOFTSPEED || row==THY_ROW_SENSITIVITY_HARDSPEED;
        uint16_t low_row=speed?THY_ROW_SENSITIVITY_SOFTSPEED:THY_ROW_SENSITIVITY_MINVELOCITY;
        uint16_t high_row=speed?THY_ROW_SENSITIVITY_HARDSPEED:THY_ROW_SENSITIVITY_MAXVELOCITY;
        if(thy_state_get_instance(state,low_row,instance,&low)!=THY_STATE_OK ||
           thy_state_get_instance(state,high_row,instance,&high)!=THY_STATE_OK)
            return fail(state,THY_STATE_ADAPTER_ERROR,THY_NOTICE_ADAPTER);
        if(row==low_row)low=value;else high=value;
        if(speed?low>=high:low>high)return fail(state,THY_STATE_INVALID,THY_NOTICE_SENSITIVITY);
    }
    if(row==THY_ROW_SENSITIVITY_AMOUNT) {
        int32_t low,high,current;
        if(thy_state_get_instance(state,row,instance,&current)!=THY_STATE_OK)return fail(state,THY_STATE_ADAPTER_ERROR,THY_NOTICE_ADAPTER);
        if(value!=current && (thy_state_edit_bounds(state,row,instance,&low,&high)!=THY_STATE_OK || value<low || value>high))
            return fail(state,THY_STATE_INVALID,THY_NOTICE_SENSITIVITY);
    }
    return THY_STATE_OK;
}

/* Reject malformed, overlong UTF-8, surrogates and control code points. Bounds
 * are encoded bytes including a separate terminator, not JS UTF-16 units. */
static int valid_text(const char *text, uint8_t maximum) {
    size_t at = 0;
    if (!text) return 0;
    while (text[at]) {
        uint32_t code;
        uint8_t first = (uint8_t)text[at];
        size_t length, offset;
        if (first < 0x80u) { code = first; length = 1; }
        else if (first >= 0xc2u && first <= 0xdfu) { code = first & 0x1fu; length = 2; }
        else if (first >= 0xe0u && first <= 0xefu) { code = first & 0x0fu; length = 3; }
        else if (first >= 0xf0u && first <= 0xf4u) { code = first & 0x07u; length = 4; }
        else return 0;
        if (at + length > maximum || at + length >= THY_STATE_TEXT_BYTES) return 0;
        for (offset = 1; offset < length; ++offset) {
            uint8_t continuation = (uint8_t)text[at + offset];
            if ((continuation & 0xc0u) != 0x80u) return 0;
            code = (code << 6u) | (continuation & 0x3fu);
        }
        if (code < 0x20u || (code >= 0x7fu && code <= 0x9fu) ||
            (length == 2u && code < 0x80u) || (length == 3u && code < 0x800u) ||
            (length == 4u && code < 0x10000u) || code > 0x10ffffu ||
            (code >= 0xd800u && code <= 0xdfffu)) return 0;
        at += length;
    }
    return 1;
}

static int validate_text(thy_state *state, uint16_t row, const char *text) {
    if (!state || row >= THY_MENU_ROW_COUNT) return fail(state, THY_STATE_INVALID, THY_NOTICE_INVALID);
    if (thy_menu_rows[row].type != THY_MENU_TEXT) return fail(state, THY_STATE_UNAVAILABLE, THY_NOTICE_UNAVAILABLE);
    if (!valid_text(text, thy_menu_rows[row].max_length)) return fail(state, THY_STATE_INVALID, THY_NOTICE_TEXT);
    return THY_STATE_OK;
}

static void record(thy_state *state, uint8_t kind, uint16_t row, uint8_t instance, int32_t value, const char *text) {
    thy_state_event *event;
    char snapshot[THY_STATE_TEXT_BYTES];
    if (text) copy_text(snapshot, sizeof(snapshot), text);
    if (++state->revision == 0u) { state->revision = 1u; state->history_count = 0; }
    event = &state->events[(state->revision - 1u) % THY_STATE_HISTORY];
    clear_bytes(event, sizeof(*event));
    event->sequence = state->revision;
    event->row = row;
    event->instance = instance;
    event->value = value;
    event->kind = kind;
    if (text) copy_text(event->text, sizeof(event->text), snapshot);
    if (state->history_count < THY_STATE_HISTORY) ++state->history_count;
}

static int set_scalar_at(thy_state *state, uint16_t row, uint8_t instance, int32_t value) {
    const thy_menu_row *definition;
    if(!state || row>=THY_MENU_ROW_COUNT || instance>=thy_state_instance_count(row))
        return fail(state,THY_STATE_INVALID,THY_NOTICE_INVALID);
    if(state->reset_open)return fail(state,THY_STATE_BLOCKED,THY_NOTICE_RELEASE);
    int result = validate_scalar(state, row, instance, value);
    if (result != THY_STATE_OK) return result;
    if (instance >= thy_state_instance_count(row)) return fail(state, THY_STATE_INVALID, THY_NOTICE_INVALID);
    definition = &thy_menu_rows[row];
    if (definition->status == THY_MENU_LIVE) {
        if (!state->callbacks.write) return fail(state, THY_STATE_UNAVAILABLE, THY_NOTICE_UNAVAILABLE);
        result = state->callbacks.write(state->context, row, instance, value);
        if (result == THY_STATE_BLOCKED) return fail(state, result, THY_NOTICE_RELEASE);
        if (result != THY_STATE_OK) return fail(state, THY_STATE_ADAPTER_ERROR, THY_NOTICE_ADAPTER);
        state->notice = THY_NOTICE_APPLIED;
        record(state, THY_STATE_SETTING_APPLIED, row, instance, value, NULL);
    } else {
        state->values[definition->storage_offset + instance] = value;
        if (row == THY_ROW_MIDI_ROUTINGPROFILE) {
            int32_t target;
            (void)thy_state_get(state, THY_ROW_MIDI_ROUTETARGET, &target);
            if (!thy_state_option_allowed(state, THY_ROW_MIDI_ROUTETARGET, target)) state->values[thy_menu_rows[THY_ROW_MIDI_ROUTETARGET].storage_offset] = value == 0 ? 0 : value == 1 ? 1 : 3;
        }
        state->notice = THY_NOTICE_DRAFT;
        record(state, THY_STATE_CONFIG_CHANGED, row, instance, value, NULL);
    }
    return THY_STATE_OK;
}

int thy_state_set(thy_state *state, uint16_t row, int32_t value) {
    if (!state || row >= THY_MENU_ROW_COUNT) return fail(state, THY_STATE_INVALID, THY_NOTICE_INVALID);
    return set_scalar_at(state, row, current_instance(state, row), value);
}

int thy_state_set_instance(thy_state *state, uint16_t row, uint8_t instance, int32_t value) {
    return set_scalar_at(state, row, instance, value);
}

static int set_text_at(thy_state *state, uint16_t row, uint8_t instance, const char *text) {
    if(state&&state->reset_open)return fail(state,THY_STATE_BLOCKED,THY_NOTICE_RELEASE);
    int result = validate_text(state, row, text);
    if (result != THY_STATE_OK) return result;
    if (instance >= thy_state_instance_count(row)) return fail(state, THY_STATE_INVALID, THY_NOTICE_INVALID);
    copy_text(state->texts[thy_menu_rows[row].text_offset + instance], THY_STATE_TEXT_BYTES, text);
    state->notice = THY_NOTICE_DRAFT;
    record(state, THY_STATE_CONFIG_CHANGED, row, instance, 0, text);
    return THY_STATE_OK;
}

int thy_state_set_text(thy_state *state, uint16_t row, const char *text) {
    if (!state || row >= THY_MENU_ROW_COUNT) return fail(state, THY_STATE_INVALID, THY_NOTICE_INVALID);
    return set_text_at(state, row, current_instance(state, row), text);
}

int thy_state_reset_row(uint16_t row)
{
    return row<THY_MENU_ROW_COUNT&&(equal(thy_menu_rows[row].binding,"menu.reset")||
        equal(thy_menu_rows[row].binding,"menu.resetTrack"));
}
int thy_state_reset_begin(thy_state *state,uint16_t row)
{
    if(!state||!thy_state_reset_row(row))return THY_STATE_INVALID;
    if(state->reset_open||state->editing||state->info_open||state->about_open)return THY_STATE_BLOCKED;
    if(!state->engine)return THY_STATE_UNAVAILABLE;
    state->reset_group=thy_menu_rows[row].group;state->reset_track=UINT8_MAX;
    for(unsigned kind=0;kind<9u;++kind)state->reset_targets[kind]=0u;
    for(unsigned r=0;r<THY_MENU_ROW_COUNT;++r){unsigned kind=thy_menu_rows[r].instance_kind;
        if(kind&&kind<9u)state->reset_targets[kind]=current_instance(state,(uint16_t)r);}
    if(row==THY_ROW_SEQUENCER_RESETTRACK){
        if(!state->engine->sequencer_ui.options_open)return THY_STATE_BLOCKED;
        state->reset_track=state->engine->sequencer_ui.options_track;
    }
    if(state->reset_epoch==UINT32_MAX)return THY_STATE_BLOCKED;
    ++state->reset_epoch;state->reset_open=1u;state->reset_confirm=0u;state->notice=THY_NOTICE_NONE;
    state->transition_direction=-1;if(++state->transition_revision==0u)state->transition_revision=1u;
    return THY_STATE_OK;
}
int thy_state_reset_resolve(thy_state *state,uint32_t epoch,int confirmed)
{
    if(!state||!state->reset_open||epoch!=state->reset_epoch||(confirmed!=0&&confirmed!=1))return THY_STATE_BLOCKED;
    if(confirmed){
        int result=thy_engine_reset_options((thy_engine *)state->engine,state->reset_group,state->reset_targets,state->reset_track);
        if(result!=THY_STATE_OK)return fail(state,result,result==THY_STATE_BLOCKED?THY_NOTICE_RELEASE:THY_NOTICE_ADAPTER);
        uint16_t row=state->reset_track<THY_SEQ_TRACKS?THY_ROW_SEQUENCER_RESETTRACK:
            (uint16_t)(thy_menu_groups[state->reset_group].first_row+thy_menu_groups[state->reset_group].row_count-1u);
        /* Reset only the selected design/text scope, never action rows. */
        const thy_menu_group *group=&thy_menu_groups[state->reset_group];
        for(unsigned n=0;n<group->row_count;++n){unsigned r=group->first_row+n;const thy_menu_row *d=&thy_menu_rows[r];
            if(state->reset_track<THY_SEQ_TRACKS&&!thy_state_track_row((uint16_t)r))continue;
            if(state->reset_track==UINT8_MAX&&thy_state_track_row((uint16_t)r))continue;
            unsigned instance=state->reset_targets[d->instance_kind];
            if(instance>=thy_state_instance_count((uint16_t)r))continue;
            if(d->storage_offset!=UINT16_MAX)state->values[d->storage_offset+instance]=default_scalar((uint16_t)r,(uint8_t)instance);
            if(d->text_offset!=UINT16_MAX)default_text((uint16_t)r,(uint8_t)instance,state->texts[d->text_offset+instance]);
        }
        record(state,THY_STATE_SETTING_APPLIED,row,0u,1,NULL);state->notice=THY_NOTICE_APPLIED;
    }else state->notice=THY_NOTICE_CANCELLED;
    state->reset_open=state->reset_confirm=0u;state->reset_track=UINT8_MAX;
    state->transition_direction=1;if(++state->transition_revision==0u)state->transition_revision=1u;
    return THY_STATE_OK;
}
int thy_state_begin_edit(thy_state *state, uint16_t row) {
    if(state && state->engine)thy_engine_repeat_cancel((thy_engine *)state->engine);
    const thy_menu_row *definition;
    if (!state || row >= THY_MENU_ROW_COUNT) return fail(state, THY_STATE_INVALID, THY_NOTICE_INVALID);
    if(state->reset_open)return fail(state,THY_STATE_BLOCKED,THY_NOTICE_RELEASE);
    if(thy_state_reset_row(row))return thy_state_reset_begin(state,row);
    definition = &thy_menu_rows[row];
    if (definition->status == THY_MENU_DESIGN && !state->design_editing &&
        !(definition->type == THY_MENU_INFO && definition->group == thy_menu_rows[THY_ROW_ENGINES_PLAY].group))
        return fail(state, THY_STATE_UNAVAILABLE, THY_NOTICE_UNAVAILABLE);
    if (definition->type == THY_MENU_INFO) {
        if (!state->info_open) {
            state->info_open = 1u;
            state->transition_direction = -1;
            if (++state->transition_revision == 0u) state->transition_revision = 1u;
        }
        state->notice = THY_NOTICE_NONE;
        return THY_STATE_OK;
    }
    if (definition->type == THY_MENU_ACTION && definition->status == THY_MENU_LIVE) {
        if (!state->callbacks.write) return fail(state, THY_STATE_UNAVAILABLE, THY_NOTICE_UNAVAILABLE);
        uint8_t instance = current_instance(state, row);
        int result = state->callbacks.write(state->context, row, instance, 1);
        if (result == THY_STATE_BLOCKED) return fail(state, result, THY_NOTICE_RELEASE);
        if (result != THY_STATE_OK) return fail(state, THY_STATE_ADAPTER_ERROR, THY_NOTICE_ADAPTER);
        state->notice = THY_NOTICE_APPLIED;
        record(state, THY_STATE_SETTING_APPLIED, row, instance, 1, NULL);
        return THY_STATE_OK;
    }
    if (definition->type == THY_MENU_ACTION) return fail(state, THY_STATE_UNAVAILABLE, THY_NOTICE_UNAVAILABLE);
    if (definition->type == THY_MENU_TEXT) copy_text(state->edit_text, sizeof(state->edit_text), thy_state_get_text(state, row));
    else {
        int32_t value;
        if (thy_state_get(state, row, &value) != THY_STATE_OK) return fail(state, THY_STATE_ADAPTER_ERROR, THY_NOTICE_ADAPTER);
        state->edit_value = value;
    }
    state->editing = 1;
    state->edit_row = row;
    state->edit_instance = current_instance(state, row);
    state->edit_event = state->engine && (row==THY_ROW_SEQUENCER_STEPLENGTH ||
        row==THY_ROW_SEQUENCER_STEPVELOCITY)?state->engine->sequencer_ui.selected_event:0u;
    state->notice = THY_NOTICE_NONE;
    return THY_STATE_OK;
}

int thy_state_edit_set(thy_state *state, int32_t value) {
    if (!state || !state->editing) return THY_STATE_NO_EDIT;
    if (thy_menu_rows[state->edit_row].type == THY_MENU_TEXT) return fail(state, THY_STATE_TEXT_REQUIRED, THY_NOTICE_TEXT);
    state->edit_value = value;
    state->notice = THY_NOTICE_NONE;
    return THY_STATE_OK;
}

int thy_state_edit_text(thy_state *state, const char *text) {
    int result;
    if (!state || !state->editing) return THY_STATE_NO_EDIT;
    result = validate_text(state, state->edit_row, text);
    if (result == THY_STATE_OK) { copy_text(state->edit_text, sizeof(state->edit_text), text); state->notice = THY_NOTICE_NONE; }
    return result;
}

int thy_state_edit_bounds(const thy_state *state,uint16_t row,uint8_t instance,
    int32_t *minimum,int32_t *maximum)
{
    if(!state || row>=THY_MENU_ROW_COUNT || instance>=thy_state_instance_count(row) ||
       !minimum || !maximum || thy_menu_rows[row].type!=THY_MENU_NUMBER)return THY_STATE_INVALID;
    int32_t lo=thy_menu_rows[row].min,hi=thy_menu_rows[row].max,other;
    uint16_t related=THY_STATE_NO_ROW;int lower=0,strict=0;
    switch(row) {
    case THY_ROW_SENSITIVITY_ATTACK: {
        int32_t linked=0;
        if(thy_state_get_instance(state,THY_ROW_SENSITIVITY_LINKEDRELEASE,instance,&linked)!=THY_STATE_OK)return THY_STATE_ADAPTER_ERROR;
        if(!linked){related=THY_ROW_SENSITIVITY_RELEASE;lower=1;strict=1;}break;
    }
    case THY_ROW_SENSITIVITY_RELEASE:related=THY_ROW_SENSITIVITY_ATTACK;strict=1;break;
    case THY_ROW_SENSITIVITY_FLOOR:related=THY_ROW_SENSITIVITY_CEILING;strict=1;break;
    case THY_ROW_SENSITIVITY_CEILING:related=THY_ROW_SENSITIVITY_FLOOR;lower=1;strict=1;break;
    case THY_ROW_SENSITIVITY_SOFTSPEED:related=THY_ROW_SENSITIVITY_HARDSPEED;strict=1;break;
    case THY_ROW_SENSITIVITY_HARDSPEED:related=THY_ROW_SENSITIVITY_SOFTSPEED;lower=1;strict=1;break;
    case THY_ROW_SENSITIVITY_MINVELOCITY:related=THY_ROW_SENSITIVITY_MAXVELOCITY;break;
    case THY_ROW_SENSITIVITY_MAXVELOCITY:related=THY_ROW_SENSITIVITY_MINVELOCITY;lower=1;break;
    case THY_ROW_SENSITIVITY_RELEASEGAP: {
        int32_t linked,attack;
        if(thy_state_get_instance(state,THY_ROW_SENSITIVITY_LINKEDRELEASE,instance,&linked)!=THY_STATE_OK ||
           thy_state_get_instance(state,THY_ROW_SENSITIVITY_ATTACK,instance,&attack)!=THY_STATE_OK)return THY_STATE_ADAPTER_ERROR;
        if(linked) {
            int32_t floor=0;
            if(state->engine && state->instrument) {
                int target=thy_engine_sensitivity_electrical(state->engine,instance);
                if(target== -2)return THY_STATE_INVALID;
                unsigned first=target<0?0u:(unsigned)target,last=target<0?96u:(unsigned)target+1u;
                for(unsigned p=first;p<last;++p) {
                    if(target<0 && state->instrument->mapping[p]/48u!=(target== -3?1u:0u))continue;
                    const thy_instrument_sensor *s=&state->instrument->sensors[p];
                    if(s->warmup_count<THY_INSTRUMENT_WARMUP_SAMPLES)continue;
                    int32_t value=(int32_t)s->calibration.baseline+2*(int32_t)(s->warmup_max-s->warmup_min)+4;
                    if(state->instrument->sensitivity_guard[p]>value)value=state->instrument->sensitivity_guard[p];
                    if(value>floor)floor=value;
                }
            }
            hi=attack-floor;if(hi>4095)hi=4095;
        }
        break;
    }
    case THY_ROW_MIDI_REMOTELOW:related=THY_ROW_MIDI_REMOTEHIGH;break;
    case THY_ROW_MIDI_REMOTEHIGH:related=THY_ROW_MIDI_REMOTELOW;lower=1;break;
    case THY_ROW_SENSITIVITY_AMOUNT: {
        /* Percentage spans the safe usable raw interval, including its upper
         * noise guard. It is not an absolute ADC percentage with a capped UI. */
        if(state->engine){int32_t raw_low,raw_high;
            int result=thy_engine_sensitivity_raw_bounds(state->engine,instance,&raw_low,&raw_high);
            if(result!=THY_STATE_OK)return result;
        }
        lo=0;hi=100;break;
    }
    default:break;
    }
    if(related!=THY_STATE_NO_ROW) {
        if(thy_state_get_instance(state,related,instance,&other)!=THY_STATE_OK)return THY_STATE_ADAPTER_ERROR;
        if(lower) {if(other+strict>lo)lo=other+strict;}
        else if(other-strict<hi)hi=other-strict;
    }
    if(lo>hi)return THY_STATE_INVALID;
    *minimum=lo;*maximum=hi;return THY_STATE_OK;
}

int thy_state_adjust(thy_state *state, int delta) {
    const thy_menu_row *definition;
    if (!state || !state->editing) return THY_STATE_NO_EDIT;
    if (!delta) return THY_STATE_OK;
    definition = &thy_menu_rows[state->edit_row];
    if(state->edit_row==THY_ROW_SENSITIVITY_TARGET) {
        if(delta>0)state->edit_value=state->edit_value==97?0:
            state->edit_value==0 || state->edit_value>48?97:0;
        else if(state->edit_value==97)state->edit_value=49;
        else if(state->edit_value==48)state->edit_value=97;
        else if(state->edit_value==96)state->edit_value=0;
        else ++state->edit_value;
    } else if (definition->type == THY_MENU_TOGGLE) state->edit_value = !state->edit_value;
    else if (definition->type == THY_MENU_NUMBER) {
        int32_t minimum,maximum;
        int bounds=thy_state_edit_bounds(state,state->edit_row,state->edit_instance,&minimum,&maximum);
        if(bounds!=THY_STATE_OK)return bounds;
        int64_t candidate = (int64_t)state->edit_value + (int64_t)delta * definition->step;
        state->edit_value = candidate < minimum ? minimum : candidate > maximum ? maximum : (int32_t)candidate;
    } else if (definition->type == THY_MENU_SELECT) {
        int32_t count = definition->dynamic == 2u ? THY_MENU_DEFAULT_SCALE_COUNT + 1u + THY_LIBRARY_SCALES : definition->dynamic ? state->scale_count : definition->option_count;
        int32_t step = delta > 0 ? 1 : -1;
        uint32_t remaining = delta < 0 ? (uint32_t)(-(int64_t)delta) : (uint32_t)delta;
        int32_t allowed_count = 0, candidate;
        if (!count) return THY_STATE_INVALID;
        state->edit_value %= count;
        if (state->edit_value < 0) state->edit_value += count;
        for (candidate = 0; candidate < count; ++candidate) if (thy_state_option_allowed(state, state->edit_row, candidate)) ++allowed_count;
        if (!allowed_count) return THY_STATE_INVALID;
        remaining %= (uint32_t)allowed_count;
        while (remaining--) {
            do { state->edit_value = (state->edit_value + step + count) % count; }
            while (!thy_state_option_allowed(state, state->edit_row, state->edit_value));
        }
    } else return fail(state, THY_STATE_TEXT_REQUIRED, THY_NOTICE_TEXT);
    state->notice = THY_NOTICE_NONE;
    return THY_STATE_OK;
}

int thy_state_commit(thy_state *state) {
    int result;
    if (!state || !state->editing) return THY_STATE_NO_EDIT;
    result = thy_menu_rows[state->edit_row].type == THY_MENU_TEXT ?
        set_text_at(state, state->edit_row, state->edit_instance, state->edit_text) :
        set_scalar_at(state, state->edit_row, state->edit_instance, state->edit_value);
    if (result == THY_STATE_OK) { state->editing = 0; state->edit_row = THY_STATE_NO_ROW; }
    return result;
}

int thy_state_cancel(thy_state *state) {
    if(state && state->engine)thy_engine_repeat_cancel((thy_engine *)state->engine);
    if (!state || !state->editing) return THY_STATE_NO_EDIT;
    record(state, THY_STATE_EDIT_CANCELLED, state->edit_row, state->edit_instance, state->edit_value,
        thy_menu_rows[state->edit_row].type == THY_MENU_TEXT ? state->edit_text : NULL);
    state->editing = 0;
    state->edit_row = THY_STATE_NO_ROW;
    state->notice = THY_NOTICE_CANCELLED;
    return THY_STATE_OK;
}

static void hierarchy(thy_state *state, uint8_t level, uint8_t group) {
    if (state->level != level || (level == THY_STATE_ROWS && state->group_index != group)) {
        state->transition_direction = level < state->level ? 1 : -1;
        if (++state->transition_revision == 0u) state->transition_revision = 1u;
    }
    state->level = level;
    state->group_index = group;
    state->notice = THY_NOTICE_NONE;
}

int thy_state_focus(thy_state *state, uint8_t group, uint16_t row) {
    if (!state || group >= THY_MENU_GROUP_COUNT || (row != THY_STATE_NO_ROW && (row >= THY_MENU_ROW_COUNT || thy_menu_rows[row].group != group))) return THY_STATE_INVALID;
    if(state->reset_open)return THY_STATE_BLOCKED;
    if(state->engine && state->engine->sequencer_ui.options_open &&
        group!=thy_menu_rows[THY_ROW_SEQUENCER_OPEN].group && state->engine->sequencer_ui.options_open)return THY_STATE_BLOCKED;
    if (row != THY_STATE_NO_ROW && !thy_state_row_visible(state, row))
        return fail(state, THY_STATE_UNAVAILABLE, THY_NOTICE_UNAVAILABLE);
    if (state->editing) (void)thy_state_cancel(state);
    hierarchy(state, THY_STATE_ROWS, group);
    state->about_open = 0u;
    state->info_open = 0u;
    if (row != THY_STATE_NO_ROW) state->cursors[group] = (uint8_t)(row - thy_menu_groups[group].first_row);
    normalize_cursor(state, group);
    return THY_STATE_OK;
}

void thy_state_open(thy_state *state) {
    if (!state) return;
    state->about_open = 0u;
    state->info_open = 0u;
    state->reset_open=state->reset_confirm=0u;state->reset_track=UINT8_MAX;
    if (state->editing) (void)thy_state_cancel(state);
    hierarchy(state, THY_STATE_GROUPS, thy_state_group_visible(state->group_index)?state->group_index:thy_menu_rows[THY_ROW_ENGINES_PLAY].group);
}

void thy_state_play(thy_state *state) {
    if (!state) return;
    state->about_open = 0u;
    state->info_open = 0u;
    state->reset_open=state->reset_confirm=0u;state->reset_track=UINT8_MAX;
    if (state->editing) (void)thy_state_cancel(state);
    hierarchy(state, THY_STATE_PLAY, state->group_index);
}

uint16_t thy_state_current_row(const thy_state *state) {
    if (!state || state->level != THY_STATE_ROWS || state->group_index >= THY_MENU_GROUP_COUNT) return THY_STATE_NO_ROW;
    const thy_menu_group *group = &thy_menu_groups[state->group_index];
    if (state->cursors[state->group_index] >= group->row_count) return THY_STATE_NO_ROW;
    uint16_t row = (uint16_t)(group->first_row + state->cursors[state->group_index]);
    return thy_state_row_visible(state, row) ? row : THY_STATE_NO_ROW;
}

int thy_state_navigate(thy_state *state, uint8_t action) {
    const thy_menu_group *group;
    if (!state || action > THY_STATE_RIGHT) return THY_STATE_INVALID;
    if(state->reset_open){
        if(action==THY_STATE_SELECT){thy_state_play(state);return THY_STATE_OK;}
        if(action==THY_STATE_LEFT)return thy_state_reset_resolve(state,state->reset_epoch,0);
        if(action==THY_STATE_UP||action==THY_STATE_DOWN){state->reset_confirm^=1u;state->notice=THY_NOTICE_NONE;return THY_STATE_OK;}
        return thy_state_reset_resolve(state,state->reset_epoch,state->reset_confirm);
    }
    if (action == THY_STATE_SELECT) {
        if (state->level == THY_STATE_PLAY && !state->about_open) thy_state_open(state);
        else thy_state_play(state);
        return THY_STATE_OK;
    }
    if (state->about_open) {
        if (action == THY_STATE_LEFT)
            state->about_open = 0u;
        return THY_STATE_OK;
    }
    if (state->info_open) {
        if (action == THY_STATE_LEFT) {
            state->info_open = 0u;
            state->transition_direction = 1;
            if (++state->transition_revision == 0u) state->transition_revision = 1u;
        }
        return THY_STATE_OK;
    }
    if (state->level == THY_STATE_PLAY) {
        if (action == THY_STATE_RIGHT) thy_state_open(state);
        return THY_STATE_OK;
    }
    if (state->editing) {
        if (action == THY_STATE_LEFT) return thy_state_cancel(state);
        if (action == THY_STATE_RIGHT) return thy_state_commit(state);
        return thy_state_adjust(state, action == THY_STATE_UP ? 1 : -1);
    }
    group = &thy_menu_groups[state->group_index];
    if (action == THY_STATE_LEFT) {
        if(state->level==THY_STATE_ROWS && !thy_state_group_visible(state->group_index)) {
            uint16_t back=state->group_index==thy_menu_rows[THY_ROW_ENGINESETTINGS_HARMONY].group?THY_ROW_ENGINES_SETTINGS:
                state->group_index==thy_menu_rows[THY_ROW_HARMONY_VIEW].group?THY_ROW_ENGINESETTINGS_HARMONY:THY_ROW_ENGINESETTINGS_SEQUENCER;
            return thy_state_focus(state,thy_menu_rows[back].group,back);
        }
        hierarchy(state, state->level == THY_STATE_ROWS ? THY_STATE_GROUPS : THY_STATE_PLAY, state->group_index);
    }
    else if (action == THY_STATE_UP || action == THY_STATE_DOWN) {
        int delta = action == THY_STATE_UP ? -1 : 1;
        if (state->level == THY_STATE_GROUPS) {
            do {state->group_index=(uint8_t)((state->group_index+delta+THY_MENU_GROUP_COUNT)%THY_MENU_GROUP_COUNT);}
            while(!thy_state_group_visible(state->group_index));
        }
        else {
            unsigned cursor = state->cursors[state->group_index];
            if (cursor >= group->row_count) cursor = 0u;
            for (unsigned scanned = 0u; scanned < group->row_count; ++scanned) {
                cursor = (unsigned)((int)cursor + delta + group->row_count) % group->row_count;
                if (thy_state_row_visible(state, (uint16_t)(group->first_row + cursor))) {
                    state->cursors[state->group_index] = (uint8_t)cursor;
                    break;
                }
            }
        }
        state->notice = THY_NOTICE_NONE;
    } else if (state->level == THY_STATE_GROUPS) {
        hierarchy(state, THY_STATE_ROWS, state->group_index);
        normalize_cursor(state, state->group_index);
    } else {
        uint16_t row = thy_state_current_row(state);
        if (row == THY_STATE_NO_ROW) { state->notice = THY_NOTICE_NONE; return THY_STATE_OK; }
        return thy_state_begin_edit(state, row);
    }
    return THY_STATE_OK;
}

int thy_state_format(const thy_state *state, uint16_t row, int use_edit, char *out, size_t capacity) {
    const thy_menu_row *definition;
    int32_t value;
    int editing;
    if (!state || row >= THY_MENU_ROW_COUNT || !out || !capacity) return THY_STATE_INVALID;
    definition = &thy_menu_rows[row];
    editing = use_edit && state->editing && state->edit_row == row;
    if (definition->type == THY_MENU_ACTION) {
        const char *label = definition->status == THY_MENU_LIVE ? "Run" : "Planned";
        if (definition->group == thy_menu_rows[THY_ROW_ENGINES_PLAY].group) {
            label="Open";
            int engine=row==THY_ROW_ENGINES_PLAY?THY_PLAY_ENGINE_PLAY:row==THY_ROW_ENGINES_HARMONY?THY_PLAY_ENGINE_HARMONY:row==THY_ROW_ENGINES_SEQUENCER?THY_PLAY_ENGINE_SEQUENCER:-1;
            if(engine>=0)label=state->instrument&&state->instrument->engine_kind==engine?"Active":"Select";
        }else if(definition->group==thy_menu_rows[THY_ROW_ENGINESETTINGS_HARMONY].group)label="Open";
        else if(row==THY_ROW_HARMONY_APPLY)label="Apply";
        else if(row==THY_ROW_ARPS_OPEN||row==THY_ROW_SEQUENCER_OPEN||row==THY_ROW_SEQUENCER_HELP)label="Open";
        copy_text(out, capacity, label); return THY_STATE_OK;
    }
    if (definition->type == THY_MENU_TEXT || definition->type == THY_MENU_INFO) {
        if (definition->type == THY_MENU_INFO && definition->group == thy_menu_rows[THY_ROW_ENGINES_PLAY].group) {
            copy_text(out, capacity, "Open"); return THY_STATE_OK;
        }
        copy_text(out, capacity, editing ? state->edit_text : thy_state_get_text(state, row)); return THY_STATE_OK;
    }
    if (editing) value = state->edit_value;
    else if (thy_state_get(state, row, &value) != THY_STATE_OK) { copy_text(out, capacity, "Unavailable"); return THY_STATE_ADAPTER_ERROR; }
    if(row==THY_ROW_SENSITIVITY_TARGET) {
        if(value==0 || value==97)copy_text(out,capacity,value==97?"Right":"Left");
        else {
            unsigned logical=(unsigned)(value-1);
            copy_text(out,capacity,logical<48u?"L":"R");
            size_t at=1u;
            if(at<capacity)integer_text((int32_t)(logical%4u+1u),out+at,capacity-at);
            while(at<capacity && out[at])++at;
            if(at+2u<capacity){out[at++]=' ';out[at++]='C';out[at]=0;}
            if(at<capacity)integer_text((int32_t)((logical%48u)/4u+1u),out+at,capacity-at);
        }
        return THY_STATE_OK;
    }
    if(row==THY_ROW_SENSITIVITY_AMOUNT) {
        integer_text(value,out,capacity);size_t n=0;while(n<capacity && out[n])++n;
        if(n+1u<capacity){out[n++]='%';out[n]=0;}return THY_STATE_OK;
    }
    if (definition->type == THY_MENU_TOGGLE) copy_text(out, capacity, value ? "ON" : "OFF");
    else if (definition->type == THY_MENU_SELECT && definition->dynamic) {
        const char *label = state->callbacks.label ? state->callbacks.label(state->context, row, value) : NULL;
        if (label) copy_text(out, capacity, label);
        else integer_text(value, out, capacity);
    } else if (definition->type == THY_MENU_SELECT && value >= 0 && value < definition->option_count) copy_text(out, capacity, thy_menu_options[definition->option_first + value].label);
    else integer_text(value, out, capacity);
    return THY_STATE_OK;
}

const char *thy_state_notice_text(const thy_state *state) {
    static const char *const notices[] = {"", "Applied to the playable core.",
        "Configuration stored; its musical engine is not active.", "Invalid setting.",
        "This action needs its engine or storage service before it can run.",
        "The setting could not be applied. Release affected pads and retry.",
        "Edit cancelled; the previous value is retained.", "Choose a value within this menu's bounds.",
        "The lowest remote note must not exceed the highest note.",
        "Program Change uses a press trigger. Choose Press first.",
        "Use floor <= release < trigger <= ceiling.",
        "Use valid UTF-8 text within the byte limit; note slots require seven 0/1 switches.",
        "Release all pads before changing this setting.",
        "Hold only. Latch gestures are not implemented yet."};
    if (!state || state->notice >= sizeof(notices) / sizeof(notices[0])) return "";
    return notices[state->notice];
}

uint16_t thy_state_change_count(const thy_state *state) {
    uint16_t row, changes = 0;
    if (!state) return 0;
    for (row = 0; row < THY_MENU_ROW_COUNT; ++row) {
        const thy_menu_row *definition = &thy_menu_rows[row];
        uint8_t instance;
        for (instance = 0; instance < thy_state_instance_count(row); ++instance) {
            if (definition->storage_offset != UINT16_MAX && state->values[definition->storage_offset + instance] != default_scalar(row, instance)) ++changes;
            if (definition->text_offset != UINT16_MAX) {
                char original[THY_STATE_TEXT_BYTES];
                default_text(row, instance, original);
                if (!equal(original, state->texts[definition->text_offset + instance])) ++changes;
            }
        }
    }
    return changes;
}

int thy_state_get_event(const thy_state *state, uint32_t sequence, thy_state_event *event) {
    const thy_state_event *source;
    if (!state || !event || !sequence || sequence > state->revision || state->revision - sequence >= state->history_count) return 0;
    source = &state->events[(sequence - 1u) % THY_STATE_HISTORY];
    if (source->sequence != sequence) return 0;
    *event = *source;
    return 1;
}
