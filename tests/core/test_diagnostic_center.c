/* Actual center driver with fake MMIO: no device access or electrical claim. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/diagnostic/board.h"
#include "../../firmware/diagnostic/center.h"

typedef struct { uint32_t address, value; } register_record;
static register_record registers[8], writes[20000];
static unsigned register_count, write_count, read_count, nop_count, barriers;
static uint32_t now_ms, gpio_odr;
volatile diag_report_t g_diag_mailbox;

static uint32_t *stored(uint32_t address)
{
    for (unsigned i = 0u; i < register_count; ++i)
        if (registers[i].address == address) return &registers[i].value;
    assert(register_count < sizeof registers / sizeof registers[0]);
    registers[register_count].address = address;
    registers[register_count].value = UINT32_C(0xa5a5a5a5);
    return &registers[register_count++].value;
}

static uint32_t fake_read(uint32_t address)
{
    ++read_count;
    assert(address == UINT32_C(0x40023830) || address == UINT32_C(0x40021000) ||
           address == UINT32_C(0x40021004) || address == UINT32_C(0x40021008) ||
           address == UINT32_C(0x4002100c));
    return *stored(address);
}

static void fake_write(uint32_t address, uint32_t value)
{
    assert(write_count < sizeof writes / sizeof writes[0]);
    writes[write_count++] = (register_record){address, value};
    if (address == UINT32_C(0x40021018)) {
        assert(((value & 0xffffu) | (value >> 16)) <= 0x38u);
        assert(((value & 0xffffu) | (value >> 16)) ==
               (((value & 0xffffu) | (value >> 16)) & 0x38u));
        gpio_odr = (gpio_odr & ~(value >> 16)) | (value & 0xffffu);
    } else *stored(address) = value;
}

#define THY_CENTER_READ32(address) fake_read(address)
#define THY_CENTER_WRITE32(address, value) fake_write(address, value)
#define THY_CENTER_NOP() ((void)++nop_count)
#define THY_CENTER_BARRIER() ((void)++barriers)
#include "../../firmware/diagnostic/center.c"

uint32_t board_millis(void) { return now_ms; }

static void setup(void)
{
    memset(registers, 0, sizeof registers);
    memset(writes, 0, sizeof writes);
    register_count = write_count = read_count = nop_count = barriers = 0u;
    gpio_odr = UINT32_C(0xa5a5); now_ms = 0u;
    memset((void *)&g_diag_mailbox, 0, sizeof g_diag_mailbox);
    g_diag_mailbox.hclk_hz = UINT32_C(168000000);
    board_center_init();
    assert(read_count == 0u && write_count == 0u);
    assert(board_center_report()->state == DIAG_CENTER_IDLE);
    assert(board_center_report()->brightness == 128u);
    board_center_task(0u);
    assert(read_count == 0u && write_count == 0u);
}

static void decode_frame(const uint32_t *sequence, uint16_t decoded[24])
{
    uint32_t pins = 0u;
    unsigned bit_count = 0u, latch_rises = 0u;
    memset(decoded, 0, 24u * sizeof decoded[0]);
    for (unsigned i = 0u; i < DIAG_CENTER_FRAME_WORDS; ++i) {
        uint32_t value = sequence[i];
        assert((((value & 0xffffu) | (value >> 16)) & ~UINT32_C(0x38)) == 0u);
        uint32_t previous = pins;
        pins = (pins & ~(value >> 16)) | (value & 0xffffu);
        if ((pins & 0x10u) != 0u && (previous & 0x10u) == 0u) {
            assert((pins & 8u) == 0u && bit_count < 288u);
            /* Data was prepared before the rising edge, not on it. */
            assert((pins & 0x20u) == (previous & 0x20u));
            decoded[bit_count / 12u] = (uint16_t)((decoded[bit_count / 12u] << 1) | ((pins >> 5) & 1u));
            ++bit_count;
        }
        if ((pins & 8u) != 0u && (previous & 8u) == 0u) {
            ++latch_rises;
            assert(bit_count == 288u && (pins & 0x10u) == 0u);
        }
    }
    assert(bit_count == 288u && latch_rises == 1u && (pins & 0x18u) == 0u);
    assert(sequence[864] == UINT32_C(0x00180008));
    for (unsigned i = 865u; i < 887u; ++i) assert(sequence[i] == 8u);
    assert(sequence[887] == UINT32_C(0x00180000));
}

