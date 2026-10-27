#include <stdio.h>
#include <string.h>
#include "../../core/thy_midi.h"

static unsigned assertions;

#define CHECK(condition) do { \
    ++assertions; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct capture {
    thy_midi_event events[32];
    unsigned count;
} capture;

static void receive(void *context, const thy_midi_event *event)
{
    capture *captured = (capture *)context;
    if (captured->count < 32u) {
        captured->events[captured->count++] = *event;
    }
}

static int test_note_zero_and_din_poly_pressure(void)
{
    const thy_midi_event note = {THY_USB0, 0x90, 0, 127};
    const thy_midi_event pressure = {THY_DIN, 0xa7, 60, 101};
    uint8_t din[3] = {0};
    uint8_t usb[4] = {0};
    CHECK(thy_midi_encode_din(&note, din) == 3u);
    CHECK(din[0] == 0x90u && din[1] == 0u && din[2] == 127u);
    CHECK(thy_midi_encode_usb(&note, usb) == 4u);
    CHECK(usb[0] == 9u && usb[1] == 0x90u && usb[2] == 0u && usb[3] == 127u);
    CHECK(thy_midi_encode_din(&pressure, din) == 3u);
    CHECK(din[0] == 0xa7u && din[1] == 60u && din[2] == 101u);
    CHECK(thy_midi_encode_usb(&pressure, usb) == 0u);
    return 1;
}

static int test_f1_f2_and_transport_lengths(void)
{
    thy_midi_event event = {THY_USB1, 0xf1, 0x42, 0};
    uint8_t din[3] = {0};
    uint8_t usb[4] = {0};
    const uint8_t transport[] = {0xf8, 0xfa, 0xfb, 0xfc, 0xfe, 0xff};
    unsigned i;
    CHECK(thy_midi_encode_din(&event, din) == 2u);
    CHECK(din[0] == 0xf1u && din[1] == 0x42u);
    CHECK(thy_midi_encode_usb(&event, usb) == 4u);
    CHECK(usb[0] == 0x12u && usb[1] == 0xf1u && usb[2] == 0x42u && usb[3] == 0u);
    event.status = 0xf2;
    event.data2 = 0x34;
    CHECK(thy_midi_encode_din(&event, din) == 3u);
    CHECK(din[0] == 0xf2u && din[1] == 0x42u && din[2] == 0x34u);
    CHECK(thy_midi_encode_usb(&event, usb) == 4u);
    CHECK(usb[0] == 0x13u && usb[1] == 0xf2u && usb[2] == 0x42u && usb[3] == 0x34u);
    for (i = 0u; i < sizeof(transport); ++i) {
        event.status = transport[i];
        event.data1 = 0u;
        event.data2 = 0u;
        CHECK(thy_midi_encode_din(&event, din) == 1u);
        CHECK(din[0] == transport[i]);
        CHECK(thy_midi_encode_usb(&event, usb) == 4u);
        CHECK(usb[0] == 0x1fu && usb[1] == transport[i] && usb[2] == 0u && usb[3] == 0u);
    }
    event.status = 0xf6;
    CHECK(thy_midi_encode_usb(&event, usb) == 4u);
    CHECK(usb[0] == 0x15u && usb[1] == 0xf6u);
    return 1;
}

static int test_channel_messages_and_cc64_123(void)
{
    thy_midi_event event = {THY_USB0, 0xbf, 64, 127};
    uint8_t din[3] = {0};
    uint8_t usb[4] = {0};
    CHECK(thy_midi_encode_din(&event, din) == 3u);
    CHECK(din[0] == 0xbfu && din[1] == 64u && din[2] == 127u);
    CHECK(thy_midi_encode_usb(&event, usb) == 4u);
    CHECK(usb[0] == 0xbu && usb[1] == 0xbfu && usb[2] == 64u && usb[3] == 127u);
    event.data1 = 123u;
    event.data2 = 0u;
    CHECK(thy_midi_encode_din(&event, din) == 3u);
    CHECK(din[0] == 0xbfu && din[1] == 123u && din[2] == 0u);
    event.status = 0xc0u;
    event.data1 = 42u;
    CHECK(thy_midi_encode_din(&event, din) == 2u);
    CHECK(thy_midi_encode_usb(&event, usb) == 4u);
    CHECK(usb[0] == 0xcu && usb[1] == 0xc0u && usb[2] == 42u && usb[3] == 0u);
    event.status = 0xd1u;
    CHECK(thy_midi_encode_din(&event, din) == 2u);
    CHECK(thy_midi_encode_usb(&event, usb) == 4u);
    CHECK(usb[0] == 0xdu && usb[1] == 0xd1u);
    event.status = 0xe0u;
    event.data1 = 127u;
    event.data2 = 127u;
    CHECK(thy_midi_encode_din(&event, din) == 3u);
    CHECK(din[1] == 127u && din[2] == 127u);
    return 1;
}

