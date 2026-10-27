/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual stock LED DMA driver plus integrated feedback adapter with fake
 * MMIO. This checks ownership and emitted words, not optical hardware. */
#define main diagnostic_led_regression_main
#include "test_diagnostic_led.c"
#undef main
#include "../../firmware/instrument/led.c"

static void equal_bitmap(const uint32_t a[3], const uint32_t b[3])
{
    for (unsigned i = 0u; i < 3u; ++i) assert(a[i] == b[i]);
}

static void bitmap_encoder_tests(void)
{
    uint16_t frame[DIAG_LED_FRAME_WORDS + 1u];
    uint32_t selected[3] = {UINT32_C(0x80000001), UINT32_C(0x80000001), UINT32_C(0x80000001)};
    frame[DIAG_LED_FRAME_WORDS] = 0xabcd;
    assert(diag_led_encode_bitmap(frame, DIAG_LED_FRAME_WORDS, selected) == DIAG_LED_FRAME_WORDS);
    for (unsigned i = 0u; i < DIAG_LED_COUNT; ++i)
        for (unsigned c = 0u; c < 3u; ++c)
            assert(decode_component(frame, i, c) == ((i % 32u == 0u || i % 32u == 31u) ? 8u : 0u));
    for (unsigned i = 0u; i < DIAG_LED_RESET_WORDS; ++i) assert(frame[i] == 0u);
    for (unsigned i = DIAG_LED_RESET_WORDS + DIAG_LED_DATA_WORDS; i < DIAG_LED_FRAME_WORDS; ++i)
        assert(frame[i] == 0u);
    assert(frame[DIAG_LED_FRAME_WORDS] == 0xabcd);
    selected[0] = selected[1] = selected[2] = UINT32_MAX;
    assert(diag_led_encode_bitmap(frame, DIAG_LED_FRAME_WORDS, selected));
    for (unsigned i = 0u; i < DIAG_LED_COUNT; ++i)
        for (unsigned c = 0u; c < 3u; ++c) assert(decode_component(frame, i, c) == 8u);
    assert(!diag_led_encode_bitmap(NULL, DIAG_LED_FRAME_WORDS, selected));
    assert(!diag_led_encode_bitmap(frame, DIAG_LED_FRAME_WORDS - 1u, selected));
    assert(!diag_led_encode_bitmap(frame, DIAG_LED_FRAME_WORDS, NULL));
}

static void bitmap_dma_coalescing_tests(void)
{
    setup();
    uint32_t a[3] = {1u, 0u, 0u}, b[3] = {0u, 0u, 0x80000000u};
    uint32_t c[3] = {0x80000001u, 1u, 0x80000000u}, off[3] = {0u, 0u, 0u};
    uint16_t in_flight[DIAG_LED_FRAME_WORDS];
    assert(board_led_request_bitmap(NULL) == DIAG_INVALID);
    assert(board_led_request_bitmap(a) == DIAG_SUCCESS);
    assert(accesses == 0u);
    board_led_task(0u);
    memcpy(in_flight, led_words, sizeof in_flight);
    assert(board_led_request_bitmap(b) == DIAG_SUCCESS);
    assert(board_led_request_bitmap(c) == DIAG_SUCCESS); /* Latest pending wins. */
    assert(memcmp(in_flight, led_words, sizeof in_flight) == 0);
    finish(5u);
    assert(board_led_report()->frames == 1u && board_led_report()->state == DIAG_LED_SENDING);
    for (unsigned i = 0u; i < DIAG_LED_COUNT; ++i)
        for (unsigned channel = 0u; channel < 3u; ++channel)
            assert(decode_component(led_words, i, channel) ==
                ((c[i / 32u] & (1u << (i & 31u))) ? 8u : 0u));
    memcpy(in_flight, led_words, sizeof in_flight);
    assert(board_led_request_bitmap(off) == DIAG_SUCCESS);
    assert(memcmp(in_flight, led_words, sizeof in_flight) == 0);
    finish(10u);
    assert(board_led_report()->state == DIAG_LED_SENDING);
    for (unsigned i = 0u; i < DIAG_LED_COUNT; ++i)
        for (unsigned channel = 0u; channel < 3u; ++channel)
            assert(decode_component(led_words, i, channel) == 0u);
    finish(15u);
    assert(board_led_report()->frames == 3u && board_led_report()->off_confirmed == 1u);
    assert(board_led_report()->state == DIAG_LED_IDLE);

    setup();
    assert(board_led_request_bitmap(a) == DIAG_SUCCESS);
    board_led_task(0u); finish(5u);
    size_t previous_events = event_count;
    fake_now = 5000u;
    assert(board_led_request_bitmap(a) == DIAG_SUCCESS);
    board_led_task(5000u);
    assert(event_count == previous_events && board_led_report()->frames == 1u);
    assert(board_led_report()->expires_ms == 15000u);
    board_led_task(14999u);
    assert(board_led_report()->state == DIAG_LED_HOLDING);
    board_led_task(15000u); finish(15005u);
    assert(board_led_report()->off_confirmed == 1u && board_led_report()->frames == 2u);
}

