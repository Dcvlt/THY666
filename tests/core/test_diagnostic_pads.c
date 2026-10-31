/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual ADC/mux driver with fake MMIO. No timing/electrical emulation. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/diagnostic/board.h"
typedef struct { uint32_t address; volatile uint32_t value; } pad_fake_register;
static pad_fake_register registers[128];
static size_t register_count, accesses;
static uint32_t fake_now;
static int adc_stream_stuck, adc_reset_releases_stuck;
volatile diag_report_t g_diag_mailbox;

static volatile uint32_t *stored(uint32_t address)
{
    for (size_t i = 0u; i < register_count; ++i)
        if (registers[i].address == address) return &registers[i].value;
    assert(register_count < sizeof registers / sizeof registers[0]);
    registers[register_count].address = address;
    return &registers[register_count++].value;
}

static void lighting_untouched(void)
{
    assert(*stored(UINT32_C(0x40026404)) == 0x440u);
    assert(*stored(UINT32_C(0x40026488)) == 0x0c025441u);
    assert(*stored(UINT32_C(0x40010000)) == 1u);
    assert(*stored(UINT32_C(0x4001000c)) == 0x100u);
    assert((*stored(UINT32_C(0x40023810)) & 0x00400000u) == 0u);
    assert((*stored(UINT32_C(0x40021000)) & 0xfc0u) == 0xa80u);
    assert((*stored(UINT32_C(0x40021004)) & 0x38u) == 0x28u);
    assert((*stored(UINT32_C(0x40021008)) & 0xfc0u) == 0x500u);
    assert((*stored(UINT32_C(0x4002100c)) & 0xfc0u) == 0x940u);
    assert((*stored(UINT32_C(0x40021014)) & 0x38u) == 0x28u);
}

static volatile uint32_t *fake_mmio(uint32_t address)
{
    accesses++;
    assert(!(address >= UINT32_C(0x40010000) && address < UINT32_C(0x40010100)));
    assert(!(address >= UINT32_C(0x40003800) && address < UINT32_C(0x40003900)));
    assert(!(address >= UINT32_C(0x40026000) && address < UINT32_C(0x40026400)));
    assert(address != UINT32_C(0x40026404) && address != UINT32_C(0x4002640c));
    assert(!(address >= UINT32_C(0x40026488) && address <= UINT32_C(0x4002649c)));
    if (*stored(UINT32_C(0x40023824)) & 0x100u) {
        if (adc_reset_releases_stuck) adc_stream_stuck = 0;
        *stored(UINT32_C(0x40012000)) = 0u;
        *stored(UINT32_C(0x40012004)) = 0u;
        *stored(UINT32_C(0x40012008)) = 0u;
        *stored(UINT32_C(0x40012304)) = 0u;
    }
    if (adc_stream_stuck) *stored(UINT32_C(0x40026410)) |= 1u;
    if (address != UINT32_C(0x40026408)) {
        *stored(UINT32_C(0x40026400)) &= ~*stored(UINT32_C(0x40026408));
        *stored(UINT32_C(0x40026408)) = 0u;
    }
    for (unsigned port = 0u; port < 5u; ++port) {
        uint32_t base = UINT32_C(0x40020000) + port * UINT32_C(0x400);
        uint32_t bsrr = *stored(base + 0x18u);
        if (port == 4u) assert((bsrr & UINT32_C(0x00380038)) == 0u);
        *stored(base + 0x14u) = (*stored(base + 0x14u) | (bsrr & 0xffffu)) & ~(bsrr >> 16);
        *stored(base + 0x18u) = 0u;
    }
    lighting_untouched();
    return stored(address);
}

#define THY_PADS_MMIO32(address) (*fake_mmio(address))
#define THY_PADS_BARRIER() ((void)0)
#include "../../firmware/diagnostic/pads.c"
uint32_t board_millis(void) { return fake_now; }
static void task(uint32_t now) { fake_now = now; board_pads_task(now); lighting_untouched(); }

static void setup(void)
{
    memset(registers, 0, sizeof registers);
    register_count = accesses = 0u; fake_now = 0u;
    adc_stream_stuck = adc_reset_releases_stuck = 0;
    *stored(UINT32_C(0x40026404)) = 0x440u;
    *stored(UINT32_C(0x40026488)) = 0x0c025441u;
    *stored(UINT32_C(0x40010000)) = 1u;
    *stored(UINT32_C(0x4001000c)) = 0x100u;
    *stored(UINT32_C(0x40021000)) = 0xa80u;
    *stored(UINT32_C(0x40021004)) = 0x28u;
    *stored(UINT32_C(0x40021008)) = 0x500u;
    *stored(UINT32_C(0x4002100c)) = 0x940u;
    *stored(UINT32_C(0x40021014)) = 0x28u;
    memset((void *)&g_diag_mailbox, 0, sizeof g_diag_mailbox);
    g_diag_mailbox.hclk_hz = 168000000u; g_diag_mailbox.pclk2_hz = 84000000u;
    board_pads_init();
    assert(accesses == 0u && !board_pads_active() && !board_pads_busy());
    assert(board_pads_report()->lighting_decoupled == 1u);
}

