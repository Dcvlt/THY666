/* SPDX-License-Identifier: GPL-3.0-or-later
 * Integrated electrical pad/control -> music -> USB runtime. Single-thread
 * main-loop ownership. Acquisition and expression defaults require device
 * characterization; the original diagnostic and its artifacts stay intact. */
#include "app.h"
#include "data.h"
#include "transport.h"
#include "board.h"
#include "controls.h"
#include "pads.h"
#include "led.h"
#include "din.h"
#include "center.h"
#include "usb.h"
#include "tusb.h"
#include "touch_config.h"
#include "display.h"
#include "settings.h"
#include "control_lights.h"
#include "thy_calibration.h"

#ifndef THY_INSTRUMENT_CPU_ASM
#define THY_INSTRUMENT_CPU_ASM(...) __asm volatile(__VA_ARGS__)
#endif

static thy_core core;
static thy_instrument instrument;
#if defined(__arm__) || defined(__thumb__)
/* CPU-only scratch; bind_calibration fully initializes this NOLOAD object. */
/* The compact owner ledger leaves room for this CPU-only wizard in SRAM.
 * Balance the banks without sharing live storage or reducing any capacity. */
static thy_calibration calibration __attribute__((aligned(8)));
#else
static thy_calibration calibration;
#endif
static thy_mpe expression;
static thy_channel_expression channel_expression;
#if defined(__arm__) || defined(__thumb__)
/* NOLOAD is intentional: thy_engine_init completely initializes this CPU-only
 * object before initialized becomes true. No driver or DMA buffer lives here. */
static thy_engine high_level __attribute__((section(".ccm_noinit"), aligned(8)));
#else
static thy_engine high_level;
#endif
static thy_instrument_pad layout[96];
static thy_instrument_transport transport;
static thy_settings_journal settings;
static thy_song_transfer song_transfer;
static thy_settings_transfer backup_transfer;
static thy_companion companion;
static char service_uid[25];
static uint32_t last_scan, last_layout_revision;
static uint32_t last_settings_check;
static uint32_t last_song_revision;
static unsigned scale_index = 256u, group_index = 21u;
static bool initialized, acquisition_failed;
static int musical_idle(void);
static int restore_settings(void *,const uint8_t *,size_t);

const thy_instrument *thy_instrument_app_state(void) { return initialized ? &instrument : NULL; }
const thy_state *thy_instrument_app_menu_state(void) { return initialized ? &high_level.state : NULL; }
const thy_instrument_transport *thy_instrument_app_transport(void) { return initialized ? &transport : NULL; }
const thy_settings_journal *thy_instrument_app_settings(void) { return &settings; }
size_t thy_instrument_app_song_command(const char *line,char *output,size_t capacity)
{
    if(!initialized) return 0u;
    uint8_t before=song_transfer.state;
    size_t bytes=thy_song_transfer_command(&song_transfer,&high_level,
        !settings.writing&&!companion.active&&!thy_settings_transfer_busy(&backup_transfer),
        board_millis(),line,output,capacity);
    if(before && !song_transfer.state) settings.observed=0u;
    return bytes;
}
size_t thy_instrument_app_backup_command(const char *line,char *output,size_t capacity)
{
    if(!initialized)return 0u;
    return thy_settings_transfer_command(&backup_transfer,&high_level,&settings,
        !settings.writing&&!song_transfer.state&&!companion.active,
        musical_idle(),board_millis(),line,output,capacity);
}
size_t thy_instrument_app_companion_command(const char *line,char *output,size_t capacity)
{
    if(!initialized)return 0u;
    return thy_companion_command(&companion,&high_level,
        !settings.writing&&!song_transfer.state&&!thy_settings_transfer_busy(&backup_transfer),
        board_millis(),line,output,capacity);
}
void thy_instrument_app_console_reset(void)
{
    if(!initialized)return;
    /* The USB poll calls reset repeatedly while DTR is low. Do not restart
     * the autosave debounce in an ordinary disconnected/standalone session. */
    if(!song_transfer.state&&!companion.active&&!thy_settings_transfer_busy(&backup_transfer))return;
    song_transfer.state=THY_SONG_IDLE;
    companion.active=0u;
    thy_settings_transfer_disconnect(&backup_transfer);
    settings.observed=0u;
}
const char *thy_instrument_app_scale_name(void) {
    return instrument.user_scale && instrument.user_scale<=8u?
        (const char *)instrument.library.scales[instrument.user_scale-1u].name:thy_instrument_data_name(scale_index);
}
bool thy_instrument_app_ready(void) {
    if (!initialized || acquisition_failed || !thy_instrument_din_report()->ready ||
        !thy_instrument_transport_ready(&transport)) return false;
    /* Each sensor qualifies its own stable quiet startup. A boot-held or
     * noisy pad must not prevent the other 95 inputs from being played. */
    return true;
}
void thy_instrument_app_panic(void) { if (initialized) thy_engine_panic(&high_level); }

