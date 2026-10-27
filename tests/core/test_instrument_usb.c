/* SPDX-License-Identifier: GPL-3.0-or-later
 * Compile the complete instrument USB translation unit against fake TinyUSB
 * endpoints and fake STM32 declarations. Board/USB initialization is never
 * called. No operating-system device API or hardware access is performed. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/instrument/usb.c"

RCC_TypeDef fake_rcc;
GPIO_TypeDef fake_gpioa;
USB_OTG_GlobalTypeDef fake_usb;
const uint32_t fake_uid[3] = {0x00010002u, 0x00030004u, 0x00050006u};
volatile diag_report_t g_diag_mailbox;

static uint8_t rx[4096], midi_rx[128][4];
static char tx[256], wire[8192];
static size_t rx_used, rx_offset, tx_used, wire_used, write_limit, flush_limit;
static unsigned midi_used, midi_offset, midi_attempts, task_calls, command_calls;
static unsigned rx_flushes, tx_clears, usb_resets;
static bool dtr, cycle_in_task;
static thy_midi_event timing[128];
static unsigned timing_count, timing_resets;
static unsigned companion_session_resets;
void thy_instrument_app_midi(const thy_midi_event *event, uint32_t now) {
    assert(now == 123u && timing_count < 128u); timing[timing_count++] = *event;
}
void thy_instrument_app_usb_reset(uint32_t now) { assert(now == 123u); ++timing_resets; }
void thy_instrument_app_console_reset(void) { ++companion_session_resets; }

uint32_t board_millis(void) { return 123u; }
const volatile diag_report_t *board_report(void) { return &g_diag_mailbox; }
bool tusb_init(uint8_t rhport, const tusb_rhport_init_t *init) {
    (void)rhport; (void)init;
    assert(!"USB init must not run in a lifecycle test"); return false;
}
void tusb_int_handler(uint8_t rhport, bool in_isr) { (void)rhport; (void)in_isr; }
void tud_task(void) {
    task_calls++;
    if (cycle_in_task) {
        cycle_in_task = false;
        tud_umount_cb();
        dtr = false;
        rx_used = rx_offset = tx_used = 0;
        usb_resets++;
        tud_mount_cb();
    }
}
bool tud_midi_packet_read(uint8_t packet[4]) {
    midi_attempts++;
    if (midi_offset == midi_used) return false;
    memcpy(packet, midi_rx[midi_offset++], 4u); return true;
}
bool tud_cdc_connected(void) { return dtr; }
uint32_t tud_cdc_available(void) { return (uint32_t)(rx_used - rx_offset); }
int tud_cdc_read_char(void) { return rx_offset < rx_used ? rx[rx_offset++] : -1; }
void tud_cdc_read_flush(void) { rx_offset = rx_used; rx_flushes++; }
bool tud_cdc_write_clear(void) { tx_used = 0; tx_clears++; return true; }
uint32_t tud_cdc_write(const void *bytes, uint32_t count) {
    if (count > write_limit) count = (uint32_t)write_limit;
    if (count > sizeof tx - tx_used) count = (uint32_t)(sizeof tx - tx_used);
    memcpy(tx + tx_used, bytes, count); tx_used += count; return count;
}
void tud_cdc_write_flush(void) {
    size_t count = tx_used < flush_limit ? tx_used : flush_limit;
    assert(wire_used + count < sizeof wire);
    memcpy(wire + wire_used, tx, count); wire_used += count; wire[wire_used] = 0;
    memmove(tx, tx + count, tx_used - count); tx_used -= count;
}
size_t thy_diag_command(const char *line, char *output, size_t capacity) {
    command_calls++;
    assert(strcmp(line, "help") == 0 && capacity > 50u);
    strcpy(output, "OK THYINSTRUMENT/1 fake-help\r\n"); return strlen(output);
}

static void fixture(void) {
    memset(&stats, 0, sizeof stats);
    stats.initialized = true;
    instrument_usb_epoch = 0u;
    thy_usb_parser_init(&parser);
    console_reset_state();
    rx_used = rx_offset = tx_used = wire_used = 0u;
    wire[0] = 0;
    write_limit = flush_limit = sizeof tx;
    midi_used = midi_offset = midi_attempts = task_calls = command_calls = 0u;
    rx_flushes = tx_clears = usb_resets = 0u;
    dtr = cycle_in_task = false;
    timing_count = timing_resets = 0u;
}
static void enqueue(const void *bytes, size_t count) {
    assert(rx_used + count <= sizeof rx); memcpy(rx + rx_used, bytes, count); rx_used += count;
}
static void tasks(unsigned count) { while (count--) thy_diag_usb_task(); }
static void line_state(bool high, bool rts) { dtr = high; tud_cdc_line_state_cb(0u, high, rts); }
static void open_console(void) { line_state(true, false); tasks(3u); }
static unsigned occurrences(const char *text) {
    unsigned count = 0;
    for (const char *at = wire; (at = strstr(at, text)) != NULL; at += strlen(text)) count++;
    return count;
}
static const char *hello = "HELLO THYINSTRUMENT/1 alpha; type help\r\n";

static void test_mount_epoch_and_uninitialized_task(void) {
    fixture(); stats.initialized = false; tasks(2u); assert(task_calls == 0u);
    stats.initialized = true;
    tud_mount_cb(); assert(thy_instrument_usb_epoch() == 1u && stats.mounted);
    assert(g_diag_mailbox.usb_state == 2u);
    open_console(); assert(strcmp(wire, hello) == 0);
    enqueue("help\n", 5u); tasks(3u);
    assert(command_calls == 1u && stats.cdc_commands == 1u);
    assert(occurrences("OK THYINSTRUMENT/1 fake-help\r\n") == 1u);
    enqueue("unfinished", 10u); tasks(1u); assert(command_length == 10u);
    unsigned flushes = rx_flushes;
    tud_umount_cb(); assert(thy_instrument_usb_epoch() == 2u && !stats.mounted);
    assert(!console_connected && command_length == 0u && g_diag_mailbox.usb_state == 1u);
    assert(rx_flushes == flushes); /* Unmount never rearms a class endpoint. */
    dtr = false; rx_used = rx_offset = tx_used = 0;
    tud_mount_cb(); assert(thy_instrument_usb_epoch() == 3u);
    open_console(); assert(occurrences(hello) == 2u);
}

