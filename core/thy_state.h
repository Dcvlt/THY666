/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_STATE_H
#define THY_STATE_H

#include <stddef.h>
#include <stdint.h>
#include "thy_menu_data.h"

#define THY_STATE_TEXT_BYTES 49u
#define THY_STATE_HISTORY 64u
#define THY_STATE_NO_ROW UINT16_MAX

enum thy_state_result {
    THY_STATE_OK = 0, THY_STATE_INVALID = -1, THY_STATE_BLOCKED = -2,
    THY_STATE_UNAVAILABLE = -3, THY_STATE_ADAPTER_ERROR = -4,
    THY_STATE_NO_EDIT = -5, THY_STATE_TEXT_REQUIRED = -6
};
enum thy_state_level { THY_STATE_PLAY = 0, THY_STATE_GROUPS = 1, THY_STATE_ROWS = 2 };
enum thy_state_navigation { THY_STATE_LEFT = 0, THY_STATE_UP = 1,
    THY_STATE_SELECT = 2, THY_STATE_DOWN = 3, THY_STATE_RIGHT = 4 };
enum thy_state_notice { THY_NOTICE_NONE = 0, THY_NOTICE_APPLIED, THY_NOTICE_DRAFT,
    THY_NOTICE_INVALID, THY_NOTICE_UNAVAILABLE, THY_NOTICE_ADAPTER,
    THY_NOTICE_CANCELLED, THY_NOTICE_RANGE, THY_NOTICE_REMOTE_RANGE,
    THY_NOTICE_PROGRAM_TRIGGER, THY_NOTICE_SENSITIVITY, THY_NOTICE_TEXT, THY_NOTICE_RELEASE,
    THY_NOTICE_HOLD_ONLY };
enum thy_state_event_kind { THY_STATE_SETTING_APPLIED = 1,
    THY_STATE_CONFIG_CHANGED, THY_STATE_EDIT_CANCELLED };

/* Selects use option ordinal; dynamic scale values use catalog group IDs.
 * Live write is a precommit, atomic adapter: failure MUST leave its existing
 * instrument/configuration unchanged. No callback means unavailable, never a
 * successful simulated engine action. Callers serialize all access. */
typedef int (*thy_state_read_fn)(void *context, uint16_t row, uint8_t instance, int32_t *value);
typedef int (*thy_state_write_fn)(void *context, uint16_t row, uint8_t instance, int32_t value);
typedef const char *(*thy_state_label_fn)(void *context, uint16_t row, int32_t value);
typedef struct thy_state_callbacks {
    thy_state_read_fn read;
    thy_state_write_fn write;
    thy_state_label_fn label;
} thy_state_callbacks;

typedef struct thy_state_event {
    uint32_t sequence;
    int32_t value;
    uint16_t row;
    uint8_t kind;
    uint8_t instance;
    char text[THY_STATE_TEXT_BYTES];
} thy_state_event;

struct thy_instrument;
struct thy_engine;
typedef struct thy_state {
    thy_state_callbacks callbacks;
    void *context;
    const struct thy_instrument *instrument; /* Optional renderer projection. */
    const struct thy_engine *engine; /* Optional application/quick-page projection. */
    int32_t values[THY_MENU_VALUE_COUNT];
    char texts[THY_MENU_TEXT_COUNT][THY_STATE_TEXT_BYTES];
    thy_state_event events[THY_STATE_HISTORY];
    uint32_t revision;
    uint32_t transition_revision;
    uint32_t edit_event; /* Frozen note identity for a selected-event draft. */
    uint32_t reset_epoch; /* Monotonic confirmation identity, never persisted. */
    uint16_t scale_count;
    uint16_t edit_row;
    int32_t edit_value;
    char edit_text[THY_STATE_TEXT_BYTES];
    uint8_t cursors[THY_MENU_GROUP_COUNT];
    uint8_t level;
    uint8_t group_index;
    uint8_t editing;
    uint8_t about_open; /* Ephemeral credits screen, never a stored preference. */
    uint8_t info_open; /* Selected information page; RIGHT opens, LEFT closes. */
    uint8_t reset_open, reset_confirm, reset_group, reset_track;
    uint8_t reset_targets[9]; /* Frozen instances indexed by thy_menu_instance. */
    uint8_t design_editing; /* Future configuration UI is explicitly opt-in. */
    uint8_t edit_instance; /* Captured at begin; changing target cannot retarget. */
    uint8_t notice;
    uint8_t history_count;
    int8_t transition_direction; /* -1 inward, +1 outward; presentation only. */
} thy_state;