static void color_encoding_and_dma_tests(void)
{
    uint8_t a[DIAG_LED_COUNT][3], b[DIAG_LED_COUNT][3], off[DIAG_LED_COUNT][3] = {{0}};
    uint16_t frame[DIAG_LED_FRAME_WORDS + 1u], in_flight[DIAG_LED_FRAME_WORDS];
    for (unsigned i = 0u; i < DIAG_LED_COUNT; ++i)
        for (unsigned c = 0u; c < 3u; ++c) {
            a[i][c] = (uint8_t)((i + c) % 9u);
            b[i][c] = (uint8_t)(8u - a[i][c]);
        }
    frame[DIAG_LED_FRAME_WORDS] = 0xabcd;
    assert(diag_led_encode_colors(frame, DIAG_LED_FRAME_WORDS, a) == DIAG_LED_FRAME_WORDS);
    for (unsigned i = 0u; i < DIAG_LED_COUNT; ++i)
        for (unsigned c = 0u; c < 3u; ++c) assert(decode_component(frame, i, c) == a[i][c]);
    for (unsigned i = 0u; i < DIAG_LED_RESET_WORDS; ++i) assert(frame[i] == 0u);
    for (unsigned i = DIAG_LED_RESET_WORDS + DIAG_LED_DATA_WORDS; i < DIAG_LED_FRAME_WORDS; ++i)
        assert(frame[i] == 0u);
    assert(frame[DIAG_LED_FRAME_WORDS] == 0xabcd);
    assert(!diag_led_encode_colors(NULL, DIAG_LED_FRAME_WORDS, a));
    assert(!diag_led_encode_colors(frame, DIAG_LED_FRAME_WORDS - 1u, a));
    assert(!diag_led_encode_colors(frame, DIAG_LED_FRAME_WORDS, NULL));
    setup();
    assert(board_led_request_colors(NULL) == DIAG_INVALID);
    a[95][2] = 9u;
    assert(board_led_request_colors(a) == DIAG_INVALID); /* Tested amplitude cap. */
    a[95][2] = 7u;
    assert(board_led_request_colors(a) == DIAG_SUCCESS && accesses == 0u);
    board_led_task(0u);
    memcpy(in_flight, led_words, sizeof in_flight);
    assert(board_led_request_colors(b) == DIAG_SUCCESS);
    assert(memcmp(in_flight, led_words, sizeof in_flight) == 0);
    /* Producer storage can immediately be reused after queuing. */
    uint8_t last = b[95][2]; b[95][2] = 0u;
    finish(5u);
    assert(decode_component(led_words, 95u, 2u) == last);
    memcpy(in_flight, led_words, sizeof in_flight);
    assert(board_led_request_colors(off) == DIAG_SUCCESS);
    assert(memcmp(in_flight, led_words, sizeof in_flight) == 0);
    finish(10u); finish(15u);
    assert(board_led_report()->off_confirmed && board_led_report()->frames == 3u);

    setup();
    assert(board_led_request_colors(a) == DIAG_SUCCESS);
    board_led_task(0u); finish(5u);
    size_t previous_events = event_count;
    fake_now = 5000u;
    assert(board_led_request_colors(a) == DIAG_SUCCESS);
    board_led_task(fake_now);
    assert(event_count == previous_events && board_led_report()->expires_ms == 15000u);
    board_led_task(15000u); finish(15005u);
    assert(board_led_report()->off_confirmed && board_led_report()->frames == 2u);
}

