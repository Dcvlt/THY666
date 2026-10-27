/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_INSTRUMENT_DATA_H
#define THY_INSTRUMENT_DATA_H
#include "thy_instrument.h"
#define THY_INSTRUMENT_SCALE_COUNT 840u
int thy_instrument_data_find(unsigned group, unsigned root);
int thy_instrument_data_load(unsigned scale, thy_instrument_pad pads[96],
    int16_t *anchor, int16_t degrees[12], uint8_t *degree_count);
const char *thy_instrument_data_name(unsigned scale);
#endif
