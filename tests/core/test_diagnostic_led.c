/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual encoder/driver with fake register storage; no device is opened.
 * This checks state/ownership/cleanup, not STM32 timing or optical behavior. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/diagnostic/board.h"

typedef struct { uint32_t address; volatile uint32_t value; } fake_register;
static fake_register registers[64];
static size_t register_count, accesses;
static uint32_t fake_now;
typedef struct { uint32_t address, value, width; } register_event;
static register_event events[8192];
static size_t event_count;
static int prime_loaded, prime_pending, prime_reads_left, prime_stuck, prime_fault;
static unsigned prime_writes;
volatile diag_report_t g_diag_mailbox;

static volatile uint32_t *stored(uint32_t address)
{
    for (size_t i = 0u; i < register_count; ++i)
        if (registers[i].address == address) return &registers[i].value;
    assert(register_count < sizeof registers / sizeof registers[0]);
    registers[register_count].address = address;
    return &registers[register_count++].value;
}

static volatile uint32_t *fake_mmio(uint32_t address)
{
    accesses++;
    if (address == UINT32_C(0x40003808) && prime_pending) {
        if (prime_fault) *stored(address) = 0x20u;
        else if (prime_stuck || prime_reads_left-- > 0) *stored(address) = 0x80u;
        else { *stored(address) = 2u; prime_pending = 0; }
    }
    return stored(address);
}

static void record_write(uint32_t address, uint32_t value, uint32_t width)
{
    assert(event_count < sizeof events / sizeof events[0]);
    events[event_count++] = (register_event){address, value, width};
}

static void fake_write32(uint32_t address, uint32_t value)
{
    record_write(address, value, 32u);
    uint32_t old = *stored(address);
    uint32_t pin_mode = *stored(UINT32_C(0x40020800)) & 0xc0u;
    if (address == UINT32_C(0x40003800) && ((old ^ value) & 0x40u)) {
        /* SPI start AND stop transitions must occur behind GPIO-low. */
        assert(pin_mode == 0x40u);
        assert((*stored(UINT32_C(0x40020814)) & 8u) == 0u);
        if (value & 0x40u) {
            assert(prime_loaded);
            prime_pending = 1;
            prime_reads_left = 4;
        } else prime_pending = 0;
    }
    if (address == UINT32_C(0x40020800) && (value & 0xc0u) == 0x80u) {
        /* AF must never expose an unprimed/disabled/busy SPI signal. */
        assert(prime_loaded && !prime_pending);
        assert(prime_writes >= 2u);
        assert((*stored(UINT32_C(0x40003800)) & 0x40u) != 0u);
        assert((*stored(UINT32_C(0x40003808)) & 0x82u) == 2u);
        assert((*stored(UINT32_C(0x40020814)) & 8u) == 0u);
    }
    if (address == UINT32_C(0x40003804) && (value & 2u)) {
        assert(pin_mode == 0x80u && !prime_pending);
        assert((*stored(UINT32_C(0x40026070)) & 1u) != 0u);
    }
    *stored(address) = value;
    if (address == UINT32_C(0x4002600c)) *stored(UINT32_C(0x40026004)) &= ~value;
    if (address == UINT32_C(0x40020818)) {
        *stored(UINT32_C(0x40020814)) =
            (*stored(UINT32_C(0x40020814)) | (value & 0xffffu)) & ~(value >> 16);
    }
}

static void fake_write16(uint32_t address, uint16_t value)
{
    record_write(address, value, 16u);
    assert(address == UINT32_C(0x4000380c) && value == 0u);
    assert((*stored(UINT32_C(0x40020800)) & 0xc0u) == 0x40u);
    if (*stored(UINT32_C(0x40003800)) & 0x40u) {
        assert(!prime_pending && (*stored(UINT32_C(0x40003808)) & 0x82u) == 2u);
        prime_pending = 1;
        prime_reads_left = 4;
    }
    prime_writes++;
    prime_loaded = 1;
    *stored(address) = value;
    *stored(UINT32_C(0x40003808)) = 0u;
}

#define THY_LED_MMIO32(address) (*fake_mmio(address))
#define THY_LED_WRITE32(address, value) fake_write32((address), (value))
#define THY_LED_WRITE16(address, value) fake_write16((address), (value))
#define THY_LED_BARRIER() ((void)0)
#include "../../firmware/diagnostic/led.c"

uint32_t board_millis(void) { return fake_now; }

