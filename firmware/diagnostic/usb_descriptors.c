/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independently written descriptor composition; TinyUSB macros are MIT. */
#include "tusb.h"
#include "usb.h"

/* TinyUSB's example VID and its CDC+MIDI example product-bit convention.
 * Local experiments only: this is not an allocated product identity.
 * Deliberately distinct from 0333:0333 updater and the legacy 115c:572b MIDI identity. */
#define DIAG_VID 0xcafeu
#define DIAG_PID 0x4009u
enum { ITF_CDC_CONTROL, ITF_CDC_DATA, ITF_MIDI_CONTROL, ITF_MIDI_STREAMING,
       ITF_COUNT };
enum { STR_LANGUAGE, STR_MANUFACTURER, STR_PRODUCT, STR_SERIAL, STR_CDC,
       STR_MIDI, STR_CABLE0, STR_CABLE1 };

static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = 64, .idVendor = DIAG_VID, .idProduct = DIAG_PID,
    .bcdDevice = 0x0001, .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT, .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1
};

#define MIDI_TWO_CABLE_LEN (TUD_MIDI_DESC_HEAD_LEN + 2*TUD_MIDI_DESC_JACK_LEN + 2*TUD_MIDI_DESC_EP_LEN(2))
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + MIDI_TWO_CABLE_LEN)
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC_CONTROL, STR_CDC, 0x81, 8, 0x02, 0x82, 64),
    TUD_MIDI_DESC_HEAD(ITF_MIDI_CONTROL, STR_MIDI, 2),
    TUD_MIDI_DESC_JACK_DESC(1, STR_CABLE0),
    TUD_MIDI_DESC_JACK_DESC(2, STR_CABLE1),
    TUD_MIDI_DESC_EP(0x03, 64, 2),
    TUD_MIDI_JACKID_IN_EMB(1), TUD_MIDI_JACKID_IN_EMB(2),
    TUD_MIDI_DESC_EP(0x83, 64, 2),
    TUD_MIDI_JACKID_OUT_EMB(1), TUD_MIDI_JACKID_OUT_EMB(2)
};
_Static_assert(sizeof(configuration) == CONFIG_LEN, "USB descriptor length");

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&device;
}
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    return index == 0 ? configuration : NULL;
}
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    static uint16_t descriptor[33];
    static const char *const strings[] = { NULL, "THY666 Community Prototype",
        "THY666 Read-only Diagnostic", NULL, "Diagnostic Console",
        "Diagnostic MIDI RX", "USB Cable 1", "USB Cable 2" };
    char serial[25];
    size_t length = 0;
    (void)langid;
    if (index == STR_LANGUAGE) {
        descriptor[0] = (TUSB_DESC_STRING << 8) | 4;
        descriptor[1] = 0x0409;
        return descriptor;
    }
    if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
    const char *text;
    if (index == STR_SERIAL) {
        thy_diag_usb_serial(serial);
        text = serial;
    } else {
        text = strings[index];
    }
    if (text == NULL) return NULL;
    while (text[length] && length < 32) {
        descriptor[1 + length] = (uint8_t)text[length];
        length++;
    }
    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * length + 2));
    return descriptor;
}
