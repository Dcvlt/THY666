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
#include "profile.h"

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
static uint32_t profile_frames;
static unsigned scale_index = 256u, group_index = 21u;
static bool initialized, acquisition_failed;
static uint32_t restore_suppressed_controls[4];
static uint32_t restore_control_samples[4];
static thy_restore_progress restore_progress;
static uint32_t restore_terminal_ms;
static uint8_t restore_guard_seen, restore_terminal_phase, restore_controls_wait;
static int musical_idle(void);
static int restore_settings(void *,const uint8_t *,size_t);
static int hardware_ready(void)
{
    const diag_pads_report_t *pads=board_pads_report();
    return initialized&&!acquisition_failed&&thy_instrument_din_report()->ready&&
        thy_instrument_transport_ready(&transport)&&board_pads_active()&&
        pads->state!=DIAG_PADS_FAULT&&!pads->errors;
}

static void restore_input_gate(void)
{
    if(!initialized)return;
    if(backup_transfer.restore_active){
        if(!restore_guard_seen)restore_terminal_phase=0u;
        restore_guard_seen=1u;
        /* Admission already proved musical ownership quiet. This explicit
         * maintenance gate need not panic or alter the stored configuration. */
        instrument.input_enabled=0u;
    }else if(restore_guard_seen){
        restore_guard_seen=0u;
        const diag_controls_report_t *controls=board_controls_report();
        restore_controls_wait=15u;
        for(unsigned bank=0u;bank<4u;++bank){
            restore_control_samples[bank]=controls->banks[bank].samples;
            restore_suppressed_controls[bank]=UINT32_MAX;
        }
        /* Detector dispatch was suspended while raw ADC scanning continued.
         * Requalify quiet contact state instead of playing a pad held across
         * maintenance. Keep all saved calibration and response preferences. */
        for(unsigned n=0;n<THY_PAD_COUNT;++n){
            thy_instrument_sensor *sensor=&instrument.sensors[n];
            sensor->warmup_count=0u;sensor->warmup_sum=0u;
            sensor->attack_count=sensor->release_count=0u;
            sensor->strike_ready=0u;sensor->strike_peak_rate=0u;
        }
    }
}

const thy_restore_progress *thy_instrument_app_restore_progress(void)
{
    if(!initialized)return NULL;
    thy_settings_transfer_progress(&backup_transfer,&settings,&restore_progress);
    restore_progress.pads_locked=(uint8_t)(backup_transfer.restore_active||acquisition_failed);
    if(!backup_transfer.restore_active &&
        (restore_progress.phase==THY_RESTORE_SUCCESS||restore_progress.phase==THY_RESTORE_ERROR)){
        if(restore_terminal_phase!=restore_progress.phase){restore_terminal_ms=board_millis();restore_terminal_phase=restore_progress.phase;}
        restore_progress.active=(uint8_t)((uint32_t)(board_millis()-restore_terminal_ms)<2500u);
    }else restore_terminal_phase=0u;
    return &restore_progress;
}

static int backup_commit(const char *line)
{
    const char *expected="backup commit";
    if(!line)return 0;
    while(*expected && *line==*expected){++line;++expected;}
    return !*expected && !*line;
}

static int pause_restore_acquisition(void)
{
    const diag_pads_report_t *pads=board_pads_report();
    if(acquisition_failed || pads->errors || pads->state==DIAG_PADS_FAULT ||
        !board_pads_active())return 0;
    if(board_pads_request(0)!=DIAG_SUCCESS)return 0;
    uint32_t started=board_millis();
    /* A quiet restore already excludes notes and output work. Complete only
     * the driver's ADC/stream0 cleanup here; never recurse into USB, music,
     * display or journal work. The normal raw-ADC restore needs one ms.
     * Both guards stay finite if either hardware or SysTick is unavailable. */
    for(unsigned polls=0u;polls<100000u;++polls){
        board_pads_task(board_millis());
        if(pads->errors || pads->state==DIAG_PADS_FAULT)break;
        if(!board_pads_active())return 1;
        if((uint32_t)(board_millis()-started)>=5u)break;
    }
    acquisition_failed=true;
    return 0;
}