static void inject_scan(unsigned selection, unsigned subscan, uint32_t now)
{
    (void)fake_mmio(UINT32_C(0x40026400));
    *stored(UINT32_C(0x40026400)) = 0x20u;
    *stored(UINT32_C(0x40026410)) &= ~1u;
    *stored(UINT32_C(0x40012008)) &= ~UINT32_C(0x40000000);
    for (unsigned bank = 0u; bank < 6u; ++bank)
        adc_samples[bank] = (uint16_t)(bank*100u + selection*10u + subscan*3u);
    task(now);
}

static void mux_tests(void)
{
    static const uint32_t pins[5] = {0x86u,0x3c02u,0x36u,0x700u,0xff80u};
    static const uint32_t bits[4][5] = {
        {0x80u,0x2000u,0x2u,0u,0x8880u}, {0u,0x400u,0x24u,0x100u,0x1100u},
        {0x2u,0x800u,0x10u,0x200u,0x2200u}, {0x4u,0x1002u,0u,0x400u,0x4400u}
    };
    uint32_t words[5];
    for (unsigned index = 0u; index < 16u; ++index) {
        assert(diag_pads_mux_words(index,words) == DIAG_SUCCESS);
        for (unsigned port = 0u; port < 5u; ++port) {
            uint32_t high = 0u;
            for (unsigned bit = 0u; bit < 4u; ++bit) if (index & (1u<<bit)) high |= bits[bit][port];
            assert(words[port] == (high | ((pins[port]^high)<<16)));
        }
        assert((words[4] & UINT32_C(0x00380038)) == 0u);
    }
    assert(diag_pads_mux_words(16u,words) == DIAG_INVALID);
    assert(diag_pads_mux_words(0u,NULL) == DIAG_INVALID);
}

static void acquisition_tests(uint32_t start)
{
    setup(); fake_now = start;
    assert(board_pads_request(1) == DIAG_SUCCESS);
    assert(accesses == 0u && board_pads_active() && !board_pads_busy());
    assert(board_pads_request(1) == DIAG_BUSY); task(start);
    assert(board_pads_report()->state == DIAG_PADS_SETTLE);
    assert(*stored(UINT32_C(0x40023844)) == 0x100u);
    assert(*stored(UINT32_C(0x40012304)) == 0x10000u);
    assert(*stored(UINT32_C(0x4001200c)) == 6u);
    assert(*stored(UINT32_C(0x4001202c)) == 0x500000u);
    assert(*stored(UINT32_C(0x40012034)) == (10u|(3u<<5)|(4u<<10)|(5u<<15)|(6u<<20)|(8u<<25)));
    task(start+1u); assert(!board_pads_busy());
    for (unsigned selection = 0u; selection < 16u; ++selection) {
        uint32_t now = start+(selection+1u)*2u;
        task(now); assert(board_pads_busy() && board_pads_report()->subscan == 0u);
        assert(*stored(UINT32_C(0x40012008)) == UINT32_C(0x40000101));
        assert(*stored(UINT32_C(0x40026410)) == 0x22c01u);
        assert(*stored(UINT32_C(0x40026414)) == 6u);
        for (unsigned sample = 0u; sample < 3u; ++sample) {
            inject_scan(selection,sample,now);
            if (sample < 2u) {
                assert(board_pads_report()->scans == selection);
                assert(board_pads_report()->subscan == sample+1u);
            }
        }
        assert(!board_pads_busy() && board_pads_active());
        assert(board_pads_report()->scans == selection+1u);
        for (unsigned bank = 0u; bank < 6u; ++bank) {
            unsigned slot = bank*16u+selection;
            assert(board_pads_report()->raw[slot] == bank*100u+selection*10u+3u);
            assert(board_pads_report()->minimum[slot] == board_pads_report()->raw[slot]);
            assert(board_pads_report()->maximum[slot] == board_pads_report()->raw[slot]);
            assert(board_pads_report()->valid[slot/32u] & (1u<<(slot&31u)));
        }
    }
    assert(board_pads_report()->frames == 1u && board_pads_report()->index == 0u);
    assert(board_pads_report()->errors == 0u && board_pads_report()->last_failure_excite_dma_sr == 0u);
    for (unsigned word = 0u; word < 3u; ++word) assert(board_pads_report()->valid[word] == UINT32_MAX);
    task(start+34u);
    for (unsigned bank = 0u; bank < 6u; ++bank) {
        unsigned slot = bank*16u;
        assert(!(board_pads_report()->valid[slot/32u] & (1u<<(slot&31u))));
    }
    assert(board_pads_request(0) == DIAG_SUCCESS); task(start+35u);
    assert(board_pads_active() && board_pads_report()->state == DIAG_PADS_RESTORE);
    assert((*stored(UINT32_C(0x40026410)) & 1u) == 0u);
    assert(*stored(UINT32_C(0x40012004)) == 0u && *stored(UINT32_C(0x40012008)) == 1u);
    assert(*stored(UINT32_C(0x4001200c)) == 7u && *stored(UINT32_C(0x4001202c)) == 0u);
    task(start+36u);
    assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_IDLE);
    assert(board_pads_report()->dma_resets == 0u && board_pads_report()->adc_resets == 0u);
}

