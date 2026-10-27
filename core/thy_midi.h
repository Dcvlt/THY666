#ifndef THY_MIDI_H
#define THY_MIDI_H

#include <stddef.h>
#include <stdint.h>

/* Destination values are indices; the core routing masks are 1 << index. */
enum thy_destination {
    THY_USB0 = 0,
    THY_USB1 = 1,
    THY_DIN = 2,
    THY_DEST_COUNT = 3
};

/* Full MIDI status, including a zero-based channel for channel messages.
 * Unused data bytes are zero. No running status or SysEx is represented here. */
typedef struct thy_midi_event {
    uint8_t destination;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
} thy_midi_event;

/* Return encoded byte count, or zero for invalid/unsupported events. Output is
 * untouched on error. DIN encoding accepts any valid destination: this permits
 * a transport adapter to inspect the same event without rewriting its route.
 * USB encoding accepts USB0/USB1 only and always writes exactly four bytes. */
size_t thy_midi_encode_din(const thy_midi_event *event, uint8_t out[3]);
size_t thy_midi_encode_usb(const thy_midi_event *event, uint8_t out[4]);

typedef void (*thy_midi_receive_fn)(void *context,
                                    const thy_midi_event *event);

typedef struct thy_usb_parser {
    uint8_t fragment[4];
    uint8_t fragment_size;
    uint32_t received;
    uint32_t invalid;
    uint32_t unsupported;
} thy_usb_parser;

void thy_usb_parser_init(thy_usb_parser *parser);
/* Streaming USB MIDI 1.0 event packets. Partial packets persist across feeds.
 * Invalid and unsupported complete packets are consumed and counted. SysEx
 * CINs 4..7 are intentionally unsupported (CIN 5 with F6 tune request is
 * accepted), with no invented SysEx assembly behavior.
 * Cable 0/1 map to USB0/USB1; other cable numbers are unsupported. */
void thy_usb_parser_feed(thy_usb_parser *parser, const uint8_t *bytes,
                         size_t length, thy_midi_receive_fn receive,
                         void *context);

#endif
