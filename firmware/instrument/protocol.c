/* SPDX-License-Identifier: GPL-3.0-or-later
 * Bounded console. Observation commands do not write settings or device memory.
 * Explicit SONG/1 and BACKUP/1 validate staged snapshots. COMPANION/1 shares
 * existing navigation/settings actions; remote pad selection inspects only.
 * Normal idle journal maintenance persists edits. No updater/reset/raw I/O. */
#include "../diagnostic/protocol.h"
#include "app.h"
#include "board.h"
#include "pads.h"
#include "led.h"
#include "din.h"
#include "touch_config.h"
#include "display.h"

typedef struct {char *text; size_t used, capacity;} writer;
static void put(writer *w, const char *s) {
    while (*s && w->used + 1u < w->capacity) w->text[w->used++] = *s++;
}
static void number(writer *w, uint32_t n) {
    char digits[10]; unsigned used = 0;
    do {digits[used++] = (char)('0' + n % 10u); n /= 10u;} while (n);
    while (used) {char text[2] = {digits[--used], 0}; put(w, text);}
}
static void signed_number(writer *w, int32_t n) {
    if (n < 0) {put(w, "-"); number(w, (uint32_t)(-(n + 1)) + 1u);}
    else number(w, (uint32_t)n);
}
static void hex_number(writer *w, uint32_t n, unsigned digits) {
    static const char hex[] = "0123456789abcdef";
    while (digits) {char text[2] = {hex[(n >> (--digits * 4u)) & 15u], 0}; put(w, text);}
}
/* Strict bounded decimal arguments: no signs, spaces, suffixes or overflow. */
static int argument(const char *line, const char *prefix, uint32_t *value) {
    while (*prefix) if (*line++ != *prefix++) return 0;
    if (*line < '0' || *line > '9') return 0;
    uint32_t n = 0u;
    do {
        uint32_t digit = (uint32_t)(*line++ - '0');
        if (n > (UINT32_MAX - digit) / 10u) return 0;
        n = n * 10u + digit;
    } while (*line >= '0' && *line <= '9');
    if (*line) return 0;
    *value = n;
    return 1;
}
static int arguments2(const char *line, const char *prefix, uint32_t *first, uint32_t *second) {
    while (*prefix) if (*line++ != *prefix++) return 0;
    if (*line < '0' || *line > '9') return 0;
    uint32_t n = 0u;
    do {
        uint32_t digit = (uint32_t)(*line++ - '0');
        if (n > (UINT32_MAX - digit) / 10u) return 0;
        n = n * 10u + digit;
    } while (*line >= '0' && *line <= '9');
    if (*line++ != ' ' || !argument(line, "", second)) return 0;
    *first = n;
    return 1;
}
static void field(writer *w, const char *name, uint32_t value) {
    put(w, name); number(w, value);
}
static void signed_field(writer *w, const char *name, int32_t value) {
    put(w, name); signed_number(w, value);
}
static void pad_reply(writer *w, const thy_instrument *state, unsigned id) {
    const thy_instrument_sensor *s = &state->sensors[id];
    field(w, "PAD id=", id);
    field(w, " logical=", state->mapping[id]);
    field(w, " raw=", s->raw);
    field(w, " baseline=", s->calibration.baseline);
    field(w, " delta=", s->delta);
    signed_field(w, " signed_delta=", s->signed_delta);
    signed_field(w, " attack=", s->attack_raw);
    signed_field(w, " release=", s->release_raw);
    field(w, " profile=", s->detector_profile);
    field(w, " held=", s->held);
    field(w, " blocked=", s->blocked);
    field(w, " warmup=", s->warmup_count);
    field(w, " attack_count=", s->attack_count);
    field(w, " release_count=", s->release_count);
    field(w, " pressure=", s->pressure);
    field(w, " sample_ms=", s->last_sample_ms);
    field(w, " transition_ms=", s->last_transition_ms);
}
static int same_velocity(const thy_instrument_velocity *a,const thy_instrument_velocity *b) {
    return a->soft_rate==b->soft_rate && a->hard_rate==b->hard_rate && a->mode==b->mode &&
        a->curve==b->curve && a->minimum==b->minimum && a->maximum==b->maximum;
}
static void velocity_summary(writer *w,const thy_instrument *state) {
    int mixed=0;thy_instrument_velocity first,current;uint8_t overridden;
    thy_instrument_velocity_defaults(&first);
    if(state && thy_instrument_get_velocity(state,0,&first,&overridden)==THY_OK)
        for(unsigned n=1u;n<THY_PAD_COUNT;++n)
            if(thy_instrument_get_velocity(state,(int)n,&current,&overridden)!=THY_OK || !same_velocity(&current,&first))mixed=1;
    if(mixed)put(w,"mixed");
    else if(first.mode==THY_VELOCITY_STRIKE)put(w,"strike");
    else {put(w,"fixed");number(w,state?state->fixed_velocity:96u);}
}
static void velocity_reply(writer *w,const thy_instrument *state,unsigned id) {
    thy_instrument_velocity v;uint8_t overridden;
    if(thy_instrument_get_velocity(state,(int)id,&v,&overridden)!=THY_OK){put(w,"ERR velocity-config");return;}
    const thy_instrument_sensor *s=&state->sensors[id];
    field(w,"VELOCITY id=",id);field(w," logical=",state->mapping[id]);
    field(w," mode=",v.mode);field(w," override=",overridden);
    field(w," soft=",v.soft_rate);field(w," hard=",v.hard_rate);field(w," curve=",v.curve);
    field(w," minimum=",v.minimum);field(w," maximum=",v.maximum);
    field(w," last=",s->strike_velocity);field(w," peak=",s->strike_peak_rate);
    field(w," sample_ms=",s->last_sample_ms);
}
static void edge_values(writer *w, const thy_instrument_pad_transition *e) {
    number(w, e->sequence); field(w, ",", e->at_ms);
    field(w, ",", e->electrical); field(w, ",", e->logical);
    field(w, ",", e->action); field(w, ",", e->raw);
    field(w, ",", e->baseline); signed_field(w, ",", e->signed_delta);
    signed_field(w, ",", e->attack_raw); signed_field(w, ",", e->release_raw);
    field(w, ",", e->blocked); field(w, ",", e->detector_profile);
}
/* Non-destructive journal reads. Up to eight entries keep replies bounded;
 * overwritten history is explicit. A caller can resume using next=. */
