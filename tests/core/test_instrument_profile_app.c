/* SPDX-License-Identifier: GPL-3.0-or-later
 * Compare the actual app with capture off/on using the existing board mocks.
 * Only the cycle counter is synthetic; no hardware is accessed. */
static void profiling_turn(void);
#define THY_INSTRUMENT_APP_TEST_MAIN unused_actual_app_fixture_runner
#define THY_INSTRUMENT_APP_TEST_TURN profiling_turn
#include "test_instrument_app.c"

static uint32_t profile_cycles;
static unsigned profile_reads,profile_enabled,profile_starts,disconnect_checks,expiry_checks;
static uint32_t profile_counter(void){++profile_reads;profile_cycles+=168u;return profile_cycles;}
#define THY_INSTRUMENT_PROFILE_CYCLES() profile_counter()
#define THY_INSTRUMENT_PROFILE_CLOCK_READY() 1u
#include "../../firmware/instrument/profile.c"

typedef struct app_snapshot {
    thy_core core_state;
    thy_instrument instrument_state;
    thy_engine engine_state;
    thy_settings_journal journal;
    thy_instrument_transport transport_state;
    uint8_t flash[2u*THY_SETTINGS_SECTOR_BYTES];
    uint8_t din[256][3],usb[512][4],settings_bytes[THY_SETTINGS_PAYLOAD_MAX];
    size_t settings_count;
    unsigned din_count,usb_count,programs,lights,center,display,loads;
} app_snapshot;
static app_snapshot baseline;
static char profile_reply[1024];
static unsigned comparisons;
#define COMPARE(x) do{++comparisons;assert(x);}while(0)

