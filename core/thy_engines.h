/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_ENGINES_H
#define THY_ENGINES_H

/* Persistent engine IDs: 0 was the legacy Scales workspace. Current Play
 * retains ID 1 and uses an independent melody layout. Arps are processors. */
enum thy_playing_engine {
    THY_PLAY_ENGINE_LEGACY_SCALES = 0, THY_PLAY_ENGINE_PLAY = 1,
    THY_PLAY_ENGINE_SEQUENCER, THY_PLAY_ENGINE_DRUMS,
    THY_PLAY_ENGINE_LIGHTBOARD, THY_PLAY_ENGINE_USER, THY_PLAY_ENGINE_HARMONY,
    THY_PLAY_ENGINE_COUNT
};

static inline int thy_play_engine_available(unsigned value)
{
    return value == THY_PLAY_ENGINE_PLAY || value == THY_PLAY_ENGINE_SEQUENCER ||
        value == THY_PLAY_ENGINE_HARMONY;
}

#endif