static void edges_reply(writer *w, const thy_instrument *state, uint32_t cursor) {
    uint32_t newest = state->transition_sequence;
    uint32_t oldest = state->transition_count ? newest - state->transition_count + 1u : 0u;
    uint32_t first = 0u, count = 0u;
    if (cursor > newest) {put(w, "ERR edge-cursor"); return;}
    if (state->transition_count && cursor < newest) {
        first = cursor + 1u;
        if (first < oldest || cursor == 0u) first = oldest;
        count = newest - first + 1u;
        if (count > 8u) count = 8u;
    }
    uint32_t next = count ? first + count - 1u : cursor;
    field(w, "EDGES oldest=", oldest); field(w, " newest=", newest);
    field(w, " overwritten=", state->transition_overwritten);
    field(w, " count=", count); field(w, " next=", next);
    put(w, " data=");
    if (!count) put(w, "-");
    for (uint32_t i = 0u; i < count; ++i) {
        thy_instrument_pad_transition event;
        if (i) put(w, ";");
        if (thy_instrument_get_transition(state, first + i, &event)) edge_values(w, &event);
    }
}
static void io_reply(writer *w, const thy_instrument *state) {
    const thy_instrument_transport *t = thy_instrument_app_transport();
    const thy_instrument_din_stats *din = thy_instrument_din_report();
    const thy_instrument_lights_status *lights = thy_instrument_lights_report();
    unsigned held = 0u, blocked = 0u, warmed = 0u;
    if (state) for (unsigned i = 0u; i < THY_PAD_COUNT; ++i) {
        held += state->sensors[i].held != 0u;
        blocked += state->sensors[i].blocked != 0u;
        warmed += state->sensors[i].warmup_count == THY_INSTRUMENT_WARMUP_SAMPLES;
    }
    field(w, "IO din_ready=", din->ready);
    field(w, " din_errors=", din->errors);
    field(w, " din_queued=", din->queued);
    field(w, " din_tx=", din->transmitted_bytes);
    field(w, " din_accept=", din->accepted_messages);
    field(w, " usb_ready=", thy_instrument_transport_usb_ready(t));
    field(w, " usb_mounted=", t ? t->mounted : 0u);
    field(w, " usb_accepted=", t ? t->usb_accepted : 0u);
    field(w, " din_events=", t ? t->din_accepted : 0u);
    field(w, " usb_dropped=", t ? t->discarded_events + t->usb_suppressed : 0u);
    field(w, " transport_fault=", t ? t->invalid_pending : 0u);
    field(w, " core_queued=", state ? thy_core_queued(state->core) : 0u);
    field(w, " core_pending=", state ? thy_core_pending(state->core) : 0u);
    field(w, " din_pending=", t ? t->din_queue.count : 0u);
    field(w, " usb_pending=", t ? t->usb_queue.count : 0u);
    field(w, " held=", held); field(w, " blocked=", blocked); field(w, " warmed=", warmed);
    field(w, " led_errors=", board_led_report()->errors + lights->errors);
    field(w, " led_frames=", board_led_report()->frames);
    field(w, " led_fault=", lights->faulted);
}
static void touch_reply(writer *w) {
    const thy_touch_capture *capture = board_touch_config_report();
    const volatile diag_report_t *board = board_report();
    uint32_t pending = 0u, complete = 0u, failed = 0u;
    if (!capture || !board) {put(w, "ERR not-ready"); return;}
    for (unsigned i = 0u; i < THY_TOUCH_BANKS; ++i) {
        uint32_t bit = UINT32_C(1) << i, state = capture->bank[i].state;
        if (state == THY_TOUCH_CAPTURE_PENDING || state == THY_TOUCH_CAPTURE_READING) pending |= bit;
        if (state == THY_TOUCH_CAPTURE_COMPLETE) complete |= bit;
        if (state == THY_TOUCH_CAPTURE_FAILED) failed |= bit;
    }
    field(w, "TOUCH center_poll_ms=", board_touch_response_ms());
    field(w, " default_center_poll_ms=", THY_TOUCH_DEFAULT_CENTER_POLL_MS);
    field(w, " top_poll_ms=", THY_TOUCH_TOP_POLL_MS);
    field(w, " ready=", board->touch_initialized_mask);
    field(w, " failed=", board->touch_failed_mask);
    field(w, " capture_pending=", pending);
    field(w, " capture_complete=", complete);
    field(w, " capture_failed=", failed);
    put(w, " uid=");
    for (unsigned i = 0u; i < 3u; ++i) hex_number(w, capture->owner_uid[i], 8u);
    field(w, " config_bytes=", THY_TOUCH_CONFIG_BYTES); put(w, " writes=none");
}
static void touch_bank_reply(writer *w, unsigned index) {
    const thy_touch_capture *capture = board_touch_config_report();
    if (!capture) {put(w, "ERR not-ready"); return;}
    const thy_touch_snapshot *bank = &capture->bank[index];
    unsigned valid = 0u;
    for (unsigned i = 0; i < THY_TOUCH_CONFIG_BYTES; ++i)
        valid += (bank->valid[i / 8u] >> (i % 8u)) & 1u;
    field(w, "TOUCH_BANK bank=", index); field(w, " address=", bank->address);
    field(w, " state=", bank->state); signed_field(w, " result=", bank->result);
    field(w, " family=", bank->family_id); field(w, " device=", bank->device_id);
    field(w, " revision=", bank->device_rev); field(w, " attempts=", bank->attempts);
    field(w, " started_ms=", bank->started_ms); field(w, " completed_ms=", bank->completed_ms);
    field(w, " valid_bytes=", valid); field(w, " stored_crc_valid=", bank->stored_crc_valid);
    field(w, " stored_crc16=", bank->stored_crc16);
    put(w, " capture_crc32="); hex_number(w, bank->capture_crc32, 8u);
    field(w, " failed_register=", bank->failed_register);
}
static void touch_page_reply(writer *w, unsigned index, unsigned offset) {
    const thy_touch_capture *capture = board_touch_config_report();
    uint8_t bytes[THY_TOUCH_PAGE_BYTES]; uint16_t valid;
    if (!capture) {put(w, "ERR not-ready"); return;}
    if (thy_touch_capture_page(capture, index, offset, bytes, &valid) != 0) {put(w, "ERR touch-page"); return;}
    field(w, "TOUCH_PAGE bank=", index); field(w, " offset=", offset);
    field(w, " state=", capture->bank[index].state);
    put(w, " valid="); hex_number(w, valid, 4u);
    put(w, " data=");
    for (unsigned i = 0; i < THY_TOUCH_PAGE_BYTES; ++i) hex_number(w, bytes[i], 2u);
    put(w, " crc32="); hex_number(w, capture->bank[index].capture_crc32, 8u);
}
static void display_reply(writer *w) {
    const thy_display_report *report = board_display_report();
    if (!report) {put(w, "ERR not-ready"); return;}
    /* Version 2 measures coalesced dirty drains, not optical panel frames.
     * Keep the old base fields ordered, then append the scheduling counters. */
    field(w, "DISPLAY version=", 2u);
    field(w, " period_ms=", THY_DISPLAY_PERIOD_MS); field(w, " spi_hz=", THY_DISPLAY_SPI_HZ);
    field(w, " frames=", report->frames); field(w, " chunks=", report->chunks);
    field(w, " command_bytes=", report->command_bytes); field(w, " data_bytes=", report->data_bytes);
    field(w, " busy_deferrals=", report->busy_deferrals);
    field(w, " last_started_ms=", report->last_started_ms); field(w, " last_completed_ms=", report->last_completed_ms);
    field(w, " last_duration_ms=", report->last_duration_ms); field(w, " max_duration_ms=", report->max_duration_ms);
    field(w, " last_render_gap_ms=", report->last_render_gap_ms); field(w, " max_render_gap_ms=", report->max_render_gap_ms);
    field(w, " active=", report->active); field(w, " page=", report->page);
    field(w, " column=", report->column); field(w, " faulted=", report->faulted);
    field(w, " submitted=", report->submitted); field(w, " coalesced=", report->coalesced);
    field(w, " unchanged=", report->unchanged); field(w, " failed_chunks=", report->failed_chunks);
    field(w, " generation=", report->generation);
    field(w, " last_generation_age_ms=", report->last_generation_age_ms);
    field(w, " max_generation_age_ms=", report->max_generation_age_ms);
    field(w, " dirty_bytes=", report->dirty_bytes); field(w, " valid_bytes=", report->valid_bytes);
    field(w, " initial_complete=", report->initial_complete);
    field(w, " last_render_us=", report->last_render_us); field(w, " max_render_us=", report->max_render_us);
    field(w, " last_chunk_us=", report->last_chunk_us); field(w, " max_chunk_us=", report->max_chunk_us);
}
static int equal(const char *a, const char *b) {
    while (*a && *a == *b) {++a; ++b;}
    return *a == *b;
}
size_t thy_diag_command(const char *line, char *output, size_t capacity) {
    if (!line || !output || capacity < THY_DIAG_REPLY_MAX) return 0;
    if(line[0]=='s' && line[1]=='o' && line[2]=='n' && line[3]=='g' && line[4]==' ')
        return thy_instrument_app_song_command(line,output,capacity);
    if(line[0]=='b' && line[1]=='a' && line[2]=='c' && line[3]=='k' && line[4]=='u' && line[5]=='p' && line[6]==' ')
        return thy_instrument_app_backup_command(line,output,capacity);
    if(line[0]=='c' && line[1]=='o' && line[2]=='m' && line[3]=='p' && line[4]=='a' && line[5]=='n' && line[6]=='i' && line[7]=='o' && line[8]=='n' && line[9]==' ')
        return thy_instrument_app_companion_command(line,output,capacity);
    writer w = {output, 0, capacity};
    const thy_instrument *state = thy_instrument_app_state();
    uint32_t id = 0u, offset = 0u;
    if (equal(line, "help")) put(&w, "OK THYINSTRUMENT/1 commands=status,pads,pad,edges,io,panic,help,settings storage_writes=none autosave=idle");
    else if (equal(line,"help velocity")) put(&w,"OK VELOCITY/1 command=velocity_N id=0..95 config_writes=none mode=0_fixed,1_strike speed=ADC_per_ms");
    else if (equal(line, "help touch")) put(&w, "OK TOUCH/1 commands=touch;touch capture MASK;touch bank BANK;touch page BANK OFFSET;display mask=1..15 bank=0..3 offset=0,16,32,48,64,80,96,112 config_writes=none");
    else if (equal(line, "touch")) touch_reply(&w);
    else if (argument(line, "touch capture ", &id)) {
        if (!id || id > 15u) put(&w, "ERR touch-mask");
        else {
            int result = board_touch_config_begin(id);
            field(&w, "TOUCH_CAPTURE mask=", id); signed_field(&w, " result=", result);
            put(&w, " reads_only=1");
        }
    } else if (argument(line, "touch bank ", &id)) {
        if (id >= THY_TOUCH_BANKS) put(&w, "ERR touch-bank");
        else touch_bank_reply(&w, (unsigned)id);
    } else if (arguments2(line, "touch page ", &id, &offset)) {
        if (id >= THY_TOUCH_BANKS || offset >= THY_TOUCH_CONFIG_BYTES || offset % THY_TOUCH_PAGE_BYTES != 0u)
            put(&w, "ERR touch-page");
        else touch_page_reply(&w, (unsigned)id, (unsigned)offset);
    } else if (equal(line, "display")) display_reply(&w);
    else if (equal(line, "settings")) {
        const thy_settings_journal *s=thy_instrument_app_settings();
        static const char *names[]={"defaults","saved","pending","saving","error","unsupported"};
        put(&w,"SETTINGS version="); number(&w,THY_SETTINGS_VERSION); put(&w," status=");
        put(&w,s && s->status<=THY_SETTINGS_UNSUPPORTED?names[s->status]:"unavailable");
        field(&w," loaded=",s?s->loaded:0u);
        field(&w," loaded_version=",s?s->loaded_version:0u);
        field(&w," migration_pending=",s?s->migration_pending:0u);
        field(&w," recovered=",s?s->recovered:0u);
        field(&w," generation=",s?s->generation:0u); field(&w," saves=",s?s->saves:0u);
        field(&w," errors=",s?s->errors:0u); field(&w," rejected=",s?s->rejected:0u);
        put(&w," backend=internal-sectors10-11 updater_preservation=unverified writes=none");
    }
    else if (equal(line, "panic")) {thy_instrument_app_panic(); put(&w, "OK panic");}
    else if (equal(line, "status")) {
        put(&w, "STATUS THYINSTRUMENT/1 build="); put(&w, thy_diag_build_id);
        put(&w, " ready="); number(&w, thy_instrument_app_ready());
        put(&w, " uptime_ms="); number(&w, board_millis());
        put(&w, " errors="); number(&w, board_report()->errors | (state ? state->errors : 0u));
        put(&w, " presses="); number(&w, state ? state->presses : 0);
        put(&w, " releases="); number(&w, state ? state->releases : 0);
        put(&w, " voices="); number(&w, state ? thy_core_sounding(state->core) : 0);
        put(&w, " layout="); put(&w, thy_instrument_app_scale_name());
    } else if (equal(line, "pads")) {
        const diag_pads_report_t *p = board_pads_report();
        put(&w, "PADS frames="); number(&w, p->frames);
        put(&w, " scans="); number(&w, p->scans);
        put(&w, " errors="); number(&w, p->errors);
        put(&w, " profile=150us-21MHz velocity=");velocity_summary(&w,state);
        put(&w, " calibration=unverified");
    } else if(argument(line,"velocity ",&id)) {
        if(!state || id>=THY_PAD_COUNT)put(&w,"ERR pad-id");
        else velocity_reply(&w,state,(unsigned)id);
    } else if (argument(line, "pad ", &id)) {
        if (!state || id >= THY_PAD_COUNT) put(&w, "ERR pad-id");
        else pad_reply(&w, state, (unsigned)id);
    } else if (argument(line, "edges ", &id)) {
        if (!state) put(&w, "ERR not-ready");
        else edges_reply(&w, state, id);
    } else if (equal(line, "io")) {
        io_reply(&w, state);
    } else put(&w, "ERR unknown-command");
    put(&w, "\r\n"); output[w.used] = 0;
    return w.used;
}