static void encoder_tests(void)
{
    uint16_t input[24], decoded[24];
    uint32_t output[DIAG_CENTER_FRAME_WORDS + 1u];
    for (unsigned channel = 0u; channel < 24u; ++channel) input[channel] = (uint16_t)((channel * 173u) & 4095u);
    input[5] = 0u; input[6] = 4095u; input[23] = 128u;
    output[DIAG_CENTER_FRAME_WORDS] = UINT32_C(0x12345678);
    assert(diag_center_encode(output, DIAG_CENTER_FRAME_WORDS, input) == 888u);
    assert(output[DIAG_CENTER_FRAME_WORDS] == UINT32_C(0x12345678));
    decode_frame(output, decoded);
    for (unsigned channel = 0u; channel < 24u; ++channel)
        assert(decoded[channel] == (channel < 5u ? 0u : input[channel]));
    /* Every channel's maximum value and each of its twelve bit positions. */
    for (unsigned channel = 0u; channel < 24u; ++channel) {
        for (unsigned bit = 0u; bit < 12u; ++bit) {
            memset(input, 0, sizeof input); input[channel] = (uint16_t)(1u << bit);
            assert(diag_center_encode(output, DIAG_CENTER_FRAME_WORDS, input) == 888u);
            decode_frame(output, decoded);
            for (unsigned other = 0u; other < 24u; ++other)
                assert(decoded[other] == (other == channel && channel >= 5u ? input[channel] : 0u));
        }
    }
    memset(output, 0x5a, sizeof output);
    input[0] = 4096u;
    assert(diag_center_encode(output, DIAG_CENTER_FRAME_WORDS, input) == 0u);
    assert(output[0] == UINT32_C(0x5a5a5a5a));
    input[0] = 0u;
    assert(diag_center_encode(output, DIAG_CENTER_FRAME_WORDS - 1u, input) == 0u);
    assert(output[0] == UINT32_C(0x5a5a5a5a));
    assert(diag_center_encode(NULL, DIAG_CENTER_FRAME_WORDS, input) == 0u);
    assert(diag_center_encode(output, DIAG_CENTER_FRAME_WORDS, NULL) == 0u);
}

static void mapping_tests(void)
{
    static const int expected_nav[] = {15,5,6,7,8};
    static const int expected_functions[] = {16,22,10,11,21,23,9,20,19,18,17,14,13,12};
    uint32_t mask = 0u;
    for (unsigned i = 0u; i < 5u; ++i) {
        assert(diag_center_logical_channel(0u, i) == expected_nav[i]);
        mask |= UINT32_C(1) << expected_nav[i];
    }
    for (unsigned i = 0u; i < 14u; ++i) {
        assert(diag_center_logical_channel(1u, i) == expected_functions[i]);
        mask |= UINT32_C(1) << expected_functions[i];
    }
    assert(mask == DIAG_CENTER_KNOWN_MASK);
    assert(diag_center_logical_channel(0u, 5u) == -1);
    assert(diag_center_logical_channel(1u, 14u) == -1);
    assert(diag_center_logical_channel(2u, 0u) == -1);
}