void thy_state_init(thy_state *state, const thy_state_callbacks *callbacks, void *context);
int thy_state_set_scale_count(thy_state *state, uint16_t count);
int thy_state_set_design_mode(thy_state *state, uint8_t enabled);
/* Canonical row IDs/cursors never become filtered ordinals. Live mode shows
 * only implemented controls. Future controls and information are opt-in. */
int thy_state_group_visible(uint8_t group);
int thy_state_row_visible(const thy_state *state, uint16_t row);
/* Track-only rows share canonical bindings, not the global menu surface. */
int thy_state_track_row(uint16_t row);
uint16_t thy_state_find_row(const char *id);
int thy_state_get(const thy_state *state, uint16_t row, int32_t *value);
const char *thy_state_get_text(const thy_state *state, uint16_t row);
int thy_state_get_instance(const thy_state *state, uint16_t row, uint8_t instance, int32_t *value);
const char *thy_state_get_text_instance(const thy_state *state, uint16_t row, uint8_t instance);
uint8_t thy_state_instance_count(uint16_t row);
uint8_t thy_state_current_instance(const thy_state *,uint16_t row);
int thy_state_set(thy_state *state, uint16_t row, int32_t value);
int thy_state_set_instance(thy_state *state, uint16_t row, uint8_t instance, int32_t value);
int thy_state_set_text(thy_state *state, uint16_t row, const char *text);
int thy_state_begin_edit(thy_state *state, uint16_t row);
int thy_state_reset_row(uint16_t row);
int thy_state_reset_begin(thy_state *,uint16_t row);
/* An explicit second action. Stale epochs never act on a different menu. */
int thy_state_reset_resolve(thy_state *,uint32_t epoch,int confirmed);
int thy_state_edit_set(thy_state *state, int32_t value);
int thy_state_edit_text(thy_state *state, const char *text);
/* Numeric editor limits include related values for the captured instance.
 * Read-only; direct API/import validation remains strict and atomic. */
int thy_state_edit_bounds(const thy_state *,uint16_t row,uint8_t instance,
    int32_t *minimum,int32_t *maximum);
int thy_state_adjust(thy_state *state, int delta);
int thy_state_commit(thy_state *state);
int thy_state_cancel(thy_state *state);
int thy_state_focus(thy_state *state, uint8_t group, uint16_t row);
void thy_state_open(thy_state *state);
void thy_state_play(thy_state *state);
/* SELECT is the physical Center button: open Settings from Play, otherwise
 * return directly to Play and discard any unapplied editor draft. RIGHT
 * enters a group, begins/commits an edit, runs an action or opens a selected
 * future engine's description. LEFT closes that description before leaving
 * its list, goes back or cancels only the current edit; UP/DOWN move selection
 * or its draft value. */
int thy_state_navigate(thy_state *state, uint8_t action);
uint16_t thy_state_current_row(const thy_state *state);
int thy_state_option_allowed(const thy_state *state, uint16_t row, int32_t value);
int thy_state_format(const thy_state *state, uint16_t row, int use_edit, char *out, size_t capacity);
const char *thy_state_notice_text(const thy_state *state);
uint16_t thy_state_change_count(const thy_state *state);
/* Boolean reader, matching the instrument transition reader: 1 found, 0 absent. */
int thy_state_get_event(const thy_state *state, uint32_t sequence, thy_state_event *event);

#endif
