/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_CHORD_LAYOUT_H
#define THY_CHORD_LAYOUT_H
#include "thy_core.h"

#define THY_CHORD_SLOTS 7u
#define THY_CHORD_SIX_TEMPLATES 6u
#define THY_CHORD_NO_DEGREE UINT8_MAX

enum thy_chord_layout {
    THY_CHORD_LAYOUT_CLASSIC=0,
    THY_CHORD_LAYOUT_CHROMATIC,
    THY_CHORD_LAYOUT_DIATONIC,
    THY_CHORD_LAYOUT_SIX_DEFAULT,
    THY_CHORD_LAYOUT_SIX_USER,
    THY_CHORD_LAYOUT_COUNT
};
enum thy_chord_quality {
    THY_CHORD_QUALITY_MAJOR=0,
    THY_CHORD_QUALITY_MINOR,
    THY_CHORD_QUALITY_DOMINANT,
    THY_CHORD_QUALITY_DIMINISHED,
    THY_CHORD_QUALITY_AUGMENTED,
    THY_CHORD_QUALITY_SUS2,
    THY_CHORD_QUALITY_SUS4,
    THY_CHORD_QUALITY_COUNT
};

/* Relative semitone slots; validity is explicit, never inferred from zero.
 * Inactive slots are retained and validated too. Duplicate intervals are legal;
 * the existing musical owner collapses duplicate final pitches on attack. */
typedef struct thy_chord_template {
    int16_t intervals[THY_CHORD_SLOTS];
    uint8_t valid_mask;
} thy_chord_template;

typedef struct thy_chord_cell {
    int16_t notes[THY_CHORD_SLOTS];
    /* Intended untransposed root; it can be outside MIDI on an empty cell. */
    int16_t root;
    uint8_t valid_mask;
    uint8_t degree; /* Scale index, or NO_DEGREE for chromatic roots. */
} thy_chord_cell;

int thy_chord_template_valid(const thy_chord_template *);
/* Major, Minor, Dominant7, Major7, Minor7, Diminished, in that order. */
int thy_chord_default_template(uint8_t index,thy_chord_template *out);

/* Pure future-press resolver. Logical IDs0..47 are column-major (column*4+row).
 * Chromatic: semitone columns, 3/4/5/6-tone rows from the selected quality.
 * Diatonic: scale-degree columns, rows stack 3/4/5/6 alternating scale tones.
 * Six: six quality columns in each block, degree index row*2+block (top first).
 * Six User requires all six supplied templates. Other layouts ignore them.
 * Classic remains the caller's lossless archive path and is rejected here.
 * Degrees must start at0, be strictly increasing0..11, and number1..12.
 * Generated tones outside MIDI are omitted by mask, never clamped. Template
 * slots retain their intended pitches even when inactive/outside MIDI. Empty
 * cells succeed. Output remains byte-for-byte unchanged on every rejection.
 * No instrument state, heap, mutable catalogue copy, or hardware dependency. */
int thy_chord_layout_resolve(uint8_t layout,uint8_t quality,uint8_t logical,
    int16_t anchor,const int16_t *degrees,uint8_t degree_count,
    const thy_chord_template six[THY_CHORD_SIX_TEMPLATES],thy_chord_cell *out);
/* One chosen six-set template, avoiding a six-template scratch copy per pad. */
int thy_chord_six_resolve(uint8_t logical,int16_t anchor,const int16_t *degrees,
    uint8_t degree_count,const thy_chord_template *,thy_chord_cell *out);

/* Distinct Piano geometry on right-side logical IDs0..47, column-major.
 * Rows0/1 form the upper black/white pair; rows2/3 the lower pair one octave
 * below. Columns are the twelve chromatic semitones C..B. Only black-key
 * columns C# D# F# G# A# play in rows0/2; only white-key columns play in rows1/3.
 * Each pair spans one octave, with complementary gaps:24 active pads total.
 * Anchor is floored to absolute C in its octave: root selection does not turn
 * natural keys into accidentals. Caller applies its explicit octave/transpose.
 * Gaps and out-of-MIDI keys succeed with active0. A gap's pitch is0; an omitted
 * key retains its intended signed pitch. Errors leave both outputs unchanged. */
int thy_chord_piano_resolve(uint8_t logical,int16_t anchor,int16_t *pitch,
    uint8_t *active);

#endif