static void failure_and_lifecycle_tests(void)
{
    setup(); assert(board_pads_request(1) == DIAG_SUCCESS);
    assert(board_pads_request(0) == DIAG_SUCCESS); task(0u);
    assert(!board_pads_active() && accesses == 0u);
    setup(); assert(board_pads_request(1) == DIAG_SUCCESS);
    task(0u); task(2u); task(21u); assert(board_pads_busy()); task(22u);
    assert(board_pads_report()->last_failure_result == DIAG_TIMEOUT);
    assert(board_pads_report()->errors == 1u && board_pads_report()->scans == 0u);
    task(23u); assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_FAULT);
    assert(board_pads_report()->valid[0] == 0u && board_pads_report()->valid[1] == 0u);
    setup(); assert(board_pads_request(1) == DIAG_SUCCESS); task(0u); task(2u);
    (void)fake_mmio(UINT32_C(0x40026400));
    *stored(UINT32_C(0x40026400)) = 8u; *stored(UINT32_C(0x40012000)) = 0x20u;
    task(3u);
    assert(board_pads_report()->last_failure_result == DIAG_UNAVAILABLE);
    assert(board_pads_report()->last_failure_adc_dma_sr == 8u);
    assert(board_pads_report()->last_failure_excite_dma_sr == 0u);
    assert(board_pads_report()->last_failure_adc_sr == 0x20u);
    task(4u); fake_now=5u; assert(board_pads_request(1) == DIAG_SUCCESS);
    assert(board_pads_report()->errors == 1u); task(5u); task(10005u); task(10006u);
    assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_IDLE);
    setup(); fake_now=UINT32_MAX-5000u; uint32_t start=fake_now;
    assert(board_pads_request(1) == DIAG_SUCCESS); task(start); task(start+10000u); task(start+10001u);
    assert(!board_pads_active());
    setup(); g_diag_mailbox.hclk_hz=16000000u;
    assert(board_pads_request(1) == DIAG_UNAVAILABLE && board_pads_request(2) == DIAG_INVALID);
    assert(accesses == 0u);
}

static void delayed_completed_conversion_tests(uint32_t start, uint32_t delay)
{
    setup(); fake_now = start;
    assert(board_pads_request(1) == DIAG_SUCCESS);
    uint32_t expiry = board_pads_report()->expires_ms;
    task(start); task(start + 2u);
    for (unsigned sample = 0u; sample < 3u; ++sample) {
        /* DMA finished during foreground work; servicing its completion late
         * must not fault the next, newly started conversion. */
        inject_scan(0u, sample, start + 2u + (sample + 1u) * delay);
        assert(board_pads_report()->errors == 0u);
        assert(board_pads_report()->expires_ms == expiry);
        if (sample < 2u) {
            assert(board_pads_busy() && board_pads_report()->subscan == sample + 1u);
            assert(board_pads_report()->scans == 0u);
            for (unsigned word = 0u; word < 3u; ++word)
                assert(board_pads_report()->valid[word] == 0u);
        }
    }
    assert(board_pads_report()->state == DIAG_PADS_SETTLE);
    assert(board_pads_report()->scans == 1u && board_pads_report()->index == 1u);
    for (unsigned bank = 0u; bank < 6u; ++bank) {
        unsigned slot = bank * 16u;
        assert(board_pads_report()->raw[slot] == bank * 100u + 3u);
        assert(board_pads_report()->valid[slot / 32u] & (1u << (slot & 31u)));
    }
    uint32_t now = start + 2u + 3u * delay;
    assert(board_pads_request(0) == DIAG_SUCCESS); task(now);
    assert(board_pads_active() && board_pads_report()->state == DIAG_PADS_RESTORE);
    task(now + 1u);
    assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_IDLE);
}

