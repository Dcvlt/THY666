/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_LIBRARY_H
#define THY_LIBRARY_H

#include <stddef.h>
#include <stdint.h>

#define THY_LIBRARY_SCALES 8u
#define THY_LIBRARY_CHORDS 24u
#define THY_LIBRARY_SIX_SETS 3u
#define THY_LIBRARY_NAME_BYTES 13u
#define THY_LIBRARY_NAME_LENGTH 12u
#define THY_LIBRARY_EMPTY_CHORD 255u
#define THY_LIBRARY_VERSION 1u
#define THY_LIBRARY_PAYLOAD_BYTES 740u
#define THY_LIBRARY_HEADER_BYTES 16u
#define THY_LIBRARY_FILE_BYTES (THY_LIBRARY_HEADER_BYTES + THY_LIBRARY_PAYLOAD_BYTES)

enum thy_library_result {
    THY_LIBRARY_OK = 0,
    THY_LIBRARY_INVALID,
    THY_LIBRARY_IN_USE
};

/* Records contain only bytes. mask is an explicit little-endian 12-bit pitch
 * class mask. Degrees are canonical ascending semitones, including zero.
 * A used chord retains ALL seven note slots, including inactive nonzero slots;
 * valid_mask alone selects sounding notes, so MIDI zero is never a sentinel.
 * root is authored MIDI root metadata; layout generation owns transposition.
 * Unused records are entirely zero. Active names are printable ASCII with a
 * terminator and zero-filled suffix; at least one non-space character is used.
 */
typedef struct thy_library_scale {
    uint8_t name[THY_LIBRARY_NAME_BYTES];
    uint8_t mask[2];
    uint8_t used;
} thy_library_scale;

typedef struct thy_library_chord {
    uint8_t name[THY_LIBRARY_NAME_BYTES];
    uint8_t root;
    uint8_t notes[7];
    uint8_t valid_mask;
    uint8_t used;
} thy_library_chord;

typedef struct thy_library_six {
    uint8_t name[THY_LIBRARY_NAME_BYTES];
    uint8_t chords[6];
    uint8_t used;
} thy_library_six;

typedef struct thy_library {
    thy_library_scale scales[THY_LIBRARY_SCALES];
    thy_library_chord chords[THY_LIBRARY_CHORDS];
    thy_library_six six[THY_LIBRARY_SIX_SETS];
} thy_library;

void thy_library_defaults(thy_library *library);
int thy_library_valid(const thy_library *library);
int thy_library_scale_valid(const thy_library_scale *scale);
int thy_library_chord_valid(const thy_library_chord *chord);
int thy_library_six_valid(const thy_library_six *set);

/* This bounded helper never truncates or silently normalizes input. */
int thy_library_name_set(uint8_t name[THY_LIBRARY_NAME_BYTES], const char *text);
uint16_t thy_library_scale_mask(const thy_library_scale *scale);
int thy_library_scale_set_mask(thy_library_scale *scale, uint16_t mask);
/* Outputs remain unchanged on failure. Unused scales are not playable. */
int thy_library_scale_degrees(const thy_library_scale *scale, uint8_t degrees[12],
    uint8_t *count);

/* Gets include empty slots, identified by used == 0. All setters preflight the
 * complete existing library and the replacement before changing any byte.
 * Removing a chord referenced by an active six-set returns IN_USE. Selection
 * and shortcut references outside this object belong to the engine wrapper.
 */
int thy_library_get_scale(const thy_library *, uint8_t slot, thy_library_scale *);
int thy_library_get_chord(const thy_library *, uint8_t slot, thy_library_chord *);
int thy_library_get_six(const thy_library *, uint8_t slot, thy_library_six *);
int thy_library_set_scale(thy_library *, uint8_t slot, const thy_library_scale *);
int thy_library_set_chord(thy_library *, uint8_t slot, const thy_library_chord *);
int thy_library_set_six(thy_library *, uint8_t slot, const thy_library_six *);
int thy_library_delete_scale(thy_library *, uint8_t slot);
int thy_library_delete_chord(thy_library *, uint8_t slot);
int thy_library_delete_six(thy_library *, uint8_t slot);
int thy_library_chord_referenced(const thy_library *, uint8_t slot);

/* The file is exactly 756 bytes: THYL, version/flags, LE payload byte count,
 * reflected IEEE CRC32 of the payload, fixed capacities and reserved zero.
 * Every record/reference is validated before import; unknown versions fail.
 * encode writes exactly FILE_BYTES and leaves output/used untouched on failure.
 * Its source, output and used destinations must not overlap. decode permits
 * overlapping source/destination; separate input files remain unchanged.
 * No heap or whole-library automatic copy is used.
 */
uint32_t thy_library_crc32(const uint8_t *bytes, size_t count);
/* Headerless settings-tail projection. encode writes exactly 740 bytes;
 * validate/decode require exactly 740. The same atomicity/alias rules apply. */
int thy_library_payload_encode(const thy_library *, uint8_t *output, size_t capacity);
int thy_library_payload_validate(const uint8_t *payload, size_t count);
int thy_library_payload_decode(thy_library *, const uint8_t *payload, size_t count);
int thy_library_encode(const thy_library *, uint8_t *output, size_t capacity,
    size_t *used);
int thy_library_validate(const uint8_t *file, size_t count);
int thy_library_decode(thy_library *, const uint8_t *file, size_t count);

#endif