static void musical_note_color_adapter_tests(void)
{
    static thy_core core;
    static thy_instrument instrument;
    static thy_instrument_pad pads[THY_PAD_COUNT];
    const int16_t degrees[] = {0,2,4,5,7,9,11};
    uint8_t mapping[THY_PAD_COUNT];
    setup(); thy_instrument_lights_init();
    memset(pads, 0, sizeof pads);
    pads[0] = (thy_instrument_pad){{60,64,67}, 7u, 42u};
    pads[48] = (thy_instrument_pad){{62}, 1u, 19u};
    thy_core_init(&core); thy_instrument_init(&instrument, &core);
    assert(thy_instrument_set_layout(&instrument, pads, 60, degrees, 7) == THY_OK);
    for (unsigned i = 0u; i < THY_PAD_COUNT; ++i) mapping[i] = (uint8_t)i;
    mapping[0] = 95u; mapping[95] = 0u;
    assert(thy_instrument_set_mapping(&instrument, mapping) == THY_OK);
    instrument.light_fade_ms = 0u;
    thy_instrument_lights_task(&instrument, 0u); board_led_task(0u); finish(5u);
    assert(decode_component(led_words, 95u, 0u) == 0u);
    assert(decode_component(led_words, 95u, 1u) == 3u);
    assert(decode_component(led_words, 95u, 2u) == 0u);
    assert(decode_component(led_words, 0u, 1u) == 0u); /* No physical-index mixup. */
    assert(decode_component(led_words, 48u, 0u) == 1u);
    assert(decode_component(led_words, 48u, 1u) == 3u);
    assert(thy_core_press(&core, 95, pads[0].notes, 3, 0, 0, THY_ROUTE_DIN, 96) == THY_OK);
    instrument.sensors[95].held = 1u;
    fake_now = 20u;
    thy_instrument_lights_task(&instrument, fake_now); board_led_task(fake_now); finish(25u);
    assert(decode_component(led_words, 95u, 1u) == 8u);
    /* Light-disable OFF bypasses50Hz coalescing and preserves note ownership. */
    instrument.lights_enabled = 0u; fake_now = 26u;
    thy_instrument_lights_task(&instrument, fake_now); board_led_task(fake_now); finish(31u);
    assert(board_led_report()->off_confirmed && thy_core_pad_held(&core, 95));
    assert(instrument.sensors[95].held && !instrument.sensors[95].blocked);
}

