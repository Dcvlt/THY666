/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_INSTRUMENT_TOUCH_CONFIG_H
#define THY_INSTRUMENT_TOUCH_CONFIG_H
#include <stddef.h>
#include <stdint.h>

#define THY_TOUCH_BANKS 4u
#define THY_TOUCH_CONFIG_BYTES 128u
#define THY_TOUCH_PAGE_BYTES 16u
#define THY_TOUCH_DEFAULT_CENTER_POLL_MS 8u
#define THY_TOUCH_TOP_POLL_MS 20u

enum thy_touch_capture_state {
    THY_TOUCH_CAPTURE_EMPTY = 0,
    THY_TOUCH_CAPTURE_PENDING,
    THY_TOUCH_CAPTURE_READING,
    THY_TOUCH_CAPTURE_COMPLETE,
    THY_TOUCH_CAPTURE_FAILED
};

typedef struct {
    uint8_t bytes[THY_TOUCH_CONFIG_BYTES];
    uint8_t valid[THY_TOUCH_CONFIG_BYTES / 8u];
    uint32_t state, started_ms, completed_ms, attempts;
    int32_t result;
    uint32_t capture_crc32; /* Valid only when COMPLETE; provenance checksum. */
    uint16_t device_id, stored_crc16;
    uint8_t address, family_id, device_rev, stored_crc_valid;
    uint8_t phase, offset, retries, failed_register;
} thy_touch_snapshot;

typedef struct {
    thy_touch_snapshot bank[THY_TOUCH_BANKS];
    uint32_t owner_uid[3];
    uint32_t due_ms;
    uint8_t cursor;
} thy_touch_capture;

/* Callback sends only a register pointer followed by a two-byte read. There
 * is deliberately no write-value, command or nonvolatile operation API. */
typedef int (*thy_touch_read2_fn)(void *context, unsigned bank,
                                uint8_t reg, uint16_t *word);
void thy_touch_capture_init(thy_touch_capture *capture, const uint32_t uid[3]);
/* Complete originals are immutable for this boot; a failed partial capture
 * may be retried explicitly. No automatic capture or boot-time sensor writes. */
int thy_touch_capture_begin(thy_touch_capture *capture, uint32_t bank_mask, uint32_t now);
/* One read at most per call, at least two milliseconds between capture reads.
 * Caller services due control status reads before this background work. */
int thy_touch_capture_step(thy_touch_capture *capture, uint32_t now,
                          uint32_t ready_mask, thy_touch_read2_fn read2, void *context);
int thy_touch_capture_page(const thy_touch_capture *capture, unsigned bank,
                          unsigned offset, uint8_t bytes[THY_TOUCH_PAGE_BYTES],
                          uint16_t *valid_mask);
uint32_t thy_touch_snapshot_crc32(const thy_touch_capture *capture, unsigned bank);
/* Zero rejects an invalid bank or timing request; top controls stay at 20 ms. */
uint32_t thy_touch_poll_period(unsigned bank, uint32_t center_ms);

/* Target board hooks, available only in the instrument image. Polling changes
 * host response timing, never the controller's detection sensitivity. */
int board_touch_response_set(uint32_t milliseconds);
uint32_t board_touch_response_ms(void);
int board_touch_config_begin(uint32_t bank_mask);
const thy_touch_capture *board_touch_config_report(void);
void board_touch_config_task(uint32_t now);

#endif
