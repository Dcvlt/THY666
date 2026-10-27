/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_HARMONY_H
#define THY_HARMONY_H

#include <stddef.h>
#include <stdint.h>

#define THY_HARMONY_NO_ROOT UINT8_MAX
#define THY_HARMONY_NO_QUALITY UINT8_MAX
#define THY_HARMONY_TEXT_BYTES 32u

enum thy_harmony_result {
    THY_HARMONY_OK = 0, THY_HARMONY_INVALID, THY_HARMONY_TOO_SMALL
};
enum thy_harmony_kind {
    THY_HARMONY_EMPTY = 0, THY_HARMONY_NOTE, THY_HARMONY_CHORD,
    THY_HARMONY_UNKNOWN
};
enum thy_harmony_confidence {
    THY_HARMONY_CONFIDENCE_NONE = 0,
    /* One exact root/template match within this deliberately finite vocabulary. */
    THY_HARMONY_CONFIDENCE_EXACT,
    /* The caller's root selected an exact match, including an ambiguous set. */
    THY_HARMONY_CONFIDENCE_CONTEXT,
    /* Several exact names exist; a deterministic bass/rank tie break was used. */
    THY_HARMONY_CONFIDENCE_AMBIGUOUS
};
enum thy_harmony_flags {
    THY_HARMONY_ROOT_USED = 1u,
    THY_HARMONY_ROOT_REJECTED = 2u,
    THY_HARMONY_SLASH_BASS = 4u
};

/* Presentation only, never a persistence format. Notes count distinct MIDI
 * pitches; classes counts pitch classes. Quality IDs are private table indices
 * and must not be saved. They do not imply unique musical intent. All named
 * chords are EXACT pitch-class matches, never a nearest-major/minor guess.
 * The recognizer cannot recover absent authored roots, track provenance or
 * custom aliases. Callers retain those as separate source metadata.
 */
typedef struct thy_harmony {
    uint16_t pitch_classes;
    uint8_t notes, classes, lowest, highest;
    uint8_t root, bass, kind, confidence, quality, matches, flags;
} thy_harmony;

/* Input bit n denotes absolute MIDI pitch n. Preferred root is 0..11 or
 * NO_ROOT. A hint must itself be present and match a full template to win;
 * otherwise ROOT_REJECTED is reported and the ACTUAL notes are considered.
 * Empty sets and octave duplicates are valid. Output is untouched on error.
 * No heap, mutable global scratch, MIDI, time or owner mutation is used.
 */
int thy_harmony_recognize(const uint64_t pitches[2], uint8_t preferred_root,
    thy_harmony *output);

/* Adapters deduplicate before recognition. Notes must be 0..127. Keys use the
 * core's destination/channel/pitch encoding and must be < THY_KEY_COUNT.
 * Empty lists may use NULL. Caller owns the actual bounded list storage.
 */
int thy_harmony_from_notes(const int16_t *notes, size_t count,
    uint8_t preferred_root, thy_harmony *output);
int thy_harmony_from_keys(const uint16_t *keys, size_t count,
    uint8_t preferred_root, thy_harmony *output);

/* Examples: READY, C4, C OCTAVES, C MAJ7/E, C MIN, 5 NOTES.
 * Roots/basses use sharps consistently; MIN and MAJ avoid a case-only quality
 * distinction on small fonts. Alterations use ASCII b/#, no Unicode symbols.
 * The whole label plus NUL must fit; no silent truncation. On error output is
 * untouched. THY_HARMONY_TEXT_BYTES fits every canonical output of this module.
 */
int thy_harmony_format(const thy_harmony *, char *output, size_t capacity);
const char *thy_harmony_quality_name(uint8_t quality);

/* Read-only diagnostic sizes; no table storage is exposed or allocated. */
size_t thy_harmony_pattern_count(void);
size_t thy_harmony_table_bytes(void);

#endif
