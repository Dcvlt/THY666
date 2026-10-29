/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual instrument console + core/detector/journal. Board/DIN/light getters
 * are RAM fixtures; no serial, USB, UART or device handle is opened. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../firmware/instrument/app.h"
#include "../../firmware/instrument/din.h"
#include "../../firmware/instrument/led.h"
#include "../../firmware/diagnostic/board.h"
#include "../../firmware/diagnostic/pads.h"
#include "../../firmware/diagnostic/protocol.h"
#include "../../firmware/instrument/touch_config.h"
#include "../../firmware/instrument/display.h"

static unsigned checks, scenarios, panic_calls,backup_calls,companion_calls;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static thy_core core;
static thy_instrument instrument;
static thy_instrument_pad layout[THY_PAD_COUNT];
static thy_instrument_transport transport;
static thy_settings_journal settings;
static thy_instrument_din_stats din;
static thy_instrument_lights_status lights;
static diag_led_report_t leds;
static diag_pads_report_t pads;
static diag_report_t board;
static thy_touch_capture touch;
static thy_display_report display;
static thy_resources resources;
const thy_resources *board_resources(void) { return &resources; }
static unsigned capture_begin_calls, capture_read_calls;
static int touch_available = 1, display_available = 1, allow_capture;
static uint32_t now;
static int state_available = 1, transport_available = 1;
static bool ready;
const char thy_diag_build_id[] = "protocol-test-0123456789";

const thy_instrument *thy_instrument_app_state(void) { return state_available ? &instrument : NULL; }
const thy_instrument_transport *thy_instrument_app_transport(void) { return transport_available ? &transport : NULL; }
const thy_settings_journal *thy_instrument_app_settings(void) { return &settings; }
size_t thy_instrument_app_song_command(const char *line,char *output,size_t capacity) {
    (void)line; (void)output; (void)capacity; return 0u;
}
size_t thy_instrument_app_backup_command(const char *line,char *output,size_t capacity) {
    CHECK(strstr(line,"backup ")==line&&capacity>=256u);++backup_calls;
    strcpy(output,"BACKUP stub\r\n");return strlen(output);
}
size_t thy_instrument_app_companion_command(const char *line,char *output,size_t capacity) {
    CHECK(strstr(line,"companion ")==line&&capacity>=384u);++companion_calls;
    strcpy(output,"COMPANION/1 stub\r\n");return strlen(output);
}
const char *thy_instrument_app_scale_name(void) { return "MAJOR C"; }
bool thy_instrument_app_ready(void) { return ready; }
void thy_instrument_app_panic(void) { ++panic_calls; thy_instrument_panic(&instrument); }
const thy_instrument_din_stats *thy_instrument_din_report(void) { return &din; }
const thy_instrument_lights_status *thy_instrument_lights_report(void) { return &lights; }
const diag_led_report_t *board_led_report(void) { return &leds; }
const diag_pads_report_t *board_pads_report(void) { return &pads; }
const volatile diag_report_t *board_report(void) { return &board; }
uint32_t board_millis(void) { return now; }
uint32_t board_touch_response_ms(void) { return 8u; }
const thy_touch_capture *board_touch_config_report(void) { return touch_available ? &touch : NULL; }
int board_touch_config_begin(uint32_t mask) { ++capture_begin_calls; return thy_touch_capture_begin(&touch, mask, now); }
const thy_display_report *board_display_report(void) { return display_available ? &display : NULL; }

static bool sink_usb(void *context, const uint8_t packet[4])
{ (void)context; (void)packet; CHECK(!"read-only protocol invoked a MIDI sink"); return false; }
static bool sink_din(void *context, const uint8_t *bytes, size_t count)
{ (void)context; (void)bytes; (void)count; CHECK(!"read-only protocol invoked a MIDI sink"); return false; }

static void reset(void)
{
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    memset(layout, 0, sizeof layout);
    for (unsigned i = 0u; i < THY_PAD_COUNT; ++i) {
        layout[i].notes[0] = (int16_t)(24u + i % 84u);
        layout[i].valid_mask = 1u;
        thy_instrument_calibration calibration = {100u, 96u, 32u, 4095u, 1};
        CHECK(thy_instrument_calibrate(&instrument, (uint8_t)i, &calibration) == THY_OK);
    }
    int16_t degrees[7] = {0, 2, 4, 5, 7, 9, 11};
    CHECK(thy_instrument_set_layout(&instrument, layout, 60, degrees, 7u) == THY_OK);
    thy_instrument_transport_io io = {NULL, sink_usb, sink_din};
    thy_instrument_transport_init(&transport, &core, &io);
    memset(&din, 0, sizeof din); memset(&lights, 0, sizeof lights);
    memset(&leds, 0, sizeof leds); memset(&pads, 0, sizeof pads); memset(&board, 0, sizeof board);
    const uint32_t uid[3] = {0x00112233u,0x44556677u,0x8899aabbu};
    thy_touch_capture_init(&touch, uid); memset(&display, 0, sizeof display);
    capture_begin_calls = capture_read_calls = 0u; touch_available = display_available = 1; allow_capture = 0;
    state_available = transport_available = 1; ready = true; now = 0u; panic_calls = 0u;
}

/* Check every read-only command against full copies of all mutable fixture
 * state, including ownership queues and retained transition history. */