static void setup(void)
{
    memset(registers, 0, sizeof registers);
    register_count = accesses = 0u;
    event_count = 0u;
    prime_loaded = prime_pending = prime_reads_left = prime_stuck = prime_fault = 0;
    prime_writes = 0u;
    fake_now = 0u;
    *stored(UINT32_C(0x40003808)) = 2u;
    memset((void *)&g_diag_mailbox, 0, sizeof g_diag_mailbox);
    g_diag_mailbox.pclk1_hz = UINT32_C(42000000);
    board_led_init();
    assert(accesses == 0u); /* Boot must remain electrically inactive. */
    assert(board_led_report()->off_confirmed == 0u);
}

static uint8_t decode_component(const uint16_t *frame, unsigned pixel, unsigned component)
{
    uint8_t result = 0u;
    size_t offset = DIAG_LED_RESET_WORDS + pixel * 24u + component * 8u;
    for (unsigned bit = 0u; bit < 8u; ++bit) {
        assert(frame[offset + bit] == 0xf800u || frame[offset + bit] == 0xffc0u);
        result = (uint8_t)((result << 1) | (frame[offset + bit] == 0xffc0u));
    }
    return result;
}

static void finish(uint32_t now)
{
    (void)fake_mmio(UINT32_C(0x40026004));
    *stored(UINT32_C(0x40026004)) = UINT32_C(0x20);
    *stored(UINT32_C(0x40003808)) = 2u;
    *stored(UINT32_C(0x40026070)) &= ~UINT32_C(1);
    board_led_task(now);
}

static void encoder_tests(void)
{
    uint16_t frame[DIAG_LED_FRAME_WORDS + 1u];
    frame[DIAG_LED_FRAME_WORDS] = 0x1234u;
    for (unsigned component = 0u; component < 3u; ++component) {
        for (unsigned pixel = 0u; pixel < DIAG_LED_COUNT; pixel += 95u) {
            assert(diag_led_encode(frame, DIAG_LED_FRAME_WORDS,
                                   DIAG_LED_SINGLE, pixel, component) == DIAG_LED_FRAME_WORDS);
            for (unsigned i = 0u; i < DIAG_LED_COUNT; ++i)
                for (unsigned c = 0u; c < 3u; ++c)
                    assert(decode_component(frame, i, c) ==
                           (i == pixel && c == component ? DIAG_LED_BRIGHTNESS : 0u));
            assert(frame[DIAG_LED_RESET_WORDS + pixel * 24u + component * 8u + 4u] == 0xffc0u); /* 8, MSB-first. */
        }
        assert(diag_led_encode(frame, DIAG_LED_FRAME_WORDS,
                               DIAG_LED_ALL, 999u, component) == DIAG_LED_FRAME_WORDS);
        for (unsigned i = 0u; i < DIAG_LED_COUNT; ++i)
            for (unsigned c = 0u; c < 3u; ++c)
                assert(decode_component(frame, i, c) == (c == component ? 8u : 0u));
    }
    assert(diag_led_encode(frame, DIAG_LED_FRAME_WORDS, DIAG_LED_OFF, 999u, 999u));
    assert(DIAG_LED_RESET_WORDS == 256u && DIAG_LED_FRAME_WORDS == 2816u);
    /* Explicit reset words are all-low SPI bits; encoded note-zero symbols
     * remain F800. Neither boundary can be confused with an extra LED bit. */
    for (unsigned i = 0u; i < DIAG_LED_RESET_WORDS; ++i) assert(frame[i] == 0u);
    for (unsigned i = 0u; i < DIAG_LED_DATA_WORDS; ++i)
        assert(frame[DIAG_LED_RESET_WORDS + i] == 0xf800u);
    for (unsigned i = DIAG_LED_RESET_WORDS + DIAG_LED_DATA_WORDS; i < DIAG_LED_FRAME_WORDS; ++i)
        assert(frame[i] == 0u);
    assert(frame[DIAG_LED_RESET_WORDS - 1u] == 0u && frame[DIAG_LED_RESET_WORDS] == 0xf800u);
    assert(frame[DIAG_LED_RESET_WORDS + DIAG_LED_DATA_WORDS - 1u] == 0xf800u);
    assert(frame[DIAG_LED_RESET_WORDS + DIAG_LED_DATA_WORDS] == 0u);
    assert(frame[DIAG_LED_FRAME_WORDS] == 0x1234u);
    assert(!diag_led_encode(frame, DIAG_LED_FRAME_WORDS - 1u, DIAG_LED_OFF, 0u, 0u));
    assert(!diag_led_encode(NULL, DIAG_LED_FRAME_WORDS, DIAG_LED_OFF, 0u, 0u));
    assert(!diag_led_encode(frame, DIAG_LED_FRAME_WORDS, DIAG_LED_SINGLE, 96u, 0u));
    assert(!diag_led_encode(frame, DIAG_LED_FRAME_WORDS, DIAG_LED_ALL, 0u, 3u));
    assert(!diag_led_encode(frame, DIAG_LED_FRAME_WORDS, 4u, 0u, 0u));
}

