/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_STUDIO_UI_H
#define THY_STUDIO_UI_H

#include "thy_state.h"
#include "thy_ui.h"
#include "thy_resources.h"
#include "thy_restore_progress.h"

#define THY_STUDIO_UI_TRANSITION_MS 180u
/* Startup alone advances with a bounded presented-frame step. Hierarchy
 * slides use wall-clock time and never prolong themselves after a stall. */
#define THY_STUDIO_UI_MAX_STEP_MS 42u
#define THY_STUDIO_UI_BOOT_REVEAL_MS 840u
#define THY_STUDIO_UI_BOOT_HANDOFF_MS 280u

enum thy_studio_boot_stage {
    THY_STUDIO_BOOT_START = 0, THY_STUDIO_BOOT_DISPLAY,
    THY_STUDIO_BOOT_SENSORS, THY_STUDIO_BOOT_PADS, THY_STUDIO_BOOT_MIDI
};
enum thy_studio_boot_phase {
    THY_STUDIO_BOOT_OFF = 0, THY_STUDIO_BOOT_REVEAL, THY_STUDIO_BOOT_WAIT,
    THY_STUDIO_BOOT_HANDOFF, THY_STUDIO_BOOT_DONE, THY_STUDIO_BOOT_SKIPPED
};

/* Portable 128 x 64, page-addressed one-bit presentation of thy_state.
 * No display driver, allocator, browser, clock source or musical owner lives
 * here. The caller schedules frames and can disable motion at any time.
 *
 * Only hierarchy transitions slide: entering a category moves left, backing
 * out moves right. Cursor motion and edit/apply/cancel never start a slide. */
typedef struct thy_studio_ui {
    const thy_resources *resources; /* Optional live target measurements. */
    const char *build_identity; /* Optional caller-owned immutable build label. */
    uint8_t animations_enabled;
    uint8_t navigation_animations_enabled;
    uint8_t initialized;
    uint8_t transitioning;
    int8_t transition_direction;
    uint32_t transition_revision;
    uint32_t transition_started_ms;
    uint32_t transition_elapsed_ms;
    uint32_t last_render_ms;
    uint8_t boot_phase;
    uint8_t boot_stage;
    uint8_t boot_ready;
    uint32_t boot_elapsed_ms;
    uint32_t boot_handoff_elapsed_ms;
    uint8_t previous_frame[THY_UI_FRAME_BYTES];
    uint8_t transition_frame[THY_UI_FRAME_BYTES];
} thy_studio_ui;

void thy_studio_ui_init(thy_studio_ui *ui);
/* Transport presentation policy, independent of musical/view preferences.
 * Defaults ON. OFF returns the final destination frame immediately and ends
 * an active hierarchy slide; startup and Play graphic motion are unchanged. */
void thy_studio_ui_set_navigation_animations(thy_studio_ui *ui, int enabled);
/* Identity of a complete navigation page. Cursor movement, editors and live
 * musical changes do not change it; info, credits, quick pages and Play views
 * do. Drivers use it to request a coherent scene handoff. */
uint32_t thy_studio_ui_scene_key(const thy_state *state);
/* About only. Keep the immutable identity alive as long as this renderer;
 * NULL omits the line. init clears it; replaying startup preserves it. */
void thy_studio_ui_set_build_identity(thy_studio_ui *ui, const char *identity);
/* Explicit presentation lifecycle, usable before the musical state exists.
 * Progress reports completed real milestones, never a timer-derived percent.
 * Ready permits (but does not delay) performance. Render(NULL) can show startup
 * but cannot complete the handoff. Caller must skip on input/error; menu entry
 * and active musical owners also skip automatically. Startup timing advances
 * only when the caller renders a display frame, capped at 42 ms.
 * Begin preserves the caller's motion policies and restarts only UI. */
void thy_studio_ui_boot_begin(thy_studio_ui *ui, uint32_t now_ms);
void thy_studio_ui_boot_progress(thy_studio_ui *ui, uint8_t stage);
void thy_studio_ui_boot_ready(thy_studio_ui *ui);
void thy_studio_ui_boot_skip(thy_studio_ui *ui);
int thy_studio_ui_boot_active(const thy_studio_ui *ui);
void thy_studio_ui_format(const thy_state *state, uint16_t row,
    int use_edit, char out[32]);
/* The same read-only information formatter used by Play view slots/rows.
 * Values describe current shared state; outputs are route configuration and
 * zone outputs are the union for that half, marked OUT*. */
void thy_studio_ui_view_line(const thy_state *state, unsigned kind,
    const char *scale_name, int din_ready, int usb_ready, char out[32]);
void thy_studio_ui_render(thy_studio_ui *ui, const thy_state *state,
    const char *scale_name, int din_ready, int usb_ready, uint32_t now_ms,
    uint8_t out[THY_UI_FRAME_BYTES]);
/* Stateless, bounded restore overlay. The service supplies real stage/counts;
 * this only draws into the caller's existing frame and never gates inputs. */
void thy_studio_ui_render_restore(const thy_restore_progress *progress,
    uint8_t out[THY_UI_FRAME_BYTES]);

#endif
