/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_config.h"

/* Addresses and exact part identity established on this TheoryBoard. Reads
 * of 0x00..0x7f include the 16-bit CONFIG_CRC at 0x7e, never CTRL_CMD (0x86).
 * See docs/center-touch-response.md for the manufacturer's register map. */
static const uint8_t addresses[THY_TOUCH_BANKS] = {0x36u, 0x35u, 0x39u, 0x38u};
#define CAPTURE_TIMEOUT_MS 5000u
#define CAPTURE_GAP_MS 2u
#define CAPTURE_ATTEMPTS 3u
#define RESULT_INVALID (-1)
#define RESULT_TIMEOUT (-2)
#define RESULT_UNAVAILABLE (-3)
#define RESULT_BUSY (-4)

static void clear_bytes(void *destination, size_t size)
{
    uint8_t *bytes = destination;
    while (size-- != 0u) *bytes++ = 0u;
}

static int due(uint32_t now, uint32_t when)
{
    return (int32_t)(now - when) >= 0;
}

uint32_t thy_touch_poll_period(unsigned bank, uint32_t center_ms)
{
    if (bank >= THY_TOUCH_BANKS || center_ms < 8u || center_ms > 80u) return 0u;
    return bank < 2u ? center_ms : THY_TOUCH_TOP_POLL_MS;
}

void thy_touch_capture_init(thy_touch_capture *capture, const uint32_t uid[3])
{
    if (capture == NULL) return;
    clear_bytes(capture, sizeof *capture);
    for (unsigned i = 0; i < THY_TOUCH_BANKS; ++i) capture->bank[i].address = addresses[i];
    if (uid != NULL) for (unsigned i = 0; i < 3u; ++i) capture->owner_uid[i] = uid[i];
}

int thy_touch_capture_begin(thy_touch_capture *capture, uint32_t mask, uint32_t now)
{
    if (capture == NULL || mask == 0u || (mask & ~UINT32_C(15)) != 0u) return RESULT_INVALID;
    int already_active = 0;
    for (unsigned i = 0; i < THY_TOUCH_BANKS; ++i)
        if (capture->bank[i].state == THY_TOUCH_CAPTURE_PENDING ||
            capture->bank[i].state == THY_TOUCH_CAPTURE_READING) already_active = 1;
    /* Validate the whole request before modifying any bank. */
    for (unsigned i = 0; i < THY_TOUCH_BANKS; ++i) {
        if ((mask & (UINT32_C(1) << i)) == 0u) continue;
        uint32_t state = capture->bank[i].state;
        if (state == THY_TOUCH_CAPTURE_PENDING || state == THY_TOUCH_CAPTURE_READING)
            return RESULT_BUSY;
    }
    for (unsigned i = 0; i < THY_TOUCH_BANKS; ++i) {
        if ((mask & (UINT32_C(1) << i)) == 0u ||
            capture->bank[i].state == THY_TOUCH_CAPTURE_COMPLETE) continue;
        thy_touch_snapshot *bank = &capture->bank[i];
        clear_bytes(bank, sizeof *bank);
        bank->address = addresses[i];
        bank->state = THY_TOUCH_CAPTURE_PENDING;
        bank->started_ms = now;
        bank->result = RESULT_BUSY;
    }
    if (!already_active) capture->due_ms = now;
    return 0;
}

static uint32_t crc_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned bit = 0; bit < 8u; ++bit)
        crc = (crc >> 1) ^ ((crc & 1u) != 0u ? UINT32_C(0xedb88320) : 0u);
    return crc;
}

uint32_t thy_touch_snapshot_crc32(const thy_touch_capture *capture, unsigned index)
{
    if (capture == NULL || index >= THY_TOUCH_BANKS) return 0u;
    const thy_touch_snapshot *bank = &capture->bank[index];
    uint32_t crc = UINT32_MAX;
    /* Tagged, fixed little-endian provenance: board UID, bus bank/address,
     * controller identity and all original bytes including stored CRC16. */
    const uint8_t tag[] = {'T','H','Y','T',1u};
    for (unsigned i = 0; i < sizeof tag; ++i) crc = crc_byte(crc, tag[i]);
    for (unsigned i = 0; i < 3u; ++i)
        for (unsigned shift = 0; shift < 32u; shift += 8u)
            crc = crc_byte(crc, (uint8_t)(capture->owner_uid[i] >> shift));
    crc = crc_byte(crc, (uint8_t)index);
    crc = crc_byte(crc, bank->address);
    crc = crc_byte(crc, bank->family_id);
    crc = crc_byte(crc, (uint8_t)bank->device_id);
    crc = crc_byte(crc, (uint8_t)(bank->device_id >> 8));
    crc = crc_byte(crc, bank->device_rev);
    for (unsigned i = 0; i < THY_TOUCH_CONFIG_BYTES; ++i) crc = crc_byte(crc, bank->bytes[i]);
    return crc ^ UINT32_MAX;
}