static size_t find_write(size_t from, uint32_t address, uint32_t value, uint32_t width)
{
    for (size_t i = from; i < event_count; ++i)
        if (events[i].address == address && events[i].value == value && events[i].width == width)
            return i;
    assert(!"required register write absent");
    return event_count;
}

static void framing_and_transition_tests(void)
{
    setup();
    assert(board_led_request(DIAG_LED_OFF, 0u, 0u) == DIAG_SUCCESS);
    board_led_task(0u);
    size_t prime = find_write(0u, UINT32_C(0x4000380c), 0u, 16u);
    size_t enable = find_write(prime + 1u, UINT32_C(0x40003800), 0x0b4eu, 32u);
    size_t live_prime = find_write(enable + 1u, UINT32_C(0x4000380c), 0u, 16u);
    size_t expose = find_write(live_prime + 1u, UINT32_C(0x40020800), 0x80u, 32u);
    size_t stream = find_write(expose + 1u, UINT32_C(0x40026070), 0x2c41u, 32u);
    size_t request = find_write(stream + 1u, UINT32_C(0x40003804), 2u, 32u);
    assert(prime < enable && enable < live_prime && live_prime < expose && expose < stream && stream < request);
    /* Both actively shifted resets exceed 300us at the accepted bus clock.
     * This verifies generated bit counts; it is not a measured waveform. */
    assert((DIAG_LED_RESET_WORDS * 16u * UINT32_C(1000000)) / UINT32_C(10500000) >= 300u);
    for (unsigned i = 0u; i < DIAG_LED_FRAME_WORDS; ++i) {
        if (i < DIAG_LED_RESET_WORDS || i >= DIAG_LED_RESET_WORDS + DIAG_LED_DATA_WORDS)
            assert(led_words[i] == 0u);
        else assert(led_words[i] == 0xf800u);
    }
    size_t finishing = event_count;
    finish(5u); /* 2816 halfwords calculate to 4.291ms, before software overhead. */
    size_t hide = find_write(finishing, UINT32_C(0x40020800), 0x40u, 32u);
    size_t disable = find_write(hide + 1u, UINT32_C(0x40003800), 0x0b0eu, 32u);
    assert(hide < disable);
    assert(board_led_report()->off_confirmed == 1u && board_led_report()->errors == 0u);

    setup();
    prime_stuck = 1;
    assert(board_led_request(DIAG_LED_OFF, 0u, 0u) == DIAG_SUCCESS);
    board_led_task(0u);
    assert(board_led_report()->state == DIAG_LED_FAULT);
    assert(board_led_report()->last_failure_result == DIAG_TIMEOUT);
    assert(board_led_report()->frames == 0u && board_led_report()->off_confirmed == 0u);
    assert((*stored(UINT32_C(0x40020800)) & 0xc0u) == 0x40u);
    assert((*stored(UINT32_C(0x40003800)) & 0x40u) == 0u);
    assert(accesses < 1500u); /* The hidden zero-prime wait is independently bounded. */
    for (size_t i = 0u; i < event_count; ++i)
        assert(events[i].address != UINT32_C(0x40020800) || (events[i].value & 0xc0u) != 0x80u);
    size_t idle_accesses = accesses;
    board_led_task(10000u);
    assert(accesses == idle_accesses);

    setup();
    prime_fault = 1;
    assert(board_led_request(DIAG_LED_OFF, 0u, 0u) == DIAG_SUCCESS);
    board_led_task(0u);
    assert(board_led_report()->state == DIAG_LED_FAULT);
    assert(board_led_report()->last_failure_result == DIAG_UNAVAILABLE);
    assert(board_led_report()->last_failure_spi_sr == 0x20u);
    assert(board_led_report()->off_confirmed == 0u);
}