static size_t read_only(const char *command, char output[THY_DIAG_REPLY_MAX])
{
    static thy_core core_before;
    static thy_instrument instrument_before;
    static thy_instrument_transport transport_before;
    thy_instrument_din_stats din_before = din;
    thy_instrument_lights_status lights_before = lights;
    diag_led_report_t leds_before = leds;
    diag_pads_report_t pads_before = pads;
    diag_report_t board_before = board;
    thy_touch_capture touch_before = touch;
    thy_display_report display_before = display;
    unsigned before_capture_calls = capture_begin_calls;
    unsigned before_read_calls = capture_read_calls;
    memcpy(&core_before, &core, sizeof core); memcpy(&instrument_before, &instrument, sizeof instrument);
    memcpy(&transport_before, &transport, sizeof transport);
    unsigned before_panic = panic_calls;
    unsigned char guarded[THY_DIAG_REPLY_MAX + 2u]; memset(guarded, 0xa5, sizeof guarded);
    size_t length = thy_diag_command(command, (char *)guarded + 1u, THY_DIAG_REPLY_MAX);
    CHECK(length >= 2u && length < THY_DIAG_REPLY_MAX);
    CHECK(guarded[0] == 0xa5 && guarded[THY_DIAG_REPLY_MAX + 1u] == 0xa5);
    CHECK(guarded[length + 1u] == 0u);
    CHECK(guarded[length - 1u] == '\r' && guarded[length] == '\n');
    CHECK(length == strlen((char *)guarded + 1u));
    CHECK(memcmp(&core_before, &core, sizeof core) == 0);
    CHECK(memcmp(&instrument_before, &instrument, sizeof instrument) == 0);
    CHECK(memcmp(&transport_before, &transport, sizeof transport) == 0);
    CHECK(memcmp(&din_before, &din, sizeof din) == 0);
    CHECK(memcmp(&lights_before, &lights, sizeof lights) == 0);
    CHECK(memcmp(&leds_before, &leds, sizeof leds) == 0);
    CHECK(memcmp(&pads_before, &pads, sizeof pads) == 0);
    CHECK(memcmp(&board_before, &board, sizeof board) == 0);
    CHECK(memcmp(&display_before, &display, sizeof display) == 0);
    if (!allow_capture) {
        CHECK(memcmp(&touch_before, &touch, sizeof touch) == 0);
        CHECK(before_capture_calls == capture_begin_calls);
    }
    CHECK(before_read_calls == capture_read_calls); /* Console never performs bus work. */
    CHECK(before_panic == panic_calls);
    memcpy(output, guarded + 1u, length + 1u);
    return length;
}

static uint32_t unsigned_field(const char *reply, const char *field)
{
    const char *start = strstr(reply, field); CHECK(start != NULL);
    char *end;
    unsigned long long value = strtoull(start + strlen(field), &end, 10);
    CHECK(end != start + strlen(field) && value <= UINT32_MAX);
    CHECK(*end == ' ' || *end == '\r' || *end == 0);
    return (uint32_t)value;
}

static int32_t signed_field(const char *reply, const char *field)
{
    const char *start = strstr(reply, field); CHECK(start != NULL);
    char *end; long value = strtol(start + strlen(field), &end, 10);
    CHECK(end != start + strlen(field) && value >= INT32_MIN && value <= INT32_MAX);
    CHECK(*end == ' ' || *end == '\r' || *end == 0);
    return (int32_t)value;
}

static void all_pad_fields(void)
{
    reset(); ++scenarios;
    char output[THY_DIAG_REPLY_MAX], command[32];
    for (unsigned id = 0u; id < THY_PAD_COUNT; ++id) {
        thy_instrument_sensor *s = &instrument.sensors[id];
        instrument.mapping[id] = (uint8_t)(95u - id);
        s->raw = (uint16_t)(4095u - id); s->calibration.baseline = (uint16_t)(1000u + id);
        s->delta = (uint16_t)(id * 13u); s->signed_delta = (int16_t)(-4095 + (int)id);
        s->attack_raw = (int16_t)(-32768 + (int)id); s->release_raw = (int16_t)(32767 - (int)id);
        s->detector_profile = (uint8_t)(id & 1u); s->held = (uint8_t)(id & 1u);
        s->blocked = (uint8_t)((id >> 1u) & 1u); s->warmup_count = (uint8_t)(id % 9u);
        s->attack_count = (uint8_t)(id % 3u); s->release_count = (uint8_t)((id + 1u) % 3u);
        s->pressure = (uint8_t)(id + 1u); s->last_sample_ms = UINT32_MAX - id;
        s->last_transition_ms = UINT32_C(0x80000000) + id;
        snprintf(command, sizeof command, "pad %u", id);
        read_only(command, output);
        CHECK(strncmp(output, "PAD id=", 7u) == 0);
        CHECK(unsigned_field(output, "id=") == id);
        CHECK(unsigned_field(output, " logical=") == 95u - id);
        CHECK(unsigned_field(output, " raw=") == s->raw);
        CHECK(unsigned_field(output, " baseline=") == s->calibration.baseline);
        CHECK(unsigned_field(output, " delta=") == s->delta);
        CHECK(signed_field(output, " signed_delta=") == s->signed_delta);
        CHECK(signed_field(output, " attack=") == s->attack_raw);
        CHECK(signed_field(output, " release=") == s->release_raw);
        CHECK(unsigned_field(output, " profile=") == s->detector_profile);
        CHECK(unsigned_field(output, " held=") == s->held);
        CHECK(unsigned_field(output, " blocked=") == s->blocked);
        CHECK(unsigned_field(output, " warmup=") == s->warmup_count);
        CHECK(unsigned_field(output, " attack_count=") == s->attack_count);
        CHECK(unsigned_field(output, " release_count=") == s->release_count);
        CHECK(unsigned_field(output, " pressure=") == s->pressure);
        CHECK(unsigned_field(output, " sample_ms=") == s->last_sample_ms);
        CHECK(unsigned_field(output, " transition_ms=") == s->last_transition_ms);
    }
    read_only("pad 00095", output); CHECK(unsigned_field(output, "id=") == 95u);
}

