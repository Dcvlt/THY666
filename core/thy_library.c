/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_library.h"
#include "thy_crc32.h"
#include <string.h>

/* Field offsets are part of the byte codec, never compiler padding. */
_Static_assert(sizeof(thy_library_scale) == 16u, "scale record must be 16 bytes");
_Static_assert(sizeof(thy_library_chord) == 23u, "chord record must be 23 bytes");
_Static_assert(sizeof(thy_library_six) == 20u, "six-set record must be 20 bytes");
_Static_assert(sizeof(thy_library) == THY_LIBRARY_PAYLOAD_BYTES, "library payload must be 740 bytes");
_Static_assert(offsetof(thy_library_scale, mask) == 13u &&
    offsetof(thy_library_scale, used) == 15u, "scale byte offsets");
_Static_assert(offsetof(thy_library_chord, root) == 13u &&
    offsetof(thy_library_chord, notes) == 14u &&
    offsetof(thy_library_chord, valid_mask) == 21u &&
    offsetof(thy_library_chord, used) == 22u, "chord byte offsets");
_Static_assert(offsetof(thy_library_six, chords) == 13u &&
    offsetof(thy_library_six, used) == 19u, "six-set byte offsets");
_Static_assert(offsetof(thy_library, chords) == 128u &&
    offsetof(thy_library, six) == 680u, "library byte offsets");

static int zero_bytes(const uint8_t *bytes, size_t count)
{
    for(size_t n = 0; n < count; ++n) if(bytes[n]) return 0;
    return 1;
}

static int valid_name(const uint8_t *name)
{
    int ended = 0, nonspace = 0;
    for(unsigned n = 0; n < THY_LIBRARY_NAME_BYTES; ++n) {
        unsigned value = name[n];
        if(ended) { if(value) return 0; }
        else if(!value) ended = 1;
        else {
            if(value < 32u || value > 126u) return 0;
            if(value != 32u) nonspace = 1;
        }
    }
    return ended && nonspace;
}

static int valid_scale_bytes(const uint8_t *record)
{
    if(!record[15]) return zero_bytes(record, 16u);
    uint16_t mask = (uint16_t)(record[13] | (uint16_t)record[14] << 8);
    return record[15] == 1u && valid_name(record) && (mask & 1u) && !(mask & 0xf000u);
}

static int valid_chord_bytes(const uint8_t *record)
{
    if(!record[22]) return zero_bytes(record, 23u);
    if(record[22] != 1u || !valid_name(record) || record[13] > 127u ||
       !record[21] || record[21] > 127u) return 0;
    for(unsigned n = 0; n < 7u; ++n) if(record[14u + n] > 127u) return 0;
    return 1;
}

static int valid_six_bytes(const uint8_t *record)
{
    if(!record[19]) return zero_bytes(record, 20u);
    if(record[19] != 1u || !valid_name(record)) return 0;
    for(unsigned n = 0; n < 6u; ++n)
        if(record[13u + n] >= THY_LIBRARY_CHORDS &&
           record[13u + n] != THY_LIBRARY_EMPTY_CHORD) return 0;
    return 1;
}

static int valid_payload(const uint8_t *bytes)
{
    for(unsigned n = 0; n < THY_LIBRARY_SCALES; ++n)
        if(!valid_scale_bytes(bytes + n * 16u)) return 0;
    for(unsigned n = 0; n < THY_LIBRARY_CHORDS; ++n)
        if(!valid_chord_bytes(bytes + 128u + n * 23u)) return 0;
    for(unsigned n = 0; n < THY_LIBRARY_SIX_SETS; ++n) {
        const uint8_t *set = bytes + 680u + n * 20u;
        if(!valid_six_bytes(set)) return 0;
        if(set[19]) for(unsigned k = 0; k < 6u; ++k) {
            unsigned chord = set[13u + k];
            if(chord != THY_LIBRARY_EMPTY_CHORD && !bytes[128u + chord * 23u + 22u]) return 0;
        }
    }
    return 1;
}

void thy_library_defaults(thy_library *library)
{
    if(library) memset(library, 0, sizeof *library);
}