static void feedback_registration_release_tests(void)
{
    setup(); thy_instrument_lights_init();
    thy_instrument instrument = {0};
    instrument.input_enabled = 1u;
    const uint32_t off[3] = {0u, 0u, 0u};
    thy_instrument_lights_task(&instrument, 0u);
    assert(accesses == 0u);
    board_led_task(0u); finish(5u);
    assert(board_led_report()->off_confirmed == 1u);
    instrument.sensors[0].held = 1u;
    instrument.sensors[31].held = 1u;
    instrument.sensors[32].held = 1u;
    instrument.sensors[95].held = 1u;
    instrument.sensors[31].blocked = 1u;
    /* Layout remapping must not redirect physical feedback. */
    instrument.mapping[0] = 95u;
    thy_instrument_lights_task(&instrument, 10u);
    equal_bitmap(thy_instrument_lights_report()->selected, off); /* 20ms coalescing. */
    fake_now = 20u;
    thy_instrument_lights_task(&instrument, fake_now);
    const uint32_t held[3] = {1u, 1u, 0x80000000u};
    equal_bitmap(thy_instrument_lights_report()->selected, held);
    board_led_task(20u); finish(25u);
    assert(board_led_report()->off_confirmed == 0u);
    /* One released pad goes dark without losing simultaneous held pads. */
    instrument.sensors[0].held = 0u;
    fake_now = 40u;
    thy_instrument_lights_task(&instrument, fake_now);
    board_led_task(40u); finish(45u);
    const uint32_t two[3] = {0u, 1u, 0x80000000u};
    equal_bitmap(thy_instrument_lights_report()->selected, two);
    /* Panic marks remaining held inputs blocked. Its full OFF is immediate. */
    instrument.sensors[32].blocked = instrument.sensors[95].blocked = 1u;
    fake_now = 46u;
    thy_instrument_lights_task(&instrument, fake_now);
    equal_bitmap(thy_instrument_lights_report()->selected, off);
    board_led_task(46u); finish(51u);
    assert(board_led_report()->off_confirmed == 1u);
    /* Disabling acquisition/readiness also makes every held pad dark. */
    instrument.sensors[32].blocked = 0u;
    instrument.input_enabled = 0u;
    thy_instrument_lights_task(&instrument, 52u);
    equal_bitmap(thy_instrument_lights_report()->selected, off);
    instrument.input_enabled = 1u;
    fake_now = 70u;
    thy_instrument_lights_task(&instrument, fake_now);
    board_led_task(70u); finish(75u);
    assert(decode_component(led_words, 32u, 0u) == 8u);
    instrument.input_enabled = 0u;
    fake_now = 76u;
    thy_instrument_lights_task(&instrument, fake_now);
    board_led_task(76u); finish(81u);
    assert(board_led_report()->off_confirmed == 1u);
    size_t previous_events = event_count;
    thy_instrument_lights_task(&instrument, 6000u);
    board_led_task(6000u);
    assert(event_count == previous_events); /* No repeated idle OFF frames. */
}

static void feedback_lease_fault_wrap_tests(void)
{
    setup(); thy_instrument_lights_init();
    thy_instrument instrument = {0}; instrument.input_enabled = 1u;
    instrument.sensors[64].held = 1u;
    fake_now = UINT32_MAX - 100u;
    thy_instrument_lights_task(&instrument, fake_now);
    board_led_task(fake_now); finish(fake_now + 5u);
    uint32_t start = fake_now;
    size_t previous_events = event_count;
    fake_now = start + 5000u;
    thy_instrument_lights_task(&instrument, fake_now); board_led_task(fake_now);
    assert(event_count == previous_events && board_led_report()->frames == 1u);
    assert(board_led_report()->expires_ms == fake_now + 10000u);
    /* A single DMA fault permanently gates feedback for this boot and queues
     * one full OFF; sensor state is never changed by the lighting adapter. */
    instrument.sensors[65].held = 1u;
    fake_now += 20u;
    thy_instrument_lights_task(&instrument, fake_now); board_led_task(fake_now);
    *stored(UINT32_C(0x40026004)) = 8u;
    board_led_task(fake_now + 1u);
    assert(board_led_report()->state == DIAG_LED_FAULT);
    thy_instrument_lights_task(&instrument, fake_now + 1u);
    assert(thy_instrument_lights_report()->faulted == 1u &&
           thy_instrument_lights_report()->errors == 1u);
    assert(instrument.sensors[64].held && instrument.sensors[65].held);
    board_led_task(fake_now + 2u); finish(fake_now + 7u);
    assert(board_led_report()->off_confirmed == 1u);
    previous_events = event_count;
    thy_instrument_lights_task(&instrument, fake_now + 8u);
    board_led_task(fake_now + 8u);
    assert(event_count == previous_events);
}

