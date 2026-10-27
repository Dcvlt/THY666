/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "thy_library.h"

static thy_library library, before, decoded;
static uint8_t file[THY_LIBRARY_FILE_BYTES + 16u], saved_file[sizeof file];
static union { max_align_t alignment; uint8_t bytes[THY_LIBRARY_FILE_BYTES * 3u]; } alias_space;
static unsigned assertions;
#define CHECK(x) do { ++assertions; if(!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 0; } } while(0)

static int name(uint8_t output[THY_LIBRARY_NAME_BYTES], const char *prefix, unsigned slot)
{
    char text[THY_LIBRARY_NAME_BYTES];
    (void)snprintf(text, sizeof text, "%s%u", prefix, slot + 1u);
    CHECK(thy_library_name_set(output, text) == THY_LIBRARY_OK);
    return 1;
}

static int fixture(void)
{
    thy_library_defaults(&library);
    for(unsigned n = 0; n < THY_LIBRARY_SCALES; ++n) {
        thy_library_scale scale = {0}; scale.used = 1u;
        CHECK(name(scale.name, "Scale", n));
        CHECK(thy_library_scale_set_mask(&scale, (uint16_t)(1u | (1u << n))) == THY_LIBRARY_OK);
        CHECK(thy_library_set_scale(&library, (uint8_t)n, &scale) == THY_LIBRARY_OK);
    }
    for(unsigned n = 0; n < THY_LIBRARY_CHORDS; ++n) {
        thy_library_chord chord = {0}; chord.used = 1u; chord.root = (uint8_t)(n == 23u ? 127u : 0u);
        CHECK(name(chord.name, "Chord", n));
        for(unsigned k = 0; k < 7u; ++k) chord.notes[k] = (uint8_t)((n * 7u + k * 17u) & 127u);
        chord.valid_mask = (uint8_t)(1u | (n << 1));
        CHECK(thy_library_set_chord(&library, (uint8_t)n, &chord) == THY_LIBRARY_OK);
    }
    for(unsigned n = 0; n < THY_LIBRARY_SIX_SETS; ++n) {
        thy_library_six set = {0}; set.used = 1u;
        CHECK(name(set.name, "Set", n));
        for(unsigned k = 0; k < 6u; ++k) set.chords[k] = (uint8_t)(n * 6u + k);
        set.chords[5] = n ? THY_LIBRARY_EMPTY_CHORD : 23u;
        CHECK(thy_library_set_six(&library, (uint8_t)n, &set) == THY_LIBRARY_OK);
    }
    CHECK(thy_library_valid(&library));
    return 1;
}

static int defaults_and_names(void)
{
    CHECK(sizeof(thy_library_scale) == 16u && sizeof(thy_library_chord) == 23u);
    CHECK(sizeof(thy_library_six) == 20u && sizeof(thy_library) == 740u);
    memset(&library, 0xff, sizeof library); thy_library_defaults(&library);
    CHECK(thy_library_valid(&library));
    for(unsigned n = 0; n < sizeof library; ++n) CHECK(((uint8_t *)&library)[n] == 0u);
    CHECK(!thy_library_valid(NULL)); thy_library_defaults(NULL);
    thy_library_scale scale;
    CHECK(thy_library_get_scale(&library, 7u, &scale) == THY_LIBRARY_OK && !scale.used);
    memset(scale.name, 0xa5, sizeof scale.name);
    uint8_t old[THY_LIBRARY_NAME_BYTES]; memcpy(old, scale.name, sizeof old);
    const char *bad[] = {"", "   ", "ABCDEFGHIJKLM", "a\tb", "a\177", "a\200"};
    for(unsigned n = 0; n < sizeof bad / sizeof bad[0]; ++n) {
        CHECK(thy_library_name_set(scale.name, bad[n]) == THY_LIBRARY_INVALID);
        CHECK(!memcmp(scale.name, old, sizeof old));
    }
    CHECK(thy_library_name_set(NULL, "One") == THY_LIBRARY_INVALID);
    CHECK(thy_library_name_set(scale.name, NULL) == THY_LIBRARY_INVALID);
    CHECK(thy_library_name_set(scale.name, "ABCDEFGHIJKL") == THY_LIBRARY_OK);
    CHECK(scale.name[12] == 0u && !memcmp(scale.name, "ABCDEFGHIJKL", 12u));
    CHECK(thy_library_name_set(scale.name, " Dm #11 ") == THY_LIBRARY_OK);
    CHECK(!memcmp(scale.name, " Dm #11 ", 8u));
    for(unsigned n = 8u; n < sizeof scale.name; ++n) CHECK(scale.name[n] == 0u);
    CHECK(thy_library_name_set(scale.name, (const char *)scale.name + 1u) == THY_LIBRARY_OK);
    CHECK(!strcmp((const char *)scale.name, "Dm #11 "));
    return 1;
}

