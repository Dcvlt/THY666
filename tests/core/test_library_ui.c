/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "thy_library_ui.h"

static thy_library library, before;
static thy_library_ui ui;
static unsigned assertions;
#define CHECK(x) do { ++assertions; if(!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 0; } } while(0)

static int nav(unsigned action, unsigned expected)
{
    before = library;
    CHECK(thy_library_ui_navigate(&ui, &library, (uint8_t)action) == expected);
    CHECK(!memcmp(&library, &before, sizeof library));
    if(ui.open) CHECK(thy_library_ui_valid(&ui, &library));
    return 1;
}

static int row(unsigned selected)
{
    CHECK(!ui.editing && ui.page != THY_LIBRARY_UI_NAME);
    CHECK(selected < thy_library_ui_row_count(&ui));
    while(ui.cursor < selected) CHECK(nav(3u, THY_LIBRARY_UI_NONE));
    while(ui.cursor > selected) CHECK(nav(1u, THY_LIBRARY_UI_NONE));
    return 1;
}

static int edit(void)
{
    CHECK(row(1u)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(ui.page == THY_LIBRARY_UI_EDIT && ui.cursor == 0u);
    return 1;
}

static int fixture(void)
{
    thy_library_defaults(&library);
    thy_library_chord chord = {0}; chord.used = 1u; chord.root = 60u; chord.valid_mask = 7u;
    const uint8_t notes[] = {60u, 64u, 67u, 117u, 81u, 121u, 127u};
    memcpy(chord.notes, notes, sizeof notes);
    CHECK(thy_library_name_set(chord.name, "C major") == THY_LIBRARY_OK);
    CHECK(thy_library_set_chord(&library, 0u, &chord) == THY_LIBRARY_OK);
    chord.root = 0u; chord.notes[0] = 0u;
    CHECK(thy_library_name_set(chord.name, "MIDI zero") == THY_LIBRARY_OK);
    CHECK(thy_library_set_chord(&library, 23u, &chord) == THY_LIBRARY_OK);
    thy_library_six set = {0}; set.used = 1u;
    CHECK(thy_library_name_set(set.name, "Reference") == THY_LIBRARY_OK);
    memset(set.chords, THY_LIBRARY_EMPTY_CHORD, sizeof set.chords); set.chords[0] = 0u;
    CHECK(thy_library_set_six(&library, 0u, &set) == THY_LIBRARY_OK);
    return 1;
}

static int construction_and_slot_bounds(void)
{
    CHECK(sizeof(thy_library_ui) <= 80u);
    thy_library_defaults(&library);
    const unsigned limits[] = {8u, 24u, 3u};
    for(unsigned kind = 0; kind < 3u; ++kind) {
        CHECK(thy_library_ui_open(&ui, &library, (uint8_t)kind, 1u) == THY_LIBRARY_OK);
        CHECK(thy_library_ui_valid(&ui, &library));
        CHECK(thy_library_ui_row_count(&ui) == 4u && !ui.cursor);
        CHECK(nav(4u, THY_LIBRARY_UI_NONE) && ui.editing == THY_LIBRARY_UI_SLOT_EDIT);
        for(unsigned n = 0; n < 100u; ++n) CHECK(nav(1u, THY_LIBRARY_UI_NONE));
        CHECK(ui.slot == limits[kind] - 1u);
        CHECK(nav(0u, THY_LIBRARY_UI_NONE) && ui.slot == 1u && !ui.editing);
        CHECK(nav(4u, THY_LIBRARY_UI_NONE));
        for(unsigned n = 0; n < 100u; ++n) CHECK(nav(3u, THY_LIBRARY_UI_NONE));
        CHECK(ui.slot == 0u);
        CHECK(nav(4u, THY_LIBRARY_UI_NONE) && !ui.editing);
        CHECK(nav(0u, THY_LIBRARY_UI_BACK) && !ui.open);
    }
    CHECK(thy_library_ui_open(&ui, &library, 0u, 0u) == THY_LIBRARY_OK);
    thy_library_ui old = ui;
    CHECK(thy_library_ui_open(&ui, &library, 3u, 0u) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&ui, &old, sizeof ui));
    CHECK(thy_library_ui_open(&ui, &library, 0u, 8u) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&ui, &old, sizeof ui));
    CHECK(thy_library_ui_open(&ui, NULL, 0u, 0u) == THY_LIBRARY_INVALID);
    CHECK(thy_library_ui_open(NULL, &library, 0u, 0u) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&ui, &old, sizeof ui));
    CHECK(nav(5u, THY_LIBRARY_UI_NONE)); CHECK(!memcmp(&ui, &old, sizeof ui));
    return 1;
}

