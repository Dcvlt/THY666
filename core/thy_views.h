/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_VIEWS_H
#define THY_VIEWS_H

#include <stdint.h>

/* Stable saved identities. Retired views are reserved for migration rather
 * than being reused for a different picture. */
enum thy_view {
    THY_VIEW_DETAILED = 0, THY_VIEW_PIANO = 1,
    THY_VIEW_RETIRED_LAYOUTS = 2, THY_VIEW_RETIRED_ROUTES = 3,
    THY_VIEW_SIMPLE = 4, THY_VIEW_CHLADNI = 5,
    THY_VIEW_ORBIT = 6, THY_VIEW_WAVES = 7,
    THY_VIEW_FIFTHS = 8, THY_VIEW_COUNT = 9
};

enum thy_view_info {
    THY_VIEW_INFO_OFF = 0, THY_VIEW_INFO_VELOCITY, THY_VIEW_INFO_COUNTS,
    THY_VIEW_INFO_NOTES, THY_VIEW_INFO_ROOT_SCALE, THY_VIEW_INFO_CHANNELS,
    THY_VIEW_INFO_OUTPUTS, THY_VIEW_INFO_MODIFIERS, THY_VIEW_INFO_PRESSURE,
    THY_VIEW_INFO_OCTAVES, THY_VIEW_INFO_CLOCK, THY_VIEW_INFO_ARPS,
    THY_VIEW_INFO_MUSICAL_NAMES = 12,
    THY_VIEW_INFO_COUNT
};

#define THY_VIEW_DETAIL_ROWS 6u
typedef struct thy_view_config {
    uint8_t slots[2];
    uint8_t header;
    uint8_t motion;
    uint8_t detail_rows[THY_VIEW_DETAIL_ROWS];
} thy_view_config;

static inline int thy_view_available(unsigned view)
{
    return view < THY_VIEW_COUNT && view != THY_VIEW_RETIRED_LAYOUTS &&
        view != THY_VIEW_RETIRED_ROUTES;
}

static inline int thy_view_config_valid(const thy_view_config *config)
{
    if (config == 0 || config->header > 1u || config->motion > 1u ||
        config->slots[0] >= THY_VIEW_INFO_COUNT ||
        config->slots[1] >= THY_VIEW_INFO_COUNT) return 0;
    for (unsigned row = 0u; row < THY_VIEW_DETAIL_ROWS; ++row)
        if (config->detail_rows[row] >= THY_VIEW_INFO_COUNT) return 0;
    return 1;
}

static inline void thy_views_defaults(thy_view_config configs[THY_VIEW_COUNT])
{
    static const uint8_t rows[THY_VIEW_DETAIL_ROWS] = {
        THY_VIEW_INFO_MUSICAL_NAMES, THY_VIEW_INFO_NOTES, THY_VIEW_INFO_MODIFIERS,
        THY_VIEW_INFO_OUTPUTS, THY_VIEW_INFO_VELOCITY, THY_VIEW_INFO_COUNTS
    };
    for (unsigned view = 0u; view < THY_VIEW_COUNT; ++view) {
        configs[view].slots[0] = THY_VIEW_INFO_MUSICAL_NAMES;
        configs[view].slots[1] = THY_VIEW_INFO_COUNTS;
        configs[view].header = 1u;
        configs[view].motion = 1u;
        for (unsigned row = 0u; row < THY_VIEW_DETAIL_ROWS; ++row)
            configs[view].detail_rows[row] = rows[row];
    }
}

#endif
