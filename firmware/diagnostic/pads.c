/* SPDX-License-Identifier: GPL-3.0-or-later
 * Experimental raw pad acquisition using the board's mux/ADC contract:
 * mux table 080c4020 / selector 08092270; three ADC scans 08045afc.
 * GPIOE's separate 600-word stream is centre indicator serialization, not
 * established pad excitation. This driver never accesses that stream, TIM1,
 * or the indicator data/clock/latch pins PE3..5.
 *
 * Each raw slot is bank*16+selection, not a physical pad identity. ADC uses
 * 21MHz (/4), 144-cycle sampling and three software-started six-rank scans.
 * Software polling introduces gaps; no calibrated pressure, velocity,
 * sensitivity or equivalence to other firmware acquisition timing is claimed.
 */
#include "board.h"
#include "pads.h"

#ifndef THY_PADS_MMIO32
#define THY_PADS_MMIO32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#endif
#ifndef THY_PADS_BARRIER
#define THY_PADS_BARRIER() __asm volatile("dmb" ::: "memory")
#endif

#define PAD_RCC UINT32_C(0x40023800)
#define PAD_GPIOA UINT32_C(0x40020000)
#define PAD_ADC UINT32_C(0x40012000)
#define PAD_ADC_COMMON UINT32_C(0x40012300)
#define PAD_DMA2 UINT32_C(0x40026400)
#define PAD_ADC_STREAM UINT32_C(0x40026410)
#define PAD_DISABLE_LIMIT 1000u

/* The bit numbering here is copied from the recovered six-bank selector
 * table, not from an assumed left/right or row/column physical geometry. */
static const uint8_t mux_port[6][4] = {
    {2,2,0,0}, {0,2,2,1}, {4,4,4,4},
    {4,4,4,4}, {4,1,1,1}, {1,3,3,3}
};
static const uint8_t mux_pin[6][4] = {
    {1,2,1,2}, {7,5,4,1}, {7,8,9,10},
    {11,12,13,14}, {15,10,11,12}, {13,8,9,10}
};
static volatile uint16_t adc_samples[6] __attribute__((aligned(4)));
static diag_pads_report_t pad_report;
static uint32_t selected_ms, acquire_ms, restore_ms;
static int running, stop_requested, initialized, failed_restore;

int diag_pads_mux_words(unsigned index, uint32_t words[5])
{
    if (index >= DIAG_PAD_SELECTIONS || words == NULL) return DIAG_INVALID;
    for (unsigned port = 0u; port < 5u; ++port) words[port] = 0u;
    for (unsigned bank = 0u; bank < 6u; ++bank)
        for (unsigned bit = 0u; bit < 4u; ++bit)
            words[mux_port[bank][bit]] |= UINT32_C(1) <<
                (mux_pin[bank][bit] + ((index & (1u << bit)) ? 0u : 16u));
    return DIAG_SUCCESS;
}

void board_pads_init(void)
{
    pad_report = (diag_pads_report_t){0};
    pad_report.lighting_decoupled = 1u;
    selected_ms = acquire_ms = restore_ms = 0u;
    running = stop_requested = initialized = failed_restore = 0;
}

int board_pads_active(void) { return running; }
int board_pads_busy(void) { return running && pad_report.state == DIAG_PADS_ACQUIRE; }
const diag_pads_report_t *board_pads_report(void) { return &pad_report; }

int board_pads_request(int start)
{
    if (start != 0 && start != 1) return DIAG_INVALID;
    if (!start) { if (running) stop_requested = 1; return DIAG_SUCCESS; }
    if (running) return DIAG_BUSY;
    if (g_diag_mailbox.hclk_hz != UINT32_C(168000000) ||
        g_diag_mailbox.pclk2_hz != UINT32_C(84000000)) return DIAG_UNAVAILABLE;
    for (unsigned slot = 0u; slot < DIAG_PAD_COUNT; ++slot) {
        pad_report.raw[slot] = pad_report.maximum[slot] = 0u;
        pad_report.minimum[slot] = 4095u;
    }
    for (unsigned word = 0u; word < 3u; ++word) pad_report.valid[word] = 0u;
    for (unsigned sample = 0u; sample < 3u; ++sample)
        for (unsigned bank = 0u; bank < 6u; ++bank) pad_report.samples[sample][bank] = 0u;
    pad_report.index = pad_report.subscan = pad_report.frames = pad_report.scans = 0u;
    pad_report.started_ms = board_millis();
    pad_report.expires_ms = pad_report.started_ms + DIAG_PAD_TEST_MS;
    pad_report.state = DIAG_PADS_QUEUED;
    pad_report.last_result = DIAG_BUSY;
    running = 1;
    stop_requested = initialized = failed_restore = 0;
    return DIAG_SUCCESS;
}

