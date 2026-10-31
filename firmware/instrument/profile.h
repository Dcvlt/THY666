/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_INSTRUMENT_PROFILE_H
#define THY_INSTRUMENT_PROFILE_H
#include "thy_profile.h"
#include "thy_settings.h"
#include <stddef.h>

typedef struct thy_instrument_profile_mark {uint32_t id,cycles,ms;} thy_instrument_profile_mark;
typedef struct thy_instrument_profile_context {
    uint32_t engine,view,tempo_milli_bpm,settings_crc;
} thy_instrument_profile_context;
void thy_instrument_profile_init(uint32_t hz);
thy_profile *thy_instrument_profile_state(void);
uint32_t thy_instrument_profile_now(void *);
thy_instrument_profile_mark thy_instrument_profile_begin(void);
void thy_instrument_profile_end(unsigned metric,thy_instrument_profile_mark);
/* LOOP, INPUT_GAP or ADC_FRAME_GAP only. Session IDs isolate successive runs. */
void thy_instrument_profile_gap(unsigned metric);
void thy_instrument_profile_service(uint32_t now_ms);
const thy_settings_observer *thy_instrument_profile_journal_observer(void);
size_t thy_instrument_profile_command(const char *,char *,size_t,int available,
    const thy_instrument_profile_context *);
#endif
