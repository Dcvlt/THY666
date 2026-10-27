/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "protocol.h"
#include "board.h"
#include "usb.h"
#include "thy_core.h"
#include "led.h"
#include "controls.h"
#include "pads.h"
#include "center.h"

typedef struct {
    char *bytes;
    size_t length;
    size_t capacity;
    int overflow;
} reply;
static void character(reply *r, char c) {
    if (r->length + 1 < r->capacity) r->bytes[r->length++] = c;
    else r->overflow = 1;
}
static void text(reply *r, const char *s) {
    while (*s) character(r, *s++);
}
static void number(reply *r, uint32_t n) {
    char digits[10];
    unsigned count = 0;
    do { digits[count++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (count) character(r, digits[--count]);
}
static void hex(reply *r, uint32_t n, unsigned digits) {
    static const char chars[] = "0123456789abcdef";
    while (digits) { --digits; character(r, chars[(n >> (4 * digits)) & 15]); }
}
static void field(reply *r, const char *name, uint32_t n) {
    character(r, ' '); text(r, name); character(r, '='); number(r, n);
}
static void hexfield(reply *r, const char *name, uint32_t n) {
    character(r, ' '); text(r, name); text(r, "=0x"); hex(r, n, 8);
}
static void bank_key(reply *r, unsigned bank, const char *name) {
    text(r, " bank"); number(r, bank); character(r, '_'); text(r, name); character(r, '=');
}
static const char *i2c_phase(uint32_t phase) {
    switch (phase) {
    case DIAG_I2C_NONE: return "none";
    case DIAG_I2C_BUS_FREE: return "bus-free";
    case DIAG_I2C_START_WRITE: return "start-write";
    case DIAG_I2C_ADDRESS_WRITE: return "address-write";
    case DIAG_I2C_POINTER_READY: return "pointer-ready";
    case DIAG_I2C_POINTER_SENT: return "pointer-sent";
    case DIAG_I2C_START_READ: return "start-read";
    case DIAG_I2C_ADDRESS_READ: return "address-read";
    case DIAG_I2C_RECEIVE: return "receive";
    default: return "unknown";
    }
}
static void i2c_flags(reply *r, uint32_t sr1, uint32_t sr2) {
    const uint32_t masks[] = {0x100u, 0x200u, 0x400u, 0x800u};
    const char *names[] = {"BERR", "ARLO", "NACK", "OVR"};
    unsigned count = 0;
    for (unsigned i = 0; i < 4; i++) if (sr1 & masks[i]) {
        if (count++) character(r, ',');
        text(r, names[i]);
    }
    if (sr2 & 2u) { if (count++) character(r, ','); text(r, "BUSY"); }
    if (!count) text(r, "NONE");
}
static const char *sensor_state(uint32_t state) {
    switch (state) {
    case DIAG_SENSOR_SETTLE: return "settle";
    case DIAG_SENSOR_FAMILY: return "family";
    case DIAG_SENSOR_IDENTITY: return "identity";
    case DIAG_SENSOR_STATUS: return "status";
    case DIAG_SENSOR_READY: return "ready";
    case DIAG_SENSOR_FAILED: return "failed";
    case DIAG_SENSOR_UNSUPPORTED: return "unsupported";
    default: return "unknown";
    }
}
static void led_reply(reply *r) {
    const diag_led_report_t *s = board_led_report();
    static const char *states[] = {"idle", "queued", "sending", "holding", "fault"};
    static const char *modes[] = {"off", "pixel", "all", "walk"};
    text(r, "LED state="); text(r, s->state < 5u ? states[s->state] : "unknown");
    text(r, " mode="); text(r, s->mode < 4u ? modes[s->mode] : "unknown");
    field(r, "pixel", s->pixel); field(r, "component", s->component);
    field(r, "brightness", s->brightness); field(r, "code", 0u - (uint32_t)s->last_result);
    field(r, "frames", s->frames); field(r, "errors", s->errors);
    field(r, "started_ms", s->started_ms); field(r, "expires_ms", s->expires_ms);
    field(r, "walk_steps", s->walk_steps); field(r, "off_frame_sent", s->off_confirmed);
    field(r, "last_code", 0u - (uint32_t)s->last_failure_result);
    field(r, "last_at_ms", s->last_failure_ms);
    hexfield(r, "dma_sr", s->last_failure_dma_sr); hexfield(r, "spi_sr", s->last_failure_spi_sr);
    text(r, " nvm_writes=none");
}
static void pads_reply(reply *r, int bank) {
    const diag_pads_report_t *s = board_pads_report();
    text(r, "PADS"); field(r, "state", s->state); field(r, "index", s->index);
    field(r, "active", board_pads_active()); field(r, "busy", board_pads_busy());
    field(r, "subscan", s->subscan); field(r, "frames", s->frames); field(r, "scans", s->scans);
    field(r, "errors", s->errors); field(r, "code", 0u - (uint32_t)s->last_result);
    field(r, "dma_resets", s->dma_resets);
    field(r, "adc_resets", s->adc_resets);
    field(r, "lighting_decoupled", s->lighting_decoupled);
    field(r, "started_ms", s->started_ms); field(r, "expires_ms", s->expires_ms);
    for (unsigned i = 0; i < 3; ++i) {
        text(r, " valid"); number(r, i); text(r, "=0x"); hex(r, s->valid[i], 8);
    }
    field(r, "last_code", 0u - (uint32_t)s->last_failure_result);
    field(r, "last_at_ms", s->last_failure_ms);
    hexfield(r, "adc_dma_sr", s->last_failure_adc_dma_sr);
    hexfield(r, "excite_dma_sr", s->last_failure_excite_dma_sr);
    hexfield(r, "adc_sr", s->last_failure_adc_sr);
    if (bank >= 0) {
        field(r, "bank", (unsigned)bank);
        const uint32_t *arrays[] = {s->raw, s->minimum, s->maximum};
        const char *names[] = {"raw", "min", "max"};
        for (unsigned a = 0; a < 3; ++a) {
            character(r, ' '); text(r, names[a]); character(r, '=');
            for (unsigned i = 0; i < 16; ++i) {
                if (i) character(r, ',');
                number(r, arrays[a][16u * (unsigned)bank + i]);
            }
        }
    }
    text(r, " units=adc-counts velocity=uncalibrated nvm_writes=none");
}
static void center_reply(reply *r) {
    const diag_center_report_t *s = board_center_report();
    text(r, "CENTER"); field(r, "state", s->state); field(r, "mode", s->mode);
    field(r, "channel", s->channel); field(r, "brightness", s->brightness);
    hexfield(r, "enabled_mask", s->enabled_mask);
    field(r, "frames", s->frames); field(r, "errors", s->errors);
    field(r, "updated_ms", s->updated_ms);
    field(r, "code", 0u - (uint32_t)s->last_result);
    text(r, " driver=cpu lighting_dma=none nvm_writes=none");
}
static int same(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static int unsigned_number(const char *s, unsigned base, uint32_t *out) {
    uint32_t value = 0;
    unsigned count = 0;
    if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        unsigned digit;
        char c = *s++;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
        else return 0;
        if (digit >= base || value > (UINT32_MAX - digit) / base) return 0;
        value = value * base + digit;
        count++;
    }
    if (!count) return 0;
    *out = value;
    return 1;
}
static uint32_t crc32(const uint8_t *bytes, size_t count) {
    uint32_t crc = UINT32_MAX;
    for (size_t n = 0; n < count; n++) {
        crc ^= bytes[n];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
    return crc ^ UINT32_MAX;
}

uint32_t thy_diag_core_selftest(void) {
    /* Static allocation keeps the ~54 KB core out of the interrupt stack. */
    static thy_core core;
    const int16_t notes[] = { 0, 60, 60 };
    thy_midi_event event;
    uint8_t packet[4];
    uint32_t failures = 0;
    unsigned on = 0, off = 0;
    thy_core_init(&core);
    if (thy_core_press(&core, 0, notes, 3, 0, 0, THY_ROUTE_USB0, 96) != THY_OK) failures |= 1;
    if (thy_core_press(&core, 1, notes, 3, 0, 0, THY_ROUTE_USB0, 96) != THY_OK) failures |= 2;
    while (thy_core_poll(&core, &event)) {
        if (event.status == 0x90 && event.destination == THY_USB0) on++;
        else failures |= 4;
    }
    if (on != 2 || thy_core_sounding(&core) != 2) failures |= 8;
    if (thy_core_release(&core, 0) != THY_OK || thy_core_queued(&core) != 0) failures |= 16;
    if (thy_core_release(&core, 1) != THY_OK) failures |= 32;
    while (thy_core_poll(&core, &event)) {
        if (event.status == 0x80 && event.destination == THY_USB0) off++;
        else failures |= 64;
    }
    if (off != 2 || thy_core_sounding(&core) != 0 || thy_core_reserved(&core) != 0) failures |= 128;
    if (thy_core_bend(&core, THY_ROUTE_USB0, 0, 16384) != THY_OK ||
        !thy_core_poll(&core, &event) || thy_midi_encode_usb(&event, packet) != 4 ||
        packet[0] != 0x0e || packet[1] != 0xe0 || packet[2] != 127 || packet[3] != 127) failures |= 256;
    thy_core_panic(&core);
    return failures;
}

size_t thy_diag_command(const char *line, char *output, size_t capacity) {
    char copy[THY_DIAG_LINE_MAX];
    char *tokens[4];
    unsigned token_count = 0;
    size_t length = 0;
    reply r = { output, 0, capacity, 0 };
    if (!line || !output || capacity < THY_DIAG_REPLY_MAX) return 0;
    while (line[length] && length < sizeof(copy) - 1) { copy[length] = line[length]; length++; }
    if (line[length]) { text(&r, "ERR line-too-long"); goto done; }
    copy[length] = 0;
    for (size_t at = 0; at < length;) {
        while (at < length && (copy[at] == ' ' || copy[at] == '\t')) copy[at++] = 0;
        if (at == length) break;
        if (token_count == 4) { text(&r, "ERR arguments"); goto done; }
        tokens[token_count++] = copy + at;
        while (at < length && copy[at] != ' ' && copy[at] != '\t') at++;
    }
    if (!token_count) { text(&r, "ERR empty"); goto done; }
    if (same(tokens[0], "nor") || same(tokens[0], "internal")) {
        uint32_t address, count;
        uint8_t bytes[DIAG_READ_MAX];
        int internal = same(tokens[0], "internal");
        if (token_count != 3 || !unsigned_number(tokens[1], 16, &address) ||
            !unsigned_number(tokens[2], 10, &count) || !count || count > DIAG_READ_MAX ||
            (internal ? (address < DIAG_INTERNAL_FLASH_BASE ||
              address - DIAG_INTERNAL_FLASH_BASE > DIAG_INTERNAL_FLASH_BYTES - count) :
              address > DIAG_EXTERNAL_FLASH_BYTES - count)) {
            text(&r, "ERR read-range"); goto done;
        }
        int result = internal ? board_internal_read(address, bytes, count) : board_nor_read(address, bytes, count);
        if (result != DIAG_SUCCESS) {
            text(&r, "ERR read");
            field(&r, "code", (uint32_t)(-result));
            goto done;
        }
        text(&r, "DATA "); text(&r, tokens[0]);
        hexfield(&r, "address", address); field(&r, "bytes", count);
        text(&r, " hex=");
        for (uint32_t i = 0; i < count; i++) hex(&r, bytes[i], 2);
        hexfield(&r, "crc32", crc32(bytes, count));
        goto done;
    }
    if (same(tokens[0], "led")) {
        uint32_t mode, pixel = 0u, component = 0u;
        if (token_count == 2 && same(tokens[1], "status")) { led_reply(&r); goto done; }
        if (token_count == 2 && same(tokens[1], "off")) mode = DIAG_LED_OFF;
        else if (token_count == 3 && (same(tokens[1], "all") || same(tokens[1], "walk")) &&
                 unsigned_number(tokens[2], 10, &component) && component < 3u)
            mode = same(tokens[1], "all") ? DIAG_LED_ALL : DIAG_LED_WALK;
        else if (token_count == 4 && same(tokens[1], "pixel") &&
                 unsigned_number(tokens[2], 10, &pixel) && pixel < DIAG_LED_COUNT &&
                 unsigned_number(tokens[3], 10, &component) && component < 3u) mode = DIAG_LED_SINGLE;
        else { text(&r, "ERR led-arguments"); goto done; }
        int result = board_led_request(mode, pixel, component);
        if (result != DIAG_SUCCESS) { text(&r, "ERR led"); field(&r, "code", 0u - (uint32_t)result); }
        else led_reply(&r);
        goto done;
    }
    if (same(tokens[0], "center")) {
        uint32_t mode, channel = 0u;
        if (token_count == 1 || (token_count == 2 && same(tokens[1], "status"))) {
            center_reply(&r); goto done;
        }
        if (token_count == 2 && same(tokens[1], "off")) mode = DIAG_CENTER_OFF;
        else if (token_count == 2 && same(tokens[1], "all")) mode = DIAG_CENTER_ALL;
        else if (token_count == 3 && same(tokens[1], "channel") &&
                 unsigned_number(tokens[2], 10, &channel) && channel < 24u) mode = DIAG_CENTER_CHANNEL;
        else { text(&r, "ERR center-arguments"); goto done; }
        int result = board_center_request(mode, channel, mode == DIAG_CENTER_OFF ? 0u : DIAG_CENTER_DEFAULT_BRIGHTNESS);
        if (result != DIAG_SUCCESS) { text(&r, "ERR center"); field(&r, "code", 0u - (uint32_t)result); }
        else center_reply(&r);
        goto done;
    }
    if (same(tokens[0], "pads")) {
        uint32_t bank;
        if (token_count == 2 && same(tokens[1], "status")) { pads_reply(&r, -1); goto done; }
        if (token_count == 3 && same(tokens[1], "bank") &&
            unsigned_number(tokens[2], 10, &bank) && bank < 6u) { pads_reply(&r, (int)bank); goto done; }
        if (token_count == 2 && (same(tokens[1], "start") || same(tokens[1], "stop"))) {
            int start = same(tokens[1], "start");
            int result = board_pads_request(start);
            if (result) { text(&r, "ERR pads"); field(&r, "code", 0u - (uint32_t)result); }
            else {
                if (start) g_diag_mailbox.adc_ok_mask = 0;
                pads_reply(&r, -1);
            }
            goto done;
        }
        text(&r, "ERR pads-arguments"); goto done;
    }
    if (same(tokens[0], "controls") && token_count == 2 && same(tokens[1], "clear"))
        board_controls_clear();
    else if (token_count != 1) { text(&r, "ERR arguments"); goto done; }
    const volatile diag_report_t *b = board_report();
    if (same(tokens[0], "help")) {
        text(&r, "OK THYDIAG/1 commands=help,status,startup,touch,i2c,sensors,controls,led,center,pads,adc,midi,core-test,nor,internal reads=hex-address,decimal-count-1..256 writes=volatile-only nvm_writes=none");
    } else if (same(tokens[0], "startup")) {
        const volatile diag_entry_t *e = &g_diag_entry;
        text(&r, "STARTUP THYDIAG/1");
        field(&r, "valid", e->magic == DIAG_ENTRY_MAGIC && e->version == 1u);
        hexfield(&r, "control", e->control); field(&r, "ipsr", e->ipsr);
        field(&r, "primask", e->primask); hexfield(&r, "basepri", e->basepri);
        field(&r, "faultmask", e->faultmask); hexfield(&r, "msp", e->msp);
        hexfield(&r, "psp", e->psp); hexfield(&r, "vtor", e->vtor);
        hexfield(&r, "mpu_ctrl", e->mpu_ctrl); hexfield(&r, "cpacr", e->cpacr);
        hexfield(&r, "fpccr", e->fpccr); hexfield(&r, "aircr", e->aircr);
        hexfield(&r, "ccr", e->ccr);
        hexfield(&r, "icsr", e->icsr); hexfield(&r, "shcsr", e->shcsr);
        hexfield(&r, "cfsr", e->cfsr); hexfield(&r, "hfsr", e->hfsr);
        hexfield(&r, "rcc_cr", e->rcc_cr); hexfield(&r, "rcc_cfgr", e->rcc_cfgr);
        hexfield(&r, "rcc_csr", e->rcc_csr); hexfield(&r, "rcc_apb1enr", e->rcc_apb1enr);
        hexfield(&r, "iwdg_sr", e->iwdg_sr); hexfield(&r, "iwdg_pr", e->iwdg_pr);
        hexfield(&r, "iwdg_rlr", e->iwdg_rlr); hexfield(&r, "wwdg_cr", e->wwdg_cr);
        hexfield(&r, "wwdg_cfr", e->wwdg_cfr);
    } else if (same(tokens[0], "status")) {
        text(&r, "STATUS THYDIAG/1 build="); text(&r, thy_diag_build_id);
        field(&r, "uptime_ms", b->uptime_ms); field(&r, "stage", b->boot_stage);
        field(&r, "mailbox_version", b->version);
        hexfield(&r, "errors", b->errors); field(&r, "hclk", b->hclk_hz);
        field(&r, "pclk1", b->pclk1_hz); field(&r, "pclk2", b->pclk2_hz);
        field(&r, "clock_source", b->clock_source); field(&r, "usb_clock_ready", b->usb_clock_ready);
        hexfield(&r, "device_id", b->device_id); field(&r, "flash_kib", b->flash_kib);
        text(&r, " uid="); for (unsigned i = 0; i < 3; i++) hex(&r, b->uid[i], 8);
        hexfield(&r, "reset_flags", b->reset_flags); hexfield(&r, "nor_jedec", b->nor_jedec);
        hexfield(&r, "nor_status", b->nor_status); field(&r, "nor_ready", b->nor_ready);
        field(&r, "oled_frames", b->oled_frames); field(&r, "usb_state", b->usb_state);
        const diag_center_report_t *center = board_center_report();
        hexfield(&r, "center_mask", center->enabled_mask);
        field(&r, "center_brightness", center->brightness); field(&r, "center_errors", center->errors);
        const thy_diag_usb_stats *s = thy_diag_usb_status();
        field(&r, "usb_initialized", s->initialized); field(&r, "usb_mounted", s->mounted);
        field(&r, "cdc_commands", s->cdc_commands);
        hexfield(&r, "core_selftest", b->core_selftest);
        field(&r, "fault_exception", b->fault_exception); hexfield(&r, "fault_cfsr", b->fault_cfsr);
        hexfield(&r, "fault_hfsr", b->fault_hfsr); hexfield(&r, "fault_pc", b->fault_pc);
        hexfield(&r, "fault_mmfar", b->fault_mmfar); hexfield(&r, "fault_bfar", b->fault_bfar);
        hexfield(&r, "fault_sp", b->fault_sp); hexfield(&r, "fault_lr", b->fault_lr);
    } else if (same(tokens[0], "touch")) {
        text(&r, "TOUCH"); hexfield(&r, "ok", b->touch_ok_mask);
        for (unsigned i = 0; i < 4; i++) {
            text(&r, " bank"); number(&r, i); character(&r, '='); hex(&r, b->touch_masks[i], 4);
            text(&r, " errors"); number(&r, i); character(&r, '='); number(&r, b->touch_errors[i]);
        }
        hexfield(&r, "attempted", b->touch_attempted_mask);
        hexfield(&r, "history", b->touch_history_mask);
    } else if (same(tokens[0], "i2c")) {
        static const uint8_t addresses[] = {0x36u, 0x35u, 0x39u, 0x38u};
        text(&r, "I2C"); hexfield(&r, "attempted", b->touch_attempted_mask);
        hexfield(&r, "ok", b->touch_ok_mask); hexfield(&r, "history", b->touch_history_mask);
        hexfield(&r, "initialized", b->touch_initialized_mask);
        for (unsigned i = 0; i < 4; i++) {
            const volatile diag_i2c_bank_t *bank = &b->i2c[i];
            bank_key(&r, i, "addr"); text(&r, "0x"); hex(&r, addresses[i], 2);
            bank_key(&r, i, "attempts"); number(&r, bank->attempts);
            bank_key(&r, i, "successes"); number(&r, bank->successes);
            bank_key(&r, i, "code"); number(&r, 0u - (uint32_t)bank->result);
            bank_key(&r, i, "errors"); number(&r, b->touch_errors[i]);
            bank_key(&r, i, "phase"); text(&r, i2c_phase(bank->last_failure_phase));
            bank_key(&r, i, "last_code"); number(&r, 0u - (uint32_t)bank->last_failure_result);
            bank_key(&r, i, "sr1"); text(&r, "0x"); hex(&r, bank->last_failure_sr1, 8);
            bank_key(&r, i, "sr2"); text(&r, "0x"); hex(&r, bank->last_failure_sr2, 8);
            bank_key(&r, i, "at_ms"); number(&r, bank->last_failure_ms);
            bank_key(&r, i, "flags"); i2c_flags(&r, bank->last_failure_sr1, bank->last_failure_sr2);
        }
    } else if (same(tokens[0], "sensors")) {
        text(&r, "SENSORS"); hexfield(&r, "ready", b->touch_initialized_mask);
        hexfield(&r, "failed", b->touch_failed_mask);
        for (unsigned i = 0; i < 4; ++i) {
            const volatile diag_sensor_bank_t *s = &b->sensor[i];
            bank_key(&r, i, "state"); text(&r, sensor_state(s->state));
            bank_key(&r, i, "code"); number(&r, 0u - (uint32_t)s->readiness_result);
            bank_key(&r, i, "identity"); number(&r, s->identity_valid);
            bank_key(&r, i, "family"); text(&r, "0x"); hex(&r, s->family_id, 2);
            bank_key(&r, i, "device"); text(&r, "0x"); hex(&r, s->device_id, 4);
            bank_key(&r, i, "revision"); number(&r, s->device_rev);
            bank_key(&r, i, "ready_ms"); number(&r, s->first_ready_ms);
            bank_key(&r, i, "wake_nacks"); number(&r, s->wake_nacks);
            bank_key(&r, i, "retries"); number(&r, s->retries);
            bank_key(&r, i, "init_failures"); number(&r, s->init_failures);
            bank_key(&r, i, "last_reg"); text(&r, "0x"); hex(&r, s->last_failed_register, 2);
            bank_key(&r, i, "resets"); number(&r, s->peripheral_resets);
            bank_key(&r, i, "status_reads"); number(&r, s->status_reads);
            bank_key(&r, i, "status_ms"); number(&r, s->last_status_ms);
        }
        text(&r, " readiness=bus-and-status calibration=unverified");
    } else if (same(tokens[0], "controls")) {
        const diag_controls_report_t *s = board_controls_report();
        text(&r, "CONTROLS"); hexfield(&r, "valid", s->valid_mask);
        field(&r, "covered", s->known_seen_count); field(&r, "total", s->total_known_controls);
        for (unsigned i = 0; i < 4; ++i) {
            const diag_control_bank_t *k = &s->banks[i];
            bank_key(&r, i, "raw"); text(&r, "0x"); hex(&r, k->raw, 4);
            bank_key(&r, i, "seen"); text(&r, "0x"); hex(&r, k->seen_pressed, 4);
            bank_key(&r, i, "rose"); text(&r, "0x"); hex(&r, k->rose_latched, 4);
            bank_key(&r, i, "fell"); text(&r, "0x"); hex(&r, k->fell_latched, 4);
            bank_key(&r, i, "logical"); text(&r, "0x"); hex(&r, k->logical_pressed, 4);
            bank_key(&r, i, "known_seen"); text(&r, "0x"); hex(&r, k->known_seen, 4);
            bank_key(&r, i, "changes"); number(&r, k->changes);
            bank_key(&r, i, "samples"); number(&r, k->samples);
            bank_key(&r, i, "changed_ms"); number(&r, k->last_change_ms);
            bank_key(&r, i, "sample_ms"); number(&r, k->last_sample_ms);
        }
        text(&r, " physical_map=observe");
    } else if (same(tokens[0], "adc")) {
        text(&r, "ADC"); hexfield(&r, "ok", b->adc_ok_mask);
        text(&r, " raw=");
        for (unsigned i = 0; i < 6; i++) { if (i) character(&r, ','); number(&r, b->raw_adc[i]); }
        text(&r, board_pads_active() ? " ownership=pad-test snapshots=paused" : " ownership=raw-adc snapshots=active");
    } else if (same(tokens[0], "midi")) {
        const thy_diag_usb_stats *s = thy_diag_usb_status();
        text(&r, "MIDI"); field(&r, "received", s->midi_received);
        field(&r, "invalid", s->midi_invalid); field(&r, "unsupported", s->midi_unsupported);
        text(&r, " echo=off tx=off");
    } else if (same(tokens[0], "core-test")) {
        uint32_t failures = thy_diag_core_selftest();
        g_diag_mailbox.core_selftest = failures;
        text(&r, failures ? "CORE FAIL" : "CORE PASS"); hexfield(&r, "failures", failures);
        field(&r, "state_bytes", sizeof(thy_core)); text(&r, " scope=ram-only");
    } else {
        text(&r, "ERR unknown-command");
    }
done:
    if (r.overflow) { r.length = 0; r.overflow = 0; text(&r, "ERR response-overflow"); }
    text(&r, "\r\n");
    output[r.length] = 0;
    return r.length;
}
