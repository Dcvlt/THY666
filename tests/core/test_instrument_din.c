/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual USART driver against fake registers. Checks pin/peripheral isolation
 * and retained bytes. Does not emulate electrical wiring or 31250baud timing. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint32_t address, value; } fake_register;
static fake_register registers[32];
static unsigned register_count, accesses;
static uint8_t transmitted[4096];
static unsigned transmitted_count;
static uint32_t *stored(uint32_t address)
{
    for (unsigned i = 0u; i < register_count; ++i)
        if (registers[i].address == address) return &registers[i].value;
    assert(register_count < 32u);
    registers[register_count] = (fake_register){address, 0u};
    return &registers[register_count++].value;
}
static void allowed(uint32_t address)
{
    ++accesses;
    assert((address >= 0x40011000u && address <= 0x40011014u) ||
        (address >= 0x40020400u && address <= 0x40020420u) ||
        address == 0x40023824u || address == 0x40023830u || address == 0x40023844u ||
        address == 0xe000e104u || address == 0xe000e184u || address == 0xe000e284u ||
        address == 0xe000e424u);
}
static uint32_t read32(uint32_t address)
{ allowed(address); return *stored(address); }
static void write32(uint32_t address, uint32_t value)
{
    allowed(address);
    if (address == 0x40011004u) {
        assert(transmitted_count < sizeof transmitted && value < 256u);
        assert(*stored(0x40011000u) & 0x80u);
        transmitted[transmitted_count++] = (uint8_t)value;
        *stored(0x40011000u) &= ~0x80u;
    }
    *stored(address) = value;
}
#define THY_DIN_READ32(address) read32(address)
#define THY_DIN_WRITE32(address, value) write32(address, value)
#define THY_DIN_BARRIER() ((void)0)
#include "../../firmware/instrument/din.c"