static void malformed_and_bounds(void)
{
    reset(); ++scenarios;
    char output[THY_DIAG_REPLY_MAX];
    static const char *malformed[] = {"", "pad", "pad ", "pad -1", "pad +1", "pad 1 ", "pad 1x", "pad  1",
        "pad\t1", "pad 1\n", "pad 1\r\n", "PAD 1", "pad 4294967296", "pad 9999999999999999999999999999999999999999",
        "edges", "edges ", "edges -1", "edges +1", "edges 1 ", "edges 1;panic", "edges 4294967296", "edges 1\npanic",
        "panic ", "panic\r\n", "panic;reset", "reset", "nor 0 32", "internal 08000000 4", "led all 0", "io extra", "help extra",
        "velocity", "velocity ", "velocity -1", "velocity +1", "velocity 1 ", "velocity 1x", "velocity  1",
        "velocity 4294967296", "velocity 1;panic", "velocity 1\n"};
    for (unsigned i = 0u; i < sizeof malformed / sizeof malformed[0]; ++i) {
        read_only(malformed[i], output); CHECK(strcmp(output, "ERR unknown-command\r\n") == 0);
    }
    read_only("pad 96", output); CHECK(strcmp(output, "ERR pad-id\r\n") == 0);
    read_only("pad 4294967295", output); CHECK(strcmp(output, "ERR pad-id\r\n") == 0);
    read_only("velocity 96", output); CHECK(strcmp(output,"ERR pad-id\r\n")==0);
    read_only("edges 1", output); CHECK(strcmp(output, "ERR edge-cursor\r\n") == 0);
    unsigned char buffer[THY_DIAG_REPLY_MAX + 2u]; memset(buffer, 0xa5, sizeof buffer);
    CHECK(thy_diag_command(NULL, (char *)buffer, sizeof buffer) == 0u);
    CHECK(thy_diag_command("panic", NULL, sizeof buffer) == 0u);
    for (size_t capacity = 0u; capacity < THY_DIAG_REPLY_MAX; ++capacity) {
        CHECK(thy_diag_command("panic", (char *)buffer, capacity) == 0u);
        CHECK(panic_calls == 0u);
    }
    for (size_t i = 0u; i < sizeof buffer; ++i) CHECK(buffer[i] == 0xa5);
    state_available = 0;
    read_only("pad 0", output); CHECK(strcmp(output, "ERR pad-id\r\n") == 0);
    read_only("velocity 0", output); CHECK(strcmp(output,"ERR pad-id\r\n")==0);
    read_only("edges 0", output); CHECK(strcmp(output, "ERR not-ready\r\n") == 0);
    read_only("status", output); CHECK(unsigned_field(output, " voices=") == 0u);
    transport_available = 0;
    read_only("io", output); CHECK(unsigned_field(output, " usb_ready=") == 0u);
    CHECK(unsigned_field(output, " core_queued=") == 0u);
}

static void velocity_observation_is_effective_read_only_and_backwards_compatible(void)
{
    reset();++scenarios;char output[THY_DIAG_REPLY_MAX],command[32];
    pads.frames=297u;pads.scans=4752u;
    read_only("pads",output);
    CHECK(strcmp(output,"PADS frames=297 scans=4752 errors=0 profile=150us-21MHz velocity=fixed96 calibration=unverified\r\n")==0);
    instrument.fixed_velocity=117u;read_only("pads",output);CHECK(strstr(output," velocity=fixed117 "));
    thy_instrument_velocity global={2u,128u,THY_VELOCITY_STRIKE,THY_PRESSURE_SOFT,7u,119u};
    CHECK(thy_instrument_set_velocity(&instrument,&global)==THY_OK);
    read_only("pads",output);CHECK(strstr(output," velocity=strike "));
    thy_instrument_velocity right=global;right.mode=THY_VELOCITY_FIXED;
    CHECK(thy_instrument_set_cluster_velocity(&instrument,1u,&right)==THY_OK);
    read_only("pads",output);CHECK(strstr(output," velocity=mixed "));
    read_only("velocity 48",output);CHECK(unsigned_field(output," mode=")==THY_VELOCITY_FIXED);
    CHECK(unsigned_field(output," override=")==0u);
    CHECK(thy_instrument_set_velocity(&instrument,&global)==THY_OK);
    for(unsigned id=0u;id<THY_PAD_COUNT;++id) {
        thy_instrument_velocity expected=global;
        if(id%2u) {
            expected=(thy_instrument_velocity){(uint16_t)(2u+id),(uint16_t)(140u+id),
                (uint8_t)((id/2u)%2u),(uint8_t)(id%3u),11u,123u};
            CHECK(thy_instrument_set_pad_velocity(&instrument,(uint8_t)id,&expected)==THY_OK);
        }
        instrument.mapping[id]=(uint8_t)(95u-id);
        instrument.sensors[id].strike_velocity=(uint8_t)(id%128u);
        instrument.sensors[id].strike_peak_rate=(uint16_t)(40u*id);
        instrument.sensors[id].last_sample_ms=UINT32_MAX-id;
        snprintf(command,sizeof command,"velocity %u",id);read_only(command,output);
        CHECK(strncmp(output,"VELOCITY id=",12u)==0);
        CHECK(unsigned_field(output,"id=")==id && unsigned_field(output," logical=")==95u-id);
        CHECK(unsigned_field(output," mode=")==expected.mode && unsigned_field(output," override=")==id%2u);
        CHECK(unsigned_field(output," soft=")==expected.soft_rate && unsigned_field(output," hard=")==expected.hard_rate);
        CHECK(unsigned_field(output," curve=")==expected.curve && unsigned_field(output," minimum=")==expected.minimum && unsigned_field(output," maximum=")==expected.maximum);
        CHECK(unsigned_field(output," last=")==id%128u && unsigned_field(output," peak=")==40u*id);
        CHECK(unsigned_field(output," sample_ms=")==UINT32_MAX-id);
    }
    read_only("pads",output);CHECK(strstr(output," velocity=mixed "));
    read_only("help velocity",output);CHECK(strstr(output,"OK VELOCITY/1 ") && strstr(output,"config_writes=none"));
    state_available=0;read_only("pads",output);CHECK(strstr(output," velocity=fixed96 "));
}