const thy_instrument *thy_instrument_app_state(void) { return initialized ? &instrument : NULL; }
const thy_state *thy_instrument_app_menu_state(void) { return initialized ? &high_level.state : NULL; }
const thy_instrument_transport *thy_instrument_app_transport(void) { return initialized ? &transport : NULL; }
const thy_settings_journal *thy_instrument_app_settings(void) { return &settings; }
size_t thy_instrument_app_song_command(const char *line,char *output,size_t capacity)
{
    if(!initialized) return 0u;
    uint8_t before=song_transfer.state;
    size_t bytes=thy_song_transfer_command(&song_transfer,&high_level,
        !settings.writing&&!companion.active&&!backup_transfer.restore_active&&!thy_settings_transfer_busy(&backup_transfer),
        board_millis(),line,output,capacity);
    if(before && !song_transfer.state) settings.observed=0u;
    return bytes;
}
size_t thy_instrument_app_backup_command(const char *line,char *output,size_t capacity)
{
    if(!initialized || !line || !output || capacity<256u)return 0u;
    int available=!settings.writing&&!song_transfer.state&&!companion.active;
    int quiet=musical_idle(),paused=0;
    const char *completion=line,*head="backup complete ";
    while(*head&&*completion==*head){++completion;++head;}
    if(!*head && (!hardware_ready()||!board_pads_report()->frames))quiet=0;
    if(backup_commit(line) && backup_transfer.state==THY_BACKUP_UPLOAD &&
        backup_transfer.received==backup_transfer.bytes && available && quiet){
        /* File CRC/validation also runs before the apply callback. Stop ADC
         * across the entire commit, including rejected complete candidates,
         * so a long CPU-only validation cannot age an in-flight conversion.
         * Detector baselines and settings remain untouched on rejection. */
        paused=pause_restore_acquisition();
        if(!paused)quiet=0;
    }
    size_t bytes=thy_settings_transfer_command(&backup_transfer,&high_level,&settings,
        available,quiet,board_millis(),line,output,capacity);
    if(paused){
        /* The driver owns no old DMA at this point. Never clear a real fault
         * or replay contacts; accepted settings deliberately warm up again. */
        if(board_pads_request(1)!=DIAG_SUCCESS)acquisition_failed=true;
        /* The restarted driver's frame counter is reset, not a completed
         * frame. Preserve the profiler's prior real-frame timestamp so its
         * next interval includes the acquisition pause. */
        last_scan=profile_frames=0u;
    }
    restore_input_gate();
    return bytes;
}
size_t thy_instrument_app_companion_command(const char *line,char *output,size_t capacity)
{
    if(!initialized)return 0u;
    return thy_companion_command(&companion,&high_level,
        !settings.writing&&!song_transfer.state&&!backup_transfer.restore_active&&!thy_settings_transfer_busy(&backup_transfer),
        board_millis(),line,output,capacity);
}
size_t thy_instrument_app_profile_command(const char *line,char *output,size_t capacity)
{
    thy_instrument_profile_context snapshot={instrument.engine_kind,instrument.display_view,
        high_level.musical_clock.tempo_milli_bpm,settings.saved_crc};
    /* Starting is a diagnostic action, permitted during music but not across
     * another maintenance transaction. It never changes musical settings. */
    int available=hardware_ready()&&!settings.writing&&!song_transfer.state&&
        !companion.active&&!backup_transfer.restore_active&&
        !thy_settings_transfer_busy(&backup_transfer);
    return thy_instrument_profile_command(line,output,capacity,available,&snapshot);
}
void thy_instrument_app_console_reset(void)
{
    if(!initialized)return;
    /* The USB poll calls reset repeatedly while DTR is low. Do not restart
     * the autosave debounce in an ordinary disconnected/standalone session. */
    if(!song_transfer.state&&!companion.active&&!thy_settings_transfer_staging(&backup_transfer)&&!backup_transfer.restore_active)return;
    int had_staging=song_transfer.state||companion.active||thy_settings_transfer_staging(&backup_transfer);
    song_transfer.state=THY_SONG_IDLE;
    companion.active=0u;
    thy_settings_transfer_disconnect(&backup_transfer);
    /* Applied restoration owns ordinary durable saving even without a host.
     * Repeated DTR-low callbacks must never restart its save debounce. */
    if(had_staging&&!backup_transfer.restore_applied)settings.observed=0u;
    restore_input_gate();
}
const char *thy_instrument_app_scale_name(void) {
    return instrument.user_scale && instrument.user_scale<=8u?
        (const char *)instrument.library.scales[instrument.user_scale-1u].name:thy_instrument_data_name(scale_index);
}
bool thy_instrument_app_ready(void) {
    if (!hardware_ready() || backup_transfer.restore_active) return false;
    /* Each sensor qualifies its own stable quiet startup. A boot-held or
     * noisy pad must not prevent the other 95 inputs from being played. */
    return true;
}
void thy_instrument_app_panic(void) { if (initialized) thy_engine_panic(&high_level); }

