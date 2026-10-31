/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_SETTINGS_TRANSFER_H
#define THY_SETTINGS_TRANSFER_H
#include "thy_settings.h"
#include "thy_restore_progress.h"

#define THY_BACKUP_HEADER_BYTES 64u
#define THY_BACKUP_PAYLOAD_MAX (THY_SETTINGS_V19_PARAMETER_BYTES + THY_SEQ_SONG_MAX)
#define THY_BACKUP_FILE_MAX (THY_BACKUP_HEADER_BYTES + THY_BACKUP_PAYLOAD_MAX)
#define THY_BACKUP_COMPATIBILITY UINT32_C(0x00010001)
enum thy_backup_state {
    THY_BACKUP_IDLE, THY_BACKUP_UPLOAD, THY_BACKUP_DOWNLOAD,
    THY_BACKUP_APPLIED, THY_BACKUP_QUEUED, THY_BACKUP_SAVED_VERIFIED, THY_BACKUP_ERROR
};
typedef struct thy_backup_identity {
    const char *uid; /* 24 hex digits, same presentation as USB serial. */
    const char *build; /* 16 hex digits; provenance, not an equal-build lock. */
    const char *schema; /* Full 64-hex menu hash; file retains first eight bytes. */
    uint16_t target;
    uint32_t compatibility;
} thy_backup_identity;
/* Caller performs nonmutating preflight, then atomically restores catalog,
 * settings, hardware presentation and input warmup. No notes/setup messages. */
typedef int (*thy_backup_apply_fn)(void *,const uint8_t *,size_t);
/* BACKUP/1 restores current-version payloads only. Guard-aware clients must
 * finish an exact saved-payload comparison with backup complete <decimalCRC>.
 * Older clients can transfer but cannot acknowledge that final guard; pair
 * guarded restore firmware with the matching Client. A client migrating an older
 * supported backup must first apply/encode it with the shared C codec off-device
 * and rebuild the versioned header and checksums; wire validation stays strict. */
typedef struct thy_settings_transfer {
    uint8_t *buffer; /* Shared journal payload buffer, never another large copy. */
    size_t capacity;
    thy_backup_identity identity;
    thy_backup_apply_fn apply;
    void *context;
    uint8_t header[THY_BACKUP_HEADER_BYTES];
    uint32_t bytes, received, crc, payload_crc, payload_bytes, last_ms, start_generation;
    uint8_t state;
    /* Restore ownership survives the verification export's separate staging
     * lease. These are runtime receipts, never serialized settings. */
    uint32_t restore_crc, restore_bytes, restore_generation;
    uint8_t restore_active, restore_applied, restore_verified, restore_host_verified;
    uint8_t restore_disconnected, restore_phase;
} thy_settings_transfer;

void thy_settings_transfer_init(thy_settings_transfer *,uint8_t *,size_t,
    const thy_backup_identity *,thy_backup_apply_fn,void *);
int thy_settings_transfer_busy(const thy_settings_transfer *);
int thy_settings_transfer_staging(const thy_settings_transfer *);
void thy_settings_transfer_progress(const thy_settings_transfer *,
    const thy_settings_journal *,thy_restore_progress *);
/* Disconnect releases transport staging. An already applied restore is not
 * undone; ordinary journal saving continues without a claimed verification. */
void thy_settings_transfer_disconnect(thy_settings_transfer *);
int thy_settings_transfer_expire(thy_settings_transfer *,uint32_t now);
void thy_settings_transfer_poll(thy_settings_transfer *,const thy_engine *,
    const thy_settings_journal *,int quiet,uint32_t now);
/* available excludes SONG/companion/journal writers. quiet also excludes
 * physical controls, drafts, music owners and transport queues. */
size_t thy_settings_transfer_command(thy_settings_transfer *,const thy_engine *,
    thy_settings_journal *,int available,int quiet,uint32_t now,
    const char *line,char *response,size_t capacity);
#endif