static void compare_edge_page(const char *reply, uint32_t first, unsigned expected_count)
{
    CHECK(unsigned_field(reply, " count=") == expected_count);
    const char *entry = strstr(reply, " data="); CHECK(entry != NULL); entry += 6u;
    if (!expected_count) { CHECK(strcmp(entry, "-\r\n") == 0); return; }
    for (unsigned i = 0u; i < expected_count; ++i) {
        unsigned sequence, at, electrical, logical, action, raw, baseline, blocked, profile;
        int delta, attack, release, consumed = 0;
        CHECK(sscanf(entry, "%u,%u,%u,%u,%u,%u,%u,%d,%d,%d,%u,%u%n", &sequence, &at,
            &electrical, &logical, &action, &raw, &baseline, &delta, &attack, &release, &blocked, &profile, &consumed) == 12);
        thy_instrument_pad_transition e;
        CHECK(thy_instrument_get_transition(&instrument, first + i, &e) == 1);
        CHECK(sequence == e.sequence && at == e.at_ms && electrical == e.electrical && logical == e.logical);
        CHECK(action == e.action && raw == e.raw && baseline == e.baseline && delta == e.signed_delta);
        CHECK(attack == e.attack_raw && release == e.release_raw && blocked == e.blocked && profile == e.detector_profile);
        entry += consumed;
        if (i + 1u < expected_count) { CHECK(*entry == ';'); ++entry; }
        else CHECK(strcmp(entry, "\r\n") == 0);
    }
}

static void stroke(unsigned pad)
{
    CHECK(thy_instrument_sample(&instrument, (uint8_t)pad, 250u, ++now) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, (uint8_t)pad, 250u, ++now) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, (uint8_t)pad, 100u, ++now) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, (uint8_t)pad, 100u, ++now) == THY_OK);
}

static void journal_pagination_and_overwrite(void)
{
    reset(); ++scenarios;
    char output[THY_DIAG_REPLY_MAX], repeated[THY_DIAG_REPLY_MAX], command[48];
    read_only("edges 0", output); CHECK(strcmp(output, "EDGES oldest=0 newest=0 overwritten=0 count=0 next=0 data=-\r\n") == 0);
    stroke(0u);
    CHECK(instrument.transition_sequence == 2u);
    read_only("edges 0", output); compare_edge_page(output, 1u, 2u);
    read_only("edges 0", repeated); CHECK(strcmp(output, repeated) == 0);
    for (unsigned i = 1u; i < 40u; ++i) stroke(i);
    CHECK(instrument.transition_sequence == 80u && instrument.transition_count == 64u && instrument.transition_overwritten == 16u);
    uint32_t cursor = 0u;
    for (unsigned page = 0u; page < 8u; ++page) {
        snprintf(command, sizeof command, "edges %u", cursor);
        read_only(command, output);
        CHECK(unsigned_field(output, "oldest=") == 17u && unsigned_field(output, " newest=") == 80u);
        CHECK(unsigned_field(output, " overwritten=") == 16u);
        compare_edge_page(output, 17u + page * 8u, 8u);
        cursor = unsigned_field(output, " next="); CHECK(cursor == 24u + page * 8u);
    }
    read_only("edges 80", output); compare_edge_page(output, 0u, 0u);
    CHECK(unsigned_field(output, " next=") == 80u);
    read_only("edges 5", output); compare_edge_page(output, 17u, 8u);
    read_only("edges 16", output); compare_edge_page(output, 17u, 8u);
    read_only("edges 77", output); compare_edge_page(output, 78u, 3u);
    read_only("edges 81", output); CHECK(strcmp(output, "ERR edge-cursor\r\n") == 0);
}

static void journal_maximum_cursor_and_reply(void)
{
    reset(); ++scenarios;
    char output[THY_DIAG_REPLY_MAX];
    /* Directly seed the final representable epoch, which cannot be reached
     * with billions of physical gestures in a regression test. */
    instrument.transition_sequence = UINT32_MAX;
    instrument.transition_count = THY_INSTRUMENT_TRANSITIONS;
    instrument.transition_overwritten = UINT32_MAX;
    for (uint32_t i = 0u; i < THY_INSTRUMENT_TRANSITIONS; ++i) {
        uint32_t sequence = UINT32_MAX - 63u + i;
        thy_instrument_pad_transition e = {0};
        e.sequence = sequence; e.at_ms = UINT32_MAX; e.raw = e.baseline = UINT16_MAX;
        e.signed_delta = e.attack_raw = INT16_MIN; e.release_raw = INT16_MAX;
        e.electrical = e.logical = e.action = e.blocked = e.detector_profile = UINT8_MAX;
        instrument.transitions[(sequence - 1u) % THY_INSTRUMENT_TRANSITIONS] = e;
    }
    read_only("edges 0", output); compare_edge_page(output, UINT32_MAX - 63u, 8u);
    CHECK(unsigned_field(output, " next=") == UINT32_MAX - 56u);
    read_only("edges 4294967294", output); compare_edge_page(output, UINT32_MAX, 1u);
    CHECK(unsigned_field(output, " next=") == UINT32_MAX);
    read_only("edges 4294967295", output); compare_edge_page(output, 0u, 0u);
    CHECK(unsigned_field(output, " next=") == UINT32_MAX);
    /* The next real edge rolls history to a new sequence epoch. */
    CHECK(thy_instrument_sample(&instrument, 0u, 250u, ++now) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 250u, ++now) == THY_OK);
    CHECK(instrument.transition_sequence == 1u && instrument.transition_count == 1u);
    read_only("edges 4294967295", output); CHECK(strcmp(output, "ERR edge-cursor\r\n") == 0);
    read_only("edges 0", output); compare_edge_page(output, 1u, 1u);
}