static void all_electrical_feedback_slots_tests(void)
{
    setup(); thy_instrument_lights_init();
    thy_instrument instrument = {0}; instrument.input_enabled = 1u;
    for (unsigned i = 0u; i < THY_PAD_COUNT; ++i)
        instrument.mapping[i] = (uint8_t)(95u - i);
    for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad) {
        if (pad != 0u) instrument.sensors[pad - 1u].held = 0u;
        instrument.sensors[pad].held = 1u;
        fake_now = pad * 20u;
        thy_instrument_lights_task(&instrument, fake_now);
        board_led_task(fake_now); finish(fake_now + 5u);
        for (unsigned pixel = 0u; pixel < DIAG_LED_COUNT; ++pixel)
            for (unsigned channel = 0u; channel < 3u; ++channel)
                assert(decode_component(led_words, pixel, channel) == (pixel == pad ? 8u : 0u));
    }
    assert(board_led_report()->frames == 96u && board_led_report()->errors == 0u);
    instrument.sensors[95].held = 0u;
    fake_now += 6u;
    thy_instrument_lights_task(&instrument, fake_now);
    board_led_task(fake_now); finish(fake_now + 5u);
    assert(board_led_report()->off_confirmed == 1u);
}

static unsigned calibration_target;
static int calibration_colors(void *context,uint32_t now,uint32_t colors[96])
{
    (void)now;assert(context==&lights);
    for(unsigned p=0;p<96u;++p)colors[p]=0u;
    colors[calibration_target]=0x080001u;return 1;
}
static void calibration_override_adapter_tests(void)
{
    static thy_core core;static thy_instrument instrument;static thy_instrument_pad pads[96];
    const int16_t degrees[]={0,2,4,5,7,9,11};
    setup();thy_instrument_lights_init();thy_core_init(&core);thy_instrument_init(&instrument,&core);
    memset(pads,0,sizeof pads);
    assert(thy_instrument_set_layout(&instrument,pads,60,degrees,7u)==THY_OK);
    /* A full nontrivial bijection catches accidental identity, inverse-map,
     * row-major and column-major assumptions at every emitted LED slot. */
    uint8_t mapping[96];
    for(unsigned p=0;p<96u;++p)mapping[p]=(uint8_t)((p*37u+11u)%96u);
    assert(thy_instrument_set_mapping(&instrument,mapping)==THY_OK);
    thy_instrument_lights_set_override(calibration_colors,&lights);
    for(unsigned logical=0u;logical<96u;++logical){
        calibration_target=logical;fake_now=logical*25u;
        thy_instrument_lights_task(&instrument,fake_now);board_led_task(fake_now);finish(fake_now+5u);
        for(unsigned electrical=0u;electrical<96u;++electrical){
            int selected=mapping[electrical]==logical;
            assert(decode_component(led_words,electrical,0u)==(selected?8u:0u));
            assert(decode_component(led_words,electrical,1u)==0u);
            assert(decode_component(led_words,electrical,2u)==(selected?1u:0u));
        }
    }
    assert(board_led_report()->frames==96u && !board_led_report()->errors);
    thy_instrument_lights_set_override(NULL,NULL);
    fake_now=2410u;thy_instrument_lights_task(&instrument,fake_now);board_led_task(fake_now);finish(fake_now+5u);
    assert(board_led_report()->off_confirmed);
}
int main(void)
{
    diagnostic_led_regression_main(0, NULL);
    bitmap_encoder_tests();
    bitmap_dma_coalescing_tests();
    color_encoding_and_dma_tests();
    feedback_registration_release_tests();
    feedback_lease_fault_wrap_tests();
    all_electrical_feedback_slots_tests();
    musical_note_color_adapter_tests();
    calibration_override_adapter_tests();
    puts("instrument bitmap LED encoding, DMA coalescing, registered feedback, OFF, lease, fault and wrap tests passed");
    return 0;
}