static void profiling_turn(void)
{
    /* Existing fixture starts with pad5 held at power-on. Releasing its raw
     * contact after the warmup assertions lets the real detector settle and
     * permits the actual background settings save later in this scenario. */
    if(mock_iteration>140u)mock_pads.raw[5]=4u;
    if(mock_iteration==141u && profile_enabled){
        assert(thy_instrument_app_profile_command("profile start 5",profile_reply,sizeof profile_reply));
        assert(strstr(profile_reply,"PROFILE version=1 id=1 active=1"));++profile_starts;
    }
    mock_controls.banks[2].valid=1u;
    mock_controls.banks[2].logical_pressed=mock_iteration==170u?4u:mock_iteration==300u?1u:0u;
    if(mock_iteration==172u)assert(instrument.performance.root==2u);
    if(mock_iteration==302u)assert(instrument.performance.root==0u);
    if(mock_iteration>=190u && mock_iteration<=210u){
        thy_instrument_app_console_reset();++disconnect_checks;
        if(profile_enabled)assert(capture.active && capture.id==1u);
    }
    if(mock_iteration==5142u && profile_enabled){
        assert(!capture.active && capture.status==THY_PROFILE_TIMED_OUT && capture.elapsed_ms==5000u);
        ++expiry_checks;
    }
}
static void run_profile_app(unsigned enabled)
{
    profile_enabled=enabled;profile_reads=profile_cycles=profile_starts=disconnect_checks=expiry_checks=0u;
    mock_case=2u;mock_limit=5300u;
    mock_iteration=mock_now=mock_epoch=mock_restore_delay_ms=mock_restore_catalog_checks=0u;
    mock_elapsed_ms=mock_pad_stop_stuck=mock_display_calls=mock_din_count=mock_usb_count=0u;
    mock_boot_stage_count=mock_boot_ready_calls=mock_boot_skip_calls=0u;
    mock_catalog_loads=mock_settings_programs=0u;center_poll=8u;
    mock_boot_stage=THY_STUDIO_BOOT_DISPLAY;mock_usb=(thy_diag_usb_stats){0};
    last_scan=last_layout_revision=last_settings_check=last_song_revision=profile_frames=0u;
    scale_index=256u;group_index=21u;initialized=acquisition_failed=false;
    restore_guard_seen=restore_terminal_phase=restore_controls_wait=0u;
    memset(restore_suppressed_controls,0,sizeof restore_suppressed_controls);
    memset(restore_control_samples,0,sizeof restore_control_samples);
    memset(mock_settings_flash,255u,sizeof mock_settings_flash);
    if(setjmp(mock_escape)==0)(void)thy_test_app_main();
    COMPARE(mock_light_calls==mock_limit && mock_display_calls==mock_limit);
    COMPARE(mock_control_light_calls==mock_limit+1u && disconnect_checks==21u);
    COMPARE(thy_instrument_app_ready() && instrument.input_enabled && !acquisition_failed);
    COMPARE(!core.queue_count && !core.sounding_count && !core.pending_keys && !core.generated_count);
    COMPARE(!transport.din_queue.count && !transport.usb_queue.count && !transport.pending_valid);
    COMPARE(!instrument.errors && !settings.errors && settings.status==THY_SETTINGS_SAVED && settings.loaded);
    COMPARE(mock_settings_programs>0u && instrument.performance.root==0u);
    COMPARE(!instrument.sensors[0].held && !instrument.sensors[5].blocked && !instrument.sensors[5].held);
    for(unsigned n=0;n<4u;++n)COMPARE(!instrument.control_masks[n]);
    if(enabled){
        COMPARE(profile_starts==1u && expiry_checks==1u && profile_reads>64u);
        COMPARE(!capture.active && capture.status==THY_PROFILE_TIMED_OUT && capture.elapsed_ms==5000u);
        const unsigned populated[]={THY_PROFILE_LOOP_US,THY_PROFILE_USB_US,THY_PROFILE_TRANSPORT_US,
            THY_PROFILE_INPUT_US,THY_PROFILE_ENGINE_US,THY_PROFILE_LIGHTS_US,THY_PROFILE_BACKGROUND_US,
            THY_PROFILE_JOURNAL_US,THY_PROFILE_SETTINGS_ENCODE_US,THY_PROFILE_SETTINGS_VALIDATE_US,
            THY_PROFILE_SETTINGS_CRC_US,THY_PROFILE_ADC_FRAME_GAP_US,THY_PROFILE_INPUT_GAP_US,
            THY_PROFILE_USB_WAIT_MS,THY_PROFILE_DIN_WAIT_MS,THY_PROFILE_SETTINGS_PROGRAM_US};
        for(unsigned n=0;n<sizeof populated/sizeof populated[0];++n)COMPARE(capture.stats[populated[n]].count>0u);
        COMPARE(capture.peaks[THY_PROFILE_PEAK_CORE]>0u && capture.peaks[THY_PROFILE_PEAK_DIN]>0u);
        /* Display and physical DIN byte-queue timing belong to their actual
         * drivers, which this application fixture intentionally substitutes. */
        COMPARE(!capture.stats[THY_PROFILE_DISPLAY_RENDER_US].count && !capture.stats[THY_PROFILE_DISPLAY_CHUNK_US].count);
        COMPARE(!capture.invalid_samples && !capture.dropped_samples);
    }else COMPARE(!profile_starts && !profile_reads && !capture.id && !capture.active);
}
static void capture_baseline(void)
{
    baseline.core_state=core;baseline.instrument_state=instrument;baseline.engine_state=high_level;
    baseline.journal=settings;baseline.transport_state=transport;
    memcpy(baseline.flash,mock_settings_flash,sizeof baseline.flash);
    memcpy(baseline.din,mock_din_messages,sizeof baseline.din);memcpy(baseline.usb,mock_usb_packets,sizeof baseline.usb);
    baseline.din_count=mock_din_count;baseline.usb_count=mock_usb_count;baseline.programs=mock_settings_programs;
    baseline.lights=mock_light_calls;baseline.center=mock_control_light_calls;baseline.display=mock_display_calls;
    baseline.loads=mock_catalog_loads;
    COMPARE(thy_settings_encode(&high_level,baseline.settings_bytes,sizeof baseline.settings_bytes,&baseline.settings_count));
}
static void compare_baseline(void)
{
    static uint8_t encoded[THY_SETTINGS_PAYLOAD_MAX];size_t count=0u;
    COMPARE(!memcmp(&core,&baseline.core_state,sizeof core));
    COMPARE(!memcmp(&instrument,&baseline.instrument_state,sizeof instrument));
    COMPARE(!memcmp(&high_level,&baseline.engine_state,sizeof high_level));
    COMPARE(!memcmp(&settings,&baseline.journal,sizeof settings));
    COMPARE(!memcmp(&transport,&baseline.transport_state,sizeof transport));
    COMPARE(!memcmp(mock_settings_flash,baseline.flash,sizeof baseline.flash));
    COMPARE(mock_din_count==baseline.din_count && mock_usb_count==baseline.usb_count);
    COMPARE(!memcmp(mock_din_messages,baseline.din,mock_din_count*3u));
    COMPARE(!memcmp(mock_usb_packets,baseline.usb,mock_usb_count*4u));
    COMPARE(mock_settings_programs==baseline.programs && mock_light_calls==baseline.lights);
    COMPARE(mock_control_light_calls==baseline.center && mock_display_calls==baseline.display && mock_catalog_loads==baseline.loads);
    COMPARE(thy_settings_encode(&high_level,encoded,sizeof encoded,&count));
    COMPARE(count==baseline.settings_count && !memcmp(encoded,baseline.settings_bytes,count));
}
static void rejected_start_preserves_all_musical_and_settings_state(void)
{
    static thy_core old_core;static thy_engine old_engine;static thy_instrument old_instrument;
    static thy_settings_journal old_settings;static thy_profile old_capture;
    old_core=core;old_engine=high_level;old_instrument=instrument;old_settings=settings;old_capture=capture;
    COMPARE(thy_instrument_app_profile_command("profile start 1",profile_reply,sizeof profile_reply));
    COMPARE(strstr(profile_reply,"ERR profile-busy\r\n")==profile_reply);
    COMPARE(!memcmp(&core,&old_core,sizeof core) && !memcmp(&high_level,&old_engine,sizeof high_level));
    COMPARE(!memcmp(&instrument,&old_instrument,sizeof instrument) && !memcmp(&settings,&old_settings,sizeof settings));
    COMPARE(!memcmp(&capture,&old_capture,sizeof capture));
}
static void maintenance_rejection_and_live_note_admission(void)
{
    COMPARE(!capture.active && thy_instrument_app_ready());
    settings.writing=1u;rejected_start_preserves_all_musical_and_settings_state();settings.writing=0u;
    song_transfer.state=THY_SONG_DOWNLOAD;rejected_start_preserves_all_musical_and_settings_state();song_transfer.state=THY_SONG_IDLE;
    companion.active=1u;rejected_start_preserves_all_musical_and_settings_state();companion.active=0u;
    backup_transfer.restore_active=1u;rejected_start_preserves_all_musical_and_settings_state();backup_transfer.restore_active=0u;
    backup_transfer.state=THY_BACKUP_UPLOAD;rejected_start_preserves_all_musical_and_settings_state();backup_transfer.state=THY_BACKUP_IDLE;
    acquisition_failed=true;rejected_start_preserves_all_musical_and_settings_state();acquisition_failed=false;
    int16_t pitch=72;COMPARE(thy_core_press(&core,95u,&pitch,1u,0,0u,THY_ROUTE_DIN,96u)==THY_OK);
    static thy_core held;held=core;static uint8_t bytes[THY_SETTINGS_PAYLOAD_MAX];size_t count=0u;
    COMPARE(thy_settings_encode(&high_level,bytes,sizeof bytes,&count));
    COMPARE(thy_instrument_app_profile_command("profile start 1",profile_reply,sizeof profile_reply));
    COMPARE(strstr(profile_reply,"PROFILE version=1 id=2 active=1")==profile_reply && !memcmp(&core,&held,sizeof core));
    thy_instrument_app_console_reset();COMPARE(capture.active && capture.id==2u && !memcmp(&core,&held,sizeof core));
    COMPARE(thy_instrument_app_profile_command("profile stop 2",profile_reply,sizeof profile_reply));
    COMPARE(!capture.active && !memcmp(&core,&held,sizeof core));
    static uint8_t after[THY_SETTINGS_PAYLOAD_MAX];size_t after_count=0u;
    COMPARE(thy_settings_encode(&high_level,after,sizeof after,&after_count));
    COMPARE(count==after_count && !memcmp(bytes,after,count));
    COMPARE(thy_core_release(&core,95u)==THY_OK);
    thy_midi_event event;while(thy_core_poll(&core,&event)){}
    COMPARE(!core.sounding_count && !core.queue_count);
}
int main(void)
{
    run_profile_app(0u);capture_baseline();
    run_profile_app(1u);compare_baseline();
    maintenance_rejection_and_live_note_admission();
    printf("Instrument profiling app: 2 scenarios, %u comparisons; actual on/off music, controls, saved bytes, disconnect, expiry and maintenance passed\n",comparisons);
    return 0;
}
