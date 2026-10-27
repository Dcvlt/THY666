/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "usb.h"
#include "board.h"
#include "protocol.h"
#include "thy_midi.h"
#include "app.h"
#include "stm32f4xx.h"
#include "tusb.h"

static uint32_t instrument_usb_epoch;
uint32_t thy_instrument_usb_epoch(void) { return instrument_usb_epoch; }
static thy_diag_usb_stats stats;
static thy_usb_parser parser;
/* The instrument's schema-bound companion RPC is larger than the immutable
 * diagnostic console. Reply storage and all transport ownership stay bounded. */
static char command[160u];
static char response[THY_DIAG_REPLY_MAX];
static size_t command_length, response_length, response_offset;
static bool line_overflow, line_invalid, console_connected, console_greeting;

static void console_reset_state(void);
static void console_reset(void);

uint32_t tusb_time_millis_api(void) { return board_millis(); }
const thy_diag_usb_stats *thy_diag_usb_status(void) { return &stats; }
void thy_diag_usb_serial(char out[25]) {
    static const char alphabet[] = "0123456789ABCDEF";
    const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE;
    for (unsigned i = 0; i < 3; i++) {
        uint32_t word = uid[i];
        for (unsigned n = 0; n < 8; n++) out[8 * i + n] = alphabet[(word >> (28 - 4 * n)) & 15];
    }
    out[24] = 0;
}

bool thy_diag_usb_init(void) {
    if (stats.initialized) return true;
    if (!board_report()->usb_clock_ready) return false;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;
    GPIOA->MODER = (GPIOA->MODER & ~((3u << 22) | (3u << 24))) | (2u << 22) | (2u << 24);
    GPIOA->OTYPER &= ~((1u << 11) | (1u << 12));
    GPIOA->OSPEEDR |= (3u << 22) | (3u << 24);
    GPIOA->PUPDR &= ~((3u << 22) | (3u << 24));
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~((15u << 12) | (15u << 16))) | (10u << 12) | (10u << 16);
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;
    /* An updater may hand off with its USB personality still attached. Hold a
     * real soft disconnect before resetting the controller and changing IDs.
     * SysTick/interrupts are already running by this API's precondition. */
    USB_OTG_DeviceTypeDef *device = (USB_OTG_DeviceTypeDef *)
        ((uintptr_t)USB_OTG_FS + USB_OTG_DEVICE_BASE);
    device->DCTL |= USB_OTG_DCTL_SDIS;
    uint32_t detached_at = board_millis();
    uint32_t detach_limit = 4000000u;
    while ((uint32_t)(board_millis() - detached_at) < 20u) {
        if (!--detach_limit) return false;
        __NOP();
    }
    RCC->AHB2RSTR |= RCC_AHB2RSTR_OTGFSRST;
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_OTGFSRST;
    /* F405 0.18 DWC2 uses B-session-valid override. PA9 wiring is not assumed.
     * Internal PHY and no VBUS-sensing are for a USB-connected lab prototype. */
    USB_OTG_FS->GCCFG = USB_OTG_GCCFG_PWRDWN | USB_OTG_GCCFG_NOVBUSSENS;
    NVIC_SetPriority(OTG_FS_IRQn, 6);
    NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    thy_usb_parser_init(&parser);
    const tusb_rhport_init_t init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
    stats.initialized = tusb_init(0, &init);
    g_diag_mailbox.usb_state = stats.initialized ? 1 : 0;
    return stats.initialized;
}
void OTG_FS_IRQHandler(void) { tusb_int_handler(0, true); }
void tud_mount_cb(void) {
    ++instrument_usb_epoch;
    console_reset();
    stats.mounted = true;
    thy_instrument_app_usb_reset(board_millis());
    g_diag_mailbox.usb_state = 2;
}
void tud_umount_cb(void) {
    ++instrument_usb_epoch;
    /* The class driver clears its FIFOs on reset. Do not rearm an endpoint
     * while unmounting; discard the application's session state as well. */
    console_reset_state();
    stats.mounted = false;
    thy_instrument_app_usb_reset(board_millis());
    g_diag_mailbox.usb_state = 1;
}

