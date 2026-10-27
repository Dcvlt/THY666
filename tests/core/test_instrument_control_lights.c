/* Actual shared-state -> board center encoder with fake GPIO; no device I/O. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/diagnostic/board.h"
#include "../../firmware/diagnostic/center.h"
#include "../../firmware/diagnostic/controls.h"
#include "../../firmware/instrument/control_lights.h"

volatile diag_report_t g_diag_mailbox;
static uint32_t now_ms, pins, mmio_writes, bits;
static uint16_t decoded[DIAG_CENTER_CHANNELS];
static thy_core core, core_before;
static thy_instrument instrument, instrument_before;
static thy_engine engine;

uint32_t board_millis(void) { return now_ms; }
const volatile diag_report_t *board_report(void) { return &g_diag_mailbox; }
static uint32_t read_mmio(uint32_t address) { (void)address; return 0u; }
static void write_mmio(uint32_t address, uint32_t value)
{
    ++mmio_writes;
    if (address != UINT32_C(0x40021018)) return;
    uint32_t previous = pins;
    pins = (pins & ~(value >> 16)) | (value & 0xffffu);
    if ((pins & 16u) && !(previous & 16u)) {
        if (bits == 288u) { bits = 0u; memset(decoded, 0, sizeof decoded); }
        assert(bits < 288u);
        decoded[bits / 12u] = (uint16_t)((decoded[bits / 12u] << 1) | ((pins >> 5) & 1u));
        ++bits;
    }
}
#define THY_CENTER_READ32(address) read_mmio(address)
#define THY_CENTER_WRITE32(address,value) write_mmio(address,value)
#define THY_CENTER_NOP() ((void)0)
#define THY_CENTER_BARRIER() ((void)0)
#include "../../firmware/diagnostic/center.c"
#include "../../firmware/diagnostic/controls.c"
#include "../../firmware/instrument/control_lights.c"

static void setup(void)
{
    memset((void *)&g_diag_mailbox, 0, sizeof g_diag_mailbox);
    g_diag_mailbox.hclk_hz = 168000000u;
    now_ms = pins = mmio_writes = bits = 0u;
    memset(decoded, 0, sizeof decoded);
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&engine, &instrument, 21u, 70u, undo_workspace_1);
    board_center_init();
    board_controls_init();
    thy_instrument_control_lights_init();
    assert(!mmio_writes);
}

static void tick(uint32_t now)
{
    now_ms = now;
    thy_instrument_control_lights_task(&engine, now);
}

static void complete(uint32_t now)
{
    now_ms = now;
    board_center_task(now);
    tick(now);
}

static void assert_current_frame(void)
{
    uint16_t logical[19];
    thy_engine_center_levels(&engine, logical);
    assert(bits == 288u);
    for (unsigned n = 0u; n < 5u; ++n) assert(!decoded[n]);
    for (unsigned n = 0u; n < 19u; ++n) {
        int channel = diag_center_logical_channel(n < 5u ? 0u : 1u, n < 5u ? n : n - 5u);
        assert(channel >= 5 && decoded[channel] == logical[n]);
    }
}

static void changed_frames_only_and_coalescing(void)
{
    setup();
    instrument_before = instrument; core_before = core;
    tick(0u);
    assert(!mmio_writes && status.waiting && !status.updates);
    tick(1u);
    assert(board_center_report()->state == DIAG_CENTER_QUEUED);
    complete(2u);
    assert(status.updates == 1u && !status.waiting);
    assert_current_frame();
    assert(memcmp(&instrument_before, &instrument, sizeof instrument) == 0);
    assert(memcmp(&core_before, &core, sizeof core) == 0);
    uint32_t writes = mmio_writes;
    for (unsigned n = 3u; n < 50u; ++n) tick(n);
    assert(mmio_writes == writes && board_center_report()->frames == 1u);
    instrument.performance.inversion = 1u;
    tick(50u);
    instrument.performance.inversion = 2u; /* Coalesce while the old frame is queued. */
    tick(51u);
    complete(52u);
    assert(status.updates == 2u && !status.waiting);
    tick(65u); assert(!status.waiting);
    tick(66u); assert(status.waiting);
    complete(67u);
    assert(status.updates == 3u && board_center_report()->frames == 3u);
    assert_current_frame();
    writes = mmio_writes;
    tick(100u); assert(mmio_writes == writes && !status.waiting);
    instrument.center_brightness = 0u;
    tick(101u); assert(status.waiting);
    complete(102u);
    assert(board_center_report()->enabled_mask == 0u);
    assert_current_frame();
}