static int scale_masks_and_degrees(void)
{
    thy_library_scale scale = {0}; scale.used = 1u;
    CHECK(thy_library_name_set(scale.name, "Degrees") == THY_LIBRARY_OK);
    for(unsigned mask = 0u; mask <= 4095u; ++mask) {
        thy_library_scale original = scale;
        int result = thy_library_scale_set_mask(&scale, (uint16_t)mask);
        CHECK(result == ((mask & 1u) ? THY_LIBRARY_OK : THY_LIBRARY_INVALID));
        if(!(mask & 1u)) { CHECK(!memcmp(&scale, &original, sizeof scale)); continue; }
        CHECK(thy_library_scale_valid(&scale));
        CHECK(thy_library_scale_mask(&scale) == mask);
        uint8_t degrees[12], count = 0xffu;
        memset(degrees, 0xff, sizeof degrees);
        CHECK(thy_library_scale_degrees(&scale, degrees, &count) == THY_LIBRARY_OK);
        unsigned expected_count = 0u;
        for(unsigned n = 0u; n < 12u; ++n) if(mask & (1u << n)) CHECK(degrees[expected_count++] == n);
        CHECK(count == expected_count);
        for(unsigned n = count; n < 12u; ++n) CHECK(degrees[n] == 0u);
    }
    CHECK(!thy_library_scale_mask(NULL));
    thy_library_scale original = scale;
    CHECK(thy_library_scale_set_mask(&scale, 0x1001u) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&scale, &original, sizeof scale));
    uint8_t degrees[12]; memset(degrees, 0xa5, sizeof degrees); uint8_t count = 99u;
    scale.used = 0u;
    CHECK(thy_library_scale_degrees(&scale, degrees, &count) == THY_LIBRARY_INVALID);
    CHECK(count == 99u); for(unsigned n = 0; n < 12u; ++n) CHECK(degrees[n] == 0xa5u);
    return 1;
}

static int records_references_and_atomic_mutation(void)
{
    CHECK(fixture()); before = library;
    thy_library_chord chord;
    CHECK(thy_library_get_chord(&library, 0u, &chord) == THY_LIBRARY_OK);
    CHECK(chord.notes[0] == 0u && chord.root == 0u && chord.valid_mask == 1u);
    CHECK(chord.notes[6] != 0u); /* An inactive original slot still carries data. */
    CHECK(thy_library_set_chord(&library, 20u, &library.chords[0]) == THY_LIBRARY_OK);
    CHECK(!memcmp(&library.chords[20], &chord, sizeof chord));
    library = before;
    CHECK(thy_library_chord_referenced(&library, 23u));
    CHECK(!thy_library_chord_referenced(&library, 22u));
    CHECK(thy_library_delete_chord(&library, 23u) == THY_LIBRARY_IN_USE);
    CHECK(!memcmp(&library, &before, sizeof library));
    const thy_library_chord empty = {0};
    CHECK(thy_library_set_chord(&library, 23u, &empty) == THY_LIBRARY_IN_USE);
    CHECK(!memcmp(&library, &before, sizeof library));
    chord.valid_mask = 0u;
    CHECK(thy_library_set_chord(&library, 0u, &chord) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&library, &before, sizeof library));
    chord = library.chords[0]; chord.notes[6] = 128u;
    CHECK(thy_library_set_chord(&library, 0u, &chord) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&library, &before, sizeof library));
    CHECK(thy_library_delete_chord(&library, 22u) == THY_LIBRARY_OK);
    before = library;
    thy_library_six set = library.six[0]; set.chords[0] = 22u;
    CHECK(thy_library_six_valid(&set));
    CHECK(thy_library_set_six(&library, 0u, &set) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&library, &before, sizeof library));
    CHECK(thy_library_delete_six(&library, 0u) == THY_LIBRARY_OK);
    CHECK(thy_library_delete_chord(&library, 23u) == THY_LIBRARY_OK);
    CHECK(thy_library_delete_chord(&library, 23u) == THY_LIBRARY_OK);
    CHECK(!library.chords[23].used && thy_library_valid(&library));
    set = (thy_library_six){0}; set.used = 1u;
    CHECK(thy_library_name_set(set.name, "Empty set") == THY_LIBRARY_OK);
    memset(set.chords, THY_LIBRARY_EMPTY_CHORD, sizeof set.chords);
    CHECK(thy_library_set_six(&library, 0u, &set) == THY_LIBRARY_OK);
    CHECK(thy_library_delete_scale(&library, 7u) == THY_LIBRARY_OK && !library.scales[7].used);
    before = library;
    thy_library_scale scale; memset(&scale, 0xa5, sizeof scale);
    CHECK(thy_library_get_scale(&library, 8u, &scale) == THY_LIBRARY_INVALID);
    CHECK(((uint8_t *)&scale)[0] == 0xa5u);
    CHECK(thy_library_set_chord(&library, 24u, &empty) == THY_LIBRARY_INVALID);
    CHECK(thy_library_delete_six(&library, 3u) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&library, &before, sizeof library));
    library.scales[7].name[0] = 'X'; before = library;
    CHECK(!thy_library_valid(&library));
    CHECK(thy_library_delete_scale(&library, 7u) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&library, &before, sizeof library));
    return 1;
}