static void queue_error(const char *text) {
    response_length = 0;
    while (text[response_length]) { response[response_length] = text[response_length]; response_length++; }
    response_offset = 0;
}
static void console_reset_state(void) {
    thy_instrument_app_console_reset();
    command_length = response_length = response_offset = 0;
    line_overflow = line_invalid = console_connected = console_greeting = false;
}
static void console_reset(void) {
    console_reset_state();
    /* RX is buffered even with DTR low. Clearing only the line parser leaves
     * pre-session bytes queued to poison the first real command. read_flush
     * also rearms OUT; write_clear discards TX bytes not yet submitted to USB.
     * An in-flight IN packet cannot be recalled: hosts must await HELLO. */
    tud_cdc_read_flush();
    tud_cdc_write_clear();
}
static void console_connect(void) {
    console_reset();
    console_connected = console_greeting = true;
    queue_error("HELLO THYINSTRUMENT/1 alpha; type help\r\n");
}
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)rts;
    if (itf != 0) return;
    /* This callback runs in tud_task at the control request's ACK, so a close
     * and reopen between console polls still create a new session. Repeated
     * DTR-high/RTS-only changes must not erase an active command or greeting. */
    if (!dtr) console_reset();
    else if (!console_connected) console_connect();
}
/* Keep command callbacks outside TinyUSB/MIDI parsing scratch. This final
 * foreground task can tail-dispatch after the USB frame has been released. */
__attribute__((noinline)) static void console_task(void) {
    bool connected = tud_cdc_connected();
    if (!connected) {
        console_reset();
        return;
    }
    if (!console_connected) console_connect();
    if (response_offset < response_length) {
        /* Discard input received before the complete greeting is queued,
         * including OUT events processed after DTR in the same tud_task.
         * Once HELLO is queued, never flush input from the active session. */
        if (console_greeting) tud_cdc_read_flush();
        response_offset += tud_cdc_write(response + response_offset, (uint32_t)(response_length - response_offset));
        tud_cdc_write_flush();
        if (response_offset == response_length) console_greeting = false;
        return;
    }
    response_length = response_offset = 0;
    for (unsigned budget = 0; budget < 128 && tud_cdc_available(); budget++) {
        int value = tud_cdc_read_char();
        if (value < 0) break;
        if (value == '\r') continue;
        if (value == '\n') {
            command[command_length] = 0;
            stats.cdc_commands++;
            if (line_overflow) queue_error("ERR line-too-long\r\n");
            else if (line_invalid) queue_error("ERR invalid-character\r\n");
            else {
                response_length = thy_diag_command(command, response, sizeof(response));
                response_offset = 0;
            }
            command_length = 0;
            line_overflow = line_invalid = false;
            break;
        }
        if (value == '\b' || value == 127) {
            if (command_length) command_length--;
        } else if ((value >= 32 && value <= 126) || value == '\t') {
            if (command_length < sizeof(command) - 1) command[command_length++] = (char)value;
            else line_overflow = true;
        } else line_invalid = true;
    }
}
static void receive_midi(void *context, const thy_midi_event *event) {
    (void)context;
    if (event->destination != THY_USB0 || (event->status != 0xf8u && event->status != 0xfau &&
        event->status != 0xfbu && event->status != 0xfcu && event->status != 0xf2u)) return;
    thy_instrument_app_midi(event, board_millis());
}
/* LTO must not retain USB/parser scratch in main's permanent loop frame. */
__attribute__((noinline)) void thy_diag_usb_task(void) {
    if (!stats.initialized) return;
    tud_task();
    uint8_t packet[4];
    /* Bounded receive processing keeps peripheral polling responsive. MIDI
     * packets are validated before clock/transport dispatch. Other MIDI is
     * counted but has no implicit echo, note or remote-control consumer. */
    for (unsigned budget = 0; budget < 32 && tud_midi_packet_read(packet); budget++)
        thy_usb_parser_feed(&parser, packet, sizeof(packet), receive_midi, NULL);
    stats.midi_received = parser.received;
    stats.midi_invalid = parser.invalid;
    stats.midi_unsupported = parser.unsupported;
    __attribute__((musttail)) return console_task();
}