static void fail(thy_touch_snapshot *bank, uint32_t now, int result, uint8_t reg)
{
    bank->state = THY_TOUCH_CAPTURE_FAILED;
    bank->result = result;
    bank->failed_register = reg;
    bank->completed_ms = now;
    bank->capture_crc32 = 0u;
}

int thy_touch_capture_step(thy_touch_capture *capture, uint32_t now,
                          uint32_t ready_mask, thy_touch_read2_fn read2, void *context)
{
    if (capture == NULL || read2 == NULL || !due(now, capture->due_ms)) return 0;
    for (unsigned scanned = 0; scanned < THY_TOUCH_BANKS; ++scanned) {
        unsigned index = capture->cursor;
        capture->cursor = (uint8_t)((capture->cursor + 1u) % THY_TOUCH_BANKS);
        thy_touch_snapshot *bank = &capture->bank[index];
        if (bank->state != THY_TOUCH_CAPTURE_PENDING && bank->state != THY_TOUCH_CAPTURE_READING) continue;
        uint8_t reg = bank->phase == 0u || bank->phase == 3u ? 0x8fu :
                      bank->phase == 1u || bank->phase == 4u ? 0x91u : bank->offset;
        if (due(now, bank->started_ms + CAPTURE_TIMEOUT_MS)) {
            fail(bank, now, RESULT_TIMEOUT, reg);
            continue;
        }
        if ((ready_mask & (UINT32_C(1) << index)) == 0u) continue;
        uint16_t word = 0u;
        bank->state = THY_TOUCH_CAPTURE_READING;
        ++bank->attempts;
        int status = read2(context, index, reg, &word);
        capture->due_ms = now + CAPTURE_GAP_MS;
        if (status != 0) {
            bank->result = status;
            bank->failed_register = reg;
            if (++bank->retries >= CAPTURE_ATTEMPTS) fail(bank, now, status, reg);
            return 1;
        }
        bank->retries = 0u;
        bank->result = RESULT_BUSY;
        if (bank->phase == 0u) {
            bank->family_id = (uint8_t)word;
            bank->device_id = word >> 8;
            if (bank->family_id != 0x9au) fail(bank, now, RESULT_UNAVAILABLE, reg);
            else bank->phase = 1u;
        } else if (bank->phase == 1u) {
            bank->device_id |= (uint16_t)((word & 255u) << 8);
            bank->device_rev = (uint8_t)(word >> 8);
            if (bank->device_id != 0x0a05u || bank->device_rev != 1u)
                fail(bank, now, RESULT_UNAVAILABLE, reg);
            else bank->phase = 2u;
        } else if (bank->phase == 2u) {
            unsigned offset = bank->offset;
            bank->bytes[offset] = (uint8_t)word;
            bank->bytes[offset + 1u] = (uint8_t)(word >> 8);
            bank->valid[offset / 8u] |= (uint8_t)(3u << (offset % 8u));
            if (offset == 126u) {
                bank->stored_crc16 = word;
                bank->stored_crc_valid = 1u;
                bank->phase = 3u;
            } else bank->offset = (uint8_t)(offset + 2u);
        } else if (bank->phase == 3u) {
            if ((uint8_t)word != bank->family_id || (uint8_t)(word >> 8) != (uint8_t)bank->device_id)
                fail(bank, now, RESULT_UNAVAILABLE, reg);
            else bank->phase = 4u;
        } else {
            if ((uint8_t)word != (uint8_t)(bank->device_id >> 8) || (uint8_t)(word >> 8) != bank->device_rev)
                fail(bank, now, RESULT_UNAVAILABLE, reg);
            else {
                bank->capture_crc32 = thy_touch_snapshot_crc32(capture, index);
                bank->state = THY_TOUCH_CAPTURE_COMPLETE;
                bank->result = 0;
                bank->completed_ms = now;
            }
        }
        return 1;
    }
    return 0;
}

int thy_touch_capture_page(const thy_touch_capture *capture, unsigned index,
                          unsigned offset, uint8_t bytes[THY_TOUCH_PAGE_BYTES], uint16_t *valid_mask)
{
    if (capture == NULL || index >= THY_TOUCH_BANKS || bytes == NULL || valid_mask == NULL ||
        offset >= THY_TOUCH_CONFIG_BYTES || offset % THY_TOUCH_PAGE_BYTES != 0u) return RESULT_INVALID;
    const thy_touch_snapshot *bank = &capture->bank[index];
    *valid_mask = 0u;
    for (unsigned i = 0; i < THY_TOUCH_PAGE_BYTES; ++i) {
        unsigned source = offset + i;
        bytes[i] = bank->bytes[source];
        if ((bank->valid[source / 8u] & (1u << (source % 8u))) != 0u) *valid_mask |= (uint16_t)(1u << i);
    }
    return 0;
}
