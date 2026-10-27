/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_SETTINGS_H
#define THY_SETTINGS_H
#include "thy_engine.h"

#define THY_SETTINGS_VERSION 15u
/* The v1 codec had no embedded version. Its exact length remains an import
 * contract; v2 has an in-band marker after the three catalog bytes, so a torn
 * v2 export cannot be mistaken for a complete legacy export. */
#define THY_SETTINGS_V1_PAYLOAD_BYTES 2103u
#define THY_SETTINGS_V2_PAYLOAD_BYTES (THY_SETTINGS_V1_PAYLOAD_BYTES + 54u)
#define THY_SETTINGS_V3_PAYLOAD_BYTES (THY_SETTINGS_V2_PAYLOAD_BYTES + 60u)
#define THY_SETTINGS_V4_PAYLOAD_BYTES (THY_SETTINGS_V3_PAYLOAD_BYTES + 93u)
#define THY_SETTINGS_V5_PAYLOAD_BYTES (THY_SETTINGS_V4_PAYLOAD_BYTES + 10u)
#define THY_SETTINGS_V6_PARAMETER_BYTES (THY_SETTINGS_V5_PAYLOAD_BYTES + 25u)
#define THY_SETTINGS_V7_PARAMETER_BYTES (THY_SETTINGS_V6_PARAMETER_BYTES + 8u + THY_PAD_COUNT * 9u)
#define THY_SETTINGS_V8_PARAMETER_BYTES (THY_SETTINGS_V7_PARAMETER_BYTES + (THY_PAD_COUNT + 1u) * 3u)
#define THY_SETTINGS_V9_PARAMETER_BYTES (THY_SETTINGS_V8_PARAMETER_BYTES + 2u * (9u + 8u + 3u) + THY_PAD_COUNT * 2u)
#define THY_SETTINGS_V10_PARAMETER_BYTES (THY_SETTINGS_V9_PARAMETER_BYTES + 8u)
#define THY_SETTINGS_V11_PARAMETER_BYTES (THY_SETTINGS_V10_PARAMETER_BYTES + THY_INSTRUMENT_ROUTE_TARGETS * 3u + 2u * 4u + THY_PAD_COUNT * 5u)
#define THY_SETTINGS_V12_POLICY_BYTES 10u
#define THY_SETTINGS_V12_LIBRARY_OFFSET (THY_SETTINGS_V11_PARAMETER_BYTES + THY_SETTINGS_V12_POLICY_BYTES)
#define THY_SETTINGS_V12_PARAMETER_BYTES (THY_SETTINGS_V12_LIBRARY_OFFSET + THY_LIBRARY_PAYLOAD_BYTES)
/* V13 extends saved view-info identities only. All byte offsets stay frozen. */
#define THY_SETTINGS_V13_PARAMETER_BYTES THY_SETTINGS_V12_PARAMETER_BYTES
/* V14 adds Auto leading to the voicing policy; byte offsets stay frozen. */
#define THY_SETTINGS_V14_PARAMETER_BYTES THY_SETTINGS_V13_PARAMETER_BYTES
/* V15 adds octave fold and guitar register; all offsets stay frozen. */
#define THY_SETTINGS_V15_PARAMETER_BYTES THY_SETTINGS_V14_PARAMETER_BYTES
/* V6+ embed the independently versioned song codec after their own parameter bytes.
 * Scan remains at legacy 4 KiB boundaries; new records reserve four slots. */
#define THY_SETTINGS_PAYLOAD_MAX ((THY_SETTINGS_V13_PARAMETER_BYTES + THY_SEQ_SONG_MAX + 3u) & ~3u)
#define THY_SETTINGS_WRITE_SLOT_SPAN 4u
#define THY_SETTINGS_HEADER_BYTES 32u
#define THY_SETTINGS_RECORD_BYTES (THY_SETTINGS_HEADER_BYTES + THY_SETTINGS_PAYLOAD_MAX)
#define THY_SETTINGS_SLOT_BYTES 4096u
#define THY_SETTINGS_SECTOR_BYTES 131072u
#define THY_SETTINGS_SLOTS_PER_SECTOR (THY_SETTINGS_SECTOR_BYTES / THY_SETTINGS_SLOT_BYTES)
#define THY_SETTINGS_SLOT_COUNT (2u * THY_SETTINGS_SLOTS_PER_SECTOR)
#define THY_SETTINGS_NONE UINT32_MAX

