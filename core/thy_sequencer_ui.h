/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_SEQUENCER_UI_H
#define THY_SEQUENCER_UI_H
#include "thy_sequencer.h"
#include "thy_state.h"

enum thy_seq_view { THY_SEQ_VIEW_TRACKS, THY_SEQ_VIEW_STEPS };
enum thy_seq_layout { THY_SEQ_SPLIT, THY_SEQ_FULL_TIME, THY_SEQ_FULL_PITCH };
enum thy_seq_input_layout { THY_SEQ_INPUT_NOTES, THY_SEQ_INPUT_CHORDS };
enum thy_seq_ui_field { THY_SEQ_UI_LAYOUT, THY_SEQ_UI_SWAPPED,
    THY_SEQ_UI_INPUT, THY_SEQ_UI_GRID, THY_SEQ_UI_WINDOW,
    THY_SEQ_UI_PITCH, THY_SEQ_UI_REGION, THY_SEQ_UI_REGION_START,
    THY_SEQ_UI_REGION_END, THY_SEQ_UI_SELECTED_STEP,
    THY_SEQ_UI_OVERDUB_VALUE, THY_SEQ_UI_STEP_COUNT };
enum thy_seq_ui_command { THY_SEQ_UI_TRANSPORT, THY_SEQ_UI_RECORD,
    THY_SEQ_UI_LAUNCH, THY_SEQ_UI_UNDO, THY_SEQ_UI_REDO,
    THY_SEQ_UI_TRACK_OPTIONS, THY_SEQ_UI_RECORD_OPTIONS,
    THY_SEQ_UI_PLAY, THY_SEQ_UI_LOOP_CHANGED,
    THY_SEQ_UI_CREATE_TRACK, THY_SEQ_UI_DELETE_TRACK,
    THY_SEQ_UI_PLAY_TRACK, THY_SEQ_UI_PLAY_ALL,
    THY_SEQ_UI_CLEAR_STEP, THY_SEQ_UI_OVERDUB,
    THY_SEQ_UI_INPUT_TOGGLE, THY_SEQ_UI_HELP, THY_SEQ_UI_INPUT_CONTEXT,
    THY_SEQ_UI_OCTAVE_DOWN, THY_SEQ_UI_OCTAVE_UP };
typedef int (*thy_seq_ui_command_fn)(void *, uint8_t command, uint8_t track);

enum thy_seq_pad_role { THY_SEQ_PAD_NONE, THY_SEQ_PAD_EDITOR,
    THY_SEQ_PAD_INPUT_NOTES, THY_SEQ_PAD_INPUT_CHORDS,
    THY_SEQ_PAD_TRACK, THY_SEQ_PAD_COMMAND, THY_SEQ_PAD_SEPARATOR };
typedef struct thy_seq_pad_gesture {
    uint32_t tick, duration;
    uint16_t event_id;
    uint8_t role, track, pitch, consumed, held, cell_kind;
} thy_seq_pad_gesture;

/* UI owns only a viewport and physical gesture lifetimes. Timing/content live
 * in thy_sequencer; commands use the engine's normal transaction paths. All
 * pads capture their role on attack and keep it until release, including after
 * CENTER, bank changes and leaving this workspace. */
typedef struct thy_sequencer_ui {
    thy_sequencer *sequencer;
    thy_transport *transport;
    const thy_clock *clock;
    void *context;
    thy_seq_ui_command_fn command;
    thy_seq_pad_gesture pads[96];
    uint32_t window_tick, grid_ticks, region_start, region_end;
    uint32_t revision;
    uint32_t edit_tick, edit_duration;
    uint16_t selected_event;
    uint16_t control_down;
    uint8_t open, view, layout, swapped, input_layout, base_pitch;
    uint8_t modifier, selected_pad, region_enabled, last_error;
    uint8_t selected_step, track_new, overdub, delete_pending, solo;
    uint8_t help_open, help_page;
    /* Ephemeral track-local menu; never part of song/settings persistence. */
    uint8_t options_open, options_track, options_cursor, options_saved_cursor;
    uint8_t edit_track, edit_overdub, gesture_active, gesture_written;
} thy_sequencer_ui;

void thy_sequencer_ui_init(thy_sequencer_ui *ui, thy_sequencer *sequencer,
    thy_transport *transport, const thy_clock *clock,
    thy_seq_ui_command_fn command, void *context);
void thy_sequencer_ui_open(thy_sequencer_ui *ui, uint8_t view);
/* Cancels pending taps; already auditioned musical input still releases. */
void thy_sequencer_ui_close(thy_sequencer_ui *ui);
int thy_sequencer_ui_configure(thy_sequencer_ui *ui, uint8_t field,
    uint32_t value);
int thy_sequencer_ui_navigate(thy_sequencer_ui *ui, uint8_t action);
/* Returns 1 when consumed. Musical root/function keys return 0. */
int thy_sequencer_ui_control(thy_sequencer_ui *ui, uint8_t bank,
    uint8_t index, int pressed);
int thy_sequencer_ui_pad(thy_sequencer_ui *ui, uint8_t pad, int pressed);
/* Frozen input role; 0 ordinary, 1 notes, 2 chords. Root resolves the chosen
 * bank using the normal instrument planner before calling assignment. */
int thy_sequencer_ui_input(const thy_sequencer_ui *ui, uint8_t pad);
/* Current mapping for unheld pads; held pads project their captured role. */
int thy_sequencer_ui_pad_role(const thy_sequencer_ui *ui, uint8_t pad,
    uint8_t *track, uint32_t *tick, uint8_t *pitch);
/* Assignment suppresses real-time recording for this input attack. The most
 * recently tapped LEFT step remains selected after release. Overlapping RIGHT
 * input lifetimes form one frozen-target undo transaction; a new gesture
 * replaces that step unless Overdub is selected. During live capture this
 * returns 0 so the input follows the recorder instead. */
int thy_sequencer_ui_assign(thy_sequencer_ui *ui, uint8_t source_pad,
    const int16_t *notes, size_t count, uint8_t velocity, uint8_t chord);
/* Explicit step erase; tapping a step never inserts or removes content. */
int thy_sequencer_ui_clear_step(thy_sequencer_ui *ui);
int thy_sequencer_ui_set_overdub(thy_sequencer_ui *ui, int enabled);
/* The 48 left pads are chronological, row-major on the physical surface.
 * Return one representative onset pitch for the step's note-color display. */
uint32_t thy_sequencer_ui_step_tick(const thy_sequencer_ui *ui, uint8_t step);
int thy_sequencer_ui_step_note(const thy_sequencer_ui *ui, uint8_t step,
    uint8_t *pitch);
/* Pure projection. Returns 1 for sequencer-owned light; 0 for musical bank.
 * 12-bit RGB follows the board lighting convention. */
int thy_sequencer_ui_color(const thy_sequencer_ui *ui, uint8_t pad,
    uint16_t rgb[3]);
uint32_t thy_sequencer_ui_scene_key(const thy_sequencer_ui *ui);

#endif
