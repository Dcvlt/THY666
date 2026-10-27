#include "thy_midi.h"

/* Channel messages use CIN 8..E; system-common length follows exact status. */
static uint8_t event_length(const thy_midi_event *event)
{
    uint8_t kind;
    if (event == NULL || event->destination >= THY_DEST_COUNT ||
        event->status < 0x80u) {
        return 0u;
    }
    kind = (uint8_t)(event->status >> 4);
    if (kind < 0x0fu) {
        if (event->data1 > 127u) {
            return 0u;
        }
        if (kind == 0x0cu || kind == 0x0du) {
            return event->data2 == 0u ? 2u : 0u;
        }
        return event->data2 <= 127u ? 3u : 0u;
    }
    switch (event->status) {
    case 0xf1u:
    case 0xf3u:
        return event->data1 <= 127u && event->data2 == 0u ? 2u : 0u;
    case 0xf2u:
        return event->data1 <= 127u && event->data2 <= 127u ? 3u : 0u;
    case 0xf6u:
    case 0xf8u:
    case 0xfau:
    case 0xfbu:
    case 0xfcu:
    case 0xfeu:
    case 0xffu:
        return event->data1 == 0u && event->data2 == 0u ? 1u : 0u;
    default:
        return 0u;
    }
}

size_t thy_midi_encode_din(const thy_midi_event *event, uint8_t out[3])
{
    uint8_t length = event_length(event);
    if (length == 0u || out == NULL) {
        return 0u;
    }
    out[0] = event->status;
    if (length >= 2u) {
        out[1] = event->data1;
    }
    if (length == 3u) {
        out[2] = event->data2;
    }
    return length;
}

size_t thy_midi_encode_usb(const thy_midi_event *event, uint8_t out[4])
{
    uint8_t length = event_length(event);
    uint8_t cin;
    if (length == 0u || out == NULL || event->destination > THY_USB1) {
        return 0u;
    }
    if (event->status < 0xf0u) {
        cin = (uint8_t)(event->status >> 4);
    } else if (length == 3u) {
        cin = 3u;
    } else if (length == 2u) {
        cin = 2u;
    } else if (event->status == 0xf6u) {
        cin = 5u;
    } else {
        cin = 0x0fu;
    }
    out[0] = (uint8_t)((event->destination << 4) | cin);
    out[1] = event->status;
    out[2] = length >= 2u ? event->data1 : 0u;
    out[3] = length == 3u ? event->data2 : 0u;
    return 4u;
}

void thy_usb_parser_init(thy_usb_parser *parser)
{
    uint8_t i;
    if (parser == NULL) {
        return;
    }
    for (i = 0u; i < 4u; ++i) {
        parser->fragment[i] = 0u;
    }
    parser->fragment_size = 0u;
    parser->received = 0u;
    parser->invalid = 0u;
    parser->unsupported = 0u;
}

static void decode_packet(thy_usb_parser *parser, thy_midi_receive_fn receive,
                           void *context)
{
    const uint8_t *bytes = parser->fragment;
    uint8_t cable = (uint8_t)(bytes[0] >> 4);
    uint8_t cin = (uint8_t)(bytes[0] & 0x0fu);
    thy_midi_event event;
    uint8_t length;
    uint8_t expected;
    if (cable > THY_USB1 || (cin >= 4u && cin <= 7u && cin != 5u)) {
        ++parser->unsupported;
        return;
    }
    if (cin == 5u && bytes[1] != 0xf6u) {
        /* CIN 5 may be a one-byte SysEx terminator; leave SysEx unimplemented. */
        if (bytes[1] == 0xf7u) {
            ++parser->unsupported;
        } else {
            ++parser->invalid;
        }
        return;
    }
    if (cin == 0u || cin == 1u || bytes[1] < 0x80u) {
        ++parser->invalid;
        return;
    }
    event.destination = cable;
    event.status = bytes[1];
    event.data1 = bytes[2];
    event.data2 = bytes[3];
    length = event_length(&event);
    if (length == 0u) {
        ++parser->invalid;
        return;
    }
    if (event.status < 0xf0u) {
        expected = (uint8_t)(event.status >> 4);
    } else if (length == 3u) {
        expected = 3u;
    } else if (length == 2u) {
        expected = 2u;
    } else if (event.status == 0xf6u) {
        expected = 5u;
    } else {
        expected = 0x0fu;
    }
    if (cin != expected) {
        ++parser->invalid;
        return;
    }
    ++parser->received;
    if (receive != NULL) {
        receive(context, &event);
    }
}

void thy_usb_parser_feed(thy_usb_parser *parser, const uint8_t *bytes,
                         size_t length, thy_midi_receive_fn receive,
                         void *context)
{
    size_t i;
    if (parser == NULL || (bytes == NULL && length != 0u)) {
        return;
    }
    for (i = 0u; i < length; ++i) {
        parser->fragment[parser->fragment_size++] = bytes[i];
        if (parser->fragment_size == 4u) {
            decode_packet(parser, receive, context);
            parser->fragment_size = 0u;
        }
    }
}