void thy_instrument_app_midi(const thy_midi_event *event, uint32_t now_ms)
{
    if (!initialized || backup_transfer.restore_active || !event || event->destination != THY_USB0) return;
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
    if(backup_transfer.restore_active)return;
    int result = thy_engine_usb_reset(&high_level, now_ms);
    if (result != THY_OK) { ++instrument.errors; instrument.last_error = result; }
}

static bool write_usb(void *context, const uint8_t packet[4]) {
    (void)context;
    return tud_midi_packet_write(packet);
}
static bool write_din(void *context, const uint8_t *bytes, size_t count) {
    (void)context;
    bool written=thy_instrument_din_write(bytes, count);
    thy_profile *profile=thy_instrument_profile_state();
    if(written&&thy_profile_active(profile))
        thy_profile_peak(profile,THY_PROFILE_PEAK_DIN_BYTES,thy_instrument_din_report()->queued);
    return written;
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
    restore_guard_seen=restore_terminal_phase=restore_controls_wait=0u;
    for(unsigned bank=0u;bank<4u;++bank)restore_suppressed_controls[bank]=restore_control_samples[bank]=0u;
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
    thy_instrument_profile_init(g_diag_mailbox.hclk_hz);profile_frames=0u;
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
    settings.observer=thy_instrument_profile_journal_observer();
    if (!thy_instrument_din_init(g_diag_mailbox.pclk2_hz)) acquisition_failed = true;
    thy_instrument_transport_io io = {NULL, write_usb, write_din};
    thy_instrument_transport_init(&transport, &core, &io);
    thy_instrument_transport_profile_bind(&transport,thy_instrument_profile_state(),
        thy_instrument_profile_now,NULL);
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
        if(thy_profile_active(thy_instrument_profile_state()))
            thy_instrument_profile_service(board_millis());
        thy_instrument_profile_gap(THY_PROFILE_LOOP_US);
        board_resources_task(board_millis());
        thy_instrument_profile_mark profile_mark=thy_instrument_profile_begin();
        thy_diag_usb_task();
        thy_instrument_profile_end(THY_PROFILE_USB_US,profile_mark);
        profile_mark=thy_instrument_profile_begin();
        thy_instrument_din_task(board_millis());
        const thy_diag_usb_stats *usb = thy_diag_usb_status();
        thy_instrument_transport_poll(&transport, usb->mounted, thy_instrument_usb_epoch(), 16u);
        /* Empty-map synchronization only ORs zero into exclusions. The
         * transport hint proves it is empty without another owner scan. */
        if(transport.usb_blocks_pending)
            thy_channel_expression_usb_blocked(&channel_expression,transport.usb_blocked,sizeof transport.usb_blocked);
        thy_instrument_profile_end(THY_PROFILE_TRANSPORT_US,profile_mark);
        thy_instrument_profile_gap(THY_PROFILE_INPUT_GAP_US);
        profile_mark=thy_instrument_profile_begin();
        uint32_t now = board_millis();
        if (board_touch_response_ms() != instrument.center_poll_ms)
            (void)board_touch_response_set(instrument.center_poll_ms);
        thy_instrument_pads_renew(now);
        board_poll();
        board_controls_task(board_millis());
        const diag_pads_report_t *pads = board_pads_report();
        if(profile_frames!=pads->frames){
            profile_frames=pads->frames;
            thy_instrument_profile_gap(THY_PROFILE_ADC_FRAME_GAP_US);
        }
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
            for (unsigned bank = 0; !backup_transfer.restore_active && bank < 6; ++bank) {
                unsigned slot = bank * 16u + selection;
                if (pads->valid[slot / 32u] & (UINT32_C(1) << (slot & 31u)))
                    (void)thy_instrument_sample(&instrument, (uint8_t)slot,
                        (uint16_t)pads->raw[slot], board_millis());
            }
        }
        if (instrument.presses != presses_before) board_display_boot_skip();
        const diag_controls_report_t *controls = board_controls_report();
        for (unsigned bank = 0; bank < 4; ++bank) if (controls->banks[bank].valid){
            uint32_t mask=controls->banks[bank].logical_pressed;
            if(backup_transfer.restore_active)restore_suppressed_controls[bank]=mask;
            else{
                if(restore_controls_wait&(1u<<bank)){
                    if(controls->banks[bank].samples==restore_control_samples[bank])continue;
                    restore_controls_wait&=(uint8_t)~(1u<<bank);
                }
                restore_suppressed_controls[bank]&=mask;
                (void)thy_instrument_controls(&instrument,(uint8_t)bank,mask&~restore_suppressed_controls[bank]);
            }
        }
        thy_instrument_profile_end(THY_PROFILE_INPUT_US,profile_mark);
        profile_mark=thy_instrument_profile_begin();
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
        thy_instrument_profile_end(THY_PROFILE_ENGINE_US,profile_mark);
        /* Deliver the current generated-note changes before presentation.
         * Display service never waits for a full frame and has no music clock. */
        profile_mark=thy_instrument_profile_begin();
        thy_instrument_transport_poll(&transport, usb->mounted, thy_instrument_usb_epoch(), 16u);
        if(transport.usb_blocks_pending)
            thy_channel_expression_usb_blocked(&channel_expression,transport.usb_blocked,sizeof transport.usb_blocked);
        thy_instrument_din_task(board_millis());
        thy_instrument_profile_end(THY_PROFILE_TRANSPORT_US,profile_mark);
        board_display_task();
        profile_mark=thy_instrument_profile_begin();
        thy_instrument_lights_task(&instrument, board_millis());
        thy_instrument_control_lights_task(&high_level, board_millis());
        thy_instrument_profile_end(THY_PROFILE_LIGHTS_US,profile_mark);
        /* Programming is sliced into one verified word per idle foreground
         * turn; hashing settings is limited to 10Hz when not already saving.
         * Sector rotation can pause this single-bank MCU while fully idle. */
        now=board_millis();
        profile_mark=thy_instrument_profile_begin();
        if(thy_song_transfer_expire(&song_transfer,now)) {
            settings.observed=0u; high_level.song_status=3u;
        }
        if(thy_settings_transfer_expire(&backup_transfer,now))settings.observed=0u;
        if(thy_companion_expire(&companion,now))settings.observed=0u;
        (void)thy_companion_service(&companion,&high_level,now);
        if(high_level.song_recall_requested && !settings.writing && !song_transfer.state&&
            !companion.active&&!thy_settings_transfer_busy(&backup_transfer)) {
            high_level.song_recall_requested=0u;
            high_level.song_status=thy_settings_journal_recall_song(&settings,&high_level)?2u:3u;
        }
        thy_instrument_profile_end(THY_PROFILE_BACKGROUND_US,profile_mark);
        if (!song_transfer.state&&!companion.active&&!thy_settings_transfer_staging(&backup_transfer)&&
            (settings.writing || (uint32_t)(now-last_settings_check)>=100u)) {
            last_settings_check=now;
            int idle=settings_idle();
            int maintenance_idle=idle&&!board_pads_busy();
            /* A full-song CRC/semantic pass is background work too. Keep it
             * out of active music, rather than merely blocking flash writes. */
            if(settings.writing || maintenance_idle) {
                profile_mark=thy_instrument_profile_begin();
                thy_settings_journal_task(&settings,&high_level,now,maintenance_idle);
                thy_instrument_profile_end(THY_PROFILE_JOURNAL_US,profile_mark);
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
        profile_mark=thy_instrument_profile_begin();
        thy_settings_transfer_poll(&backup_transfer,&high_level,&settings,
            settings_idle(),now);
        restore_input_gate();
        thy_instrument_profile_end(THY_PROFILE_BACKGROUND_US,profile_mark);
    }
}