static void transfer_and_timeout_tests(void)
{
    setup();
    assert(board_led_request(DIAG_LED_SINGLE, 95u, 2u) == DIAG_SUCCESS);
    assert(accesses == 0u); /* Accepted request only queues; task owns the peripheral. */
    board_led_task(0u);
    assert(board_led_report()->state == DIAG_LED_SENDING);
    assert(*stored(UINT32_C(0x40026070)) == UINT32_C(0x2c41)); /* No CIRC/DBM/interrupts. */
    assert(*stored(UINT32_C(0x40026074)) == 2816u);
    assert(*stored(UINT32_C(0x40026078)) == UINT32_C(0x4000380c));
    assert(*stored(UINT32_C(0x40003800)) == UINT32_C(0x0b4e));
    assert((*stored(UINT32_C(0x40020820)) & 0xf000u) == 0x5000u);
    assert(board_led_request(DIAG_LED_ALL, 0u, 0u) == DIAG_BUSY);
    finish(5u);
    assert(board_led_report()->frames == 1u);
    assert(board_led_report()->state == DIAG_LED_HOLDING);
    assert(board_led_report()->off_confirmed == 0u);
    assert((*stored(UINT32_C(0x40020800)) & 0xc0u) == 0x40u); /* Idle GPIO low. */
    board_led_task(10000u);
    assert(board_led_report()->mode == DIAG_LED_OFF);
    assert(board_led_report()->state == DIAG_LED_SENDING);
    finish(10005u);
    assert(board_led_report()->off_confirmed == 1u);
    assert(board_led_report()->state == DIAG_LED_IDLE);
    assert(board_led_report()->frames == 2u);

    setup();
    assert(board_led_request(DIAG_LED_ALL, 0u, 0u) == DIAG_SUCCESS);
    board_led_task(0u);
    board_led_task(19u);
    assert(board_led_report()->state == DIAG_LED_SENDING);
    board_led_task(20u);
    assert(board_led_report()->state == DIAG_LED_FAULT);
    assert(board_led_report()->last_result == DIAG_TIMEOUT);
    assert(board_led_report()->errors == 1u && board_led_report()->frames == 0u);
    assert(board_led_report()->off_confirmed == 0u);
    assert(*stored(UINT32_C(0x40003804)) == 0u);
    assert((*stored(UINT32_C(0x40026070)) & 1u) == 0u);
    board_led_task(10000u); /* One best-effort auto-off after a failed lit frame. */
    board_led_task(10020u);
    assert(board_led_report()->errors == 2u);
    assert(board_led_report()->off_confirmed == 0u);
    size_t after_failure = accesses;
    board_led_task(20000u);
    assert(accesses == after_failure); /* No indefinite OFF retry. */
}

static void off_during_transfer_and_error_tests(void)
{
    setup();
    assert(board_led_request(DIAG_LED_ALL, 0u, 1u) == DIAG_SUCCESS);
    board_led_task(0u);
    assert(board_led_request(DIAG_LED_OFF, 0u, 0u) == DIAG_SUCCESS);
    finish(5u); /* Completes lit transfer and starts pending OFF. */
    assert(board_led_report()->state == DIAG_LED_SENDING);
    assert(board_led_report()->off_confirmed == 0u);
    finish(10u);
    assert(board_led_report()->off_confirmed == 1u);

    setup();
    assert(board_led_request(DIAG_LED_SINGLE, 0u, 0u) == DIAG_SUCCESS);
    board_led_task(0u);
    (void)fake_mmio(UINT32_C(0x40026004));
    *stored(UINT32_C(0x40026004)) = 8u; /* TEIF4. */
    *stored(UINT32_C(0x40003808)) = 0x80u;
    board_led_task(2u);
    assert(board_led_report()->state == DIAG_LED_FAULT);
    assert(board_led_report()->last_failure_dma_sr == 8u);
    assert(board_led_report()->last_failure_spi_sr == 0x80u);
    assert(board_led_report()->last_failure_ms == 2u);
    assert(board_led_report()->last_failure_result == DIAG_UNAVAILABLE);
    assert(board_led_report()->off_confirmed == 0u);
    assert(board_led_request(DIAG_LED_OFF, 0u, 0u) == DIAG_SUCCESS);
    board_led_task(3u);
    finish(8u);
    assert(board_led_report()->off_confirmed == 1u);
    assert(board_led_report()->errors == 1u); /* Recovery does not erase history. */
    assert(board_led_report()->last_failure_result == DIAG_UNAVAILABLE);

    setup();
    g_diag_mailbox.pclk1_hz = 16000000u;
    assert(board_led_request(DIAG_LED_ALL, 0u, 0u) == DIAG_UNAVAILABLE);
    assert(board_led_request(DIAG_LED_SINGLE, 96u, 0u) == DIAG_INVALID);
    assert(board_led_request(99u, 0u, 0u) == DIAG_INVALID);
    assert(accesses == 0u);
}