void thy_instrument_app_midi(const thy_midi_event *event, uint32_t now_ms)
{
    if (!initialized || !event || event->destination != THY_USB0) return;
    uint8_t bytes[3] = {event->status, event->data1, event->data2};
    uint8_t length=event->status>=0xf8u?1u:
        (event->status&0xf0u)==0xc0u || (event->status&0xf0u)==0xd0u?2u:3u;
    int result = thy_engine_receive_midi(&high_level, bytes,length, now_ms);
    if (result != THY_OK) { ++instrument.errors; instrument.last_error = result; }
}

void thy_instrument_app_usb_reset(uint32_t now_ms)
{
    if (!initialized) return;
    thy_instrument_app_console_reset();
    int result = thy_engine_usb_reset(&high_level, now_ms);
    if (result != THY_OK) { ++instrument.errors; instrument.last_error = result; }
}

static bool write_usb(void *context, const uint8_t packet[4]) {
    (void)context;
    return tud_midi_packet_write(packet);
}
static bool write_din(void *context, const uint8_t *bytes, size_t count) {
    (void)context;
    return thy_instrument_din_write(bytes, count);
}

static void action(void *context, const thy_instrument_control_event *event)
{
    (void)context;
    if (!event->pressed || event->action == THY_ACTION_NONE) return;
    board_display_boot_skip();
    /* The shared engine resolves configurable scale shortcuts. The device
     * action hook only skips the boot presentation; no scale policy lives here. */
}

static int refresh_layout(void)
{
    int16_t degrees[12], anchor;
    uint8_t count;
    group_index = high_level.scale;
    int selected = thy_instrument_data_find(group_index, instrument.performance.root);
    /* Melody-layout cycling, remapped shortcuts and reselecting the same
     * root can request a refresh without changing the chord catalog. Keep
     * selected-chord note/layer edits in that case, just as the web preview
     * does. First startup must still load, even when scale_index matches. */
    bool same_catalog = selected >= 0 && instrument.layout != NULL &&
                        (unsigned)selected == scale_index;
    if (selected >= 0 && (same_catalog ||
        (thy_instrument_data_load((unsigned)selected, layout, &anchor, degrees, &count) &&
         thy_instrument_set_layout(&instrument, layout, anchor, degrees, count) == THY_OK)))
    {
        scale_index = (unsigned)selected;
        (void)thy_engine_catalog(&high_level, (uint16_t)group_index, 70u);
    }
    else { ++instrument.errors; acquisition_failed = true; return 0; }
    last_layout_revision = instrument.layout_revision;
    return 1;
}

static int calibration_colors(void *context,uint32_t now,uint32_t colors[96])
{return thy_engine_calibration_lights(context,now,colors);}

