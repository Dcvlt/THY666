/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_LIBRARY_UI_H
#define THY_LIBRARY_UI_H

#include "thy_library.h"

enum thy_library_ui_kind {
    THY_LIBRARY_UI_SCALE, THY_LIBRARY_UI_CHORD, THY_LIBRARY_UI_SIX
};
enum thy_library_ui_page {
    THY_LIBRARY_UI_BROWSE, THY_LIBRARY_UI_EDIT,
    THY_LIBRARY_UI_NAME, THY_LIBRARY_UI_DELETE_CONFIRM
};
enum thy_library_ui_command {
    THY_LIBRARY_UI_NONE, THY_LIBRARY_UI_BACK, THY_LIBRARY_UI_PLAY,
    THY_LIBRARY_UI_SAVE, THY_LIBRARY_UI_DELETE, THY_LIBRARY_UI_COPY_CURRENT
};
enum thy_library_ui_edit {
    THY_LIBRARY_UI_NOT_EDITING, THY_LIBRARY_UI_SLOT_EDIT, THY_LIBRARY_UI_VALUE_EDIT
};
enum thy_library_ui_notice {
    THY_LIBRARY_UI_NOTICE_NONE, THY_LIBRARY_UI_NOTICE_DRAFT,
    THY_LIBRARY_UI_NOTICE_SAVED, THY_LIBRARY_UI_NOTICE_DELETED,
    THY_LIBRARY_UI_NOTICE_INVALID, THY_LIBRARY_UI_NOTICE_IN_USE,
    THY_LIBRARY_UI_NOTICE_RELEASE
};
/* ack accepts the pure library's 0/1/2 results plus this engine busy result. */
enum thy_library_ui_result { THY_LIBRARY_UI_BUSY = 3 };

typedef union thy_library_ui_draft {
    thy_library_scale scale;
    thy_library_chord chord;
    thy_library_six six;
} thy_library_ui_draft;

/* Caller-owned presentation state; no pointer, heap or library mutation.
 * CENTER always closes to Play and discards the draft. LEFT from the record
 * editor cancels the entire record; LEFT while adjusting a field cancels just
 * that field. Name entry uses twelve characters plus a final Done position.
 * Root executes commands with its ownership/storage checks, then acknowledges.
 */
typedef struct thy_library_ui {
    thy_library_ui_draft draft;
    uint8_t name_before[THY_LIBRARY_NAME_BYTES];
    uint16_t field_before;
    uint8_t open, kind, slot, page, cursor, editing;
    uint8_t name_cursor, name_changed, command, notice;
    uint32_t revision;
} thy_library_ui;

int thy_library_ui_open(thy_library_ui *, const thy_library *, uint8_t kind, uint8_t slot);
int thy_library_ui_valid(const thy_library_ui *, const thy_library *);
/* Actions match the shared physical navigation: LEFT0 UP1 CENTER2 DOWN3 RIGHT4.
 * Pending SAVE/DELETE/COPY commands freeze navigation until ack/begin_copy;
 * CENTER remains available. Commands are returned once, never executed here.
 */
uint8_t thy_library_ui_navigate(thy_library_ui *, const thy_library *, uint8_t action);
/* Successful SAVE/DELETE returns to Browse and reloads the chosen slot. Errors
 * retain the draft/confirmation and show Invalid/In use. COPY success uses
 * begin_copy instead; ack handles a failed capture. */
int thy_library_ui_ack(thy_library_ui *, const thy_library *, int result);
/* Complete COPY_CURRENT into a tiny local draft. A NULL captured record seeds
 * the current slot/default for the adapter to fill explicitly. Supplied active
 * records and six-set references are checked before any UI mutation. */
int thy_library_ui_begin_copy(thy_library_ui *, const thy_library *,
    const thy_library_ui_draft *captured);

uint8_t thy_library_ui_row_count(const thy_library_ui *);
uint8_t thy_library_ui_selected(const thy_library_ui *);
int thy_library_ui_title(const thy_library_ui *, char *output, size_t capacity);
int thy_library_ui_format(const thy_library_ui *, const thy_library *, uint8_t row,
    char *output, size_t capacity);
const char *thy_library_ui_status(const thy_library_ui *);
const char *thy_library_ui_legend(const thy_library_ui *);
/* NAME exposes the full draft text and name_cursor for an optional OLED caret.
 * It has three formatter lines: complete text, character/Done, navigation. */
const char *thy_library_ui_name(const thy_library_ui *);

#endif