static void initialize(uint32_t clock)
{
    memset(registers, 0, sizeof registers);
    register_count = accesses = transmitted_count = 0u;
    *stored(0x40020400u) = 0xaaaaaaaau;
    *stored(0x40020404u) = 0x55555555u;
    *stored(0x40020408u) = 0xccccccccu;
    *stored(0x4002040cu) = 0x33333333u;
    *stored(0x40020420u) = 0xabcdef12u;
    *stored(0x40023824u) = 0x00000100u; /* ADC reset unrelated bit retained */
    *stored(0x40023830u) = 0x00400001u; /* RGB DMA2 / GPIOA clocks retained */
    *stored(0x40023844u) = 0x00004100u; /* ADC/SYSCFG clocks retained */
    *stored(0xe000e424u) = 0xa1b2c3d4u;
    assert(thy_instrument_din_init(clock));
}
static void irq_byte(void)
{ *stored(0x40011000u) |= 0x80u; USART1_IRQHandler(); }
static void drain(void)
{
    unsigned guard = 4096u;
    while (thy_instrument_din_report()->queued && guard--) irq_byte();
    assert(guard);
}
static void test_exact_baud_8n1_tx_only_and_pin_isolation(void)
{
    initialize(84000000u);
    const thy_instrument_din_stats *report = thy_instrument_din_report();
    assert(report->initialized && report->ready && report->brr == 2688u && report->baud == 31250u);
    assert(*stored(0x40011008u) == 2688u);
    assert(*stored(0x4001100cu) == 0x2008u);
    assert(*stored(0x40011010u) == 0u && *stored(0x40011014u) == 0u);
    assert((*stored(0x40020400u) & ~0x3000u) == (0xaaaaaaaau & ~0x3000u));
    assert((*stored(0x40020400u) & 0x3000u) == 0x2000u);
    assert((*stored(0x40020404u) & ~0x40u) == (0x55555555u & ~0x40u));
    assert(!(*stored(0x40020404u) & 0x40u));
    assert((*stored(0x40020408u) & ~0x3000u) == (0xccccccccu & ~0x3000u));
    assert((*stored(0x4002040cu) & ~0x3000u) == (0x33333333u & ~0x3000u));
    assert(*stored(0x40020420u) == 0xa7cdef12u);
    assert(*stored(0x40020418u) == 0x40u);
    assert(*stored(0x40023824u) == 0x100u && *stored(0x40023830u) == 0x00400003u);
    assert(*stored(0xe000e104u) == 0x20u && *stored(0xe000e284u) == 0x20u);
    assert(*stored(0xe000e424u) == 0xa1b250d4u);
    thy_instrument_din_task(10u); assert(thy_instrument_din_report()->ready);
    initialize(16000000u); assert(thy_instrument_din_report()->brr == 512u);
}
static void test_invalid_clock_and_arguments_do_not_touch_hardware(void)
{
    unsigned before = accesses;
    assert(!thy_instrument_din_init(42000000u));
    assert(accesses == before && !thy_instrument_din_report()->ready);
    const uint8_t bytes[] = {0x90u, 60u, 96u};
    assert(!thy_instrument_din_write(bytes, sizeof bytes));
    initialize(84000000u); before = accesses;
    assert(!thy_instrument_din_write(NULL, 3u));
    assert(!thy_instrument_din_write(bytes, 0u));
    assert(!thy_instrument_din_write(bytes, 4u));
    assert(accesses == before);
}
static void test_busy_hardware_and_partial_note_off_are_retained(void)
{
    initialize(84000000u);
    const uint8_t on[] = {0x92u, 60u, 96u}, off[] = {0x82u, 60u, 0u};
    assert(thy_instrument_din_write(on, 3u));
    assert(transmitted_count == 0u && thy_instrument_din_report()->queued == 3u);
    USART1_IRQHandler(); assert(transmitted_count == 0u); /* TXE not ready */
    drain(); assert(transmitted_count == 3u && !memcmp(transmitted, on, 3u));
    assert(thy_instrument_din_write(off, 3u)); irq_byte();
    assert(transmitted[3] == 0x82u && thy_instrument_din_report()->queued == 2u);
    USART1_IRQHandler(); USART1_IRQHandler();
    assert(transmitted_count == 4u && thy_instrument_din_report()->queued == 2u);
    thy_instrument_din_task(10u); thy_instrument_din_task(20u);
    drain();
    assert(transmitted_count == 6u && !memcmp(&transmitted[3], off, 3u));
    assert(!(*stored(0x4001100cu) & 0x80u));
    assert(thy_instrument_din_report()->transmitted_bytes == 6u);
}
static void test_full_buffer_acceptance_is_atomic_and_wrap_keeps_order(void)
{
    initialize(84000000u); uint8_t message[] = {0xb0u, 1u, 0u};
    for (unsigned i = 0u; i < 85u; ++i) {
        message[2] = i; assert(thy_instrument_din_write(message, 3u));
    }
    assert(thy_instrument_din_report()->queued == 255u);
    assert(!thy_instrument_din_write(message, 3u));
    irq_byte(); irq_byte();
    assert(!thy_instrument_din_write(message, 3u)); /* two spaces cannot take three bytes */
    irq_byte(); message[2] = 85u; assert(thy_instrument_din_write(message, 3u));
    drain();
    assert(transmitted_count == 258u);
    for (unsigned i = 0u; i < 86u; ++i) {
        assert(transmitted[3u*i] == 0xb0u && transmitted[3u*i+1u] == 1u &&
            transmitted[3u*i+2u] == i);
    }
    assert(thy_instrument_din_report()->accepted_messages == 86u);
    assert(thy_instrument_din_report()->backpressure == 2u);
}
static void test_empty_irq_does_not_repeat_and_new_enqueue_rearms(void)
{
    initialize(84000000u); const uint8_t clock = 0xf8u;
    irq_byte(); assert(transmitted_count == 0u);
    for (unsigned i = 0u; i < 20u; ++i) {
        assert(thy_instrument_din_write(&clock, 1u));
        assert(*stored(0x4001100cu) & 0x80u); irq_byte();
        irq_byte(); assert(transmitted_count == i + 1u);
    }
}
static void test_stall_fault_is_bounded_and_keeps_remaining_bytes(void)
{
    initialize(84000000u); const uint8_t off[] = {0x80u, 60u, 0u};
    assert(thy_instrument_din_write(off, 3u)); irq_byte();
    thy_instrument_din_task(0xfffffff0u); thy_instrument_din_task(0x53u);
    assert(thy_instrument_din_report()->ready && thy_instrument_din_report()->stall_ms == 99u);
    thy_instrument_din_task(0x54u);
    assert(!thy_instrument_din_report()->ready && thy_instrument_din_report()->errors == 1u);
    assert(thy_instrument_din_report()->queued == 2u);
    assert(!thy_instrument_din_write(off, 3u));
    thy_instrument_din_task(200u); assert(thy_instrument_din_report()->errors == 1u);
}
static void test_configuration_fault_is_latched_without_resetting_queue(void)
{
    initialize(84000000u); const uint8_t off[] = {0x80u, 60u, 0u};
    assert(thy_instrument_din_write(off, 3u));
    *stored(0x40011008u) = 1234u;
    thy_instrument_din_task(10u);
    assert(!thy_instrument_din_report()->ready && thy_instrument_din_report()->errors == 1u);
    assert(thy_instrument_din_report()->queued == 3u && *stored(0x40011008u) == 1234u);
}
int main(void)
{
    test_exact_baud_8n1_tx_only_and_pin_isolation();
    test_invalid_clock_and_arguments_do_not_touch_hardware();
    test_busy_hardware_and_partial_note_off_are_retained();
    test_full_buffer_acceptance_is_atomic_and_wrap_keeps_order();
    test_empty_irq_does_not_repeat_and_new_enqueue_rearms();
    test_stall_fault_is_bounded_and_keeps_remaining_bytes();
    test_configuration_fault_is_latched_without_resetting_queue();
    puts("instrument DIN UART tests passed: 7 scenarios");
    return 0;
}