static void status_and_all_io_fields(void)
{
    reset(); ++scenarios;
    char output[THY_DIAG_REPLY_MAX];
    din.ready = 1u; din.errors = 2u; din.queued = 3u; din.transmitted_bytes = 4u; din.accepted_messages = 5u;
    transport.mounted = 1u; transport.session_seen = 1u;
    transport.reconcile_index = THY_INSTRUMENT_TRANSPORT_RECONCILE_EVENTS; transport.usb_restore_index = 32u;
    transport.usb_accepted = 6u; transport.din_accepted = 7u;
    transport.discarded_events = 8u; transport.usb_suppressed = 9u;
    transport.din_queue.count = 10u; transport.usb_queue.count = 11u;
    instrument.sensors[0].held = instrument.sensors[1].held = 1u; instrument.sensors[1].blocked = 1u;
    instrument.sensors[95].warmup_count = 7u;
    leds.errors = 12u; lights.errors = 13u; leds.frames = 14u; lights.faulted = 1u;
    read_only("io", output);
    CHECK(strcmp(output, "IO din_ready=1 din_errors=2 din_queued=3 din_tx=4 din_accept=5 usb_ready=1 usb_mounted=1 usb_accepted=6 din_events=7 usb_dropped=17 transport_fault=0 core_queued=0 core_pending=0 din_pending=10 usb_pending=11 held=2 blocked=1 warmed=95 led_errors=25 led_frames=14 led_fault=1 uptime_ms=0\r\n") == 0);
    int16_t chord[3] = {60, 64, 67}; thy_shape strum = {0}; strum.strum_ms = 20u;
    CHECK(thy_core_press_shaped(&core, 5u, chord, 3u, 0, 0u,
        THY_ROUTE_USB0 | THY_ROUTE_DIN, 96u, &strum) == THY_OK);
    CHECK(thy_core_queued(&core) == 2u && thy_core_pending(&core) == 4u);
    read_only("io", output);
    CHECK(unsigned_field(output, " core_queued=") == 2u);
    CHECK(unsigned_field(output, " core_pending=") == 4u);
    thy_core_panic(&core);
    thy_midi_event event; while (thy_core_poll(&core, &event)) { }
    transport.invalid_pending = 1u;
    read_only("io", output); CHECK(unsigned_field(output, " usb_ready=") == 0u);
    CHECK(unsigned_field(output, " transport_fault=") == 1u);
    now = UINT32_MAX; board.errors = 1u; instrument.errors = 2u; instrument.presses = 123u; instrument.releases = 122u;
    read_only("status", output);
    CHECK(strcmp(output, "STATUS THYINSTRUMENT/1 build=protocol-test-0123456789 ready=1 uptime_ms=4294967295 errors=3 presses=123 releases=122 voices=0 layout=MAJOR C\r\n") == 0);
    pads.frames = 200u; pads.scans = 3200u; pads.errors = 4u;
    read_only("pads", output);
    CHECK(strcmp(output, "PADS frames=200 scans=3200 errors=4 profile=150us-21MHz velocity=fixed96 calibration=unverified\r\n") == 0);
    /* Configurable attack velocity and custom detector IDs must be visible in
     * the actual wire schema, without changing the default fixed96 reply. */
    const thy_instrument_response response = {1200u, 1100u, 1000u, 3500u, THY_PRESSURE_LINEAR};
    CHECK(thy_instrument_set_pad_response(&instrument, 48u, &response) == THY_OK);
    CHECK(thy_instrument_set_expression(&instrument, 112u, 0u) == THY_OK);
    for (unsigned sample = 0u; sample < THY_INSTRUMENT_WARMUP_SAMPLES; ++sample)
        CHECK(thy_instrument_sample(&instrument, 48u, 100u, ++now) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 48u, 2000u, ++now) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 48u, 2000u, ++now) == THY_OK);
    read_only("pads", output);
    CHECK(strcmp(output, "PADS frames=200 scans=3200 errors=4 profile=150us-21MHz velocity=fixed112 calibration=unverified\r\n") == 0);
    read_only("pad 48", output);
    CHECK(unsigned_field(output, " profile=") == THY_DETECTOR_CUSTOM_ABSOLUTE);
    CHECK(unsigned_field(output, " attack=") == 1200u && unsigned_field(output, " release=") == 1100u);
    read_only("edges 0", output);
    compare_edge_page(output, 1u, 1u);
    CHECK(strstr(output, ",1100,0,2\r\n") != NULL);
    CHECK(thy_core_poll(&core, &event) == 1);
    CHECK(event.status == 0x91u && event.data1 == 72u && event.data2 == 112u);
    CHECK(thy_instrument_sample(&instrument, 48u, 100u, ++now) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 48u, 100u, ++now) == THY_OK);
    CHECK(thy_core_poll(&core, &event) == 1);
    CHECK(event.status == 0x81u && event.data1 == 72u && event.data2 == 0u);
    read_only("help", output); CHECK(strstr(output, "commands=status,pads,pad,edges,io,panic,help") != NULL);
    CHECK(strstr(output, "storage_writes=none autosave=idle\r\n") != NULL);
}