static void repair_crc(uint8_t *bytes)
{
    uint32_t crc = thy_library_crc32(bytes + THY_LIBRARY_HEADER_BYTES, THY_LIBRARY_PAYLOAD_BYTES);
    for(unsigned n = 0; n < 4u; ++n) bytes[8u + n] = (uint8_t)(crc >> (8u * n));
}

static int file_roundtrip_and_exact_wire_format(void)
{
    CHECK(fixture()); before = library;
    memset(file, 0xa5, sizeof file); size_t used = 123u;
    CHECK(thy_library_encode(&library, file, sizeof file, &used) == THY_LIBRARY_OK);
    CHECK(used == 756u && !memcmp(&library, &before, sizeof library));
    CHECK(!memcmp(file, "THYL", 4u) && file[4] == 1u && !file[5]);
    CHECK(file[6] == 0xe4u && file[7] == 2u);
    CHECK(file[12] == 8u && file[13] == 24u && file[14] == 3u && !file[15]);
    CHECK(!memcmp(file + 16u, library.scales[0].name, 13u));
    CHECK(file[16u + 128u + 14u] == 0u && file[16u + 128u + 21u] == 1u);
    CHECK(file[16u + 128u + 20u] == library.chords[0].notes[6]);
    for(unsigned n = THY_LIBRARY_FILE_BYTES; n < sizeof file; ++n) CHECK(file[n] == 0xa5u);
    CHECK(thy_library_validate(file, THY_LIBRARY_FILE_BYTES));
    memcpy(saved_file, file, sizeof file);
    memset(&decoded, 0xa5, sizeof decoded);
    CHECK(thy_library_decode(&decoded, file, THY_LIBRARY_FILE_BYTES) == THY_LIBRARY_OK);
    CHECK(!memcmp(&library, &decoded, sizeof library));
    CHECK(!memcmp(saved_file, file, sizeof file));
    CHECK(thy_library_crc32((const uint8_t *)"123456789", 9u) == UINT32_C(0xcbf43926));
    CHECK(thy_library_crc32(NULL, 0u) == 0u && thy_library_crc32(NULL, 1u) == 0u);
    return 1;
}

