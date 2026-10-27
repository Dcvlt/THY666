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


/* Advisory tools: absolute 12-bit pitch-class masks, never device mutation.
 * Scale family IDs are presentation-only and must not be persisted as catalog
 * IDs. Results are bounded by caller capacity (1..24), ranked deterministically.
 * Discovery matches contain ALL selected notes; no notes means no evidence.
 * On invalid input outputs/counts remain untouched. */
#define THY_GUIDANCE_LIMIT 24u
typedef struct thy_chord_suggestion {
    uint16_t mask;
    uint8_t root, shared, score;
} thy_chord_suggestion;
typedef struct thy_scale_match {
    uint16_t mask;
    uint8_t family, root, extra;
} thy_scale_match;
typedef struct thy_modulation {
    uint16_t common, added, removed, pivot;
    uint8_t pivot_root, current_fits;
} thy_modulation;
size_t thy_guidance_scale_count(void);
const char *thy_guidance_scale_name(unsigned family);
uint16_t thy_guidance_scale_mask(unsigned family,unsigned root);
int thy_guidance_chords(uint16_t key,uint8_t tonic,uint16_t current,
    thy_chord_suggestion *out,size_t capacity,size_t *count);
int thy_guidance_scales(uint16_t selected,thy_scale_match *out,size_t capacity,
    size_t *count,size_t *total);
int thy_guidance_modulation(uint16_t source,uint16_t target,uint16_t current,
    thy_modulation *out);

/* Transient board browser. Cached results are recomputed only on navigation,
 * never in the render/audio path. No settings, library slots or notes change. */
typedef struct thy_guidance_ui {
    union { thy_chord_suggestion chords[24]; thy_scale_match scales[24]; } rows;
    thy_modulation modulation;
    uint16_t key, notes, total;
    uint8_t open, page, cursor, count, tonic, target_root, target_family, editing;
} thy_guidance_ui;
void thy_guidance_ui_refresh(thy_guidance_ui *ui);
unsigned thy_guidance_ui_count(const thy_guidance_ui *ui);
void thy_guidance_ui_navigate(thy_guidance_ui *ui, uint8_t action);
void thy_guidance_ui_line(const thy_guidance_ui *ui, unsigned row, char out[32]);
void thy_guidance_notes_text(uint16_t mask, char out[32]);

#endif