static void panic_real_ownership_and_block_journal(void)
{
    reset(); ++scenarios;
    CHECK(thy_instrument_set_route(&instrument, 0u, 0u, THY_ROUTE_USB0 | THY_ROUTE_DIN, 0) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 250u, ++now) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 250u, ++now) == THY_OK);
    CHECK(thy_core_sounding(&core) == 2u && core.owners[0].held == 1u);
    thy_midi_event event; unsigned ons = 0u;
    while (thy_core_poll(&core, &event)) { CHECK((event.status & 0xf0u) == 0x90u); ++ons; }
    CHECK(ons == 2u);
    char output[THY_DIAG_REPLY_MAX];
    read_only("pad 0", output); CHECK(unsigned_field(output, " held=") == 1u);
    CHECK(thy_diag_command("panic", output, sizeof output) == strlen("OK panic\r\n"));
    CHECK(strcmp(output, "OK panic\r\n") == 0 && panic_calls == 1u);
    CHECK(thy_core_sounding(&core) == 0u && core.owners[0].held == 0u);
    CHECK(instrument.sensors[0].held == 1u && instrument.sensors[0].blocked == 1u);
    unsigned offs = 0u, destination_mask = 0u;
    while (thy_core_poll(&core, &event)) {
        CHECK((event.status & 0xf0u) == 0x80u && event.data1 == 24u);
        destination_mask |= 1u << event.destination; ++offs;
    }
    CHECK(offs == 2u && destination_mask == (THY_ROUTE_USB0 | THY_ROUTE_DIN));
    read_only("edges 1", output); compare_edge_page(output, 2u, 1u);
    CHECK(strstr(output, ",3,") != NULL); /* New blocking transition. */
    read_only("io", output); CHECK(unsigned_field(output, " held=") == 1u);
    CHECK(unsigned_field(output, " blocked=") == 1u && unsigned_field(output, " core_queued=") == 0u);
    CHECK(thy_instrument_sample(&instrument, 0u, 250u, ++now) == THY_OK);
    CHECK(thy_core_sounding(&core) == 0u); /* Holding after panic cannot retrigger. */
}

static int touch_read_fixture(void *context, unsigned index, uint8_t reg, uint16_t *word)
{
    (void)context; ++capture_read_calls;
    if (reg == 0x8fu) *word = 0x059au;
    else if (reg == 0x91u) *word = 0x010au;
    else *word = (uint16_t)((uint8_t)(index * 43u + reg * 29u + 7u) |
        (uint16_t)((uint8_t)(index * 43u + (reg + 1u) * 29u + 7u)) << 8);
    return 0;
}

static void touch_capture_and_lossless_pages(void)
{
    reset(); ++scenarios;
    char output[THY_DIAG_REPLY_MAX], command[48];
    /* Requests and observations cannot affect an actively owned note. */
    CHECK(thy_instrument_set_route(&instrument, 0u, 0u, THY_ROUTE_USB0 | THY_ROUTE_DIN, 0) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 250u, ++now) == THY_OK);
    CHECK(thy_instrument_sample(&instrument, 0u, 250u, ++now) == THY_OK);
    board.touch_initialized_mask = 15u;
    read_only("help", output);
    CHECK(strcmp(output,"OK THYINSTRUMENT/1 commands=status,pads,pad,edges,io,panic,help,settings storage_writes=none autosave=idle\r\n") == 0);
    read_only("help touch", output); CHECK(strstr(output,"OK TOUCH/1 commands=touch;") == output);
    read_only("touch", output);
    CHECK(strcmp(output,"TOUCH center_poll_ms=8 default_center_poll_ms=8 top_poll_ms=20 ready=15 failed=0 capture_pending=0 capture_complete=0 capture_failed=0 uid=00112233445566778899aabb config_bytes=128 writes=none\r\n") == 0);
    allow_capture = 1;
    read_only("touch capture 15", output);
    CHECK(strcmp(output,"TOUCH_CAPTURE mask=15 result=0 reads_only=1\r\n") == 0);
    CHECK(capture_begin_calls == 1u && capture_read_calls == 0u);
    read_only("touch capture 1", output);
    CHECK(strcmp(output,"TOUCH_CAPTURE mask=1 result=-4 reads_only=1\r\n") == 0);
    allow_capture = 0;
    read_only("touch", output); CHECK(unsigned_field(output," capture_pending=") == 15u);
    for (unsigned i = 0; i < 272u; ++i)
        (void)thy_touch_capture_step(&touch, now + 2u * i, 15u, touch_read_fixture, NULL);
    read_only("touch", output); CHECK(unsigned_field(output," capture_complete=") == 15u);
    CHECK(capture_read_calls == 272u);
    for (unsigned bank_index = 0; bank_index < 4u; ++bank_index) {
        snprintf(command,sizeof command,"touch bank %u",bank_index);
        read_only(command,output);
        CHECK(unsigned_field(output," bank=") == bank_index);
        CHECK(unsigned_field(output," state=") == THY_TOUCH_CAPTURE_COMPLETE);
        CHECK(unsigned_field(output," valid_bytes=") == 128u);
        CHECK(unsigned_field(output," stored_crc_valid=") == 1u);
        char crc[32]; snprintf(crc,sizeof crc," capture_crc32=%08x ",touch.bank[bank_index].capture_crc32);
        CHECK(strstr(output,crc) != NULL);
        for (unsigned offset = 0; offset < 128u; offset += 16u) {
            snprintf(command,sizeof command,"touch page %u %u",bank_index,offset);
            read_only(command,output);
            CHECK(unsigned_field(output," bank=") == bank_index && unsigned_field(output," offset=") == offset);
            CHECK(strstr(output," valid=ffff data=") != NULL);
            const char *data = strstr(output," data=") + 6u;
            for (unsigned i = 0; i < 16u; ++i) {
                char hex[3]; snprintf(hex,sizeof hex,"%02x",touch.bank[bank_index].bytes[offset+i]);
                CHECK(data[i*2u] == hex[0] && data[i*2u+1u] == hex[1]);
            }
            CHECK(data[32] == ' ' && strstr(data+32," crc32=") == data+32);
        }
    }
    CHECK(touch.bank[0].capture_crc32 == UINT32_C(0x35750aad));
    CHECK(thy_core_sounding(&core) == 2u);
}