int thy_library_valid(const thy_library *library)
{
    return library && valid_payload((const uint8_t *)library);
}

int thy_library_scale_valid(const thy_library_scale *scale)
{
    return scale && valid_scale_bytes((const uint8_t *)scale);
}

int thy_library_chord_valid(const thy_library_chord *chord)
{
    return chord && valid_chord_bytes((const uint8_t *)chord);
}

int thy_library_six_valid(const thy_library_six *set)
{
    return set && valid_six_bytes((const uint8_t *)set);
}

int thy_library_name_set(uint8_t name[THY_LIBRARY_NAME_BYTES], const char *text)
{
    if(!name || !text) return THY_LIBRARY_INVALID;
    uint8_t proposed[THY_LIBRARY_NAME_BYTES] = {0};
    unsigned n;
    for(n = 0; n < THY_LIBRARY_NAME_BYTES; ++n) {
        unsigned value = (unsigned char)text[n];
        if(!value) break;
        if(n == THY_LIBRARY_NAME_LENGTH || value < 32u || value > 126u)
            return THY_LIBRARY_INVALID;
        proposed[n] = (uint8_t)value;
    }
    if(n == THY_LIBRARY_NAME_BYTES || !valid_name(proposed)) return THY_LIBRARY_INVALID;
    memcpy(name, proposed, sizeof proposed);
    return THY_LIBRARY_OK;
}

uint16_t thy_library_scale_mask(const thy_library_scale *scale)
{
    return scale ? (uint16_t)(scale->mask[0] | (uint16_t)scale->mask[1] << 8) : 0u;
}

int thy_library_scale_set_mask(thy_library_scale *scale, uint16_t mask)
{
    if(!scale || !mask || !(mask & 1u) || (mask & 0xf000u)) return THY_LIBRARY_INVALID;
    scale->mask[0] = (uint8_t)mask;
    scale->mask[1] = (uint8_t)(mask >> 8);
    return THY_LIBRARY_OK;
}

int thy_library_scale_degrees(const thy_library_scale *scale, uint8_t degrees[12], uint8_t *count)
{
    if(!scale || !degrees || !count || !scale->used || !thy_library_scale_valid(scale))
        return THY_LIBRARY_INVALID;
    uint16_t mask = thy_library_scale_mask(scale);
    uint8_t values[12] = {0};
    uint8_t used = 0;
    for(unsigned n = 0; n < 12u; ++n) if(mask & (1u << n)) values[used++] = (uint8_t)n;
    memcpy(degrees, values, sizeof values);
    *count = used;
    return THY_LIBRARY_OK;
}

int thy_library_get_scale(const thy_library *library, uint8_t slot, thy_library_scale *out)
{
    if(!out || slot >= THY_LIBRARY_SCALES || !thy_library_valid(library)) return THY_LIBRARY_INVALID;
    memmove(out, &library->scales[slot], sizeof *out);
    return THY_LIBRARY_OK;
}

int thy_library_get_chord(const thy_library *library, uint8_t slot, thy_library_chord *out)
{
    if(!out || slot >= THY_LIBRARY_CHORDS || !thy_library_valid(library)) return THY_LIBRARY_INVALID;
    memmove(out, &library->chords[slot], sizeof *out);
    return THY_LIBRARY_OK;
}

int thy_library_get_six(const thy_library *library, uint8_t slot, thy_library_six *out)
{
    if(!out || slot >= THY_LIBRARY_SIX_SETS || !thy_library_valid(library)) return THY_LIBRARY_INVALID;
    memmove(out, &library->six[slot], sizeof *out);
    return THY_LIBRARY_OK;
}

int thy_library_chord_referenced(const thy_library *library, uint8_t slot)
{
    if(slot >= THY_LIBRARY_CHORDS || !thy_library_valid(library)) return 0;
    for(unsigned n = 0; n < THY_LIBRARY_SIX_SETS; ++n)
        if(library->six[n].used) for(unsigned k = 0; k < 6u; ++k)
            if(library->six[n].chords[k] == slot) return 1;
    return 0;
}

