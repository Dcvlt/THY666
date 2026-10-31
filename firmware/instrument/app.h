/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_INSTRUMENT_APP_H
#define THY_INSTRUMENT_APP_H
#include "thy_instrument.h"
#include "thy_engine.h"
#include "transport.h"
#include "thy_settings.h"
#include "thy_song_transfer.h"
#include "thy_settings_transfer.h"
#include "thy_companion.h"
#include <stdbool.h>
#include "thy_resources.h"
const thy_resources *board_resources(void);
void board_resources_task(uint32_t now_ms);
const thy_instrument *thy_instrument_app_state(void);
const thy_state *thy_instrument_app_menu_state(void);
const char *thy_instrument_app_scale_name(void);
bool thy_instrument_app_ready(void);
uint32_t thy_instrument_usb_epoch(void);
void thy_instrument_pads_renew(uint32_t now);
void thy_instrument_app_panic(void);
/* Foreground-only USB cable0 ingress. Clock/transport and opt-in filtered song
 * recording are separate permissions. Incoming notes are never echoed. */
void thy_instrument_app_midi(const thy_midi_event *event, uint32_t now_ms);
void thy_instrument_app_usb_reset(uint32_t now_ms);
/* Enable CPU-only CCM before the explicitly initialized shared engine is used. */
void thy_instrument_memory_init(void);
const thy_instrument_transport *thy_instrument_app_transport(void);
const thy_settings_journal *thy_instrument_app_settings(void);
const thy_restore_progress *thy_instrument_app_restore_progress(void);
size_t thy_instrument_app_song_command(const char *line,char *output,size_t capacity);
size_t thy_instrument_app_backup_command(const char *line,char *output,size_t capacity);
size_t thy_instrument_app_companion_command(const char *line,char *output,size_t capacity);
size_t thy_instrument_app_profile_command(const char *line,char *output,size_t capacity);
void thy_instrument_app_console_reset(void);
#endif