static void lifecycle_tests(void)
{
    setup();
    assert(board_center_request(DIAG_CENTER_ALL, 0u, DIAG_CENTER_DEFAULT_BRIGHTNESS) == DIAG_SUCCESS);
    assert(write_count == 0u && read_count == 0u);
    assert(board_center_report()->state == DIAG_CENTER_QUEUED);
    assert(board_center_request(DIAG_CENTER_CHANNEL, 5u, 128u) == DIAG_BUSY);
    board_center_task(10u);
    const diag_center_report_t *r = board_center_report();
    assert(r->state == DIAG_CENTER_READY && r->last_result == DIAG_SUCCESS);
    assert(r->enabled_mask == DIAG_CENTER_KNOWN_MASK && r->frames == 1u && r->updated_ms == 10u);
    assert(nop_count == 64u * (DIAG_CENTER_FRAME_WORDS + 2u));
    assert(barriers == 2u && (gpio_odr & 0x38u) == 0u);
    assert((gpio_odr & ~0x38u) == (0xa5a5u & ~0x38u));
    assert(*stored(UINT32_C(0x40023830)) == (UINT32_C(0xa5a5a5a5) | 0x10u));
    assert(*stored(UINT32_C(0x40021000)) == ((UINT32_C(0xa5a5a5a5) & ~0xfc0u) | 0x540u));
    assert(*stored(UINT32_C(0x40021004)) == (UINT32_C(0xa5a5a5a5) & ~0x38u));
    assert(*stored(UINT32_C(0x40021008)) == (UINT32_C(0xa5a5a5a5) | 0xfc0u));
    assert(*stored(UINT32_C(0x4002100c)) == (UINT32_C(0xa5a5a5a5) & ~0xfc0u));
    uint32_t sequence[DIAG_CENTER_FRAME_WORDS], low_count = 0u;
    unsigned captured = 0u, first_low = UINT32_MAX, mode_write = UINT32_MAX;
    for (unsigned i = 0u; i < write_count; ++i) {
        if (writes[i].address == UINT32_C(0x40021000)) mode_write = i;
        if (writes[i].address != UINT32_C(0x40021018)) continue;
        if (low_count == 0u) { first_low = i; assert(writes[i].value == CENTER_ALL_LOW); }
        else if (captured < DIAG_CENTER_FRAME_WORDS) sequence[captured++] = writes[i].value;
        else assert(writes[i].value == CENTER_ALL_LOW);
        ++low_count;
    }
    assert(first_low < mode_write && captured == DIAG_CENTER_FRAME_WORDS && low_count == DIAG_CENTER_FRAME_WORDS + 2u);
    uint16_t decoded[24]; decode_frame(sequence, decoded);
    for (unsigned channel = 0u; channel < 24u; ++channel) assert(decoded[channel] == (channel >= 5u ? 128u : 0u));
    unsigned saved_writes = write_count;
    board_center_task(11u); assert(write_count == saved_writes);
    assert(board_center_request(DIAG_CENTER_CHANNEL, 23u, 4095u) == DIAG_SUCCESS);
    board_center_task(UINT32_MAX);
    assert(r->enabled_mask == (UINT32_C(1) << 23) && r->updated_ms == UINT32_MAX && r->frames == 2u);
    assert(board_center_request(DIAG_CENTER_ALL, 0u, 128u) == DIAG_SUCCESS);
    assert(board_center_request(DIAG_CENTER_OFF, 0u, 0u) == DIAG_SUCCESS);
    board_center_task(0u);
    assert(r->enabled_mask == 0u && r->frames == 3u && r->brightness == 0u);
    assert(board_center_request(DIAG_CENTER_CHANNEL, 0u, 4095u) == DIAG_SUCCESS);
    board_center_task(1u); assert(r->enabled_mask == 0u && r->frames == 4u);
}