int thy_library_set_scale(thy_library *library, uint8_t slot, const thy_library_scale *scale)
{
    if(slot >= THY_LIBRARY_SCALES || !thy_library_valid(library) || !thy_library_scale_valid(scale))
        return THY_LIBRARY_INVALID;
    memmove(&library->scales[slot], scale, sizeof *scale);
    return THY_LIBRARY_OK;
}

int thy_library_set_chord(thy_library *library, uint8_t slot, const thy_library_chord *chord)
{
    if(slot >= THY_LIBRARY_CHORDS || !thy_library_valid(library) || !thy_library_chord_valid(chord))
        return THY_LIBRARY_INVALID;
    if(!chord->used && thy_library_chord_referenced(library, slot)) return THY_LIBRARY_IN_USE;
    memmove(&library->chords[slot], chord, sizeof *chord);
    return THY_LIBRARY_OK;
}

int thy_library_set_six(thy_library *library, uint8_t slot, const thy_library_six *set)
{
    if(slot >= THY_LIBRARY_SIX_SETS || !thy_library_valid(library) || !thy_library_six_valid(set))
        return THY_LIBRARY_INVALID;
    if(set->used) for(unsigned n = 0; n < 6u; ++n) {
        unsigned chord = set->chords[n];
        if(chord != THY_LIBRARY_EMPTY_CHORD && !library->chords[chord].used) return THY_LIBRARY_INVALID;
    }
    memmove(&library->six[slot], set, sizeof *set);
    return THY_LIBRARY_OK;
}

int thy_library_delete_scale(thy_library *library, uint8_t slot)
{
    const thy_library_scale empty = {0};
    return thy_library_set_scale(library, slot, &empty);
}

int thy_library_delete_chord(thy_library *library, uint8_t slot)
{
    const thy_library_chord empty = {0};
    return thy_library_set_chord(library, slot, &empty);
}

int thy_library_delete_six(thy_library *library, uint8_t slot)
{
    const thy_library_six empty = {0};
    return thy_library_set_six(library, slot, &empty);
}

uint32_t thy_library_crc32(const uint8_t *bytes, size_t count)
{
    if(!bytes && count) return 0u;
    return thy_crc32(bytes,count);
}

static int overlaps(const void *left, size_t left_count, const void *right, size_t right_count)
{
    uintptr_t a = (uintptr_t)left, b = (uintptr_t)right;
    if(left_count > UINTPTR_MAX - a || right_count > UINTPTR_MAX - b) return 1;
    return a < b + right_count && b < a + left_count;
}

/* Records are copied field-by-field so wire order remains explicit. Their
 * compile-time offsets permit an overlap-safe final import after validation. */
static void encode_payload(const thy_library *library, uint8_t *output)
{
    size_t at = 0;
    for(unsigned n = 0; n < THY_LIBRARY_SCALES; ++n) {
        const thy_library_scale *scale = &library->scales[n];
        for(unsigned k = 0; k < THY_LIBRARY_NAME_BYTES; ++k) output[at++] = scale->name[k];
        output[at++] = scale->mask[0]; output[at++] = scale->mask[1]; output[at++] = scale->used;
    }
    for(unsigned n = 0; n < THY_LIBRARY_CHORDS; ++n) {
        const thy_library_chord *chord = &library->chords[n];
        for(unsigned k = 0; k < THY_LIBRARY_NAME_BYTES; ++k) output[at++] = chord->name[k];
        output[at++] = chord->root;
        for(unsigned k = 0; k < 7u; ++k) output[at++] = chord->notes[k];
        output[at++] = chord->valid_mask; output[at++] = chord->used;
    }
    for(unsigned n = 0; n < THY_LIBRARY_SIX_SETS; ++n) {
        const thy_library_six *set = &library->six[n];
        for(unsigned k = 0; k < THY_LIBRARY_NAME_BYTES; ++k) output[at++] = set->name[k];
        for(unsigned k = 0; k < 6u; ++k) output[at++] = set->chords[k];
        output[at++] = set->used;
    }
}