static int musical_idle(void)
{
    if (!initialized || acquisition_failed || high_level.state.editing ||
        high_level.calibration_open || instrument.library_editor.open ||
        (high_level.sequencer.capture.state!=THY_SEQ_CAPTURE_OFF && high_level.sequencer.capture.state!=THY_SEQ_FAILED) ||
        core.sounding_count || core.pending_keys || core.queue_count || core.sustain_count ||
        core.generated_count || high_level.rhythm_transport.state != THY_TRANSPORT_STOPPED ||
        high_level.arps[0].held_count || high_level.arps[1].held_count ||
        high_level.arps[0].output_count || high_level.arps[1].output_count ||
        transport.pending_valid || transport.din_queue.count || transport.usb_queue.count ||
        thy_instrument_din_report()->queued) return 0;
    for (unsigned bank=0;bank<4u;++bank) if (instrument.control_masks[bank]) return 0;
    for (unsigned n=0;n<THY_PAD_COUNT;++n) {
        const thy_instrument_sensor *s=&instrument.sensors[n];
        if (s->held || s->blocked || s->attack_count || core.owners[n].held) return 0;
    }
    return 1;
}
static int settings_idle(void)
{
    return !song_transfer.state&&!companion.active&&
        !thy_settings_transfer_staging(&backup_transfer)&&musical_idle();
}
static int restore_settings(void *context,const uint8_t *payload,size_t count)
{
    (void)context;
    if(!musical_idle())return 0;
    uint16_t target_scale;uint8_t target_root;
    if(!thy_settings_catalog(payload,count,&target_scale,&target_root)||
        target_scale>=70u||target_root>=12u||thy_instrument_data_find(target_scale,target_root)<0)return 0;
    uint8_t was_enabled=instrument.input_enabled;
    /* Quiet preflight must not invoke panic or mutate detector state when a
     * candidate is rejected. This foreground-only gate restores the bit. */
    instrument.input_enabled=0u;
    if(!thy_settings_can_apply(&high_level,payload,count)){
        instrument.input_enabled=was_enabled;return 0;
    }
    /* All rejection happens before mutation. The linked catalog's records are
     * build-validated. Apply its saved library first, then refresh the catalog,
     * and reapply masks that set_layout deliberately clears. These operations
     * run in one foreground turn with input disabled and no musical owners. */
    if(!thy_settings_apply(&high_level,payload,count)){
        instrument.input_enabled=was_enabled;return 0;
    }
    if(!refresh_layout()||!thy_settings_apply(&high_level,payload,count)){
        instrument.input_enabled=was_enabled;return 0;
    }
    (void)board_touch_response_set(instrument.center_poll_ms);
    last_layout_revision=instrument.layout_revision;
    last_song_revision=high_level.sequencer.revision;
    /* Fresh sensor baselines are intentional; no saved contact is replayed. */
    instrument.input_enabled=was_enabled;
    high_level.song_save_requested=0u;high_level.song_status=1u;
    return 1;
}
static void init_services(void)
{
    static const char hex[]="0123456789abcdef";
    for(unsigned word=0;word<3u;++word)
        for(unsigned digit=0;digit<8u;++digit)
            service_uid[word*8u+digit]=hex[(g_diag_mailbox.uid[word]>>(28u-digit*4u))&15u];
    service_uid[24]=0;
    thy_backup_identity backup_id={service_uid,thy_diag_build_id,THY_MENU_SCHEMA_SHA256,
        UINT16_C(0x413),THY_BACKUP_COMPATIBILITY};
    thy_settings_transfer_init(&backup_transfer,settings.record+THY_SETTINGS_HEADER_BYTES,
        THY_SETTINGS_PAYLOAD_MAX,&backup_id,restore_settings,NULL);
    thy_companion_identity companion_id={thy_diag_build_id,service_uid,&settings.generation};
    thy_companion_init(&companion,settings.record,sizeof settings.record,&companion_id,
        board_companion_visual,NULL);
}