static void touch_partial_and_strict_arguments(void)
{
    reset(); ++scenarios;
    char output[THY_DIAG_REPLY_MAX];
    static const char *malformed[] = {"touch ","touch capture","touch capture ","touch capture -1",
        "touch capture +1","touch capture 1 ","touch capture 4294967296","touch capture 1;panic",
        "touch bank","touch bank 0 ","touch bank -1","touch bank 4294967296","touch bank 0 extra",
        "touch page 0","touch page 0 ","touch page  0 16","touch page 0  16","touch page 0 -16",
        "touch page 0 16 ","touch page 0 16x","touch page 4294967296 0","touch page 0 4294967296",
        "touch page 0 16\n","touch page 0 16;panic","display ","display extra","help touch extra"};
    for (unsigned i = 0; i < sizeof malformed / sizeof malformed[0]; ++i) {
        read_only(malformed[i],output); CHECK(strcmp(output,"ERR unknown-command\r\n") == 0);
    }
    read_only("touch capture 0",output); CHECK(strcmp(output,"ERR touch-mask\r\n") == 0);
    read_only("touch capture 16",output); CHECK(strcmp(output,"ERR touch-mask\r\n") == 0);
    read_only("touch bank 4",output); CHECK(strcmp(output,"ERR touch-bank\r\n") == 0);
    read_only("touch page 4 0",output); CHECK(strcmp(output,"ERR touch-page\r\n") == 0);
    read_only("touch page 0 1",output); CHECK(strcmp(output,"ERR touch-page\r\n") == 0);
    read_only("touch page 0 128",output); CHECK(strcmp(output,"ERR touch-page\r\n") == 0);
    touch.bank[3].state = THY_TOUCH_CAPTURE_FAILED; touch.bank[3].result = -2;
    touch.bank[3].valid[14] = 0x55u; touch.bank[3].valid[15] = 0xaau;
    for (unsigned i = 112u; i < 128u; ++i) touch.bank[3].bytes[i] = (uint8_t)i;
    read_only("touch page 3 112",output);
    CHECK(strcmp(output,"TOUCH_PAGE bank=3 offset=112 state=4 valid=aa55 data=707172737475767778797a7b7c7d7e7f crc32=00000000\r\n") == 0);
    read_only("touch bank 3",output);
    CHECK(unsigned_field(output," valid_bytes=") == 8u && signed_field(output," result=") == -2);
    read_only("touch",output); CHECK(unsigned_field(output," capture_failed=") == 8u);
    touch_available = 0;
    read_only("touch",output); CHECK(strcmp(output,"ERR not-ready\r\n") == 0);
    read_only("touch bank 0",output); CHECK(strcmp(output,"ERR not-ready\r\n") == 0);
    read_only("touch page 0 0",output); CHECK(strcmp(output,"ERR not-ready\r\n") == 0);
}

static void display_read_only_report(void)
{
    reset(); ++scenarios;
    char output[THY_DIAG_REPLY_MAX];
    display.frames = 1u; display.chunks = 2u; display.command_bytes = 3u; display.data_bytes = 4u;
    display.busy_deferrals = 5u; display.last_started_ms = 6u; display.last_completed_ms = 7u;
    display.last_duration_ms = 8u; display.max_duration_ms = 9u; display.last_render_gap_ms = 10u;
    display.max_render_gap_ms = UINT32_MAX; display.active = 1u; display.page = 7u; display.column = 127u; display.faulted = 0u;
    display.submitted = 11u; display.coalesced = 12u; display.unchanged = 13u; display.failed_chunks = 14u;
    display.generation = 15u; display.last_generation_age_ms = 16u; display.max_generation_age_ms = 17u;
    display.dirty_bytes = 18u; display.valid_bytes = 1024u; display.initial_complete = 1u;
    display.last_render_us = 19u; display.max_render_us = 20u;
    display.last_chunk_us = 21u; display.max_chunk_us = UINT32_MAX;
    read_only("display",output);
    CHECK(strcmp(output,"DISPLAY version=2 period_ms=16 spi_hz=328125 frames=1 chunks=2 command_bytes=3 data_bytes=4 busy_deferrals=5 last_started_ms=6 last_completed_ms=7 last_duration_ms=8 max_duration_ms=9 last_render_gap_ms=10 max_render_gap_ms=4294967295 active=1 page=7 column=127 faulted=0 submitted=11 coalesced=12 unchanged=13 failed_chunks=14 generation=15 last_generation_age_ms=16 max_generation_age_ms=17 dirty_bytes=18 valid_bytes=1024 initial_complete=1 last_render_us=19 max_render_us=20 last_chunk_us=21 max_chunk_us=4294967295\r\n") == 0);
    /* Every uint32 counter may reach its full width without truncating the
     * read-only reply or changing transfer/source ownership. */
    display.frames = display.chunks = display.command_bytes = display.data_bytes = UINT32_MAX;
    display.busy_deferrals = display.last_started_ms = display.last_completed_ms = UINT32_MAX;
    display.last_duration_ms = display.max_duration_ms = UINT32_MAX;
    display.last_render_gap_ms = display.max_render_gap_ms = UINT32_MAX;
    display.submitted = display.coalesced = display.unchanged = display.failed_chunks = UINT32_MAX;
    display.generation = display.last_generation_age_ms = display.max_generation_age_ms = UINT32_MAX;
    display.last_render_us = display.max_render_us = display.last_chunk_us = display.max_chunk_us = UINT32_MAX;
    display.dirty_bytes = 1024u; display.valid_bytes = 0u; display.initial_complete = 0u;
    read_only("display",output);
    CHECK(unsigned_field(output," version=") == 2u && unsigned_field(output," period_ms=") == 16u);
    CHECK(unsigned_field(output," dirty_bytes=") == 1024u && unsigned_field(output," valid_bytes=") == 0u);
    CHECK(unsigned_field(output," submitted=") == UINT32_MAX && unsigned_field(output," max_render_us=") == UINT32_MAX);
    CHECK(unsigned_field(output," failed_chunks=") == UINT32_MAX && unsigned_field(output," max_chunk_us=") == UINT32_MAX);
    display_available = 0;
    read_only("display",output); CHECK(strcmp(output,"ERR not-ready\r\n") == 0);
}