static void restarted_conversion_timeout_tests(uint32_t start)
{
    setup(); fake_now = start;
    assert(board_pads_request(1) == DIAG_SUCCESS);
    task(start); task(start + 2u);
    inject_scan(0u, 0u, start + 22u);
    assert(board_pads_busy() && board_pads_report()->errors == 0u);
    task(start + 41u);
    assert(board_pads_busy() && board_pads_report()->errors == 0u);
    task(start + 42u); /* The new conversion really failed to complete. */
    assert(board_pads_report()->state == DIAG_PADS_RESTORE);
    assert(board_pads_report()->errors == 1u && board_pads_report()->scans == 0u);
    assert(board_pads_report()->last_failure_result == DIAG_TIMEOUT);
    assert(board_pads_report()->last_failure_ms == start + 42u);
    assert((*stored(UINT32_C(0x40026410)) & 1u) == 0u);
    assert(*stored(UINT32_C(0x40012008)) == 1u);
    for (unsigned word = 0u; word < 3u; ++word)
        assert(board_pads_report()->valid[word] == 0u);
    task(start + 43u);
    assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_FAULT);
    assert(board_pads_report()->dma_resets == 0u && board_pads_report()->adc_resets == 0u);
}

static void completed_conversion_error_priority_tests(void)
{
    for (unsigned adc_overrun = 0u; adc_overrun < 2u; ++adc_overrun) {
        setup(); assert(board_pads_request(1) == DIAG_SUCCESS);
        task(0u); task(2u);
        (void)fake_mmio(UINT32_C(0x40026400));
        *stored(UINT32_C(0x40026400)) = adc_overrun ? 0x20u : 0x28u;
        *stored(UINT32_C(0x40012000)) = adc_overrun ? 0x20u : 0u;
        task(22u);
        assert(board_pads_report()->last_failure_result == DIAG_UNAVAILABLE);
        assert(board_pads_report()->errors == 1u && board_pads_report()->subscan == 0u);
        assert(board_pads_report()->scans == 0u);
        task(23u);
        assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_FAULT);
    }
}

static void stuck_stream_cleanup_tests(void)
{
    setup(); assert(board_pads_request(1) == DIAG_SUCCESS); task(0u); task(2u);
    adc_stream_stuck=1; adc_reset_releases_stuck=1;
    assert(board_pads_request(0) == DIAG_SUCCESS); task(3u);
    assert(board_pads_report()->dma_resets == 0u && board_pads_report()->adc_resets == 1u);
    assert(board_pads_report()->state == DIAG_PADS_RESTORE);
    assert((*stored(UINT32_C(0x40026410)) & 1u) == 0u);
    assert(*stored(UINT32_C(0x40012304)) == 0x10000u); /* Reset must not leave ADC at /2. */
    task(4u); assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_FAULT);
    setup(); assert(board_pads_request(1) == DIAG_SUCCESS); task(0u); task(2u);
    adc_stream_stuck=1; assert(board_pads_request(0) == DIAG_SUCCESS); task(3u);
    assert(board_pads_report()->dma_resets == 0u && board_pads_report()->adc_resets == 1u);
    assert(board_pads_active() && !board_pads_busy() && board_pads_report()->state == DIAG_PADS_FAULT);
    assert(*stored(UINT32_C(0x40012008)) == 0u);
    size_t after_failure=accesses; task(10001u); task(20000u);
    assert(accesses == after_failure && board_pads_active());
    assert(board_pads_request(1) == DIAG_BUSY);
    adc_reset_releases_stuck=1; assert(board_pads_request(0) == DIAG_SUCCESS);
    task(20001u); task(20002u);
    assert(board_pads_report()->adc_resets == 2u && board_pads_report()->dma_resets == 0u);
    assert(!board_pads_active());
}

int main(void)
{
    mux_tests(); acquisition_tests(0u); acquisition_tests(UINT32_MAX-1u);
    delayed_completed_conversion_tests(0u, 20u);
    delayed_completed_conversion_tests(UINT32_MAX - 1u, 27u);
    restarted_conversion_timeout_tests(0u);
    restarted_conversion_timeout_tests(UINT32_MAX - 1u);
    completed_conversion_error_priority_tests();
    failure_and_lifecycle_tests(); stuck_stream_cleanup_tests();
    puts("diagnostic pad mux, ADC-only DMA lifecycle and lighting isolation tests passed");
    return 0;
}