static int invalid_files_and_atomic_decode(void)
{
    CHECK(file_roundtrip_and_exact_wire_format()); before = decoded;
    for(size_t count = 0; count < THY_LIBRARY_FILE_BYTES; ++count) {
        CHECK(!thy_library_validate(file, count));
        CHECK(thy_library_decode(&decoded, file, count) == THY_LIBRARY_INVALID);
        CHECK(!memcmp(&decoded, &before, sizeof decoded));
    }
    CHECK(!thy_library_validate(file, THY_LIBRARY_FILE_BYTES + 1u));
    CHECK(!thy_library_validate(file, SIZE_MAX));
    CHECK(!thy_library_validate(NULL, THY_LIBRARY_FILE_BYTES));
    CHECK(thy_library_decode(NULL, file, THY_LIBRARY_FILE_BYTES) == THY_LIBRARY_INVALID);
    for(unsigned n = 0; n < THY_LIBRARY_FILE_BYTES; ++n) for(unsigned bit = 0; bit < 8u; ++bit) {
        file[n] ^= (uint8_t)(1u << bit);
        CHECK(!thy_library_validate(file, THY_LIBRARY_FILE_BYTES));
        CHECK(thy_library_decode(&decoded, file, THY_LIBRARY_FILE_BYTES) == THY_LIBRARY_INVALID);
        CHECK(!memcmp(&decoded, &before, sizeof decoded));
        file[n] ^= (uint8_t)(1u << bit);
    }
    CHECK(!memcmp(file, saved_file, sizeof file));
    const struct { unsigned offset; uint8_t value; } malformed[] = {
        {16u + 15u, 2u}, /* Scale used flag. */
        {16u + 13u, 2u}, /* Scale omits root degree. */
        {16u + 14u, 0x10u}, /* Scale mask exceeds octave. */
        {16u + 12u, 'X'}, /* Unterminated maximum-length name. */
        {16u + 7u, 'X'}, /* Nonzero bytes after the name's terminator. */
        {16u + 128u + 13u, 128u}, /* MIDI root. */
        {16u + 128u + 20u, 128u}, /* Inactive note slot still must be MIDI. */
        {16u + 128u + 21u, 0u}, /* Empty active chord mask. */
        {16u + 128u + 21u, 128u}, /* Eighth validity bit. */
        {16u + 128u + 22u, 2u}, /* Chord used flag. */
        {16u + 680u + 13u, 24u}, /* Undefined six-set reference. */
        {16u + 680u + 19u, 2u}, /* Six-set used flag. */
    };
    for(unsigned n = 0; n < sizeof malformed / sizeof malformed[0]; ++n) {
        memcpy(file, saved_file, sizeof file); file[malformed[n].offset] = malformed[n].value; repair_crc(file);
        CHECK(!thy_library_validate(file, THY_LIBRARY_FILE_BYTES));
        CHECK(thy_library_decode(&decoded, file, THY_LIBRARY_FILE_BYTES) == THY_LIBRARY_INVALID);
        CHECK(!memcmp(&decoded, &before, sizeof decoded));
    }
    memcpy(file, saved_file, sizeof file);
    memset(file + 16u + 128u + 23u * 23u, 0u, 23u); repair_crc(file);
    CHECK(!thy_library_validate(file, THY_LIBRARY_FILE_BYTES)); /* Referenced chord now unused. */
    CHECK(thy_library_decode(&decoded, file, THY_LIBRARY_FILE_BYTES) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&decoded, &before, sizeof decoded));
    thy_library_defaults(&library); size_t used;
    CHECK(thy_library_encode(&library, file, sizeof file, &used) == THY_LIBRARY_OK);
    file[16u + 128u + 13u] = 60u; repair_crc(file);
    CHECK(!thy_library_validate(file, THY_LIBRARY_FILE_BYTES)); /* Unused records are canonical zero. */
    return 1;
}

static int encode_rejection_and_overlapping_decode(void)
{
    CHECK(fixture()); before = library;
    memset(file, 0xa5, sizeof file); memcpy(saved_file, file, sizeof file);
    size_t used = 123u;
    CHECK(thy_library_encode(&library, file, THY_LIBRARY_FILE_BYTES - 1u, &used) == THY_LIBRARY_INVALID);
    CHECK(used == 123u && !memcmp(file, saved_file, sizeof file));
    CHECK(thy_library_encode(&library, NULL, sizeof file, &used) == THY_LIBRARY_INVALID);
    CHECK(thy_library_encode(&library, file, sizeof file, NULL) == THY_LIBRARY_INVALID);
    CHECK(thy_library_encode(NULL, file, sizeof file, &used) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(file, saved_file, sizeof file));
    CHECK(thy_library_encode(&library, (uint8_t *)&library, THY_LIBRARY_FILE_BYTES, &used) == THY_LIBRARY_INVALID);
    CHECK(used == 123u && !memcmp(&library, &before, sizeof library));
    CHECK(thy_library_encode(&library, alias_space.bytes, THY_LIBRARY_FILE_BYTES,
        (size_t *)(void *)alias_space.bytes) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&library, &before, sizeof library));
    library.chords[0].used = 2u;
    CHECK(thy_library_encode(&library, file, sizeof file, &used) == THY_LIBRARY_INVALID);
    CHECK(used == 123u && !memcmp(file, saved_file, sizeof file));
    library = before;
    CHECK(thy_library_encode(&library, file, sizeof file, &used) == THY_LIBRARY_OK);
    memcpy(saved_file, file, sizeof file);
    const unsigned input_at[] = {8u, 8u, 8u};
    const unsigned output_at[] = {0u, 24u, 40u};
    for(unsigned n = 0; n < 3u; ++n) {
        memset(alias_space.bytes, 0xa5, sizeof alias_space.bytes);
        memcpy(alias_space.bytes + input_at[n], file, THY_LIBRARY_FILE_BYTES);
        thy_library *out = (thy_library *)(void *)(alias_space.bytes + output_at[n]);
        CHECK(thy_library_decode(out, alias_space.bytes + input_at[n], THY_LIBRARY_FILE_BYTES) == THY_LIBRARY_OK);
        CHECK(!memcmp(out, &library, sizeof library));
    }
    CHECK(!memcmp(saved_file, file, sizeof file));
    CHECK(!memcmp(&library, &before, sizeof library));
    return 1;
}