static void settings_read_only_report(void)
{
    reset(); ++scenarios;
    char output[THY_DIAG_REPLY_MAX];
    settings=(thy_settings_journal){0}; settings.status=THY_SETTINGS_ERROR;
    settings.loaded=1u; settings.recovered=1u; settings.generation=43u;
    settings.saves=5u; settings.errors=2u; settings.rejected=3u;
    settings.loaded_version=1u; settings.migration_pending=1u;
    thy_settings_journal before=settings;
    char expected[THY_DIAG_REPLY_MAX];
    (void)snprintf(expected,sizeof expected,"SETTINGS version=%u status=error loaded=1 loaded_version=1 migration_pending=1 recovered=1 generation=43 saves=5 errors=2 rejected=3 backend=internal-sectors10-11 updater_preservation=unverified writes=none\r\n",THY_SETTINGS_VERSION);
    read_only("settings",output);
    CHECK(strcmp(output,expected)==0);
    CHECK(!memcmp(&before,&settings,sizeof settings));
    settings.status=THY_SETTINGS_SAVED; settings.recovered=0u; settings.errors=0u;
    settings.loaded_version=THY_SETTINGS_VERSION; settings.migration_pending=0u;
    before=settings;
    (void)snprintf(expected,sizeof expected,"SETTINGS version=%u status=saved loaded=1 loaded_version=%u migration_pending=0 recovered=0 generation=43 saves=5 errors=0 rejected=3 backend=internal-sectors10-11 updater_preservation=unverified writes=none\r\n",THY_SETTINGS_VERSION,THY_SETTINGS_VERSION);
    read_only("settings",output);
    CHECK(strcmp(output,expected)==0);
    CHECK(!memcmp(&before,&settings,sizeof settings));
    const char *invalid[]={"settings ","settings save","settings reset","settings;panic","settings\n"};
    for(unsigned n=0;n<sizeof invalid/sizeof invalid[0];++n) {
        read_only(invalid[n],output); CHECK(strcmp(output,"ERR unknown-command\r\n")==0);
    }
}

static void resources_read_only_report(void)
{
    reset();++scenarios;char output[THY_DIAG_REPLY_MAX];
    resources=(thy_resources){.valid=1u,.timing_valid=1u,.stack_valid=1u,.cpu_active=100u,
        .ram_used=102400u,.ram_total=131072u,.ccm_used=49152u,.ccm_total=65536u,
        .flash_used=409600u,.flash_total=524288u,.stack_used=3072u,.stack_total=8192u,
        .loop_us=30u,.loop_peak_us=420u,.sampled_ms=1000u};
    thy_resources before=resources;read_only("status",output);
    CHECK(unsigned_field(output," resources=")==1u);
    CHECK(unsigned_field(output," cpu_active=")==100u);
    CHECK(unsigned_field(output," ram_used=")==102400u);
    CHECK(unsigned_field(output," flash_total=")==524288u);
    CHECK(unsigned_field(output," stack_used=")==3072u);
    CHECK(unsigned_field(output," loop_peak_us=")==420u);
    CHECK(strstr(output," layout=") && strlen(output)<THY_DIAG_REPLY_MAX-1u);
    CHECK(!memcmp(&before,&resources,sizeof resources));
    resources=(thy_resources){0};
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--fixtures") == 0) {
        reset(); stroke(0u);
        char output[THY_DIAG_REPLY_MAX];
        const char *commands[4] = {"help", "pad 0", "edges 0", "io"};
        for (unsigned i = 0u; i < 4u; ++i) { read_only(commands[i], output); fputs(output, stdout); }
        return 0;
    }
    CHECK(argc == 1);
    all_pad_fields();
    malformed_and_bounds();
    journal_pagination_and_overwrite();
    journal_maximum_cursor_and_reply();
    status_and_all_io_fields();
    panic_real_ownership_and_block_journal();
    touch_capture_and_lossless_pages();
    touch_partial_and_strict_arguments();
    display_read_only_report();
    settings_read_only_report();
    resources_read_only_report();
    velocity_observation_is_effective_read_only_and_backwards_compatible();
    {
        ++scenarios;char output[THY_DIAG_REPLY_MAX];
        read_only("backup caps",output);CHECK(!strcmp(output,"BACKUP stub\r\n")&&backup_calls==1u);
        read_only("companion caps",output);CHECK(!strcmp(output,"COMPANION/1 stub\r\n")&&companion_calls==1u);
    }
    printf("instrument protocol: %u scenarios, %u assertions; pad/journal/IO, strict arguments, touch readback, display reports, read-only ownership and panic passed\n", scenarios, checks);
    return 0;
}