static int test_invalid_encoding_does_not_write(void)
{
    thy_midi_event event = {THY_USB0, 0x90, 128, 100};
    uint8_t out[4] = {1, 2, 3, 4};
    const uint8_t before[] = {1, 2, 3, 4};
    CHECK(thy_midi_encode_din(&event, out) == 0u);
    CHECK(thy_midi_encode_usb(&event, out) == 0u);
    CHECK(memcmp(out, before, sizeof(out)) == 0);
    event.data1 = 60;
    event.data2 = 128;
    CHECK(thy_midi_encode_din(&event, out) == 0u);
    event.status = 0xc0;
    event.data2 = 1;
    CHECK(thy_midi_encode_usb(&event, out) == 0u);
    event.status = 0xf0;
    event.data2 = 0;
    CHECK(thy_midi_encode_din(&event, out) == 0u);
    event.status = 0xf9;
    CHECK(thy_midi_encode_usb(&event, out) == 0u);
    event.destination = 3;
    event.status = 0x90;
    CHECK(thy_midi_encode_din(&event, out) == 0u);
    CHECK(thy_midi_encode_usb(NULL, out) == 0u);
    CHECK(thy_midi_encode_din(NULL, out) == 0u);
    event.destination = THY_USB0;
    CHECK(thy_midi_encode_usb(&event, NULL) == 0u);
    CHECK(memcmp(out, before, sizeof(out)) == 0);
    return 1;
}

static int test_back_to_back_and_fragmented_receive(void)
{
    const uint8_t packets[] = {
        0x09, 0x90, 0, 100,
        0x1a, 0xa7, 60, 70,
        0x03, 0xf2, 1, 2,
        0x0f, 0xfa, 0, 0,
        0x0b, 0xb0, 64, 127
    };
    thy_usb_parser parser;
    capture captured = {{{0}}, 0u};
    unsigned split;
    for (split = 0u; split <= sizeof(packets); ++split) {
        thy_usb_parser_init(&parser);
        captured.count = 0u;
        thy_usb_parser_feed(&parser, packets, split, receive, &captured);
        CHECK(parser.fragment_size == split % 4u);
        thy_usb_parser_feed(&parser, packets + split, sizeof(packets) - split,
                            receive, &captured);
        CHECK(parser.received == 5u && parser.invalid == 0u && parser.unsupported == 0u);
        CHECK(parser.fragment_size == 0u);
        CHECK(captured.count == 5u);
        CHECK(captured.events[0].destination == THY_USB0 && captured.events[0].data1 == 0u);
        CHECK(captured.events[1].destination == THY_USB1 && captured.events[1].status == 0xa7u && captured.events[1].data2 == 70u);
        CHECK(captured.events[2].status == 0xf2u && captured.events[2].data1 == 1u && captured.events[2].data2 == 2u);
        CHECK(captured.events[3].status == 0xfau);
        CHECK(captured.events[4].status == 0xb0u && captured.events[4].data1 == 64u);
    }
    /* Single-byte feeds exercise persistent fragment state and old-data order. */
    thy_usb_parser_init(&parser);
    captured.count = 0u;
    for (split = 0u; split < sizeof(packets); ++split) {
        thy_usb_parser_feed(&parser, packets + split, 1u, receive, &captured);
    }
    CHECK(captured.count == 5u && parser.received == 5u);
    return 1;
}