static int disable_dma(uint32_t stream)
{
    THY_PADS_MMIO32(stream) &= ~UINT32_C(1);
    for (unsigned budget = PAD_DISABLE_LIMIT; budget != 0u; --budget)
        if ((THY_PADS_MMIO32(stream) & 1u) == 0u) return DIAG_SUCCESS;
    return DIAG_TIMEOUT;
}

static void gpio_mode(unsigned port, unsigned pin, unsigned mode)
{
    uint32_t base = PAD_GPIOA + port * UINT32_C(0x400);
    uint32_t mask = UINT32_C(3) << (pin * 2u);
    THY_PADS_MMIO32(base) = (THY_PADS_MMIO32(base) & ~mask) | ((uint32_t)mode << (pin * 2u));
    THY_PADS_MMIO32(base + 0x0cu) &= ~mask;
    if (mode == 1u) {
        THY_PADS_MMIO32(base + 0x04u) &= ~(UINT32_C(1) << pin);
        THY_PADS_MMIO32(base + 0x08u) |= mask; /* Recovered high GPIO speed. */
    }
}

static void select_mux(unsigned index)
{
    uint32_t words[5];
    (void)diag_pads_mux_words(index, words);
    for (unsigned port = 0u; port < 5u; ++port)
        THY_PADS_MMIO32(PAD_GPIOA + port * UINT32_C(0x400) + 0x18u) = words[port];
}

static int hardware_init(void)
{
    THY_PADS_MMIO32(PAD_RCC + 0x30u) |= UINT32_C(0x0040001f); /* DMA2, GPIOA..E. */
    THY_PADS_MMIO32(PAD_RCC + 0x44u) |= UINT32_C(0x100); /* ADC1 only. */
    (void)THY_PADS_MMIO32(PAD_RCC + 0x44u);
    THY_PADS_MMIO32(PAD_ADC + 0x08u) = 0u;
    if (disable_dma(PAD_ADC_STREAM) != DIAG_SUCCESS) return DIAG_TIMEOUT;
    select_mux(0u);
    for (unsigned bank = 0u; bank < 6u; ++bank)
        for (unsigned bit = 0u; bit < 4u; ++bit)
            gpio_mode(mux_port[bank][bit], mux_pin[bank][bit], 1u);
    gpio_mode(2u, 0u, 3u);
    for (unsigned pin = 3u; pin <= 6u; ++pin) gpio_mode(0u, pin, 3u);
    gpio_mode(1u, 0u, 3u);
    THY_PADS_MMIO32(PAD_ADC_COMMON + 0x04u) = UINT32_C(1) << 16; /* /4 = 21MHz. */
    THY_PADS_MMIO32(PAD_ADC + 0x04u) = UINT32_C(0x100); /* SCAN, 12 bit. */
    THY_PADS_MMIO32(PAD_ADC + 0x0cu) = 6u; /* Channel 10: 144 cycles. */
    THY_PADS_MMIO32(PAD_ADC + 0x10u) =
        (6u << 9) | (6u << 12) | (6u << 15) | (6u << 18) | (6u << 24);
    THY_PADS_MMIO32(PAD_ADC + 0x2cu) = UINT32_C(5) << 20;
    THY_PADS_MMIO32(PAD_ADC + 0x30u) = 0u;
    THY_PADS_MMIO32(PAD_ADC + 0x34u) =
        10u | (3u << 5) | (4u << 10) | (5u << 15) | (6u << 20) | (8u << 25);
    THY_PADS_MMIO32(PAD_ADC) = 0u;
    THY_PADS_MMIO32(PAD_ADC + 0x08u) = 1u;
    initialized = 1;
    return DIAG_SUCCESS;
}

