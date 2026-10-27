/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_LIGHTING_H
#define THY_LIGHTING_H

#include "thy_instrument.h"

#define THY_LIGHTING_MAX_FADE_MS 2000u

enum thy_lighting_palette { THY_LIGHTING_CLASSIC = 0, THY_LIGHTING_SPECTRUM = 1 };
enum thy_lighting_chord_mode {
    THY_LIGHTING_CHORD_ROOT = 0,
    THY_LIGHTING_CHORD_BASS = 1,
    THY_LIGHTING_CHORD_DEGREE = 2
};
enum thy_lighting_held_style {
    THY_LIGHTING_HELD_NOTE = 0, THY_LIGHTING_HELD_WHITE = 1
};
enum thy_lighting_right_illumination {
    THY_LIGHTING_RIGHT_ALL = 0, THY_LIGHTING_RIGHT_IN_KEY = 1
};

/* Classic uses default note->palette indices and ordered
 * component ratios, peak-normalized for independently controlled brightness.
 * Spectrum is a new twelve-hue palette, distinct at brightness >=3. Packed
 * components are GRB, as established by the owner's full-board photograph.
 * Archived numeric color IDs remain separate metadata, never remapped.
 * Brightness is a linear component multiplier, 0=off, 255=full palette value.
 * The physical adapter separately limits output to its tested amplitude. */
typedef struct thy_lighting_config {
    uint8_t enabled;
    uint8_t idle_brightness;
    uint8_t held_brightness;
    uint8_t selected_brightness;
    uint8_t selected_enabled;
    uint8_t palette;
    uint16_t fade_ms;
    uint8_t chord_color_mode;
    uint8_t key_contrast;
    uint8_t key_dim_percent; /* Remaining idle light for black keys, 0..100. */
    uint8_t held_style;
    uint8_t held_feedback;
    uint8_t right_illumination;
} thy_lighting_config;

typedef struct thy_lighting {
    uint32_t from[THY_PAD_COUNT];
    uint32_t target[THY_PAD_COUNT];
    uint32_t changed_ms[THY_PAD_COUNT];
    uint32_t last_ms;
    /* Archive metadata is retained independently of displayed RGB. */
    uint8_t source_color[THY_PAD_COUNT];
    uint8_t initialized;
} thy_lighting;

void thy_lighting_init(thy_lighting *lighting);
void thy_lighting_defaults(thy_lighting_config *config);
int thy_lighting_config_valid(const thy_lighting_config *config);
uint32_t thy_lighting_note_color(uint8_t midi_note);
uint32_t thy_lighting_note_color_palette(uint8_t midi_note, uint8_t palette);
/* Resolve the archive's degree/color token through its original melody
 * column, requiring that pitch class in the playable chord. Returns its
 * transposed pitch class 0..11 without rotating/replacing any musical notes.
 * On failure output is untouched; callers may use explicit Bass policy. */
int thy_lighting_chord_root(const thy_instrument *instrument, uint8_t logical,
    int16_t *pitch_class);
/* Logical layout order: chords 0..47, melody 48..95. Uses the same read-only
 * voicing preview as actual presses; empty/out-of-range/blocked pads are dark,
 * except explicitly enabled context-control legends. Chord colors select the
 * harmonic root, actual bass, or fixed major-degree policy. Held notes use
 * immutable owner pitches and captured color metadata across root, route and
 * layout changes. No acquisition or musical ownership is mutated.
 * now_ms is monotonic modulo 2^32; backwards jumps are rejected untouched.
 * OFF and input disable bypass fades. Optional fades affect light only. */
int thy_lighting_render(thy_lighting *lighting, const thy_instrument *instrument,
    const thy_lighting_config *config, uint32_t now_ms,
    uint32_t rgb[THY_PAD_COUNT]);

#endif