static void test_console_stale_bytes_reopen_and_rts(void) {
    fixture(); const uint8_t stale[] = {0xc0u, 0xc0u, 0xc0u};
    enqueue(stale, sizeof stale); tasks(2u); assert(!tud_cdc_available());
    line_state(true, false); enqueue(stale, sizeof stale); tasks(3u);
    assert(strcmp(wire, hello) == 0 && !command_calls);
    enqueue("broken", 6u); tasks(1u); assert(command_length == 6u);
    line_state(false, false); enqueue(stale, sizeof stale); line_state(true, false); tasks(3u);
    enqueue("he", 2u); tasks(1u); line_state(true, true);
    tud_cdc_line_state_cb(1u, false, false); /* Another interface cannot reset CDC0. */
    enqueue("lp\n", 3u); tasks(3u);
    assert(command_calls == 1u && stats.cdc_commands == 1u && occurrences(hello) == 2u);
    assert(strstr(wire, "ERR ") == NULL);
    assert(thy_instrument_usb_epoch() == 0u); /* CDC reopen does not reset MIDI. */
}

static void test_greeting_and_reply_backpressure(void) {
    fixture(); write_limit = 3u; flush_limit = 0u; line_state(true, false);
    tasks(2u); assert(response_offset == 6u && tx_used == 6u);
    enqueue("help\n", 5u); tasks(1u); assert(!tud_cdc_available() && !command_calls);
    flush_limit = 2u; tasks(40u); while (tx_used) tud_cdc_write_flush();
    assert(strcmp(wire, hello) == 0 && !console_greeting);
    enqueue("help\n", 5u); tasks(40u); while (tx_used) tud_cdc_write_flush();
    assert(command_calls == 1u && occurrences(hello) == 1u);
    assert(occurrences("OK THYINSTRUMENT/1 fake-help\r\n") == 1u);
    write_limit = 0u; flush_limit = 0u;
    enqueue("help\nhelp\n", 10u); tasks(6u);
    assert(command_calls == 2u && tud_cdc_available() == 5u);
    write_limit = flush_limit = sizeof tx; tasks(6u);
    assert(command_calls == 3u && occurrences("OK THYINSTRUMENT/1 fake-help\r\n") == 3u);
}