int thy_library_payload_encode(const thy_library *library, uint8_t *output, size_t capacity)
{
    if(!output || capacity < THY_LIBRARY_PAYLOAD_BYTES || !thy_library_valid(library) ||
       overlaps(library, sizeof *library, output, THY_LIBRARY_PAYLOAD_BYTES))
        return THY_LIBRARY_INVALID;
    encode_payload(library, output);
    return THY_LIBRARY_OK;
}

int thy_library_payload_validate(const uint8_t *payload, size_t count)
{
    return payload && count == THY_LIBRARY_PAYLOAD_BYTES && valid_payload(payload);
}

int thy_library_payload_decode(thy_library *library, const uint8_t *payload, size_t count)
{
    if(!library || !thy_library_payload_validate(payload, count)) return THY_LIBRARY_INVALID;
    memmove(library, payload, THY_LIBRARY_PAYLOAD_BYTES);
    return THY_LIBRARY_OK;
}

int thy_library_encode(const thy_library *library, uint8_t *output, size_t capacity, size_t *used)
{
    if(!output || !used || capacity < THY_LIBRARY_FILE_BYTES || !thy_library_valid(library) ||
       overlaps(library, sizeof *library, output, THY_LIBRARY_FILE_BYTES) ||
       overlaps(library, sizeof *library, used, sizeof *used) ||
       overlaps(output, THY_LIBRARY_FILE_BYTES, used, sizeof *used)) return THY_LIBRARY_INVALID;
    encode_payload(library, output + THY_LIBRARY_HEADER_BYTES);
    uint32_t crc = thy_library_crc32(output + THY_LIBRARY_HEADER_BYTES, THY_LIBRARY_PAYLOAD_BYTES);
    const uint8_t header[THY_LIBRARY_HEADER_BYTES] = {
        'T','H','Y','L', THY_LIBRARY_VERSION, 0u,
        (uint8_t)THY_LIBRARY_PAYLOAD_BYTES, (uint8_t)(THY_LIBRARY_PAYLOAD_BYTES >> 8),
        (uint8_t)crc, (uint8_t)(crc >> 8), (uint8_t)(crc >> 16), (uint8_t)(crc >> 24),
        THY_LIBRARY_SCALES, THY_LIBRARY_CHORDS, THY_LIBRARY_SIX_SETS, 0u
    };
    memcpy(output, header, sizeof header);
    *used = THY_LIBRARY_FILE_BYTES;
    return THY_LIBRARY_OK;
}

int thy_library_validate(const uint8_t *file, size_t count)
{
    if(!file || count != THY_LIBRARY_FILE_BYTES || file[0] != 'T' || file[1] != 'H' ||
       file[2] != 'Y' || file[3] != 'L' || file[4] != THY_LIBRARY_VERSION || file[5] ||
       file[6] != (uint8_t)THY_LIBRARY_PAYLOAD_BYTES ||
       file[7] != (uint8_t)(THY_LIBRARY_PAYLOAD_BYTES >> 8) ||
       file[12] != THY_LIBRARY_SCALES || file[13] != THY_LIBRARY_CHORDS ||
       file[14] != THY_LIBRARY_SIX_SETS || file[15]) return 0;
    uint32_t crc = (uint32_t)file[8] | (uint32_t)file[9] << 8 |
        (uint32_t)file[10] << 16 | (uint32_t)file[11] << 24;
    return crc == thy_library_crc32(file + THY_LIBRARY_HEADER_BYTES, THY_LIBRARY_PAYLOAD_BYTES) &&
        valid_payload(file + THY_LIBRARY_HEADER_BYTES);
}

int thy_library_decode(thy_library *library, const uint8_t *file, size_t count)
{
    if(!library || !thy_library_validate(file, count)) return THY_LIBRARY_INVALID;
    /* Every field is uint8_t. All sizes/offsets above explicitly prove the
     * payload projection, permitting memmove without padding or endianness. */
    memmove(library, file + THY_LIBRARY_HEADER_BYTES, THY_LIBRARY_PAYLOAD_BYTES);
    return THY_LIBRARY_OK;
}
