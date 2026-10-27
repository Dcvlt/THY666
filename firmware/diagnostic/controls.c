/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "controls.h"
#include "board.h"

/* Independently expressed board descriptor contract; see
 * docs/physical-control-map.md. All known inputs are single raw sensor bits.
 * Multi-bit masks are decoded bitwise, rather than by whole-mask equality. */
static const uint16_t control_bits[DIAG_CONTROL_BANKS][14] = {
    {0x0100u,0x0080u,0x0040u,0x0002u,0x0008u},
    {0x0040u,0x0020u,0x0001u,0x0002u,0x0080u,0x0010u,0x0008u,
     0x0100u,0x0200u,0x0400u,0x0800u,0x1000u,0x4000u,0x0004u},
    {0x0008u,0x0002u,0x0001u,0x0010u,0x0020u,0x0040u,0x0080u,
     0x0100u,0x0200u,0x0400u,0x0800u,0x1000u},
    {0x0008u,0x0002u,0x0001u,0x0010u,0x0020u,0x0040u,0x0080u,
     0x0100u,0x0200u,0x0400u,0x0800u,0x1000u}
};
static const unsigned control_counts[DIAG_CONTROL_BANKS] = {5u,14u,12u,12u};
static diag_controls_report_t report;
static uint32_t last_status_reads[DIAG_CONTROL_BANKS];
static uint8_t counter_seen[DIAG_CONTROL_BANKS];
static uint8_t baseline_valid[DIAG_CONTROL_BANKS];

static uint32_t bit_count(uint32_t bits)
{
    uint32_t count = 0u;
    while (bits != 0u) {
        bits &= bits - 1u;
        ++count;
    }
    return count;
}

static uint32_t logical_mask(unsigned bank, uint32_t raw)
{
    uint32_t value = 0u;
    for (unsigned i = 0u; i < control_counts[bank]; ++i) {
        if ((raw & control_bits[bank][i]) != 0u) value |= UINT32_C(1) << i;
    }
    return value;
}

static void update_summary(void)
{
    report.valid_mask = 0u;
    report.known_seen_count = 0u;
    for (unsigned i = 0u; i < DIAG_CONTROL_BANKS; ++i) {
        diag_control_bank_t *bank = &report.banks[i];
        if (bank->valid != 0u) report.valid_mask |= UINT32_C(1) << i;
        bank->known_seen = bank->seen_pressed & bank->known_mask;
        bank->logical_pressed = logical_mask(i, bank->raw);
        bank->logical_seen = logical_mask(i, bank->known_seen);
        report.known_seen_count += bit_count(bank->known_seen);
    }
}

void board_controls_init(void)
{
    report = (diag_controls_report_t){0};
    report.total_known_controls = DIAG_KNOWN_CONTROLS;
    for (unsigned i = 0u; i < DIAG_CONTROL_BANKS; ++i) {
        last_status_reads[i] = 0u;
        counter_seen[i] = 0u;
        baseline_valid[i] = 0u;
        for (unsigned j = 0u; j < control_counts[i]; ++j)
            report.banks[i].known_mask |= control_bits[i][j];
    }
}

void board_controls_task(uint32_t now)
{
    const volatile diag_report_t *source = board_report();
    report.last_service_ms = now;
    for (unsigned i = 0u; i < DIAG_CONTROL_BANKS; ++i) {
        diag_control_bank_t *bank = &report.banks[i];
        uint32_t reads = source->sensor[i].status_reads;
        if ((source->touch_ok_mask & (UINT32_C(1) << i)) == 0u) {
            /* Failure is not a release. Discard continuity so recovery does
             * not invent edges for the unobserved interval. */
            bank->valid = 0u;
            baseline_valid[i] = 0u;
            last_status_reads[i] = reads;
            counter_seen[i] = 1u;
            continue;
        }
        if (counter_seen[i] != 0u && reads == last_status_reads[i]) continue;
        last_status_reads[i] = reads;
        counter_seen[i] = 1u;
        uint32_t raw = source->touch_masks[i] & UINT32_C(0xffff);
        uint32_t sample_ms = source->sensor[i].last_status_ms;
        if (baseline_valid[i] != 0u) {
            uint32_t rose = raw & ~bank->raw;
            uint32_t fell = bank->raw & ~raw;
            bank->rose_latched |= rose;
            bank->fell_latched |= fell;
            bank->changes += bit_count(rose | fell);
            if ((rose | fell) != 0u) bank->last_change_ms = sample_ms;
        }
        bank->raw = raw;
        bank->valid = 1u;
        bank->seen_pressed |= raw;
        ++bank->samples;
        bank->last_sample_ms = sample_ms;
        baseline_valid[i] = 1u;
    }
    update_summary();
}

void board_controls_clear(void)
{
    for (unsigned i = 0u; i < DIAG_CONTROL_BANKS; ++i) {
        diag_control_bank_t *bank = &report.banks[i];
        bank->seen_pressed = bank->valid != 0u ? bank->raw : 0u;
        bank->rose_latched = 0u;
        bank->fell_latched = 0u;
        bank->changes = 0u;
        bank->samples = 0u;
        bank->last_change_ms = 0u;
    }
    update_summary();
}

const diag_controls_report_t *board_controls_report(void)
{
    return &report;
}