static int test_malformed_and_unsupported_packets_are_consumed(void)
{
    const uint8_t packets[] = {
        0x02, 0xf2, 1, 2,  /* Song Position with invalid CIN */
        0x0f, 0xf1, 1, 0,  /* Time Code with invalid CIN */
        0x09, 0x90, 128, 100,
        0x0a, 0x90, 60, 100, /* status/CIN mismatch */
        0x0c, 0xc0, 1, 99,   /* nonzero two-byte message padding */
        0x29, 0x90, 60, 100, /* unimplemented cable */
        0x04, 0xf0, 1, 2,    /* unimplemented SysEx */
        0x05, 0xf7, 0, 0,    /* unimplemented SysEx ending */
        0x00, 0, 0, 0,
        0x09, 0x90, 61, 90  /* next valid event must survive */
    };
    thy_usb_parser parser;
    capture captured = {{{0}}, 0u};
    thy_usb_parser_init(&parser);
    thy_usb_parser_feed(&parser, packets, sizeof(packets), receive, &captured);
    CHECK(parser.received == 1u);
    CHECK(parser.invalid == 6u);
    CHECK(parser.unsupported == 3u);
    CHECK(parser.fragment_size == 0u);
    CHECK(captured.count == 1u && captured.events[0].data1 == 61u);
    thy_usb_parser_feed(&parser, NULL, 0u, receive, &captured);
    CHECK(parser.received == 1u);
    return 1;
}

static int test_all_supported_events_roundtrip(void)
{
    const thy_midi_event events[] = {
        {THY_USB0, 0x80, 0, 0}, {THY_USB0, 0x91, 127, 127},
        {THY_USB1, 0xa2, 42, 77}, {THY_USB1, 0xb3, 123, 0},
        {THY_USB0, 0xc4, 100, 0}, {THY_USB0, 0xd5, 60, 0},
        {THY_USB1, 0xe6, 127, 127}, {THY_USB0, 0xf1, 42, 0},
        {THY_USB1, 0xf2, 1, 2}, {THY_USB0, 0xf3, 33, 0},
        {THY_USB0, 0xf6, 0, 0}, {THY_USB1, 0xf8, 0, 0},
        {THY_USB0, 0xfa, 0, 0}, {THY_USB1, 0xfb, 0, 0},
        {THY_USB0, 0xfc, 0, 0}, {THY_USB1, 0xfe, 0, 0},
        {THY_USB0, 0xff, 0, 0}
    };
    thy_usb_parser parser;
    capture captured = {{{0}}, 0u};
    size_t i;
    thy_usb_parser_init(&parser);
    for (i = 0u; i < sizeof(events) / sizeof(events[0]); ++i) {
        uint8_t bytes[4];
        CHECK(thy_midi_encode_usb(&events[i], bytes) == 4u);
        thy_usb_parser_feed(&parser, bytes, 4u, receive, &captured);
    }
    CHECK(captured.count == sizeof(events) / sizeof(events[0]));
    CHECK(parser.invalid == 0u && parser.unsupported == 0u);
    for (i = 0u; i < captured.count; ++i) {
        CHECK(memcmp(&captured.events[i], &events[i], sizeof(thy_midi_event)) == 0);
    }
    return 1;
}

int run_midi_tests(unsigned *test_count, unsigned *assertion_count)
{
    struct named_test { const char *name; int (*run)(void); };
    const struct named_test tests[] = {
        {"note zero and DIN poly pressure", test_note_zero_and_din_poly_pressure},
        {"F1/F2 and transport lengths", test_f1_f2_and_transport_lengths},
        {"channel messages and CC64/123", test_channel_messages_and_cc64_123},
        {"invalid encoding does not write", test_invalid_encoding_does_not_write},
        {"back-to-back and fragmented USB receive", test_back_to_back_and_fragmented_receive},
        {"malformed USB events are consumed", test_malformed_and_unsupported_packets_are_consumed},
        {"supported MIDI event roundtrips", test_all_supported_events_roundtrip}
    };
    size_t i;
    for (i = 0u; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!tests[i].run()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            return 0;
        }
        ++*test_count;
        printf("PASS: %s\n", tests[i].name);
    }
    *assertion_count += assertions;
    return 1;
}
