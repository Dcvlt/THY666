/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_library_ui.h"
#include <string.h>

_Static_assert(sizeof(thy_library_ui) <= 80u, "library editor must stay small");

static unsigned append(char *out, unsigned used, size_t capacity, const char *text)
{
    while(*text && used + 1u < capacity) out[used++] = *text++;
    out[used] = 0;
    return used;
}

static unsigned number(char *out, unsigned used, size_t capacity, unsigned value)
{
    char reversed[3]; unsigned count = 0;
    do { reversed[count++] = (char)('0' + value % 10u); value /= 10u; } while(value);
    while(count && used + 1u < capacity) out[used++] = reversed[--count];
    out[used] = 0;
    return used;
}

static unsigned pitch(char *out, unsigned used, size_t capacity, uint8_t value)
{
    static const char *const names[] = {"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
    used = append(out, used, capacity, names[value % 12u]);
    if(value < 12u) return append(out, used, capacity, "-1");
    return number(out, used, capacity, value / 12u - 1u);
}

static uint8_t limit(uint8_t kind)
{
    return kind == THY_LIBRARY_UI_SCALE ? THY_LIBRARY_SCALES :
        kind == THY_LIBRARY_UI_CHORD ? THY_LIBRARY_CHORDS :
        kind == THY_LIBRARY_UI_SIX ? THY_LIBRARY_SIX_SETS : 0u;
}

static const char *kind_name(uint8_t kind)
{
    return kind == THY_LIBRARY_UI_SCALE ? "Scale" : kind == THY_LIBRARY_UI_CHORD ? "Chord" : "Set";
}

static uint8_t *draft_name(thy_library_ui *ui)
{
    return ui->kind == THY_LIBRARY_UI_SCALE ? ui->draft.scale.name :
        ui->kind == THY_LIBRARY_UI_CHORD ? ui->draft.chord.name : ui->draft.six.name;
}

static const uint8_t *record_name(const thy_library *library, uint8_t kind, uint8_t slot)
{
    return kind == THY_LIBRARY_UI_SCALE ? library->scales[slot].name :
        kind == THY_LIBRARY_UI_CHORD ? library->chords[slot].name : library->six[slot].name;
}

static uint8_t record_used(const thy_library *library, uint8_t kind, uint8_t slot)
{
    return kind == THY_LIBRARY_UI_SCALE ? library->scales[slot].used :
        kind == THY_LIBRARY_UI_CHORD ? library->chords[slot].used : library->six[slot].used;
}

static void seed(thy_library_ui *ui, const thy_library *library)
{
    memset(&ui->draft, 0, sizeof ui->draft);
    if(record_used(library, ui->kind, ui->slot)) {
        if(ui->kind == THY_LIBRARY_UI_SCALE) ui->draft.scale = library->scales[ui->slot];
        else if(ui->kind == THY_LIBRARY_UI_CHORD) ui->draft.chord = library->chords[ui->slot];
        else ui->draft.six = library->six[ui->slot];
        return;
    }
    char name[THY_LIBRARY_NAME_BYTES];
    unsigned used = append(name, 0u, sizeof name, kind_name(ui->kind));
    (void)number(name, used, sizeof name, ui->slot + 1u);
    (void)thy_library_name_set(draft_name(ui), name);
    if(ui->kind == THY_LIBRARY_UI_SCALE) {
        ui->draft.scale.used = 1u;
        (void)thy_library_scale_set_mask(&ui->draft.scale, 0x0ab5u);
    } else if(ui->kind == THY_LIBRARY_UI_CHORD) {
        ui->draft.chord.used = 1u; ui->draft.chord.root = 60u;
        ui->draft.chord.notes[0] = 60u; ui->draft.chord.notes[1] = 64u; ui->draft.chord.notes[2] = 67u;
        ui->draft.chord.valid_mask = 7u;
    } else {
        ui->draft.six.used = 1u;
        memset(ui->draft.six.chords, THY_LIBRARY_EMPTY_CHORD, sizeof ui->draft.six.chords);
    }
}

uint8_t thy_library_ui_row_count(const thy_library_ui *ui)
{
    if(!ui || !ui->open || !limit(ui->kind)) return 0u;
    if(ui->page == THY_LIBRARY_UI_BROWSE) return 4u;
    if(ui->page == THY_LIBRARY_UI_NAME) return 3u;
    if(ui->page == THY_LIBRARY_UI_DELETE_CONFIRM) return 2u;
    if(ui->page == THY_LIBRARY_UI_EDIT) return ui->kind == THY_LIBRARY_UI_SCALE ? 14u :
        ui->kind == THY_LIBRARY_UI_CHORD ? 11u : 9u;
    return 0u;
}

uint8_t thy_library_ui_selected(const thy_library_ui *ui)
{
    return ui && ui->page != THY_LIBRARY_UI_NAME ? ui->cursor : 0u;
}

static int safe_draft(const thy_library_ui *ui)
{
    const uint8_t *name = ui->kind == THY_LIBRARY_UI_SCALE ? ui->draft.scale.name :
        ui->kind == THY_LIBRARY_UI_CHORD ? ui->draft.chord.name : ui->draft.six.name;
    if(name[12]) return 0;
    int ended = 0;
    for(unsigned n = 0; n < 12u; ++n) {
        if(!name[n]) ended = 1;
        else if(ended || name[n] < 32u || name[n] > 126u) return 0;
    }
    if(ui->kind == THY_LIBRARY_UI_SCALE)
        return ui->draft.scale.used == 1u && (ui->draft.scale.mask[0] & 1u) && !(ui->draft.scale.mask[1] & 0xf0u);
    if(ui->kind == THY_LIBRARY_UI_CHORD) {
        if(ui->draft.chord.used != 1u || ui->draft.chord.root > 127u || ui->draft.chord.valid_mask > 127u) return 0;
        for(unsigned n = 0; n < 7u; ++n) if(ui->draft.chord.notes[n] > 127u) return 0;
        return 1;
    }
    if(ui->draft.six.used != 1u) return 0;
    for(unsigned n = 0; n < 6u; ++n)
        if(ui->draft.six.chords[n] >= THY_LIBRARY_CHORDS && ui->draft.six.chords[n] != THY_LIBRARY_EMPTY_CHORD) return 0;
    return 1;
}

int thy_library_ui_valid(const thy_library_ui *ui, const thy_library *library)
{
    if(!ui || ui->open != 1u || !limit(ui->kind) || ui->slot >= limit(ui->kind) ||
       ui->page > THY_LIBRARY_UI_DELETE_CONFIRM || ui->editing > THY_LIBRARY_UI_VALUE_EDIT ||
       ui->command > THY_LIBRARY_UI_COPY_CURRENT || ui->notice > THY_LIBRARY_UI_NOTICE_RELEASE ||
       ui->name_cursor > 12u || ui->name_changed > 1u || !thy_library_valid(library)) return 0;
    if(ui->cursor >= thy_library_ui_row_count(ui)) return 0;
    if(ui->page == THY_LIBRARY_UI_NAME && ui->cursor) return 0;
    if(ui->editing == THY_LIBRARY_UI_SLOT_EDIT && (ui->page != THY_LIBRARY_UI_BROWSE || ui->cursor)) return 0;
    if(ui->editing == THY_LIBRARY_UI_VALUE_EDIT &&
       (ui->page != THY_LIBRARY_UI_EDIT || !ui->cursor || ui->cursor + 2u >= thy_library_ui_row_count(ui))) return 0;
    if(ui->editing && ui->command) return 0;
    if(ui->command == THY_LIBRARY_UI_BACK || ui->command == THY_LIBRARY_UI_PLAY) return 0;
    if(ui->command == THY_LIBRARY_UI_SAVE &&
       (ui->page != THY_LIBRARY_UI_EDIT || ui->cursor + 2u != thy_library_ui_row_count(ui))) return 0;
    if(ui->command == THY_LIBRARY_UI_DELETE && (ui->page != THY_LIBRARY_UI_DELETE_CONFIRM || ui->cursor != 1u)) return 0;
    if(ui->command == THY_LIBRARY_UI_COPY_CURRENT && (ui->page != THY_LIBRARY_UI_BROWSE || ui->cursor != 2u)) return 0;
    if(ui->editing == THY_LIBRARY_UI_SLOT_EDIT && ui->field_before >= limit(ui->kind)) return 0;
    if(ui->editing == THY_LIBRARY_UI_VALUE_EDIT) {
        if(ui->kind == THY_LIBRARY_UI_SCALE && ui->field_before > 1u) return 0;
        if(ui->kind == THY_LIBRARY_UI_CHORD &&
           (ui->cursor == 1u ? ui->field_before > 127u : (ui->field_before & ~0x017fu) != 0u)) return 0;
        if(ui->kind == THY_LIBRARY_UI_SIX && ui->field_before >= THY_LIBRARY_CHORDS && ui->field_before != THY_LIBRARY_EMPTY_CHORD) return 0;
    }
    if(ui->page == THY_LIBRARY_UI_NAME) {
        if(ui->name_before[12]) return 0;
        int ended = 0;
        for(unsigned n = 0; n < 12u; ++n) {
            unsigned value = ui->name_before[n];
            if(!value) ended = 1;
            else if(ended || value < 32u || value > 126u) return 0;
        }
    }
    return safe_draft(ui);
}

int thy_library_ui_open(thy_library_ui *ui, const thy_library *library, uint8_t kind, uint8_t slot)
{
    if(!ui || !limit(kind) || slot >= limit(kind) || !thy_library_valid(library)) return THY_LIBRARY_INVALID;
    *ui = (thy_library_ui){0}; ui->open = 1u; ui->kind = kind; ui->slot = slot;
    seed(ui, library); ui->revision = 1u;
    return THY_LIBRARY_OK;
}

static uint16_t field_value(const thy_library_ui *ui)
{
    if(ui->kind == THY_LIBRARY_UI_SCALE) return (uint16_t)!!(thy_library_scale_mask(&ui->draft.scale) & (1u << ui->cursor));
    if(ui->kind == THY_LIBRARY_UI_CHORD) {
        if(ui->cursor == 1u) return ui->draft.chord.root;
        unsigned note = ui->cursor - 2u;
        return (uint16_t)(ui->draft.chord.notes[note] |
            ((ui->draft.chord.valid_mask & (1u << note)) ? 256u : 0u));
    }
    return ui->draft.six.chords[ui->cursor - 1u];
}

static void restore_field(thy_library_ui *ui)
{
    if(ui->editing == THY_LIBRARY_UI_SLOT_EDIT) ui->slot = (uint8_t)ui->field_before;
    else if(ui->kind == THY_LIBRARY_UI_SCALE) {
        unsigned mask = thy_library_scale_mask(&ui->draft.scale), bit = 1u << ui->cursor;
        mask = ui->field_before ? mask | bit : mask & ~bit;
        (void)thy_library_scale_set_mask(&ui->draft.scale, (uint16_t)mask);
    } else if(ui->kind == THY_LIBRARY_UI_CHORD) {
        if(ui->cursor == 1u) ui->draft.chord.root = (uint8_t)ui->field_before;
        else {
            unsigned note = ui->cursor - 2u;
            ui->draft.chord.notes[note] = (uint8_t)ui->field_before;
            if(ui->field_before & 256u) ui->draft.chord.valid_mask |= (uint8_t)(1u << note);
            else ui->draft.chord.valid_mask &= (uint8_t)~(1u << note);
        }
    } else ui->draft.six.chords[ui->cursor - 1u] = (uint8_t)ui->field_before;
}

static void adjust_field(thy_library_ui *ui, const thy_library *library, int direction)
{
    if(ui->editing == THY_LIBRARY_UI_SLOT_EDIT) {
        int next = (int)ui->slot + direction;
        if(next >= 0 && next < limit(ui->kind)) ui->slot = (uint8_t)next;
    } else if(ui->kind == THY_LIBRARY_UI_SCALE) {
        unsigned mask = thy_library_scale_mask(&ui->draft.scale), bit = 1u << ui->cursor;
        mask = direction > 0 ? mask | bit : mask & ~bit;
        (void)thy_library_scale_set_mask(&ui->draft.scale, (uint16_t)mask);
    } else if(ui->kind == THY_LIBRARY_UI_CHORD) {
        if(ui->cursor == 1u) {
            int next = (int)ui->draft.chord.root + direction;
            if(next >= 0 && next <= 127) ui->draft.chord.root = (uint8_t)next;
        } else {
            unsigned note = ui->cursor - 2u;
            int value = ui->draft.chord.valid_mask & (1u << note) ? (int)ui->draft.chord.notes[note] + 1 : 0;
            int next = value + direction;
            if(next >= 0 && next <= 128) {
                if(next) { ui->draft.chord.notes[note] = (uint8_t)(next - 1); ui->draft.chord.valid_mask |= (uint8_t)(1u << note); }
                else ui->draft.chord.valid_mask &= (uint8_t)~(1u << note);
            }
        }
    } else {
        unsigned index = ui->cursor - 1u;
        int current = ui->draft.six.chords[index] == THY_LIBRARY_EMPTY_CHORD ? -1 : ui->draft.six.chords[index];
        if(direction > 0) {
            for(int n = current + 1; n < (int)THY_LIBRARY_CHORDS; ++n)
                if(library->chords[n].used) { ui->draft.six.chords[index] = (uint8_t)n; break; }
        } else {
            ui->draft.six.chords[index] = THY_LIBRARY_EMPTY_CHORD;
            for(int n = current - 1; n >= 0; --n)
                if(library->chords[n].used) { ui->draft.six.chords[index] = (uint8_t)n; break; }
        }
    }
}

static void begin_name(thy_library_ui *ui)
{
    uint8_t *name = draft_name(ui);
    memcpy(ui->name_before, name, sizeof ui->name_before);
    int ended = 0;
    for(unsigned n = 0; n < 12u; ++n) {
        if(!name[n]) ended = 1;
        if(ended) name[n] = ' ';
    }
    ui->page = THY_LIBRARY_UI_NAME; ui->cursor = 0u; ui->name_cursor = 0u; ui->name_changed = 0u;
}

static void finish_name(thy_library_ui *ui, int apply)
{
    uint8_t *name = draft_name(ui);
    if(!apply || !ui->name_changed) memcpy(name, ui->name_before, sizeof ui->name_before);
    else {
        int last = 11;
        while(last >= 0 && name[last] == ' ') name[last--] = 0u;
        name[12] = 0u;
    }
    ui->page = THY_LIBRARY_UI_EDIT; ui->cursor = 0u; ui->name_cursor = 0u;
}

static uint8_t next_character(uint8_t value, int direction)
{
    /* Alphabet first makes empty positions useful immediately; every printable
     * ASCII character remains reachable, including imported lowercase names. */
    static const char characters[] =
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    _Static_assert(sizeof characters == 96u, "all 95 printable ASCII characters");
    unsigned current = 0u;
    for(unsigned n = 0; n < 95u; ++n) if((uint8_t)characters[n] == value) { current = n; break; }
    return (uint8_t)characters[direction > 0 ? (current + 1u) % 95u : (current + 94u) % 95u];
}

uint8_t thy_library_ui_navigate(thy_library_ui *ui, const thy_library *library, uint8_t action)
{
    if(!ui || !ui->open || action > 4u) return THY_LIBRARY_UI_NONE;
    if(action == 2u) {
        ui->open = 0u; ui->editing = 0u; ui->command = THY_LIBRARY_UI_PLAY; ++ui->revision;
        return THY_LIBRARY_UI_PLAY;
    }
    if(!thy_library_ui_valid(ui, library) || ui->command) return THY_LIBRARY_UI_NONE;
    ++ui->revision; ui->notice = THY_LIBRARY_UI_NOTICE_NONE;
    if(ui->page == THY_LIBRARY_UI_NAME) {
        if(action == 0u) {
            if(ui->name_cursor) --ui->name_cursor; else finish_name(ui, 0);
        } else if(action == 4u) {
            if(ui->name_cursor < 12u) ++ui->name_cursor; else finish_name(ui, 1);
        } else if(ui->name_cursor < 12u) {
            uint8_t *name = draft_name(ui);
            name[ui->name_cursor] = next_character(name[ui->name_cursor], action == 1u ? 1 : -1);
            ui->name_changed = 1u;
        }
        return THY_LIBRARY_UI_NONE;
    }
    if(ui->editing) {
        if(action == 0u) { restore_field(ui); ui->editing = 0u; if(ui->page == THY_LIBRARY_UI_BROWSE) seed(ui, library); }
        else if(action == 4u) { ui->editing = 0u; if(ui->page == THY_LIBRARY_UI_BROWSE) seed(ui, library); }
        else adjust_field(ui, library, action == 1u ? 1 : -1);
        return THY_LIBRARY_UI_NONE;
    }
    if(action == 1u) { if(ui->cursor) --ui->cursor; return THY_LIBRARY_UI_NONE; }
    if(action == 3u) { if(ui->cursor + 1u < thy_library_ui_row_count(ui)) ++ui->cursor; return THY_LIBRARY_UI_NONE; }
    if(action == 0u) {
        if(ui->page == THY_LIBRARY_UI_BROWSE) { ui->open = 0u; ui->command = THY_LIBRARY_UI_BACK; return ui->command; }
        ui->cursor = ui->page == THY_LIBRARY_UI_DELETE_CONFIRM ? 3u : 1u;
        ui->page = THY_LIBRARY_UI_BROWSE; seed(ui, library);
        return THY_LIBRARY_UI_NONE;
    }
    if(ui->page == THY_LIBRARY_UI_BROWSE) {
        if(ui->cursor == 0u) { ui->editing = THY_LIBRARY_UI_SLOT_EDIT; ui->field_before = ui->slot; }
        else if(ui->cursor == 1u) { seed(ui, library); ui->page = THY_LIBRARY_UI_EDIT; ui->cursor = 0u; ui->notice = THY_LIBRARY_UI_NOTICE_DRAFT; }
        else if(ui->cursor == 2u) ui->command = THY_LIBRARY_UI_COPY_CURRENT;
        else { ui->page = THY_LIBRARY_UI_DELETE_CONFIRM; ui->cursor = 0u; }
    } else if(ui->page == THY_LIBRARY_UI_DELETE_CONFIRM) {
        if(ui->cursor) ui->command = THY_LIBRARY_UI_DELETE;
        else { ui->page = THY_LIBRARY_UI_BROWSE; ui->cursor = 3u; }
    } else if(ui->page == THY_LIBRARY_UI_EDIT) {
        unsigned rows = thy_library_ui_row_count(ui);
        if(!ui->cursor) begin_name(ui);
        else if(ui->cursor + 2u == rows) ui->command = THY_LIBRARY_UI_SAVE;
        else if(ui->cursor + 1u == rows) { seed(ui, library); ui->page = THY_LIBRARY_UI_BROWSE; ui->cursor = 1u; }
        else { ui->editing = THY_LIBRARY_UI_VALUE_EDIT; ui->field_before = field_value(ui); }
    }
    return ui->command;
}

int thy_library_ui_ack(thy_library_ui *ui, const thy_library *library, int result)
{
    if(!thy_library_ui_valid(ui, library) ||
       (ui->command != THY_LIBRARY_UI_SAVE && ui->command != THY_LIBRARY_UI_DELETE &&
        ui->command != THY_LIBRARY_UI_COPY_CURRENT) ||
       (ui->command == THY_LIBRARY_UI_COPY_CURRENT && result == THY_LIBRARY_OK)) return THY_LIBRARY_INVALID;
    if(result == THY_LIBRARY_OK) {
        ui->notice = ui->command == THY_LIBRARY_UI_SAVE ? THY_LIBRARY_UI_NOTICE_SAVED : THY_LIBRARY_UI_NOTICE_DELETED;
        ui->page = THY_LIBRARY_UI_BROWSE; ui->cursor = 1u; seed(ui, library);
    } else ui->notice = result == THY_LIBRARY_IN_USE ? THY_LIBRARY_UI_NOTICE_IN_USE :
        result == THY_LIBRARY_UI_BUSY ? THY_LIBRARY_UI_NOTICE_RELEASE : THY_LIBRARY_UI_NOTICE_INVALID;
    ui->command = THY_LIBRARY_UI_NONE; ++ui->revision;
    return THY_LIBRARY_OK;
}

int thy_library_ui_begin_copy(thy_library_ui *ui, const thy_library *library,
    const thy_library_ui_draft *captured)
{
    if(!thy_library_ui_valid(ui, library) || ui->command != THY_LIBRARY_UI_COPY_CURRENT) return THY_LIBRARY_INVALID;
    thy_library_ui_draft proposed = {0};
    if(captured) {
        if(ui->kind == THY_LIBRARY_UI_SCALE) {
            proposed.scale = captured->scale;
            if(!proposed.scale.used || !thy_library_scale_valid(&proposed.scale)) return THY_LIBRARY_INVALID;
        } else if(ui->kind == THY_LIBRARY_UI_CHORD) {
            proposed.chord = captured->chord;
            if(!proposed.chord.used || !thy_library_chord_valid(&proposed.chord)) return THY_LIBRARY_INVALID;
        } else {
            proposed.six = captured->six;
            if(!proposed.six.used || !thy_library_six_valid(&proposed.six)) return THY_LIBRARY_INVALID;
            for(unsigned n = 0; n < 6u; ++n) if(proposed.six.chords[n] != THY_LIBRARY_EMPTY_CHORD &&
                !library->chords[proposed.six.chords[n]].used) return THY_LIBRARY_INVALID;
        }
    }
    if(captured) ui->draft = proposed; else seed(ui, library);
    ui->page = THY_LIBRARY_UI_EDIT; ui->cursor = 0u; ui->command = THY_LIBRARY_UI_NONE;
    ui->notice = THY_LIBRARY_UI_NOTICE_DRAFT; ++ui->revision;
    return THY_LIBRARY_OK;
}

int thy_library_ui_title(const thy_library_ui *ui, char *output, size_t capacity)
{
    if(!ui || !ui->open || !limit(ui->kind) || ui->slot >= limit(ui->kind) ||
       ui->page > THY_LIBRARY_UI_DELETE_CONFIRM || !output || !capacity) return THY_LIBRARY_INVALID;
    unsigned used = 0u;
    if(ui->page == THY_LIBRARY_UI_NAME) (void)append(output, used, capacity, "Edit name");
    else {
        if(ui->page == THY_LIBRARY_UI_DELETE_CONFIRM) used = append(output, used, capacity, "Delete ");
        used = append(output, used, capacity, kind_name(ui->kind));
        if(ui->page == THY_LIBRARY_UI_BROWSE) (void)append(output, used, capacity, " library");
        else { used = append(output, used, capacity, " "); (void)number(output, used, capacity, ui->slot + 1u); }
    }
    return THY_LIBRARY_OK;
}

const char *thy_library_ui_name(const thy_library_ui *ui)
{
    if(!ui || !ui->open || !limit(ui->kind)) return "";
    const uint8_t *name = ui->kind == THY_LIBRARY_UI_SCALE ? ui->draft.scale.name :
        ui->kind == THY_LIBRARY_UI_CHORD ? ui->draft.chord.name : ui->draft.six.name;
    return name[12] ? "" : (const char *)name;
}

int thy_library_ui_format(const thy_library_ui *ui, const thy_library *library,
    uint8_t row, char *output, size_t capacity)
{
    if(!output || !capacity || !thy_library_ui_valid(ui, library) || row >= thy_library_ui_row_count(ui))
        return THY_LIBRARY_INVALID;
    unsigned used = 0u;
    if(ui->page == THY_LIBRARY_UI_NAME) {
        if(!row) (void)append(output, used, capacity, thy_library_ui_name(ui));
        else if(row == 2u) (void)append(output, used, capacity, "UP/DN CHAR  < > MOVE");
        else if(ui->name_cursor == 12u) (void)append(output, used, capacity, "Done >");
        else { used = append(output, used, capacity, "Character "); (void)number(output, used, capacity, ui->name_cursor + 1u); }
    } else if(ui->page == THY_LIBRARY_UI_BROWSE) {
        if(!row) {
            used = append(output, used, capacity, "Slot "); used = number(output, used, capacity, ui->slot + 1u);
            used = append(output, used, capacity, "/"); used = number(output, used, capacity, limit(ui->kind));
            used = append(output, used, capacity, " ");
            (void)append(output, used, capacity, record_used(library, ui->kind, ui->slot) ?
                (const char *)record_name(library, ui->kind, ui->slot) : "Empty");
        } else (void)append(output, used, capacity, row == 1u ? "Edit" : row == 2u ? "Copy current" : "Delete");
    } else if(ui->page == THY_LIBRARY_UI_DELETE_CONFIRM) {
        (void)append(output, used, capacity, row ? "Delete slot" : "Cancel");
    } else {
        unsigned rows = thy_library_ui_row_count(ui);
        if(!row) { used = append(output, used, capacity, "Name "); (void)append(output, used, capacity, thy_library_ui_name(ui)); }
        else if(row + 2u == rows) (void)append(output, used, capacity, "Save");
        else if(row + 1u == rows) (void)append(output, used, capacity, "Cancel");
        else if(ui->kind == THY_LIBRARY_UI_SCALE) {
            static const char *const names[] = {"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
            used = append(output, used, capacity, names[row]); used = append(output, used, capacity, " ");
            (void)append(output, used, capacity, thy_library_scale_mask(&ui->draft.scale) & (1u << row) ? "On" : "Off");
        } else if(ui->kind == THY_LIBRARY_UI_CHORD) {
            if(row == 1u) { used = append(output, used, capacity, "Root "); (void)pitch(output, used, capacity, ui->draft.chord.root); }
            else {
                unsigned note = row - 2u;
                used = append(output, used, capacity, "Note "); used = number(output, used, capacity, note + 1u);
                used = append(output, used, capacity, " ");
                if(ui->draft.chord.valid_mask & (1u << note)) (void)pitch(output, used, capacity, ui->draft.chord.notes[note]);
                else (void)append(output, used, capacity, "Off");
            }
        } else {
            unsigned chord = ui->draft.six.chords[row - 1u];
            used = number(output, used, capacity, row); used = append(output, used, capacity, ": ");
            if(chord == THY_LIBRARY_EMPTY_CHORD) (void)append(output, used, capacity, "Empty");
            else if(!library->chords[chord].used) (void)append(output, used, capacity, "Missing");
            else { used = number(output, used, capacity, chord + 1u); used = append(output, used, capacity, " ");
                (void)append(output, used, capacity, (const char *)library->chords[chord].name); }
        }
    }
    return THY_LIBRARY_OK;
}

const char *thy_library_ui_status(const thy_library_ui *ui)
{
    if(!ui || !ui->open) return "";
    if(ui->command) return "Working";
    switch(ui->notice) {
    case THY_LIBRARY_UI_NOTICE_DRAFT: return "Draft";
    case THY_LIBRARY_UI_NOTICE_SAVED: return "Saved";
    case THY_LIBRARY_UI_NOTICE_DELETED: return "Deleted";
    case THY_LIBRARY_UI_NOTICE_INVALID: return "Invalid";
    case THY_LIBRARY_UI_NOTICE_IN_USE: return "In use";
    case THY_LIBRARY_UI_NOTICE_RELEASE: return "Release pads / stop";
    default: return "";
    }
}

const char *thy_library_ui_legend(const thy_library_ui *ui)
{
    if(!ui || !ui->open) return "";
    if(ui->page == THY_LIBRARY_UI_NAME) return "< BACK  > NEXT/DONE";
    if(ui->editing) return "> APPLY  < CANCEL";
    return "> SELECT  < BACK  O PLAY";
}