static int adc_start(void)
{
    if (disable_dma(PAD_ADC_STREAM) != DIAG_SUCCESS) return DIAG_TIMEOUT;
    THY_PADS_MMIO32(PAD_ADC + 0x08u) = 1u;
    THY_PADS_MMIO32(PAD_ADC) = 0u;
    THY_PADS_MMIO32(PAD_DMA2 + 0x08u) = UINT32_C(0x3d);
    THY_PADS_MMIO32(PAD_ADC_STREAM) = UINT32_C(0x00022c00); /* CH0, P2M, high priority. */
    THY_PADS_MMIO32(PAD_ADC_STREAM + 0x04u) = 6u;
    THY_PADS_MMIO32(PAD_ADC_STREAM + 0x08u) = PAD_ADC + 0x4cu;
    THY_PADS_MMIO32(PAD_ADC_STREAM + 0x0cu) = (uint32_t)(uintptr_t)adc_samples;
    THY_PADS_MMIO32(PAD_ADC_STREAM + 0x10u) = 0u;
    THY_PADS_MMIO32(PAD_ADC_STREAM + 0x14u) = 0u;
    THY_PADS_BARRIER();
    THY_PADS_MMIO32(PAD_ADC_STREAM) |= 1u;
    THY_PADS_MMIO32(PAD_ADC + 0x08u) = UINT32_C(0x40000101); /* ADON, DMA(bit8), SWSTART; no CONT/DDS. */
    return DIAG_SUCCESS;
}

static void record_failure(uint32_t now, int result)
{
    pad_report.errors++;
    pad_report.last_failure_result = pad_report.last_result = result;
    pad_report.last_failure_ms = now;
    pad_report.last_failure_adc_dma_sr = THY_PADS_MMIO32(PAD_DMA2);
    pad_report.last_failure_excite_dma_sr = 0u; /* Deprecated unrelated-lighting field. */
    pad_report.last_failure_adc_sr = THY_PADS_MMIO32(PAD_ADC);
    failed_restore = 1;
}

static void stop_and_restore(uint32_t now)
{
    if (initialized || pad_report.state != DIAG_PADS_QUEUED) {
        THY_PADS_MMIO32(PAD_ADC + 0x08u) = 0u;
        if (disable_dma(PAD_ADC_STREAM) != DIAG_SUCCESS) {
            record_failure(now, DIAG_TIMEOUT);
            /* Removing ADC requests and resetting the ADC block can free an
             * outstanding stream0 transaction. Never reset DMA2 as a whole:
             * its other streams belong to unrelated drivers. If stream0
             * remains enabled, retain ownership and leave ADC off. */
            ++pad_report.adc_resets;
            THY_PADS_MMIO32(PAD_RCC + 0x24u) |= UINT32_C(0x100);
            (void)THY_PADS_MMIO32(PAD_RCC + 0x24u);
            THY_PADS_BARRIER();
            THY_PADS_MMIO32(PAD_RCC + 0x24u) &= ~UINT32_C(0x100);
            (void)THY_PADS_MMIO32(PAD_RCC + 0x24u);
            if (disable_dma(PAD_ADC_STREAM) != DIAG_SUCCESS) {
                pad_report.state = DIAG_PADS_FAULT;
                pad_report.last_result = DIAG_TIMEOUT;
                stop_requested = 0;
                return; /* Explicit pads stop may retry bounded cleanup. */
            }
        }
        select_mux(0u);
        THY_PADS_MMIO32(PAD_DMA2 + 0x08u) = UINT32_C(0x3d);
        /* Restore the existing non-scan, 480-cycle raw ADC snapshot profile.
         * ADC ownership remains held for 1ms after ADON before release. */
        THY_PADS_MMIO32(PAD_ADC_COMMON + 0x04u) = UINT32_C(1) << 16; /* Also restore after ADC reset. */
        THY_PADS_MMIO32(PAD_ADC + 0x04u) = 0u;
        THY_PADS_MMIO32(PAD_ADC + 0x0cu) = 7u;
        THY_PADS_MMIO32(PAD_ADC + 0x10u) =
            (7u << 9) | (7u << 12) | (7u << 15) | (7u << 18) | (7u << 24);
        THY_PADS_MMIO32(PAD_ADC + 0x2cu) = 0u;
        THY_PADS_MMIO32(PAD_ADC + 0x30u) = 0u;
        THY_PADS_MMIO32(PAD_ADC + 0x34u) = 10u;
        THY_PADS_MMIO32(PAD_ADC) = 0u;
        THY_PADS_MMIO32(PAD_ADC + 0x08u) = 1u;
        restore_ms = now + 1u;
        pad_report.state = DIAG_PADS_RESTORE;
    } else {
        /* A queued test cancelled before its first task did not own hardware. */
        running = 0;
        pad_report.state = DIAG_PADS_IDLE;
        pad_report.last_result = DIAG_SUCCESS;
    }
    stop_requested = 0;
}