static void diagnostic_busy_and_replacement(void)
{
    setup();
    assert(board_center_request(DIAG_CENTER_ALL, 0u, 128u) == DIAG_SUCCESS);
    tick(0u); assert(!status.waiting && !mmio_writes);
    board_center_task(1u);
    tick(1u); assert(status.waiting);
    complete(2u); assert_current_frame();
    assert(board_center_request(DIAG_CENTER_OFF, 0u, 0u) == DIAG_SUCCESS);
    board_center_task(20u);
    assert(!board_center_report()->enabled_mask);
    tick(20u); assert(status.waiting);
    complete(21u); assert_current_frame();
    assert(!status.errors);
}

static void clock_fault_retries_are_bounded_and_keep_latest_state(void)
{
    setup();
    g_diag_mailbox.hclk_hz = 0u;
    tick(0u);
    assert(status.errors == 1u && !status.waiting && !mmio_writes);
    g_diag_mailbox.hclk_hz = 168000000u;
    instrument.performance.strum = 1u;
    tick(249u); assert(!status.waiting && !mmio_writes);
    tick(250u); assert(status.waiting);
    g_diag_mailbox.hclk_hz = 0u;
    complete(251u);
    assert(status.errors == 2u && !status.waiting && !mmio_writes);
    g_diag_mailbox.hclk_hz = 168000000u;
    instrument.global_excluded_slots = 1u;
    tick(500u); assert(!status.waiting);
    tick(501u); assert(status.waiting);
    complete(502u);
    assert(status.updates == 1u && !status.waiting);
    assert_current_frame();
    assert(!core.sounding_count && !core.queue_count && !instrument.errors);
    setup();
    g_diag_mailbox.hclk_hz = 0u;
    tick(UINT32_MAX - 100u);
    g_diag_mailbox.hclk_hz = 168000000u;
    tick(148u); assert(!status.waiting);
    tick(149u); assert(status.waiting);
    complete(150u); assert_current_frame();
}

static void publish_function_status(uint8_t low, uint8_t high, uint32_t now)
{
    g_diag_mailbox.touch_masks[1] = (uint32_t)low | ((uint32_t)high << 8);
    g_diag_mailbox.touch_ok_mask |= 2u;
    ++g_diag_mailbox.sensor[1].status_reads;
    g_diag_mailbox.sensor[1].last_status_ms = now;
    board_controls_task(now);
    assert(board_controls_report()->banks[1].valid);
    assert(thy_instrument_controls(&instrument, 1u,
        board_controls_report()->banks[1].logical_pressed) == THY_OK);
}