/* Explicit versioned value format. Never serializes instrument/state structs,
 * callbacks, pointers, live note owners, detector baselines, or UI drafts. */
int thy_settings_encode(const thy_engine *, uint8_t *, size_t, size_t *);
/* Structural discriminator only. Always validate before accepting values. */
int thy_settings_payload_version(const uint8_t *, size_t);
int thy_settings_validate(const uint8_t *, size_t, uint16_t scale_count);
/* Full nonmutating live-restore preflight, including quiet ownership and mapped
 * expression dependencies. Caller must disable input before this check. */
int thy_settings_can_apply(const thy_engine *, const uint8_t *, size_t);
/* Restore-only: requires input disabled, no held inputs and an empty musical core.
 * Load the selected catalog layout first, then apply (set_layout clears slots).
 * Invalid input leaves the entire instrument and engine unchanged. Configuration
 * restores without held Arp inputs/outputs, transport playback, capture, or time
 * position. Existing musical hooks survive. Source generations advance rather
 * than revalidating stale callbacks if this is an idle browser import. */
int thy_settings_apply(thy_engine *, const uint8_t *, size_t);
int thy_settings_catalog(const uint8_t *, size_t, uint16_t *, uint8_t *);
uint32_t thy_settings_crc32(const uint8_t *, size_t);

typedef struct thy_settings_io {
    void *context;
    int (*read)(void *, uint32_t offset, uint8_t *, size_t);
    int (*program)(void *, uint32_t offset, const uint8_t word[4]);
    int (*erase)(void *, unsigned sector); /* Only relative sector 0 or 1. */
} thy_settings_io;

enum thy_settings_status {
    THY_SETTINGS_DEFAULTS, THY_SETTINGS_SAVED, THY_SETTINGS_PENDING,
    THY_SETTINGS_SAVING, THY_SETTINGS_ERROR, THY_SETTINGS_UNSUPPORTED
};
typedef struct thy_settings_journal {
    thy_settings_io io;
    uint8_t record[THY_SETTINGS_RECORD_BYTES];
    uint32_t newest_slot, generation, saved_crc, observed_crc, changed_ms;
    uint32_t write_slot, write_offset, write_bytes, saves, errors, rejected;
    uint16_t payload_bytes;
    uint8_t status, loaded, recovered, observed, writing, erase_pending;
    uint8_t loaded_version, migration_pending;
} thy_settings_journal;

/* Read-only scan. Chooses newest committed CRC/semantic-valid record. A torn
 * newer record is ignored and reported as recovery. Unknown versions block
 * automatic writes, so older firmware cannot silently erase newer settings.
 * Legacy v1-v12 records load without writes and set migration_pending; the
 * normal debounced idle task commits the current version without erasing that record. Firmware
 * predating the larger v6 allocation cannot guarantee downgrade preservation. */
int thy_settings_journal_init(thy_settings_journal *, const thy_settings_io *, uint16_t scale_count);
/* Capture committed configuration, debounce 1.5s, wait for caller's safe idle
 * window, then at most one verified word/erase per call. Last commit is always
 * programmed after the entire record; the previous record is never erased. */
void thy_settings_journal_task(thy_settings_journal *, const thy_engine *, uint32_t now_ms, int idle);
/* Explicit recall replaces only song content/routes, preserving live owners
 * and current instrument parameters. Refuses while a journal write is active. */
int thy_settings_journal_recall_song(thy_settings_journal *, thy_engine *);
#endif