static void test_malformed_recovery_is_not_a_session_error(void) {
    fixture(); open_console();
    char overlong[300]; memset(overlong, 'x', sizeof overlong);
    enqueue(overlong, sizeof overlong); enqueue("\nhelp\n", 6u); tasks(10u);
    assert(command_calls == 1u && occurrences("ERR line-too-long\r\n") == 1u);
    const uint8_t bad[] = {0xc0u, '\n'};
    enqueue(bad, sizeof bad); enqueue("hex\blp\r\n", 8u); tasks(5u);
    assert(command_calls == 2u && occurrences("ERR invalid-character\r\n") == 1u);
    assert(stats.cdc_commands == 4u);
}

static void test_same_task_usb_reset_preserves_epoch_evidence(void) {
    fixture(); tud_mount_cb(); open_console();
    assert(thy_instrument_usb_epoch() == 1u);
    cycle_in_task = true; tasks(1u);
    assert(usb_resets == 1u && stats.mounted && thy_instrument_usb_epoch() == 3u);
    assert(!console_connected && !command_length);
    open_console(); assert(occurrences(hello) == 2u);
}

static void test_bounded_rx_uses_actual_parser_without_echo(void) {
    fixture();
    const uint8_t pattern[5][4] = {
        {0x09u, 0x90u, 60u, 100u}, {0x18u, 0x81u, 64u, 0u},
        {0x09u, 0x90u, 200u, 100u}, {0x29u, 0x90u, 60u, 100u},
        {0x04u, 0xf0u, 1u, 2u}
    };
    for (unsigned i = 0u; i < 40u; i++) memcpy(midi_rx[i], pattern[i % 5u], 4u);
    midi_used = 40u;
    tasks(1u); assert(midi_offset == 32u && midi_attempts == 32u);
    tasks(1u); assert(midi_offset == 40u && midi_attempts == 41u);
    assert(stats.midi_received == 16u && stats.midi_invalid == 8u && stats.midi_unsupported == 16u);
    assert(!tx_used && !wire_used && !command_calls);
    assert(task_calls == 2u);
}

static void test_serial_format_uses_same_chip_uid(void) {
    fixture(); char serial[25]; thy_diag_usb_serial(serial);
    assert(strcmp(serial, "000100020003000400050006") == 0);
}

static void test_timing_dispatch_is_validated_cable_zero_only(void) {
    fixture();
    const uint8_t packets[][4] = {
        {0x0fu,0xf8u,0u,0u}, {0x0fu,0xfau,0u,0u}, {0x0fu,0xfbu,0u,0u},
        {0x0fu,0xfcu,0u,0u}, {0x03u,0xf2u,7u,1u},
        {0x1fu,0xf8u,0u,0u}, {0x13u,0xf2u,1u,2u},
        {0x03u,0xf2u,128u,0u}, {0x09u,0x90u,60u,96u}
    };
    for (unsigned n=0u;n<sizeof packets/sizeof packets[0];++n)
        memcpy(midi_rx[n],packets[n],4u);
    midi_used = sizeof packets/sizeof packets[0]; tasks(1u);
    assert(timing_count==5u && timing[4].status==0xf2u);
    assert(timing[4].data1==7u && timing[4].data2==1u);
    assert(stats.midi_invalid==1u && !wire_used && !tx_used);
    tud_mount_cb(); tud_umount_cb();
    assert(timing_resets==2u && thy_instrument_usb_epoch()==2u);
}

int main(void) {
    test_mount_epoch_and_uninitialized_task();
    test_console_stale_bytes_reopen_and_rts();
    test_greeting_and_reply_backpressure();
    test_malformed_recovery_is_not_a_session_error();
    test_same_task_usb_reset_preserves_epoch_evidence();
    test_bounded_rx_uses_actual_parser_without_echo();
    test_serial_format_uses_same_chip_uid();
    test_timing_dispatch_is_validated_cable_zero_only();
    unsigned before=companion_session_resets;
    tud_cdc_line_state_cb(0u,false,false);
    assert(companion_session_resets==before+1u);
    before=companion_session_resets;tud_umount_cb();
    assert(companion_session_resets==before+1u);
    puts("instrument USB lifecycle tests passed: 8 scenarios; complete actual source, fake endpoints, validated timing ingress");
    return 0;
}