int main(void)
{
    board_init();
    board_led_init(); thy_instrument_lights_init();
    board_controls_init(); board_pads_init(); board_center_init();
    thy_instrument_control_lights_init();
    if (g_diag_mailbox.errors & DIAG_ERROR_ENTRY_STATE) {
        board_boot_stage(0xbadu);
        for (;;) THY_INSTRUMENT_CPU_ASM("nop");
    }
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    thy_instrument_memory_init();
    /* CPU-only undo storage in ordinary SRAM, separate from the CCM engine.
     * No capacity sharing: history remains available during recording/playback. */
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&high_level, &instrument, (uint16_t)group_index, 70u, undo_workspace_1);
    thy_engine_bind_calibration(&high_level,&calibration);
    thy_engine_bind_expression(&high_level,&expression);
    thy_engine_bind_channel_expression(&high_level,&channel_expression);
    thy_instrument_lights_set_override(calibration_colors,&high_level);
    thy_song_transfer_init(&song_transfer,settings.record,sizeof settings.record);
    init_services();
    (void)thy_state_set_design_mode(&high_level.state, 0u);
    (void)board_touch_response_set(instrument.center_poll_ms);
    thy_engine_bind_controls(&high_level);
    (void)thy_instrument_set_detector_profile(&instrument, THY_DETECTOR_FACTORY_LINEAR);
    /* DIN is the primary instrument output. USB is a separate optional sink;
     * its mount/reconnect policy cannot reset live DIN ownership. */
    (void)thy_instrument_set_route(&instrument, 0u, 0u, THY_ROUTE_DIN | THY_ROUTE_USB0, 0);
    (void)thy_instrument_set_route(&instrument, 1u, 1u, THY_ROUTE_DIN | THY_ROUTE_USB0, 0);
    /* Start with all 48 melody inputs assigned. Stored remains selectable
     * and preserves the archive's 16 explicitly unassigned melody cells. */
    instrument.performance.melody_layout = 1u;
    thy_instrument_set_action_handler(&instrument, action, NULL);
    thy_instrument_set_input_enabled(&instrument, 0);
    thy_settings_io settings_io;
    if (thy_instrument_settings_io(&settings_io)) {
        if (thy_settings_journal_init(&settings,&settings_io,70u) && settings.loaded) {
            uint16_t saved_scale; uint8_t saved_root;
            if (thy_settings_catalog(settings.record+THY_SETTINGS_HEADER_BYTES,
                settings.payload_bytes,&saved_scale,&saved_root)) {
                high_level.scale=saved_scale; instrument.performance.root=saved_root;
            }
        }
    } else {settings.status=THY_SETTINGS_ERROR; settings.errors=1u;}
    if (!thy_instrument_din_init(g_diag_mailbox.pclk2_hz)) acquisition_failed = true;
    thy_instrument_transport_io io = {NULL, write_usb, write_din};
    thy_instrument_transport_init(&transport, &core, &io);
    refresh_layout();
    /* Applying after catalog restoration preserves the optional per-pad slot
     * masks: set_layout deliberately clears these when changing a scale. */
    if (settings.loaded && !thy_settings_apply(&high_level,
        settings.record+THY_SETTINGS_HEADER_BYTES,settings.payload_bytes)) {
        settings.status=THY_SETTINGS_ERROR; ++settings.errors;
    }
    (void)board_touch_response_set(instrument.center_poll_ms);
    initialized = true;
    thy_instrument_control_lights_task(&high_level, board_millis());
    THY_INSTRUMENT_CPU_ASM("cpsie i" ::: "memory");
    board_boot_stage(6u);
    if (!thy_diag_usb_init()) g_diag_mailbox.errors |= DIAG_ERROR_USB_INIT;
    board_boot_stage(5u);
    if (board_pads_request(1) != DIAG_SUCCESS) acquisition_failed = true;
    for (;;) {
        thy_diag_usb_task();
        thy_instrument_din_task(board_millis());
        const thy_diag_usb_stats *usb = thy_diag_usb_status();
        thy_instrument_transport_poll(&transport, usb->mounted, thy_instrument_usb_epoch(), 16u);
        thy_channel_expression_usb_blocked(&channel_expression,transport.usb_blocked,sizeof transport.usb_blocked);
        uint32_t now = board_millis();
        if (board_touch_response_ms() != instrument.center_poll_ms)
            (void)board_touch_response_set(instrument.center_poll_ms);
        thy_instrument_pads_renew(now);
        board_poll();
        board_controls_task(board_millis());
        const diag_pads_report_t *pads = board_pads_report();
        if (pads->state == DIAG_PADS_FAULT || pads->errors != 0u) {
            if (!acquisition_failed) thy_instrument_panic(&instrument);
            acquisition_failed = true;
        }
        bool enabled = thy_instrument_app_ready();
        thy_instrument_set_input_enabled(&instrument, enabled);
        /* Captions describe observed milestones, not elapsed-time guesses.
         * Presentation follows readiness; the already-enabled music path
         * never waits for it, for a USB host, or for all 96 pads to warm up. */
        if ((g_diag_mailbox.touch_initialized_mask & 15u) == 15u) {
            board_display_boot_progress(THY_STUDIO_BOOT_SENSORS);
            if (pads->frames) {
                board_display_boot_progress(THY_STUDIO_BOOT_PADS);
                if (enabled) {
                    board_display_boot_progress(THY_STUDIO_BOOT_MIDI);
                    board_display_boot_ready();
                }
            }
        }
        if (acquisition_failed || g_diag_mailbox.touch_failed_mask)
            board_display_boot_skip();
        uint32_t presses_before = instrument.presses;
        if (pads->scans && pads->scans != last_scan) {
            last_scan = pads->scans;
            unsigned selection = (pads->index + 15u) & 15u;
            for (unsigned bank = 0; bank < 6; ++bank) {
                unsigned slot = bank * 16u + selection;
                if (pads->valid[slot / 32u] & (UINT32_C(1) << (slot & 31u)))
                    (void)thy_instrument_sample(&instrument, (uint8_t)slot,
                        (uint16_t)pads->raw[slot], board_millis());
            }
        }
        if (instrument.presses != presses_before) board_display_boot_skip();
        const diag_controls_report_t *controls = board_controls_report();
        for (unsigned bank = 0; bank < 4; ++bank) if (controls->banks[bank].valid)
            (void)thy_instrument_controls(&instrument, (uint8_t)bank, controls->banks[bank].logical_pressed);
        if (last_layout_revision != instrument.layout_revision) refresh_layout();
        int music_result = thy_engine_advance(&high_level, board_millis());
        if (music_result != THY_OK) {
            ++instrument.errors; instrument.last_error = music_result;
            thy_engine_panic(&high_level);
        }
        if(last_song_revision!=high_level.sequencer.revision) {
            last_song_revision=high_level.sequencer.revision;
            high_level.song_status=1u;
        }
        /* Deliver the current generated-note changes before presentation.
         * Display service never waits for a full frame and has no music clock. */
        thy_instrument_transport_poll(&transport, usb->mounted, thy_instrument_usb_epoch(), 16u);
        thy_channel_expression_usb_blocked(&channel_expression,transport.usb_blocked,sizeof transport.usb_blocked);
        thy_instrument_din_task(board_millis());
        board_display_task();
        thy_instrument_lights_task(&instrument, board_millis());
        thy_instrument_control_lights_task(&high_level, board_millis());
        /* Programming is sliced into one verified word per idle foreground
         * turn; hashing settings is limited to 10Hz when not already saving.
         * Sector rotation can pause this single-bank MCU while fully idle. */
        now=board_millis();
        if(thy_song_transfer_expire(&song_transfer,now)) {
            settings.observed=0u; high_level.song_status=3u;
        }
        if(thy_settings_transfer_expire(&backup_transfer,now))settings.observed=0u;
        if(thy_companion_expire(&companion,now))settings.observed=0u;
        if(high_level.song_recall_requested && !settings.writing && !song_transfer.state&&
            !companion.active&&!thy_settings_transfer_busy(&backup_transfer)) {
            high_level.song_recall_requested=0u;
            high_level.song_status=thy_settings_journal_recall_song(&settings,&high_level)?2u:3u;
        }
        if (!song_transfer.state&&!companion.active&&!thy_settings_transfer_staging(&backup_transfer)&&
            (settings.writing || (uint32_t)(now-last_settings_check)>=100u)) {
            last_settings_check=now;
            int idle=settings_idle();
            /* A full-song CRC/semantic pass is background work too. Keep it
             * out of active music, rather than merely blocking flash writes. */
            if(settings.writing || idle) {
                thy_settings_journal_task(&settings,&high_level,now,idle);
                if(high_level.song_save_requested && settings.status==THY_SETTINGS_PENDING) {
                    settings.changed_ms=now-1500u;
                    high_level.song_save_requested=0u;
                }
                if(settings.status==THY_SETTINGS_SAVED &&
                    (high_level.song_status==1u || high_level.song_save_requested)) {
                    high_level.song_save_requested=0u; high_level.song_status=2u;
                } else if(settings.status==THY_SETTINGS_ERROR || settings.status==THY_SETTINGS_UNSUPPORTED)
                    high_level.song_status=3u;
            }
        }
        thy_settings_transfer_poll(&backup_transfer,&high_level,&settings,
            settings_idle(),now);
    }
}
