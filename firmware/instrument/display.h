/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_INSTRUMENT_DISPLAY_H
#define THY_INSTRUMENT_DISPLAY_H
#include "thy_display.h"
#include "thy_studio_ui.h"
/* Foreground snapshot. Read-only timing observations, never optical Hz. */
const thy_display_report *board_display_report(void);
/* COMPANION/1 provider: sent panel bytes/visibility, accepted pad RGB and
 * completed center levels. Foreground copies only; no display/LED I/O. */
int board_companion_visual(void *,uint8_t oled[1024],uint8_t rgb[288],
    uint8_t center[19],uint32_t *frame_ms,uint8_t *panel_visible);
/* One bounded slice, called after pad/control reconciliation and musical/MIDI
 * servicing. Never called from an interrupt or awaited for frame completion. */
void board_display_task(void);
/* Presentation only: these never wait for the animation, own musical state,
 * or gate acquisition, MIDI reconciliation or control processing. */
void board_display_boot_progress(uint8_t stage);
void board_display_boot_ready(void);
void board_display_boot_skip(void);
#endif
