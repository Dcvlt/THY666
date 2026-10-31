/* SPDX-License-Identifier: GPL-3.0-or-later
 * Run the actual instrument wrapper and included ADC driver against fake MMIO.
 * Electrical settling is not emulated; these tests prove scheduling and cleanup.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/diagnostic/board.h"

typedef struct { uint32_t address; volatile uint32_t value; } fake_register;
static fake_register registers[128];
static size_t register_count, accesses;
static uint32_t fake_now;
static int stream_stuck, reset_releases_stream;
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
    ++accesses;
    assert(!(address >= UINT32_C(0x40010000) && address < UINT32_C(0x40010100)));
    assert(!(address >= UINT32_C(0x40003800) && address < UINT32_C(0x40003900)));
    assert(!(address >= UINT32_C(0x40026000) && address < UINT32_C(0x40026400)));
    assert(address != UINT32_C(0x40026404) && address != UINT32_C(0x4002640c));
    assert(!(address >= UINT32_C(0x40026488) && address <= UINT32_C(0x4002649c)));
    if (*stored(UINT32_C(0x40023824)) & 0x100u) {
        if (reset_releases_stream) stream_stuck = 0;
        *stored(UINT32_C(0x40012000)) = 0u;
        *stored(UINT32_C(0x40012004)) = 0u;
        *stored(UINT32_C(0x40012008)) = 0u;
        *stored(UINT32_C(0x40012304)) = 0u;
    }
    if (stream_stuck) *stored(UINT32_C(0x40026410)) |= 1u;
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
#include "../../firmware/instrument/pads.c"

uint32_t board_millis(void) { return fake_now; }

static void task(uint32_t now, uint32_t cycles)
{
    fake_now = now;
    *stored(UINT32_C(0xe0001004)) = cycles;
    board_pads_task(now);
    lighting_untouched();
}

static void setup(uint32_t now, uint32_t cycles)
{
    memset(registers, 0, sizeof registers);
    register_count = accesses = 0u;
    fake_now = now;
    stream_stuck = reset_releases_stream = 0;
    *stored(UINT32_C(0x40026404)) = 0x440u;
    *stored(UINT32_C(0x40026488)) = 0x0c025441u;
    *stored(UINT32_C(0x40010000)) = 1u;
    *stored(UINT32_C(0x4001000c)) = 0x100u;
    *stored(UINT32_C(0x40021000)) = 0xa80u;
    *stored(UINT32_C(0x40021004)) = 0x28u;
    *stored(UINT32_C(0x40021008)) = 0x500u;
    *stored(UINT32_C(0x4002100c)) = 0x940u;
    *stored(UINT32_C(0x40021014)) = 0x28u;
    *stored(UINT32_C(0xe0001004)) = cycles;
    memset((void *)&g_diag_mailbox, 0, sizeof g_diag_mailbox);
    g_diag_mailbox.hclk_hz = 168000000u;
    g_diag_mailbox.pclk2_hz = 84000000u;
    board_pads_init();
    assert(accesses == 0u && !board_pads_active());
    assert(board_pads_request(1) == DIAG_SUCCESS);
    task(now, cycles);
    assert(board_pads_report()->state == DIAG_PADS_SETTLE);
}

static void inject(unsigned selection, unsigned subscan, uint32_t now, uint32_t cycles)
{
    (void)fake_mmio(UINT32_C(0x40026400));
    *stored(UINT32_C(0x40026400)) = 0x20u;
    *stored(UINT32_C(0x40026410)) &= ~1u;
    *stored(UINT32_C(0x40012008)) &= ~UINT32_C(0x40000000);
    for (unsigned bank = 0u; bank < 6u; ++bank)
        adc_samples[bank] = (uint16_t)(bank * 100u + selection * 10u + subscan * 3u);
    task(now, cycles);
}

static void complete_frame(uint32_t start_ms, uint32_t start_cycles)
{
    setup(start_ms, start_cycles);
    uint32_t cycles = start_cycles;
    for (unsigned selection = 0u; selection < 16u; ++selection) {
        /* Milliseconds need not advance: 150us is measured by DWT, including wrap. */
        task(start_ms, cycles + 25199u);
        assert(!board_pads_busy() && board_pads_report()->subscan <= 3u);
        assert((*stored(UINT32_C(0x40012008)) & UINT32_C(0x40000000)) == 0u);
        assert((*stored(UINT32_C(0x40026410)) & 1u) == 0u);
        cycles += 25200u;
        task(start_ms, cycles);
        assert(board_pads_busy() && board_pads_report()->subscan == 0u);
        for (unsigned sample = 0u; sample < 3u; ++sample) {
            inject(selection, sample, start_ms, cycles);
            if (sample < 2u) {
                assert(board_pads_busy());
                assert(board_pads_report()->scans == selection);
                assert(board_pads_report()->index == selection);
            }
        }
        assert(!board_pads_busy() && board_pads_active());
        assert(board_pads_report()->scans == selection + 1u);
        assert(board_pads_report()->index == ((selection + 1u) & 15u));
        for (unsigned bank = 0u; bank < 6u; ++bank) {
            unsigned slot = bank * 16u + selection;
            assert(board_pads_report()->raw[slot] == bank * 100u + selection * 10u + 3u);
            assert(board_pads_report()->valid[slot / 32u] & (1u << (slot & 31u)));
        }
    }
    assert(board_pads_report()->frames == 1u && board_pads_report()->errors == 0u);
    for (unsigned word = 0u; word < 3u; ++word)
        assert(board_pads_report()->valid[word] == UINT32_MAX);
    assert(board_pads_request(0) == DIAG_SUCCESS);
    task(start_ms, cycles); /* Stop cannot depend on advancing DWT in SETTLE. */
    assert(board_pads_report()->state == DIAG_PADS_RESTORE);
    task(start_ms + 1u, cycles);
    assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_IDLE);
    assert(board_pads_report()->dma_resets == 0u && board_pads_report()->adc_resets == 0u);
}

