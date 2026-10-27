/* Host diagnostic protocol/descriptor tests: no hardware or USB accesses. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "protocol.h"
#include "usb.h"
#include "tusb.h"
#include "led.h"
#include "controls.h"
#include "pads.h"
#include "center.h"

volatile diag_report_t g_diag_mailbox;
volatile diag_entry_t g_diag_entry;
const char thy_diag_build_id[] = "host-test";
static thy_diag_usb_stats usb_stats;
static unsigned nor_reads, internal_reads;
static int read_result;
static char response[THY_DIAG_REPLY_MAX];
static diag_led_report_t led_state;
static unsigned led_requests;
static int led_request_result;
static diag_pads_report_t pads_state;
static unsigned pads_requests;
static int pads_active, pads_request_result;
static diag_center_report_t center_state;
static unsigned center_requests;
static int center_request_result;
const diag_center_report_t *board_center_report(void) { return &center_state; }
int board_center_request(unsigned mode, unsigned channel, unsigned brightness) {
    ++center_requests;
    if (center_request_result) return center_request_result;
    center_state.state = DIAG_CENTER_QUEUED; center_state.mode = mode;
    center_state.channel = channel; center_state.brightness = brightness;
    center_state.last_result = DIAG_BUSY;
    return DIAG_SUCCESS;
}
const diag_pads_report_t *board_pads_report(void) { return &pads_state; }
int board_pads_active(void) { return pads_active; }
int board_pads_busy(void) { return 0; }
int board_pads_request(int start) {
    ++pads_requests;
    if (pads_request_result) return pads_request_result;
    pads_active = start;
    pads_state.state = start ? DIAG_PADS_QUEUED : DIAG_PADS_IDLE;
    return DIAG_SUCCESS;
}
const diag_led_report_t *board_led_report(void) { return &led_state; }
int board_led_request(unsigned mode, unsigned pixel, unsigned component) {
    ++led_requests;
    if (led_request_result) return led_request_result;
    led_state.state = DIAG_LED_QUEUED; led_state.mode = mode;
    led_state.pixel = pixel; led_state.component = component;
    led_state.brightness = DIAG_LED_BRIGHTNESS; led_state.last_result = DIAG_BUSY;
    return DIAG_SUCCESS;
}
const volatile diag_report_t *board_report(void) { return &g_diag_mailbox; }
const thy_diag_usb_stats *thy_diag_usb_status(void) { return &usb_stats; }
void thy_diag_usb_serial(char out[25]) { memcpy(out, "0123456789ABCDEF01234567", 25); }
int board_nor_read(uint32_t address, uint8_t *out, size_t count) {
    nor_reads++;
    for (size_t n = 0; n < count; n++) out[n] = (uint8_t)(address + n);
    return read_result;
}
int board_internal_read(uint32_t address, uint8_t *out, size_t count) {
    internal_reads++;
    for (size_t n = 0; n < count; n++) out[n] = (uint8_t)(address + n);
    return read_result;
}
static void command(const char *line, const char *prefix) {
    size_t length = thy_diag_command(line, response, sizeof(response));
    assert(length > 2 && length < sizeof(response));
    assert(strlen(response) == length);
    assert(response[length - 2] == '\r' && response[length - 1] == '\n');
    assert(strncmp(response, prefix, strlen(prefix)) == 0);
}
static void reject_read(const char *line) {
    unsigned n = nor_reads, i = internal_reads;
    command(line, "ERR read-range");
    assert(nor_reads == n && internal_reads == i);
}
static void test_read_ranges(void) {
    command("nor 00000000 256", "DATA nor address=0x00000000 bytes=256 hex=");
    assert(strstr(response, " crc32=0x29058c73") != NULL); /* bytes00..ff IEEECRC */
    command("nor 00ffffff 1", "DATA nor address=0x00ffffff bytes=1 hex=ff");
    command("nor 00ffff00 256", "DATA nor address=0x00ffff00 bytes=256 hex=");
    command("internal 08000000 1", "DATA internal address=0x08000000 bytes=1 hex=00");
    command("internal 080fff00 256", "DATA internal address=0x080fff00 bytes=256 hex=");
    command("internal 080fffff 1", "DATA internal address=0x080fffff bytes=1 hex=ff");
    reject_read("nor 01000000 1"); reject_read("nor 00ffffff 2");
    reject_read("nor ffffffff 1"); reject_read("nor 100000000 1");
    reject_read("nor 0 0"); reject_read("nor 0 257");
    reject_read("nor 0 4294967296"); reject_read("nor -1 4");
    reject_read("nor 0 -4"); reject_read("nor 0 0x10");
    reject_read("nor 0x 1"); reject_read("nor G 1");
    reject_read("internal 07ffffff 1"); reject_read("internal 08100000 1");
    reject_read("internal 080fffff 2"); reject_read("internal ffffffff 256");
    reject_read("internal 20000000 1"); reject_read("nor");
    read_result = DIAG_BUSY;
    command("nor 0 1", "ERR read code=4");
    assert(strstr(response, "hex=") == NULL);
    read_result = 0;
}
static void test_protocol(void) {
    g_diag_mailbox.version = DIAG_PROTOCOL_VERSION;
    g_diag_mailbox.device_id = 0x10006413;
    g_diag_mailbox.flash_kib = 1024;
    g_diag_mailbox.hclk_hz = 168000000;
    g_diag_mailbox.uid[0] = 0x11223344;
    g_diag_mailbox.uid[1] = 0x55667788;
    g_diag_mailbox.uid[2] = 0x99aabbcc;
    command("status", "STATUS THYDIAG/1 build=host-test");
    assert(strstr(response, " device_id=0x10006413 flash_kib=1024") != NULL);
    assert(strstr(response, " uid=112233445566778899aabbcc") != NULL);
    assert(strstr(response, " mailbox_version=3") != NULL);
    command("help", "OK THYDIAG/1"); command("touch", "TOUCH");
    command("startup", "STARTUP THYDIAG/1 valid=0");
    g_diag_entry.magic = DIAG_ENTRY_MAGIC; g_diag_entry.version = 1;
    g_diag_entry.control = 2; g_diag_entry.msp = 0x2001c000;
    g_diag_entry.psp = 0x20010000; g_diag_entry.vtor = 0x08000000;
    g_diag_entry.mpu_ctrl = 5; g_diag_entry.fpccr = 0xc0000000;
    g_diag_entry.cfsr = 0x10000; g_diag_entry.iwdg_rlr = 0xfff;
    command("startup", "STARTUP THYDIAG/1 valid=1 control=0x00000002 ipsr=0");
    assert(strstr(response, " msp=0x2001c000 psp=0x20010000 vtor=0x08000000 mpu_ctrl=0x00000005") != NULL);
    assert(strstr(response, " cfsr=0x00010000") != NULL);
    assert(strstr(response, " iwdg_rlr=0x00000fff") != NULL);
    command("startup extra", "ERR arguments");
    command("adc", "ADC"); command("midi", "MIDI");
    command("core-test", "CORE PASS failures=0x00000000");
    assert(thy_diag_core_selftest() == 0);
    command("reset", "ERR unknown-command");
    command("flash", "ERR unknown-command");
    command("erase", "ERR unknown-command");
    command("status now", "ERR arguments");
    command(" ", "ERR empty"); command("help a b c d", "ERR arguments");
    char long_line[120]; memset(long_line, 'a', sizeof(long_line)); long_line[119] = 0;
    command(long_line, "ERR line-too-long");
    command("  help\t", "OK THYDIAG/1");
    assert(thy_diag_command("help", response, 16) == 0);
    assert(thy_diag_command(NULL, response, sizeof(response)) == 0);
}
static void test_sensor_controls_led_protocol(void) {
    command("sensors", "SENSORS");
    assert(strstr(response, " calibration=unverified") != NULL);
    for (unsigned i = 0; i < 4; ++i) {
        g_diag_mailbox.sensor[i] = (diag_sensor_bank_t){0};
        g_diag_mailbox.sensor[i].state = DIAG_SENSOR_READY;
        g_diag_mailbox.sensor[i].identity_valid = 1;
        g_diag_mailbox.sensor[i].family_id = 0x9a;
        g_diag_mailbox.sensor[i].device_id = 0x0a05;
        g_diag_mailbox.sensor[i].device_rev = 1;
        g_diag_mailbox.sensor[i].status_reads = UINT32_MAX;
        g_diag_mailbox.sensor[i].wake_nacks = UINT32_MAX;
        g_diag_mailbox.sensor[i].retries = UINT32_MAX;
        g_diag_mailbox.sensor[i].init_failures = UINT32_MAX;
        g_diag_mailbox.sensor[i].first_ready_ms = UINT32_MAX;
        g_diag_mailbox.sensor[i].peripheral_resets = UINT32_MAX;
        g_diag_mailbox.sensor[i].last_status_ms = UINT32_MAX;
    }
    g_diag_mailbox.touch_initialized_mask = 15;
    command("sensors", "SENSORS ready=0x0000000f");
    assert(strstr(response, "bank3_state=ready bank3_code=0 bank3_identity=1 bank3_family=0x9a bank3_device=0x0a05") != NULL);
    assert(strstr(response, "bank3_status_ms=4294967295") != NULL);
    assert(strchr(response, '\n') == response + strlen(response) - 1);
    command("sensors extra", "ERR arguments");
    board_controls_init(); board_controls_task(100);
    command("controls", "CONTROLS valid=");
    assert(strstr(response, " total=43") != NULL);
    command("controls clear", "CONTROLS");
    command("controls nope", "ERR arguments");
    command("led status", "LED state=idle");
    assert(led_requests == 0);
    const char *invalid[] = {"led", "led all 3", "led pixel 96 0", "led pixel -1 0", "led walk 4294967296", "led off extra", "led pixel 1 2 extra", "led all 0x1"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        unsigned before = led_requests;
        command(invalid[i], "ERR"); assert(led_requests == before);
    }
    command("led pixel 95 2", "LED state=queued mode=pixel pixel=95 component=2 brightness=8");
    command("led all 0", "LED state=queued mode=all");
    command("led walk 1", "LED state=queued mode=walk");
    command("led off", "LED state=queued mode=off");
    led_request_result = DIAG_BUSY;
    command("led all 0", "ERR led code=4");
    led_request_result = 0;
    command("help", "OK THYDIAG/1");
    assert(strstr(response, " nvm_writes=none") != NULL);
    command("pads status", "PADS state=0"); assert(pads_requests == 0);
    const char *bad_pads[] = {"pads", "pads bank 6", "pads bank -1", "pads bank 0 extra", "pads start extra", "pads erase"};
    for (unsigned i = 0; i < sizeof(bad_pads) / sizeof(bad_pads[0]); ++i) {
        unsigned before = pads_requests;
        command(bad_pads[i], "ERR"); assert(before == pads_requests);
    }
    command("pads start", "PADS state=1");
    command("adc", "ADC"); assert(strstr(response, "ownership=pad-test snapshots=paused") != NULL);
    for (unsigned i = 0; i < DIAG_PAD_COUNT; ++i)
        pads_state.raw[i] = pads_state.minimum[i] = pads_state.maximum[i] = UINT32_MAX;
    command("pads bank 5", "PADS");
    assert(strstr(response, " bank=5 raw=4294967295,") != NULL);
    assert(strstr(response, "velocity=uncalibrated nvm_writes=none") != NULL);
    command("pads stop", "PADS state=0");
    command("adc", "ADC"); assert(strstr(response, "ownership=raw-adc snapshots=active") != NULL);
    pads_request_result = DIAG_BUSY;
    command("pads start", "ERR pads code=4");
    pads_request_result = 0;
}
static void test_center_protocol(void) {
    command("center", "CENTER state=0"); assert(center_requests == 0);
    command("center status", "CENTER state=0"); assert(center_requests == 0);
    const char *invalid[] = {"center erase", "center all 4095", "center off extra", "center channel",
        "center channel -1", "center channel 24", "center channel 4294967296", "center channel 5 extra"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        unsigned before = center_requests;
        command(invalid[i], "ERR center-arguments"); assert(before == center_requests);
    }
    command("center all", "CENTER state=1");
    assert(center_state.mode == DIAG_CENTER_ALL && center_state.brightness == 128u);
    assert(strstr(response, "driver=cpu lighting_dma=none nvm_writes=none") != NULL);
    command("center channel 23", "CENTER state=1");
    assert(center_state.mode == DIAG_CENTER_CHANNEL && center_state.channel == 23u && center_state.brightness == 128u);
    command("center off", "CENTER state=1");
    assert(center_state.mode == DIAG_CENTER_OFF && center_state.brightness == 0u);
    center_request_result = DIAG_BUSY;
    command("center all", "ERR center code=4"); center_request_result = 0;
    center_state.enabled_mask = UINT32_MAX; center_state.errors = UINT32_MAX;
    center_state.frames = UINT32_MAX; center_state.updated_ms = UINT32_MAX;
    command("center status", "CENTER");
    assert(strstr(response, "enabled_mask=0xffffffff") != NULL && strlen(response) < THY_DIAG_REPLY_MAX);
    pads_state.lighting_decoupled = 1u; pads_state.adc_resets = 2u;
    command("pads status", "PADS");
    assert(strstr(response, "adc_resets=2 lighting_decoupled=1") != NULL);
}
static void test_i2c_reporting(void) {
    command("i2c", "I2C attempted=0x00000000 ok=0x00000000 history=0x00000000");
    assert(strstr(response, " bank0_addr=0x36 bank0_attempts=0 bank0_successes=0 bank0_code=0") != NULL);
    assert(strstr(response, " bank3_phase=none bank3_last_code=0 bank3_sr1=0x00000000 bank3_sr2=0x00000000 bank3_at_ms=0 bank3_flags=NONE") != NULL);
    g_diag_mailbox.touch_attempted_mask = 15;
    g_diag_mailbox.touch_ok_mask = 15;
    g_diag_mailbox.touch_history_mask = 5;
    g_diag_mailbox.touch_errors[0] = 1;
    g_diag_mailbox.i2c[0].attempts = 4;
    g_diag_mailbox.i2c[0].successes = 3;
    g_diag_mailbox.i2c[0].last_failure_phase = DIAG_I2C_ADDRESS_WRITE;
    g_diag_mailbox.i2c[0].last_failure_result = DIAG_UNAVAILABLE;
    g_diag_mailbox.i2c[0].last_failure_sr1 = 0x400;
    g_diag_mailbox.i2c[0].last_failure_sr2 = 3;
    g_diag_mailbox.i2c[0].last_failure_ms = 200;
    command("i2c", "I2C attempted=0x0000000f ok=0x0000000f history=0x00000005");
    assert(strstr(response, " bank0_code=0 bank0_errors=1 bank0_phase=address-write bank0_last_code=3") != NULL);
    assert(strstr(response, " bank0_sr1=0x00000400 bank0_sr2=0x00000003 bank0_at_ms=200 bank0_flags=NACK,BUSY") != NULL);
    command("touch", "TOUCH ok=0x0000000f");
    assert(strstr(response, " attempted=0x0000000f history=0x00000005") != NULL);
    /* A maximal counter/flag reply remains a single complete bounded line. */
    for (unsigned i = 0; i < 4; i++) {
        g_diag_mailbox.touch_errors[i] = UINT32_MAX;
        g_diag_mailbox.i2c[i].attempts = UINT32_MAX;
        g_diag_mailbox.i2c[i].successes = UINT32_MAX;
        g_diag_mailbox.i2c[i].result = DIAG_UNAVAILABLE;
        g_diag_mailbox.i2c[i].last_failure_phase = DIAG_I2C_ADDRESS_WRITE;
        g_diag_mailbox.i2c[i].last_failure_result = DIAG_UNAVAILABLE;
        g_diag_mailbox.i2c[i].last_failure_sr1 = 0xf00;
        g_diag_mailbox.i2c[i].last_failure_sr2 = UINT32_MAX;
        g_diag_mailbox.i2c[i].last_failure_ms = UINT32_MAX;
    }
    command("i2c", "I2C");
    assert(strlen(response) < THY_DIAG_REPLY_MAX - 1);
    assert(strstr(response, " bank3_flags=BERR,ARLO,NACK,OVR,BUSY\r\n") != NULL);
    assert(strchr(response, '\n') == response + strlen(response) - 1);
    command("i2c extra", "ERR arguments");
}
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void test_descriptors(void) {
    const uint8_t *d = tud_descriptor_device_cb();
    assert(d[0] == 18 && d[1] == TUSB_DESC_DEVICE);
    assert(u16(d + 8) == 0xcafe && u16(d + 10) == 0x4009);
    assert(d[7] == 64 && d[17] == 1);
    assert(d[4] == TUSB_CLASS_MISC && d[5] == 2 && d[6] == 1);
    const uint8_t *c = tud_descriptor_configuration_cb(0);
    unsigned total = u16(c + 2), offset = 0, interfaces = 0, endpoints = 0;
    unsigned epseen = 0, midi_in_jacks = 0, midi_out_jacks = 0, midistream = 0;
    assert(c[0] == 9 && c[4] == 4 && c[7] == 0x80 && c[8] == 50);
    while (offset < total) {
        const uint8_t *p = c + offset;
        assert(p[0] >= 2 && offset + p[0] <= total);
        if (p[1] == TUSB_DESC_INTERFACE) {
            assert(p[2] == interfaces++);
            midistream = p[5] == TUSB_CLASS_AUDIO && p[6] == AUDIO_SUBCLASS_MIDI_STREAMING;
            assert(p[5] != TUSB_CLASS_MSC && p[5] != TUSB_CLASS_HID);
        }
        if (p[1] == TUSB_DESC_ENDPOINT) {
            unsigned bit = (p[2] & 15) + ((p[2] & 0x80) ? 8 : 0);
            assert(!(epseen & (1u << bit))); epseen |= 1u << bit; endpoints++;
            if (p[2] == 0x81) assert((p[3] & 3) == TUSB_XFER_INTERRUPT && u16(p + 4) == 8);
            else assert((p[3] & 3) == TUSB_XFER_BULK && u16(p + 4) == 64);
        }
        if (midistream && p[1] == TUSB_DESC_CS_INTERFACE && p[2] == MIDI_CS_INTERFACE_IN_JACK)
            midi_in_jacks++;
        if (midistream && p[1] == TUSB_DESC_CS_INTERFACE && p[2] == MIDI_CS_INTERFACE_OUT_JACK)
            midi_out_jacks++;
        if (p[1] == TUSB_DESC_CS_ENDPOINT) {
            assert(p[0] == 6 && p[3] == 2);
            if (c[offset - 9 + 2] == 0x03) { assert(p[4] == 1 && p[5] == 5); }
            else { assert(p[4] == 3 && p[5] == 7); }
        }
        offset += p[0];
    }
    assert(offset == total && interfaces == 4 && endpoints == 5);
    assert(midi_in_jacks == 4 && midi_out_jacks == 4);
    assert(tud_descriptor_configuration_cb(1) == NULL);
    const uint16_t *s = tud_descriptor_string_cb(0, 0);
    assert(s[0] == 0x0304 && s[1] == 0x0409);
    s = tud_descriptor_string_cb(3, 0x0409);
    assert((s[0] & 255) == 50 && s[1] == '0' && s[24] == '7');
    assert(tud_descriptor_string_cb(255, 0) == NULL);
}
int main(void) {
    test_read_ranges(); test_protocol(); test_i2c_reporting(); test_sensor_controls_led_protocol(); test_center_protocol(); test_descriptors();
    puts("Diagnostic protocol: bounded commands, opt-in volatile tests, CRC32, RAM core and composite USB descriptors passed");
    return 0;
}