static void fail_and_restore(uint32_t now, int result)
{
    record_failure(now, result);
    /* hardware_init may fail before setting initialized but did touch MMIO. */
    initialized = 1;
    stop_and_restore(now);
}

static void publish_samples(void)
{
    for (unsigned bank = 0u; bank < 6u; ++bank) {
        unsigned slot = bank * DIAG_PAD_SELECTIONS + pad_report.index;
        uint32_t value = (pad_report.samples[0][bank] + pad_report.samples[1][bank] +
                          pad_report.samples[2][bank]) / 3u;
        pad_report.raw[slot] = value;
        if (value < pad_report.minimum[slot]) pad_report.minimum[slot] = value;
        if (value > pad_report.maximum[slot]) pad_report.maximum[slot] = value;
        pad_report.valid[slot / 32u] |= UINT32_C(1) << (slot & 31u);
    }
    pad_report.scans++;
    if (pad_report.index == 15u) pad_report.frames++;
    pad_report.last_result = DIAG_SUCCESS;
}

void board_pads_task(uint32_t now)
{
    if (!running) return;
    if (pad_report.state == DIAG_PADS_RESTORE) {
        if ((int32_t)(now - restore_ms) >= 0) {
            running = 0;
            pad_report.state = failed_restore ? DIAG_PADS_FAULT : DIAG_PADS_IDLE;
            if (!failed_restore) pad_report.last_result = DIAG_SUCCESS;
        }
        return;
    }
    if (pad_report.state == DIAG_PADS_FAULT) {
        if (stop_requested) stop_and_restore(now);
        return;
    }
    if (stop_requested || (int32_t)(now - pad_report.expires_ms) >= 0) {
        stop_and_restore(now);
        return;
    }
    if (pad_report.state == DIAG_PADS_QUEUED) {
        int result = hardware_init();
        if (result != DIAG_SUCCESS) { fail_and_restore(now, result); return; }
        select_mux(pad_report.index);
        selected_ms = now;
        pad_report.state = DIAG_PADS_SETTLE;
        return;
    }
    if (pad_report.state == DIAG_PADS_SETTLE) {
        if ((uint32_t)(now - selected_ms) < 2u) return;
        /* Last values remain visible, but an in-flight/failed selection must
         * not inherit a fresh-valid indication from its preceding scan. */
        for (unsigned bank = 0u; bank < 6u; ++bank) {
            unsigned slot = bank * DIAG_PAD_SELECTIONS + pad_report.index;
            pad_report.valid[slot / 32u] &= ~(UINT32_C(1) << (slot & 31u));
        }
        pad_report.subscan = 0u;
        int result = adc_start();
        if (result != DIAG_SUCCESS) { fail_and_restore(now, result); return; }
        acquire_ms = now;
        pad_report.state = DIAG_PADS_ACQUIRE;
        return;
    }
    uint32_t adc_flags = THY_PADS_MMIO32(PAD_DMA2);
    if ((adc_flags & UINT32_C(0x0d)) ||
        (THY_PADS_MMIO32(PAD_ADC) & UINT32_C(0x20))) {
        fail_and_restore(now, DIAG_UNAVAILABLE);
        return;
    }
    if (pad_report.subscan < 3u && (adc_flags & UINT32_C(0x20))) {
        if (disable_dma(PAD_ADC_STREAM) != DIAG_SUCCESS) {
            fail_and_restore(now, DIAG_TIMEOUT); return;
        }
        THY_PADS_BARRIER();
        for (unsigned bank = 0u; bank < 6u; ++bank)
            pad_report.samples[pad_report.subscan][bank] = adc_samples[bank] & UINT32_C(0xfff);
        pad_report.subscan++;
        THY_PADS_MMIO32(PAD_DMA2 + 0x08u) = UINT32_C(0x3d);
        if (pad_report.subscan < 3u) {
            int result = adc_start();
            if (result != DIAG_SUCCESS) { fail_and_restore(now, result); return; }
        }
    }
    if (pad_report.subscan == 3u) {
        publish_samples();
        pad_report.index = (pad_report.index + 1u) & 15u;
        select_mux(pad_report.index);
        selected_ms = now; /* At least 2ms settling for each next selection. */
        pad_report.state = DIAG_PADS_SETTLE;
        return;
    }
    if ((uint32_t)(now - acquire_ms) >= DIAG_PAD_FRAME_TIMEOUT_MS)
        fail_and_restore(now, DIAG_TIMEOUT);
}