static void spi_completion_tests(void)
{
    setup();
    assert(board_led_request(DIAG_LED_SINGLE, 0u, 0u) == DIAG_SUCCESS);
    board_led_task(0u);
    (void)fake_mmio(UINT32_C(0x40026004));
    *stored(UINT32_C(0x40026004)) = 0x20u;
    *stored(UINT32_C(0x40003808)) = 0x82u; /* DMA TC while SPI still shifting. */
    board_led_task(4u);
    assert(board_led_report()->state == DIAG_LED_SENDING);
    assert((*stored(UINT32_C(0x40003800)) & 0x40u) != 0u);
    assert((*stored(UINT32_C(0x40020800)) & 0xc0u) == 0x80u); /* Keep AF until final bit. */
    board_led_task(20u);
    assert(board_led_report()->state == DIAG_LED_FAULT);
    assert(board_led_report()->last_failure_result == DIAG_TIMEOUT);
    assert(board_led_report()->frames == 0u && board_led_report()->off_confirmed == 0u);

    setup();
    assert(board_led_request(DIAG_LED_SINGLE, 0u, 0u) == DIAG_SUCCESS);
    board_led_task(0u);
    finish(25u); /* Complete peripheral observed after a delayed task service. */
    assert(board_led_report()->state == DIAG_LED_HOLDING);
    assert(board_led_report()->frames == 1u && board_led_report()->errors == 0u);
}

static void walk_and_wrap_tests(void)
{
    setup();
    assert(board_led_request(DIAG_LED_WALK, 0u, 2u) == DIAG_SUCCESS);
    board_led_task(0u);
    finish(5u);
    for (unsigned pixel = 1u; pixel < DIAG_LED_COUNT; ++pixel) {
        board_led_task(5u + pixel * 105u - 5u);
        assert(board_led_report()->pixel == pixel);
        assert(decode_component(led_words, pixel, 2u) == 8u);
        assert(decode_component(led_words, pixel - 1u, 2u) == 0u);
        finish(5u + pixel * 105u);
    }
    assert(board_led_report()->walk_steps == 96u);
    assert(board_led_report()->pixel == 95u);
    board_led_task(10000u);
    finish(10005u);
    assert(board_led_report()->off_confirmed == 1u);

    setup();
    fake_now = UINT32_MAX - 5000u;
    assert(board_led_request(DIAG_LED_SINGLE, 0u, 0u) == DIAG_SUCCESS);
    board_led_task(fake_now);
    finish(fake_now + 5u);
    board_led_task(fake_now + 9999u);
    assert(board_led_report()->mode == DIAG_LED_SINGLE);
    board_led_task(fake_now + 10000u);
    assert(board_led_report()->mode == DIAG_LED_OFF);
    finish(fake_now + 10005u);
    assert(board_led_report()->off_confirmed == 1u);
}

int main(int argc, char **argv)
{
    encoder_tests();
    framing_and_transition_tests();
    transfer_and_timeout_tests();
    off_during_transfer_and_error_tests();
    spi_completion_tests();
    walk_and_wrap_tests();
    if (argc == 2) {
        static const unsigned fixtures[4][3] = {
            {DIAG_LED_SINGLE, 0u, 0u}, {DIAG_LED_SINGLE, 95u, 2u},
            {DIAG_LED_ALL, 0u, 1u}, {DIAG_LED_OFF, 0u, 0u}
        };
        uint16_t frame[DIAG_LED_FRAME_WORDS];
        FILE *file = fopen(argv[1], "wb");
        assert(file);
        for (unsigned i = 0u; i < 4u; ++i) {
            assert(diag_led_encode(frame, DIAG_LED_FRAME_WORDS,
                                   fixtures[i][0], fixtures[i][1], fixtures[i][2]));
            assert(fwrite(frame, sizeof frame, 1u, file) == 1u);
        }
        assert(fclose(file) == 0);
    }
    puts("diagnostic LED encoder and DMA lifecycle tests passed");
    return 0;
}