static int headerless_settings_payload(void)
{
    CHECK(fixture()); before = library;
    memset(file, 0xa5, sizeof file); memcpy(saved_file, file, sizeof file);
    CHECK(thy_library_payload_encode(&library, file, 739u) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(file, saved_file, sizeof file));
    CHECK(thy_library_payload_encode(&library, file, sizeof file) == THY_LIBRARY_OK);
    for(unsigned n = THY_LIBRARY_PAYLOAD_BYTES; n < sizeof file; ++n) CHECK(file[n] == 0xa5u);
    CHECK(thy_library_payload_validate(file, THY_LIBRARY_PAYLOAD_BYTES));
    CHECK(!thy_library_payload_validate(file, THY_LIBRARY_PAYLOAD_BYTES - 1u));
    CHECK(!thy_library_payload_validate(file, THY_LIBRARY_PAYLOAD_BYTES + 1u));
    CHECK(!thy_library_payload_validate(NULL, THY_LIBRARY_PAYLOAD_BYTES));
    thy_library_defaults(&decoded);
    CHECK(thy_library_payload_decode(&decoded, file, THY_LIBRARY_PAYLOAD_BYTES) == THY_LIBRARY_OK);
    CHECK(!memcmp(&decoded, &before, sizeof decoded));
    CHECK(!memcmp(&library, &before, sizeof library));
    CHECK(thy_library_payload_encode(&library, (uint8_t *)&library, THY_LIBRARY_PAYLOAD_BYTES) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&library, &before, sizeof library));
    file[15] = 2u;
    CHECK(!thy_library_payload_validate(file, THY_LIBRARY_PAYLOAD_BYTES));
    CHECK(thy_library_payload_decode(&decoded, file, THY_LIBRARY_PAYLOAD_BYTES) == THY_LIBRARY_INVALID);
    CHECK(!memcmp(&decoded, &before, sizeof decoded));
    CHECK(thy_library_payload_encode(&library, file, sizeof file) == THY_LIBRARY_OK);
    memcpy(alias_space.bytes, file, THY_LIBRARY_PAYLOAD_BYTES);
    CHECK(thy_library_payload_decode((thy_library *)(void *)(alias_space.bytes + 8u),
        alias_space.bytes, THY_LIBRARY_PAYLOAD_BYTES) == THY_LIBRARY_OK);
    CHECK(!memcmp(alias_space.bytes + 8u, &library, sizeof library));
    return 1;
}

int main(void)
{
    int (*tests[])(void) = { defaults_and_names, scale_masks_and_degrees,
        records_references_and_atomic_mutation, file_roundtrip_and_exact_wire_format,
        invalid_files_and_atomic_decode, encode_rejection_and_overlapping_decode,
        headerless_settings_payload };
    for(unsigned n = 0; n < sizeof tests / sizeof tests[0]; ++n) if(!tests[n]()) return 1;
    printf("Library: %u scenarios passed (%u assertions; %zu-byte library, %u-byte file)\n",
        (unsigned)(sizeof tests / sizeof tests[0]), assertions, sizeof(thy_library), THY_LIBRARY_FILE_BYTES);
    return 0;
}
