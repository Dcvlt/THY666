/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_UI_H
#define THY_UI_H

#include "thy_instrument.h"

#define THY_UI_WIDTH 128u
#define THY_UI_HEIGHT 64u
#define THY_UI_FRAME_BYTES 1024u
#define THY_UI_TRANSITION_MS 120u

/* The same page-addressed, one-bit framebuffer is used by the MCU and WASM.
 * Rendering has no hardware, font-asset, libc, allocation or clock dependency.
 * Callers own the clock and refresh schedule. Short menu-open/close slides are
 * presentation only: no musical state or live note update is delayed. */
typedef struct thy_ui {
    uint8_t animations_enabled;
    uint8_t initialized;
    uint8_t menu_open;
    uint8_t transitioning;
    uint32_t transition_started_ms;
    uint8_t previous_frame[THY_UI_FRAME_BYTES];
    uint8_t transition_frame[THY_UI_FRAME_BYTES];
} thy_ui;

void thy_ui_init(thy_ui *ui);
void thy_ui_render(thy_ui *ui, const thy_instrument *instrument,
    const char *scale_name, int din_ready, int usb_ready, uint32_t now_ms,
    uint8_t out[THY_UI_FRAME_BYTES]);

/* Scientific pitch names: MIDI 60 is C4, MIDI 0 is C-1. Invalid input yields
 * an empty string. Five characters plus terminator is always sufficient. */
void thy_ui_note_name(int note, char out[6]);

/* Shared independent glyph primitives for additional bounded UI projections. */
void thy_ui_draw_pixel(uint8_t *out, unsigned x, unsigned y, int enabled);
void thy_ui_draw_rectangle(uint8_t *out, unsigned x, unsigned y,
    unsigned width, unsigned height, int enabled);
void thy_ui_draw_text(uint8_t *out, unsigned x, unsigned y,
    const char *label, unsigned limit, int inverse);
void thy_ui_draw_play(uint8_t *out, const thy_instrument *instrument,
    const char *scale_name, int din_ready, int usb_ready);

#endif