static int scale_draft_apply_cancel(void)
{
    thy_library_defaults(&library);
    CHECK(thy_library_ui_open(&ui, &library, THY_LIBRARY_UI_SCALE, 7u) == THY_LIBRARY_OK);
    CHECK(ui.draft.scale.used && thy_library_scale_mask(&ui.draft.scale) == 0x0ab5u);
    CHECK(!strcmp(thy_library_ui_name(&ui), "Scale8"));
    CHECK(edit()); CHECK(thy_library_ui_row_count(&ui) == 14u);
    CHECK(row(1u)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(nav(1u, THY_LIBRARY_UI_NONE)); CHECK(thy_library_scale_mask(&ui.draft.scale) & 2u);
    CHECK(nav(0u, THY_LIBRARY_UI_NONE)); CHECK(!(thy_library_scale_mask(&ui.draft.scale) & 2u));
    CHECK(nav(4u, THY_LIBRARY_UI_NONE)); CHECK(nav(1u, THY_LIBRARY_UI_NONE)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(thy_library_scale_mask(&ui.draft.scale) & 2u);
    CHECK(row(12u)); CHECK(nav(4u, THY_LIBRARY_UI_SAVE));
    thy_library_ui pending = ui;
    CHECK(nav(4u, THY_LIBRARY_UI_NONE) && !memcmp(&pending, &ui, sizeof ui));
    CHECK(nav(1u, THY_LIBRARY_UI_NONE) && !memcmp(&pending, &ui, sizeof ui));
    CHECK(!library.scales[7].used);
    CHECK(thy_library_ui_ack(&ui, &library, THY_LIBRARY_UI_BUSY) == THY_LIBRARY_OK);
    CHECK(ui.page == THY_LIBRARY_UI_EDIT && ui.cursor == 12u && !ui.command);
    CHECK(!strcmp(thy_library_ui_status(&ui), "Release pads / stop"));
    CHECK(!memcmp(&pending.draft, &ui.draft, sizeof ui.draft));
    CHECK(nav(4u, THY_LIBRARY_UI_SAVE));
    CHECK(thy_library_set_scale(&library, 7u, &ui.draft.scale) == THY_LIBRARY_OK);
    before = library;
    CHECK(thy_library_ui_ack(&ui, &library, THY_LIBRARY_OK) == THY_LIBRARY_OK);
    CHECK(!memcmp(&library, &before, sizeof library));
    CHECK(ui.page == THY_LIBRARY_UI_BROWSE && ui.cursor == 1u);
    CHECK(!strcmp(thy_library_ui_status(&ui), "Saved"));
    CHECK(edit()); CHECK(row(1u)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(nav(3u, THY_LIBRARY_UI_NONE)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(!(thy_library_scale_mask(&ui.draft.scale) & 2u));
    CHECK(nav(0u, THY_LIBRARY_UI_NONE)); CHECK(ui.page == THY_LIBRARY_UI_BROWSE);
    CHECK(thy_library_scale_mask(&ui.draft.scale) & 2u); /* Whole draft cancelled. */
    CHECK(edit()); CHECK(row(13u)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(ui.page == THY_LIBRARY_UI_BROWSE);
    return 1;
}

static int chord_notes_zero_and_inactive_preservation(void)
{
    CHECK(fixture());
    CHECK(thy_library_ui_open(&ui, &library, THY_LIBRARY_UI_CHORD, 0u) == THY_LIBRARY_OK);
    CHECK(edit()); CHECK(thy_library_ui_row_count(&ui) == 11u);
    CHECK(row(1u)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    for(unsigned n = 0; n < 80u; ++n) CHECK(nav(3u, THY_LIBRARY_UI_NONE));
    CHECK(!ui.draft.chord.root); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    char line[32];
    CHECK(thy_library_ui_format(&ui, &library, 1u, line, sizeof line) == THY_LIBRARY_OK);
    CHECK(!strcmp(line, "Root C-1"));
    CHECK(row(2u)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    for(unsigned n = 0; n < 60u; ++n) CHECK(nav(3u, THY_LIBRARY_UI_NONE));
    CHECK((ui.draft.chord.valid_mask & 1u) && !ui.draft.chord.notes[0]);
    CHECK(thy_library_ui_format(&ui, &library, 2u, line, sizeof line) == THY_LIBRARY_OK);
    CHECK(!strcmp(line, "Note 1 C-1"));
    CHECK(nav(3u, THY_LIBRARY_UI_NONE));
    CHECK(!(ui.draft.chord.valid_mask & 1u));
    CHECK(thy_library_ui_format(&ui, &library, 2u, line, sizeof line) == THY_LIBRARY_OK);
    CHECK(!strcmp(line, "Note 1 Off"));
    CHECK(nav(1u, THY_LIBRARY_UI_NONE)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK((ui.draft.chord.valid_mask & 1u) && !ui.draft.chord.notes[0]);
    CHECK(row(5u)); CHECK(ui.draft.chord.notes[3] == 117u && !(ui.draft.chord.valid_mask & 8u));
    CHECK(nav(4u, THY_LIBRARY_UI_NONE)); CHECK(nav(1u, THY_LIBRARY_UI_NONE));
    CHECK(!ui.draft.chord.notes[3] && (ui.draft.chord.valid_mask & 8u));
    CHECK(nav(0u, THY_LIBRARY_UI_NONE));
    CHECK(ui.draft.chord.notes[3] == 117u && !(ui.draft.chord.valid_mask & 8u));
    CHECK(nav(4u, THY_LIBRARY_UI_NONE)); CHECK(nav(1u, THY_LIBRARY_UI_NONE)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(!ui.draft.chord.notes[3] && (ui.draft.chord.valid_mask & 8u));
    CHECK(ui.draft.chord.notes[5] == 121u && ui.draft.chord.notes[6] == 127u);
    CHECK(row(9u)); CHECK(nav(4u, THY_LIBRARY_UI_SAVE));
    CHECK(thy_library_set_chord(&library, 0u, &ui.draft.chord) == THY_LIBRARY_OK);
    CHECK(thy_library_ui_ack(&ui, &library, THY_LIBRARY_OK) == THY_LIBRARY_OK);
    CHECK(library.chords[0].notes[0] == 0u && (library.chords[0].valid_mask & 1u));
    CHECK(edit()); ui.draft.chord.valid_mask = 0u;
    CHECK(row(9u)); CHECK(nav(4u, THY_LIBRARY_UI_SAVE));
    CHECK(thy_library_set_chord(&library, 0u, &ui.draft.chord) == THY_LIBRARY_INVALID);
    CHECK(thy_library_ui_ack(&ui, &library, THY_LIBRARY_INVALID) == THY_LIBRARY_OK);
    CHECK(ui.page == THY_LIBRARY_UI_EDIT && ui.cursor == 9u && ui.draft.chord.valid_mask == 0u);
    CHECK(!strcmp(thy_library_ui_status(&ui), "Invalid"));
    return 1;
}

static int six_set_references_and_copy_capture(void)
{
    CHECK(fixture());
    CHECK(thy_library_ui_open(&ui, &library, THY_LIBRARY_UI_SIX, 2u) == THY_LIBRARY_OK);
    CHECK(edit()); CHECK(thy_library_ui_row_count(&ui) == 9u);
    CHECK(row(1u)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(nav(1u, THY_LIBRARY_UI_NONE) && ui.draft.six.chords[0] == 0u);
    CHECK(nav(1u, THY_LIBRARY_UI_NONE) && ui.draft.six.chords[0] == 23u);
    CHECK(nav(1u, THY_LIBRARY_UI_NONE) && ui.draft.six.chords[0] == 23u);
    CHECK(nav(3u, THY_LIBRARY_UI_NONE) && ui.draft.six.chords[0] == 0u);
    CHECK(nav(3u, THY_LIBRARY_UI_NONE) && ui.draft.six.chords[0] == THY_LIBRARY_EMPTY_CHORD);
    CHECK(nav(1u, THY_LIBRARY_UI_NONE)); CHECK(nav(1u, THY_LIBRARY_UI_NONE)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(row(7u)); CHECK(nav(4u, THY_LIBRARY_UI_SAVE));
    CHECK(thy_library_set_six(&library, 2u, &ui.draft.six) == THY_LIBRARY_OK);
    CHECK(thy_library_ui_ack(&ui, &library, THY_LIBRARY_OK) == THY_LIBRARY_OK);
    CHECK(library.six[2].chords[0] == 23u);
    CHECK(thy_library_ui_open(&ui, &library, THY_LIBRARY_UI_CHORD, 1u) == THY_LIBRARY_OK);
    CHECK(row(2u)); CHECK(nav(4u, THY_LIBRARY_UI_COPY_CURRENT));
    thy_library_ui saved = ui;
    CHECK(thy_library_ui_ack(&ui, &library, THY_LIBRARY_OK) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&saved, &ui, sizeof ui));
    thy_library_ui_draft captured = {0}; captured.chord = library.chords[23]; captured.chord.valid_mask = 0u;
    CHECK(thy_library_ui_begin_copy(&ui, &library, &captured) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&saved, &ui, sizeof ui));
    captured.chord = library.chords[23];
    CHECK(thy_library_ui_begin_copy(&ui, &library, &captured) == THY_LIBRARY_OK);
    CHECK(ui.page == THY_LIBRARY_UI_EDIT && !ui.cursor && !ui.command);
    CHECK(!memcmp(&captured.chord, &ui.draft.chord, sizeof captured.chord));
    CHECK(!library.chords[1].used);
    CHECK(thy_library_ui_open(&ui, &library, THY_LIBRARY_UI_SCALE, 3u) == THY_LIBRARY_OK);
    CHECK(row(2u)); CHECK(nav(4u, THY_LIBRARY_UI_COPY_CURRENT));
    CHECK(thy_library_ui_begin_copy(&ui, &library, NULL) == THY_LIBRARY_OK);
    CHECK(ui.page == THY_LIBRARY_UI_EDIT && !strcmp(thy_library_ui_name(&ui), "Scale4"));
    return 1;
}

static int name_entry_done_cancel_and_character_reachability(void)
{
    thy_library_defaults(&library);
    CHECK(thy_library_ui_open(&ui, &library, THY_LIBRARY_UI_SCALE, 0u) == THY_LIBRARY_OK);
    CHECK(edit()); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(ui.page == THY_LIBRARY_UI_NAME && thy_library_ui_row_count(&ui) == 3u);
    CHECK(nav(1u, THY_LIBRARY_UI_NONE)); CHECK(ui.draft.scale.name[0] == 'T');
    CHECK(nav(0u, THY_LIBRARY_UI_NONE));
    CHECK(ui.page == THY_LIBRARY_UI_EDIT && !strcmp(thy_library_ui_name(&ui), "Scale1"));
    CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    for(unsigned n = 0; n < 6u; ++n) CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(ui.name_cursor == 6u && ui.draft.scale.name[6] == ' ');
    CHECK(nav(1u, THY_LIBRARY_UI_NONE) && ui.draft.scale.name[6] == 'A');
    for(unsigned n = 0; n < 6u; ++n) CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(ui.name_cursor == 12u);
    char line[32]; CHECK(thy_library_ui_format(&ui, &library, 1u, line, sizeof line) == THY_LIBRARY_OK);
    CHECK(!strcmp(line, "Done >"));
    CHECK(nav(1u, THY_LIBRARY_UI_NONE) && ui.name_cursor == 12u);
    CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(ui.page == THY_LIBRARY_UI_EDIT && !strcmp(thy_library_ui_name(&ui), "Scale1A"));
    CHECK(thy_library_scale_valid(&ui.draft.scale));
    CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    uint8_t original = ui.draft.scale.name[0], seen[128] = {0};
    for(unsigned n = 0; n < 95u; ++n) {
        CHECK(nav(1u, THY_LIBRARY_UI_NONE)); unsigned value = ui.draft.scale.name[0];
        CHECK(value >= 32u && value <= 126u && !seen[value]); seen[value] = 1u;
    }
    CHECK(ui.draft.scale.name[0] == original);
    for(unsigned n = 0; n < 95u; ++n) CHECK(nav(3u, THY_LIBRARY_UI_NONE));
    CHECK(ui.draft.scale.name[0] == original);
    CHECK(nav(2u, THY_LIBRARY_UI_PLAY) && !ui.open && !library.scales[0].used);
    return 1;
}

static int delete_confirmation_and_acknowledgement(void)
{
    CHECK(fixture());
    CHECK(thy_library_ui_open(&ui, &library, THY_LIBRARY_UI_CHORD, 0u) == THY_LIBRARY_OK);
    CHECK(row(3u)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    CHECK(ui.page == THY_LIBRARY_UI_DELETE_CONFIRM && !ui.cursor);
    CHECK(nav(4u, THY_LIBRARY_UI_NONE)); CHECK(ui.page == THY_LIBRARY_UI_BROWSE && ui.cursor == 3u);
    CHECK(nav(4u, THY_LIBRARY_UI_NONE)); CHECK(nav(3u, THY_LIBRARY_UI_NONE)); CHECK(nav(4u, THY_LIBRARY_UI_DELETE));
    CHECK(thy_library_delete_chord(&library, 0u) == THY_LIBRARY_IN_USE);
    CHECK(thy_library_ui_ack(&ui, &library, THY_LIBRARY_IN_USE) == THY_LIBRARY_OK);
    CHECK(ui.page == THY_LIBRARY_UI_DELETE_CONFIRM && ui.cursor == 1u);
    CHECK(!strcmp(thy_library_ui_status(&ui), "In use"));
    CHECK(nav(0u, THY_LIBRARY_UI_NONE));
    CHECK(thy_library_delete_six(&library, 0u) == THY_LIBRARY_OK);
    CHECK(nav(4u, THY_LIBRARY_UI_NONE)); CHECK(nav(3u, THY_LIBRARY_UI_NONE)); CHECK(nav(4u, THY_LIBRARY_UI_DELETE));
    CHECK(thy_library_delete_chord(&library, 0u) == THY_LIBRARY_OK);
    CHECK(thy_library_ui_ack(&ui, &library, THY_LIBRARY_OK) == THY_LIBRARY_OK);
    CHECK(ui.page == THY_LIBRARY_UI_BROWSE && !library.chords[0].used);
    CHECK(!strcmp(thy_library_ui_status(&ui), "Deleted"));
    CHECK(ui.draft.chord.used && ui.draft.chord.valid_mask == 7u);
    CHECK(thy_library_ui_ack(&ui, &library, THY_LIBRARY_OK) == THY_LIBRARY_INVALID);
    return 1;
}

static int projections_and_invalid_state_escape(void)
{
    CHECK(fixture());
    char text[32];
    for(unsigned kind = 0; kind < 3u; ++kind) {
        CHECK(thy_library_ui_open(&ui, &library, (uint8_t)kind, 0u) == THY_LIBRARY_OK);
        CHECK(thy_library_ui_title(&ui, text, sizeof text) == THY_LIBRARY_OK && text[0]);
        for(unsigned page = 0; page < 4u; ++page) {
            if(page == 1u) CHECK(edit());
            if(page == 2u) CHECK(nav(4u, THY_LIBRARY_UI_NONE));
            if(page == 3u) {
                CHECK(nav(0u, THY_LIBRARY_UI_NONE)); CHECK(nav(0u, THY_LIBRARY_UI_NONE));
                CHECK(row(3u)); CHECK(nav(4u, THY_LIBRARY_UI_NONE));
            }
            unsigned count = thy_library_ui_row_count(&ui);
            before = library;
            for(unsigned n = 0; n < count; ++n) {
                char tiny[2] = {'X','Y'};
                CHECK(thy_library_ui_format(&ui, &library, (uint8_t)n, tiny, 1u) == THY_LIBRARY_OK);
                CHECK(!tiny[0] && tiny[1] == 'Y');
                CHECK(thy_library_ui_format(&ui, &library, (uint8_t)n, text, sizeof text) == THY_LIBRARY_OK);
                CHECK(memchr(text, 0, sizeof text) && text[0]);
            }
            CHECK(!memcmp(&library, &before, sizeof library));
            memset(text, 'X', sizeof text);
            CHECK(thy_library_ui_format(&ui, &library, (uint8_t)count, text, sizeof text) == THY_LIBRARY_INVALID);
            CHECK(text[0] == 'X');
        }
    }
    ui.kind = 3u;
    CHECK(!thy_library_ui_valid(&ui, &library));
    thy_library_ui invalid = ui;
    CHECK(thy_library_ui_navigate(&ui, &library, 4u) == THY_LIBRARY_UI_NONE);
    CHECK(!memcmp(&ui, &invalid, sizeof ui));
    CHECK(thy_library_ui_navigate(&ui, &library, 2u) == THY_LIBRARY_UI_PLAY && !ui.open);
    CHECK(thy_library_ui_open(&ui, &library, 0u, 0u) == THY_LIBRARY_OK);
    CHECK(nav(4u, THY_LIBRARY_UI_NONE));
    ui.field_before = 255u;
    invalid = ui;
    CHECK(!thy_library_ui_valid(&ui, &library));
    CHECK(thy_library_ui_navigate(&ui, &library, 0u) == THY_LIBRARY_UI_NONE);
    CHECK(!memcmp(&ui, &invalid, sizeof ui));
    CHECK(thy_library_ui_navigate(&ui, &library, 2u) == THY_LIBRARY_UI_PLAY);
    CHECK(thy_library_ui_open(&ui, &library, 0u, 0u) == THY_LIBRARY_OK);
    library.scales[0].mask[0] = 1u; /* Corrupt an unused record. */
    before = library;
    CHECK(!thy_library_ui_valid(&ui, &library));
    CHECK(thy_library_ui_navigate(&ui, &library, 2u) == THY_LIBRARY_UI_PLAY);
    CHECK(!memcmp(&library, &before, sizeof library));
    return 1;
}

static int bounded_navigation_walks(void)
{
    CHECK(fixture()); uint32_t random = UINT32_C(0x666333);
    const unsigned limits[] = {8u, 24u, 3u};
    for(unsigned kind = 0; kind < 3u; ++kind) {
        CHECK(thy_library_ui_open(&ui, &library, (uint8_t)kind, 0u) == THY_LIBRARY_OK);
        for(unsigned n = 0; n < 10000u; ++n) {
            random = random * UINT32_C(1664525) + UINT32_C(1013904223);
            unsigned action = random % 5u;
            before = library;
            unsigned command = thy_library_ui_navigate(&ui, &library, (uint8_t)action);
            CHECK(!memcmp(&library, &before, sizeof library));
            if(command == THY_LIBRARY_UI_BACK || command == THY_LIBRARY_UI_PLAY) {
                CHECK(!ui.open);
                CHECK(thy_library_ui_open(&ui, &library, (uint8_t)kind, (uint8_t)(random % limits[kind])) == THY_LIBRARY_OK);
            } else if(command == THY_LIBRARY_UI_COPY_CURRENT) {
                CHECK(thy_library_ui_begin_copy(&ui, &library, NULL) == THY_LIBRARY_OK);
            } else if(command == THY_LIBRARY_UI_SAVE || command == THY_LIBRARY_UI_DELETE) {
                int result;
                if(command == THY_LIBRARY_UI_DELETE) result = kind == 0u ? thy_library_delete_scale(&library, ui.slot) :
                    kind == 1u ? thy_library_delete_chord(&library, ui.slot) : thy_library_delete_six(&library, ui.slot);
                else result = kind == 0u ? thy_library_set_scale(&library, ui.slot, &ui.draft.scale) :
                    kind == 1u ? thy_library_set_chord(&library, ui.slot, &ui.draft.chord) : thy_library_set_six(&library, ui.slot, &ui.draft.six);
                CHECK(thy_library_ui_ack(&ui, &library, result) == THY_LIBRARY_OK);
            }
            CHECK(thy_library_valid(&library));
            CHECK(thy_library_ui_valid(&ui, &library));
            CHECK(ui.slot < limits[kind] && ui.name_cursor <= 12u);
            char line[24];
            CHECK(thy_library_ui_format(&ui, &library, thy_library_ui_selected(&ui), line, sizeof line) == THY_LIBRARY_OK);
        }
    }
    return 1;
}

int main(void)
{
    int (*tests[])(void) = { construction_and_slot_bounds, scale_draft_apply_cancel,
        chord_notes_zero_and_inactive_preservation, six_set_references_and_copy_capture,
        name_entry_done_cancel_and_character_reachability, delete_confirmation_and_acknowledgement,
        projections_and_invalid_state_escape, bounded_navigation_walks };
    for(unsigned n = 0; n < sizeof tests / sizeof tests[0]; ++n) if(!tests[n]()) return 1;
    printf("Library UI: %u scenarios passed (%u assertions; %zu-byte workspace)\n",
        (unsigned)(sizeof tests / sizeof tests[0]), assertions, sizeof(thy_library_ui));
    return 0;
}