static void lower_handler_to_encoded_channel_regression(uint8_t mode,
    uint8_t available)
{
    /* The physical controls join adjacent channel/brightness records,
     * not array positions. Keep the independently verified mapping fixture:
     * it catches first/third indicator reversals and repeated toggle errors. */
    static const struct {
        uint8_t descriptor_low, descriptor_high, logical, brightness_offset;
    } handlers[] = {
        {0,1,7,0x1f}, {0,2,8,0x1c}, {0,4,9,0x19}, {0,8,10,0x22},
        {0,16,11,0x25}, {0,64,12,0x28}, {4,0,13,0x2b}
    };
    static const struct { uint8_t offset, channel; } initialized[] = {
        {0x18,18}, {0x1b,19}, {0x1e,20}, {0x21,17},
        {0x24,14}, {0x27,13}, {0x2a,12}
    };
    static const int16_t degrees[] = {0,2,4,5,7,9,11};
    static thy_instrument_pad pads[THY_PAD_COUNT];
    for (unsigned n = 0u; n < sizeof handlers / sizeof handlers[0]; ++n) {
        uint16_t initial_frame[DIAG_CENTER_CHANNELS];
        setup();
        assert(instrument.lower_mode == THY_LOWER_OCTAVE_LAYERS);
        assert(thy_instrument_lower_mask(&instrument) == 8u);
        for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad)
            pads[pad] = (thy_instrument_pad){{60,64,67,71,74,77,81},
                pad < 48u ? available : 1u, 1u};
        assert(thy_instrument_set_layout(&instrument, pads, 60, degrees, 7u) == THY_OK);
        assert(thy_instrument_set_lower_mode(&instrument, mode) == THY_OK);
        assert(instrument.slot_scope == THY_SLOTS_LEFT_SIDE);
        if (mode == THY_LOWER_OCTAVE_LAYERS) {
            assert(instrument.lower_mode == THY_LOWER_OCTAVE_LAYERS);
            assert(thy_instrument_lower_mask(&instrument) == 8u);
            assert(thy_instrument_lower_available(&instrument) == 127u);
            /* Keep a second layer for the center-button case so the
             * protective last-layer fallback does not hide its OFF state. */
            if (n == 3u) instrument.octave_layers = 12u;
        } else assert(thy_instrument_lower_available(&instrument) == available);
        uint8_t original_mask = thy_instrument_lower_mask(&instrument);
        uint8_t button_bit = (uint8_t)(1u << n);
        int can_change = mode == THY_LOWER_OCTAVE_LAYERS || (available & button_bit);
        uint8_t changed_mask = can_change ? original_mask ^ button_bit : original_mask;
        publish_function_status(0u, 0u, 0u);
        tick(0u); complete(1u);
        memcpy(initial_frame, decoded, sizeof initial_frame);
        unsigned channel = DIAG_CENTER_CHANNELS;
        for (unsigned j = 0u; j < sizeof initialized / sizeof initialized[0]; ++j)
            if (initialized[j].offset + 1u == handlers[n].brightness_offset)
                channel = initialized[j].channel;
        assert(channel < DIAG_CENTER_CHANNELS);
        uint32_t start = 100u + n * 100u;
        publish_function_status(handlers[n].descriptor_low,
            handlers[n].descriptor_high, start);
        assert(instrument.control_masks[1] == (UINT32_C(1) << handlers[n].logical));
        assert(thy_instrument_lower_mask(&instrument) == changed_mask);
        assert(instrument.global_excluded_slots ==
            (mode == THY_LOWER_STORED_NOTES && can_change ? button_bit : 0u));
        tick(start); complete(start + 1u);
        assert(decoded[channel] == 4095u); /* Touched switch is highlighted. */
        for (unsigned other = 0u; other < DIAG_CENTER_CHANNELS; ++other)
            if (other != channel) assert(decoded[other] == initial_frame[other]);
        publish_function_status(handlers[n].descriptor_low,
            handlers[n].descriptor_high, start + 10u);
        assert(thy_instrument_lower_mask(&instrument) == changed_mask); /* No held repeats. */
        publish_function_status(0u, 0u, start + 20u);
        tick(start + 20u); complete(start + 21u);
        uint16_t changed_level = can_change ?
            (initial_frame[channel] == 4095u ? 256u : 4095u) : 256u;
        for (unsigned other = 0u; other < DIAG_CENTER_CHANNELS; ++other)
            assert(decoded[other] == (other == channel ? changed_level : initial_frame[other]));
        /* A second complete touch restores this same channel; neighboring
         * indicators must never be used to display its toggle or hold. */
        publish_function_status(handlers[n].descriptor_low,
            handlers[n].descriptor_high, start + 40u);
        assert(!instrument.global_excluded_slots);
        assert(thy_instrument_lower_mask(&instrument) == original_mask);
        tick(start + 40u); complete(start + 41u);
        publish_function_status(0u, 0u, start + 60u);
        tick(start + 60u); complete(start + 61u);
        assert(!instrument.control_masks[1]);
        for (unsigned other = 0u; other < DIAG_CENTER_CHANNELS; ++other)
            assert(decoded[other] == initial_frame[other]);
        assert(!core.sounding_count && !core.queue_count);
        assert(!status.errors && !instrument.errors);
    }
}

int main(void)
{
    changed_frames_only_and_coalescing();
    diagnostic_busy_and_replacement();
    clock_fault_retries_are_bounded_and_keep_latest_state();
    lower_handler_to_encoded_channel_regression(THY_LOWER_OCTAVE_LAYERS, 7u);
    lower_handler_to_encoded_channel_regression(THY_LOWER_STORED_NOTES, 127u);
    lower_handler_to_encoded_channel_regression(THY_LOWER_STORED_NOTES, 7u);
    puts("instrument center lights passed: all seven control descriptor/handler/channel pairings, octave/stored repeat toggles, absent-note availability, coalescing and recovery");
    return 0;
}