static void renewable_lease_and_stopped_clock(void)
{
    setup(0u, 123u);
    thy_instrument_pads_renew(9000u);
    assert(board_pads_report()->expires_ms == 19000u);
    task(10000u, 25323u);
    assert(board_pads_busy() && board_pads_active());
    task(19000u, 25323u); /* Lease expiration still wins over acquisition timeout. */
    assert(board_pads_report()->state == DIAG_PADS_RESTORE);
    task(19001u, 25323u);
    assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_IDLE);

    setup(UINT32_MAX - 4000u, 99u);
    thy_instrument_pads_renew(UINT32_MAX - 100u);
    assert(board_pads_report()->expires_ms == 9899u);
    task(9898u, 25299u); assert(board_pads_active());
    task(9899u, 99u); assert(board_pads_report()->state == DIAG_PADS_RESTORE);
    task(9900u, 99u); assert(!board_pads_active());

    setup(0u, 0u);
    uint32_t expiry = board_pads_report()->expires_ms;
    assert(board_pads_request(0) == DIAG_SUCCESS);
    thy_instrument_pads_renew(9999u);
    assert(board_pads_report()->expires_ms == expiry);
    task(0u, 0u);
    assert(board_pads_report()->state == DIAG_PADS_RESTORE);
    task(1u, 0u); assert(!board_pads_active());
    thy_instrument_pads_renew(12345u);
    assert(board_pads_report()->expires_ms == expiry);
}

static void stopped_clock_while_main_renews(uint32_t start)
{
    setup(start, 777u);
    thy_instrument_pads_renew(start + 1u);
    task(start + 1u, 777u);
    assert(board_pads_report()->state == DIAG_PADS_SETTLE);
    assert(board_pads_report()->errors == 0u && !board_pads_busy());
    thy_instrument_pads_renew(start + 2u);
    assert(board_pads_report()->expires_ms == start + 10002u);
    task(start + 2u, 777u); /* Wall time advances despite a permanently frozen DWT. */
    assert(board_pads_report()->state == DIAG_PADS_RESTORE);
    assert(board_pads_report()->errors == 1u);
    assert(board_pads_report()->last_failure_result == DIAG_TIMEOUT);
    assert(board_pads_report()->last_failure_ms == start + 2u);
    assert(board_pads_report()->scans == 0u && board_pads_report()->frames == 0u);
    assert((*stored(UINT32_C(0x40026410)) & 1u) == 0u);
    assert(*stored(UINT32_C(0x40012004)) == 0u);
    assert(*stored(UINT32_C(0x40012008)) == 1u);
    assert(*stored(UINT32_C(0x4001200c)) == 7u);
    assert(*stored(UINT32_C(0x4001202c)) == 0u);
    assert(*stored(UINT32_C(0x40012304)) == 0x10000u);
    task(start + 3u, 777u);
    assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_FAULT);
    assert(board_pads_report()->dma_resets == 0u && board_pads_report()->adc_resets == 0u);
    uint32_t expiry = board_pads_report()->expires_ms;
    thy_instrument_pads_renew(start + 4u);
    assert(board_pads_report()->expires_ms == expiry);
    size_t before = accesses;
    task(start + 4u, 777u);
    assert(accesses == before && board_pads_report()->errors == 1u);
    lighting_untouched();
}

