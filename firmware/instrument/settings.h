/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_INSTRUMENT_SETTINGS_H
#define THY_INSTRUMENT_SETTINGS_H
#include "thy_settings.h"
/* Internal application-owned sectors only. Never accesses external NOR or
 * bootloader bytes below 0x08040000. Instrument linker reserves these sectors. */
#define THY_INSTRUMENT_SETTINGS_BASE UINT32_C(0x080c0000)
#define THY_INSTRUMENT_SETTINGS_END UINT32_C(0x08100000)
int thy_instrument_settings_io(thy_settings_io *io);
#endif