static void rejection_and_clock_tests(void)
{
    setup();
    assert(board_center_request(3u, 0u, 0u) == DIAG_INVALID);
    assert(board_center_request(DIAG_CENTER_CHANNEL, 24u, 128u) == DIAG_INVALID);
    assert(board_center_request(DIAG_CENTER_ALL, 0u, 4096u) == DIAG_INVALID);
    assert(board_center_request(UINT32_MAX, UINT32_MAX, UINT32_MAX) == DIAG_INVALID);
    assert(write_count == 0u && read_count == 0u && board_center_report()->frames == 0u);
    g_diag_mailbox.hclk_hz = UINT32_C(16000000);
    now_ms = 5u;
    assert(board_center_request(DIAG_CENTER_ALL, 0u, 128u) == DIAG_UNAVAILABLE);
    assert(write_count == 0u && read_count == 0u);
    assert(board_center_report()->state == DIAG_CENTER_FAULT && board_center_report()->errors == 1u);
    assert(board_center_report()->last_result == DIAG_UNAVAILABLE && board_center_report()->updated_ms == 5u);
    g_diag_mailbox.hclk_hz = UINT32_C(168000000);
    assert(board_center_request(DIAG_CENTER_ALL, 0u, 128u) == DIAG_SUCCESS);
    g_diag_mailbox.hclk_hz = 0u;
    board_center_task(6u);
    assert(board_center_report()->errors == 2u && write_count == 0u && read_count == 0u);
    g_diag_mailbox.hclk_hz = UINT32_C(168000000);
    assert(board_center_request(DIAG_CENTER_ALL, 0u, 128u) == DIAG_SUCCESS);
    board_center_task(7u);
    assert(board_center_report()->frames == 1u && board_center_report()->errors == 2u);
}

static void independent_levels_tests(void)
{
    uint16_t input[24] = {0}, decoded[24];
    setup();
    for (unsigned n = 5u; n < 24u; ++n) input[n] = (uint16_t)(n * 101u);
    assert(board_center_request_levels(input) == DIAG_SUCCESS);
    assert(board_center_report()->mode == DIAG_CENTER_LEVELS && !write_count);
    const diag_center_report_t before = *board_center_report();
    assert(board_center_request_levels(NULL) == DIAG_INVALID);
    input[0] = 1u;
    assert(board_center_request_levels(input) == DIAG_INVALID);
    input[0] = 0u; input[23] = 4096u;
    assert(board_center_request_levels(input) == DIAG_INVALID);
    assert(memcmp(&before, board_center_report(), sizeof before) == 0);
    input[23] = 2323u;
    assert(board_center_request_levels(input) == DIAG_BUSY);
    input[5] = 4095u; /* Mutation after queue cannot change the copied frame. */
    board_center_task(9u);
    uint32_t sequence[DIAG_CENTER_FRAME_WORDS];
    unsigned captured = 0u, seen_preload = 0u;
    for (unsigned n = 0u; n < write_count; ++n)
        if (writes[n].address == UINT32_C(0x40021018)) {
            if (!seen_preload) seen_preload = 1u;
            else if (captured < DIAG_CENTER_FRAME_WORDS) sequence[captured++] = writes[n].value;
        }
    assert(captured == DIAG_CENTER_FRAME_WORDS);
    decode_frame(sequence, decoded);
    for (unsigned n = 0u; n < 24u; ++n) assert(decoded[n] == (n < 5u ? 0u : n * 101u));
    assert(board_center_report()->enabled_mask == DIAG_CENTER_KNOWN_MASK);
    assert(board_center_request(DIAG_CENTER_LEVELS, 0u, 0u) == DIAG_INVALID);
    assert(board_center_request_levels(input) == DIAG_SUCCESS);
    assert(board_center_request(DIAG_CENTER_OFF, 0u, 0u) == DIAG_SUCCESS);
    board_center_task(10u);
    assert(board_center_report()->enabled_mask == 0u);
    g_diag_mailbox.hclk_hz = 0u;
    assert(board_center_request_levels(input) == DIAG_UNAVAILABLE);
    assert(board_center_report()->state == DIAG_CENTER_FAULT);
}

int main(void)
{
    encoder_tests();
    mapping_tests();
    lifecycle_tests();
    rejection_and_clock_tests();
    independent_levels_tests();
    puts("diagnostic center encoder, mappings and CPU GPIO lifecycle tests passed");
    return 0;
}