static void bounded_cleanup_and_timeout(void)
{
    setup(0u, 0u); task(0u, 25200u);
    task(19u, 25200u); assert(board_pads_busy());
    task(20u, 25200u);
    assert(board_pads_report()->last_failure_result == DIAG_TIMEOUT);
    task(21u, 25200u);
    assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_FAULT);

    setup(0u, 0u); task(0u, 25200u);
    stream_stuck = 1;
    assert(board_pads_request(0) == DIAG_SUCCESS);
    task(1u, 25200u);
    assert(board_pads_active() && board_pads_report()->state == DIAG_PADS_FAULT);
    assert(board_pads_report()->dma_resets == 0u && board_pads_report()->adc_resets == 1u);
    assert(*stored(UINT32_C(0x40012008)) == 0u);
    uint32_t expiry = board_pads_report()->expires_ms;
    thy_instrument_pads_renew(99999u);
    assert(board_pads_report()->expires_ms == expiry);
    reset_releases_stream = 1;
    assert(board_pads_request(0) == DIAG_SUCCESS);
    task(2u, 25200u); task(3u, 25200u);
    assert(!board_pads_active() && board_pads_report()->adc_resets == 2u);
    assert(board_pads_report()->dma_resets == 0u);
}

static void delayed_completed_conversion_tests(uint32_t start)
{
    setup(start, 0u); task(start, 25200u);
    uint32_t expiry = board_pads_report()->expires_ms;
    inject(0u, 0u, start + 20u, 25200u);
    assert(board_pads_busy() && board_pads_report()->errors == 0u);
    inject(0u, 1u, start + 47u, 25200u);
    assert(board_pads_busy() && board_pads_report()->errors == 0u);
    inject(0u, 2u, start + 74u, 25200u);
    assert(board_pads_report()->state == DIAG_PADS_SETTLE);
    assert(board_pads_report()->scans == 1u && board_pads_report()->errors == 0u);
    assert(board_pads_report()->expires_ms == expiry);
    for (unsigned bank = 0u; bank < 6u; ++bank)
        assert(board_pads_report()->raw[bank * 16u] == bank * 100u + 3u);
    assert(board_pads_request(0) == DIAG_SUCCESS);
    task(start + 74u, 25200u); task(start + 75u, 25200u);
    assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_IDLE);

    setup(start, 0u); task(start, 25200u);
    inject(0u, 0u, start + 20u, 25200u);
    task(start + 39u, 25200u);
    assert(board_pads_busy() && board_pads_report()->errors == 0u);
    task(start + 40u, 25200u);
    assert(board_pads_report()->state == DIAG_PADS_RESTORE);
    assert(board_pads_report()->last_failure_result == DIAG_TIMEOUT);
    assert(board_pads_report()->errors == 1u && board_pads_report()->scans == 0u);
    task(start + 41u, 25200u);
    assert(!board_pads_active() && board_pads_report()->state == DIAG_PADS_FAULT);
}

int main(void)
{
    complete_frame(0u, 0u);
    complete_frame(UINT32_MAX, UINT32_MAX - 10000u);
    delayed_completed_conversion_tests(0u);
    delayed_completed_conversion_tests(UINT32_MAX - 1u);
    renewable_lease_and_stopped_clock();
    stopped_clock_while_main_renews(0u);
    stopped_clock_while_main_renews(UINT32_MAX - 1u);
    bounded_cleanup_and_timeout();
    puts("instrument pad 150us scheduling, lease, wrap, publication and lighting isolation tests passed");
    return 0;
}
