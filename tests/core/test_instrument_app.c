/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual instrument main loop, real detector/ownership/transport, bounded
 * board and endpoint mocks. No device API, MMIO or electrical simulation. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define THY_INSTRUMENT_CPU_ASM(...) ((void)0)
#define main thy_test_app_main
#include "../../firmware/instrument/app.c"
#undef main

void board_resources_task(uint32_t now_ms) { (void)now_ms; }
volatile diag_report_t g_diag_mailbox;
const char thy_diag_build_id[]="0123456789abcdef";
int board_companion_visual(void *context,uint8_t oled[1024],uint8_t rgb[288],
    uint8_t center[19],uint32_t *frame_ms,uint8_t *panel_visible)
{
    (void)context;memset(oled,0,1024u);memset(rgb,0,288u);memset(center,0,19u);
    *frame_ms=0u;*panel_visible=1u;return 1;
}
static jmp_buf mock_escape;
static unsigned mock_case, mock_iteration, mock_limit;
static uint32_t mock_now, mock_epoch;
static diag_pads_report_t mock_pads;
static diag_controls_report_t mock_controls;
static thy_diag_usb_stats mock_usb;
static thy_instrument_din_stats mock_din;
static uint8_t mock_din_messages[256][3], mock_usb_packets[512][4];
static unsigned mock_din_count, mock_usb_count, mock_light_calls;
static unsigned mock_pad_active, mock_pad_busy, mock_pad_stop_pending;
static unsigned mock_pad_stops, mock_pad_starts, mock_pad_stop_services;
static unsigned mock_restore_delay_ms, mock_restore_catalog_checks, mock_elapsed_ms;
static unsigned mock_pad_stop_stuck;
static uint32_t mock_restored_crc;
static void restore_during_main(void);
static void commit_restore_during_main(void);
static void complete_restore_during_main(void);
static unsigned mock_control_light_calls;
static unsigned mock_display_calls;
static unsigned mock_boot_stage_count, mock_boot_ready_calls, mock_boot_skip_calls;
static uint8_t mock_boot_stage;
static uint8_t mock_settings_flash[2u*THY_SETTINGS_SECTOR_BYTES];
static unsigned mock_settings_programs;
static unsigned mock_catalog_loads;
static uint32_t mock_burst_attacks;
static thy_instrument_lights_override_fn mock_color_override;
static void *mock_color_context;
static void usb_cleanup_packet(unsigned index)
{
    /* Each cable/channel now receives CC64/123/120, pressure zero, bend
     * center and CC74 neutral. Verify the exact reset prefix, not merely
     * "some controls": a replayed note must still fail this fixture. */
    assert(index<mock_usb_count);
    unsigned within=index%THY_INSTRUMENT_TRANSPORT_RECONCILE_EVENTS;
    unsigned route=within/6u,phase=within%6u;
    static const uint8_t kind[6]={0xb0u,0xb0u,0xb0u,0xd0u,0xe0u,0xb0u};
    static const uint8_t first[6]={64u,123u,120u,0u,0u,74u};
    static const uint8_t second[6]={0u,0u,0u,0u,64u,64u};
    assert(mock_usb_packets[index][0]==(uint8_t)(((route/16u)<<4u)|(kind[phase]>>4u)));
    assert(mock_usb_packets[index][1]==(uint8_t)(kind[phase]|(route%16u)));
    assert(mock_usb_packets[index][2]==first[phase]&&mock_usb_packets[index][3]==second[phase]);
}
static int mock_settings_read(void *context,uint32_t offset,uint8_t *out,size_t count) {
    (void)context; assert(offset<=sizeof mock_settings_flash && count<=sizeof mock_settings_flash-offset);
    memcpy(out,mock_settings_flash+offset,count); return 1;
}
static int mock_settings_program(void *context,uint32_t offset,const uint8_t word[4]) {
    (void)context; assert(!(offset&3u) && offset<=sizeof mock_settings_flash-4u);
    assert(!mock_pad_busy); /* Internal flash maintenance waits for ADC completion. */
    for(unsigned n=0;n<4u;++n) {assert(mock_settings_flash[offset+n]==255u);mock_settings_flash[offset+n]=word[n];}
    ++mock_settings_programs; return 1;
}
static int mock_settings_erase(void *context,unsigned sector) {
    (void)context; assert(sector<2u);
    memset(mock_settings_flash+sector*THY_SETTINGS_SECTOR_BYTES,255u,THY_SETTINGS_SECTOR_BYTES);return 1;
}
int thy_instrument_settings_io(thy_settings_io *io) {
    *io=(thy_settings_io){NULL,mock_settings_read,mock_settings_program,mock_settings_erase};return 1;
}

void board_init(void)
{
    g_diag_mailbox = (diag_report_t){0};
    g_diag_mailbox.hclk_hz = 168000000u;
    g_diag_mailbox.pclk1_hz = 42000000u;
    g_diag_mailbox.pclk2_hz = 84000000u;
    g_diag_mailbox.usb_clock_ready = 1u;
    g_diag_mailbox.touch_initialized_mask = mock_case == 3u ? 14u : 15u;
    g_diag_mailbox.touch_failed_mask = mock_case == 3u ? 1u : 0u;
}
uint32_t board_millis(void) { return mock_now; }
static uint8_t center_poll = 8u;
uint32_t board_touch_response_ms(void) { return center_poll; }
int board_touch_response_set(uint32_t ms) { center_poll = ms; return 0; }
void board_boot_stage(uint32_t stage) { g_diag_mailbox.boot_stage = stage; }
void board_display_boot_progress(uint8_t stage)
{
    if (stage < mock_boot_stage) return; /* Renderer milestones are monotonic. */
    if (stage == mock_boot_stage) return;
    assert(stage == mock_boot_stage + 1u);
    if (stage >= THY_STUDIO_BOOT_PADS) assert(mock_pads.frames != 0u);
    if (stage == THY_STUDIO_BOOT_MIDI) assert(thy_instrument_app_ready());
    mock_boot_stage = stage;
    ++mock_boot_stage_count;
}
void board_display_boot_ready(void)
{
    assert(thy_instrument_app_ready() && mock_pads.frames != 0u);
    assert(mock_boot_stage == THY_STUDIO_BOOT_MIDI);
    ++mock_boot_ready_calls;
}
void board_display_boot_skip(void) { ++mock_boot_skip_calls; }
void board_display_task(void)
{
    /* Presentation sees reconciled acquisition and the current music clock.
     * The real driver sends at most one bounded chunk here, never a frame. */
    assert(initialized && last_scan == mock_pads.scans);
    assert(high_level.musical_clock.now_ms == mock_now);
    ++mock_display_calls;
}
void board_poll(void) { }
void thy_instrument_memory_init(void) { assert(!initialized); }
void board_led_init(void) { }
void thy_instrument_lights_init(void)
{mock_light_calls=0u;mock_color_override=NULL;mock_color_context=NULL;}
void thy_instrument_lights_set_override(thy_instrument_lights_override_fn fn,void *context)
{assert(fn && context==&high_level);mock_color_override=fn;mock_color_context=context;}
void thy_instrument_lights_task(const thy_instrument *state, uint32_t now)
{
    assert(state == &instrument && now == mock_now);
    ++mock_light_calls;
}
void thy_instrument_control_lights_init(void) { mock_control_light_calls = 0u; }
void thy_instrument_control_lights_task(const thy_engine *state, uint32_t now)
{
    assert(state == &high_level && state->instrument == &instrument && now == mock_now);
    ++mock_control_light_calls;
}
void board_controls_init(void) { memset(&mock_controls, 0, sizeof mock_controls); }
void board_controls_task(uint32_t now) {
    assert(now == mock_now);
    for(unsigned bank=0u;bank<4u;++bank)if(mock_controls.banks[bank].valid)++mock_controls.banks[bank].samples;
}
const diag_controls_report_t *board_controls_report(void) { return &mock_controls; }
void board_center_init(void) { }
int board_center_request(unsigned mode, unsigned channel, unsigned brightness)
{
    assert(mode == DIAG_CENTER_ALL && channel == 0u && brightness == DIAG_CENTER_DEFAULT_BRIGHTNESS);
    return DIAG_SUCCESS;
}
void board_pads_init(void)
{
    memset(&mock_pads, 0, sizeof mock_pads);
    mock_pad_active = mock_pad_busy = mock_pad_stop_pending = 0u;
    mock_pad_stops = mock_pad_starts = mock_pad_stop_services = 0u;
    for (unsigned i = 0u; i < 96u; ++i) mock_pads.raw[i] = 4u;
    mock_pads.valid[0] = mock_pads.valid[1] = mock_pads.valid[2] = UINT32_MAX;
}
int board_pads_request(int start)
{
    assert(start == 0 || start == 1);
    if (!start) {
        ++mock_pad_stops;
        mock_pad_stop_pending = mock_pad_active;
        return DIAG_SUCCESS;
    }
    assert(!mock_pad_active);
    ++mock_pad_starts;
    mock_pad_active = 1u;
    mock_pad_busy = 0u;
    mock_pads.scans = mock_pads.frames = mock_pads.index = 0u;
    mock_pads.state = DIAG_PADS_SETTLE;
    return DIAG_SUCCESS;
}
int board_pads_active(void) { return (int)mock_pad_active; }
int board_pads_busy(void) { return (int)mock_pad_busy; }
void board_pads_task(uint32_t now)
{
    assert(now == mock_now);
    if (!mock_pad_stop_pending) return;
    ++mock_pad_stop_services;
    if (mock_pad_stop_stuck) { ++mock_elapsed_ms; ++mock_now; return; }
    if (mock_pads.state != DIAG_PADS_RESTORE) {
        mock_pads.state = DIAG_PADS_RESTORE;
        mock_pad_busy = 0u;
        return;
    }
    ++mock_elapsed_ms; ++mock_now; /* Model the driver's one millisecond cleanup. */
    mock_pad_active = mock_pad_stop_pending = 0u;
    mock_pads.state = DIAG_PADS_IDLE;
}
const diag_pads_report_t *board_pads_report(void) { return &mock_pads; }
void thy_instrument_pads_renew(uint32_t now) { assert(now == mock_now); }

bool thy_instrument_din_init(uint32_t pclk2_hz)
{
    assert(pclk2_hz == 84000000u);
    mock_din = (thy_instrument_din_stats){0};
    mock_din.initialized = mock_din.ready = 1u;
    return true;
}
void thy_instrument_din_task(uint32_t now) { assert(now == mock_now); }
const thy_instrument_din_stats *thy_instrument_din_report(void) { return &mock_din; }
bool thy_instrument_din_write(const uint8_t *bytes, size_t count)
{
    assert(bytes != NULL && count == 3u && mock_din_count < 256u);
    memcpy(mock_din_messages[mock_din_count++], bytes, 3u);
    return true;
}
bool tud_midi_packet_write(const uint8_t packet[4])
{
    assert(mock_usb.mounted && mock_usb_count < 512u);
    memcpy(mock_usb_packets[mock_usb_count++], packet, 4u);
    return true;
}
bool thy_diag_usb_init(void)
{
    assert(!mock_boot_stage_count);
    assert(!mock_boot_ready_calls);
    mock_usb.initialized = true;
    return true;
}
const thy_diag_usb_stats *thy_diag_usb_status(void) { return &mock_usb; }
uint32_t thy_instrument_usb_epoch(void) { return mock_epoch; }

int thy_instrument_data_find(unsigned group, unsigned root)
{
    if (mock_restore_delay_ms && ++mock_restore_catalog_checks == 2u) {
        /* The real restore performs several settings decodes and a catalog
         * refresh synchronously. The actual ADC driver separately tests delayed
         * DMA completions; this application mock requires quiet hardware for
         * a deliberately long catalog operation before letting time advance. */
        assert(!mock_pad_active && !mock_pad_busy);
        mock_elapsed_ms += mock_restore_delay_ms;
        mock_now += mock_restore_delay_ms;
    }
    assert(group < 70u && root < 12u);
    return (int)(4u + group * 12u + root);
}
int thy_instrument_data_load(unsigned scale, thy_instrument_pad pads[96],
    int16_t *anchor, int16_t degrees[12], uint8_t *degree_count)
{
    static const int16_t major_degrees[] = {0, 2, 4, 5, 7, 9, 11};
    assert(scale >= 4u && scale < 844u);
    ++mock_catalog_loads;
    int16_t root = (int16_t)((scale - 4u) % 12u);
    for (unsigned i = 0u; i < 96u; ++i)
        pads[i] = (thy_instrument_pad){{(int16_t)(60 + root)}, 1u, 0u};
    pads[0] = (thy_instrument_pad){{(int16_t)(60 + root), (int16_t)(64 + root),
                                 (int16_t)(67 + root)}, 7u, 0u};
    *anchor = (int16_t)(60 + root);
    *degree_count = 7u;
    for (unsigned i = 0u; i < 7u; ++i) degrees[i] = major_degrees[i];
    return 1;
}
const char *thy_instrument_data_name(unsigned scale)
{
    assert(scale == 256u);
    return "MAJOR C";
}

static unsigned raw_value(unsigned slot)
{
    if (mock_case >= 7u) return slot==0u &&
        ((mock_iteration>=155u && mock_iteration<=190u)||(mock_iteration>=4000u && mock_iteration<=4040u))?3766u:4u;
    if (slot == 5u) return 3766u; /* Held throughout power-on: this pad cannot arm. */
    if (mock_case == 1u) {
        if (slot == 0u && mock_iteration >= 145u && mock_iteration <= 192u) return 3766u;
        if (slot == 48u && mock_iteration >= 225u && mock_iteration <= 272u) return 3766u;
    } else {
        if (slot == 0u && ((mock_iteration >= 145u && mock_iteration <= 320u) ||
                          (mock_iteration >= 385u && mock_iteration <= 416u))) return 3766u;
    }
    return 4u;
}

void thy_diag_usb_task(void)
{
    if (++mock_iteration > mock_limit) longjmp(mock_escape, 1);
    mock_now = mock_iteration + mock_elapsed_ms;
    if (mock_case == 5u || mock_case == 6u) {
        if (mock_iteration == 1u) {
            thy_arp_config config=high_level.arps[0].config;
            config.enabled=1u; config.channel=2u; config.destinations=THY_ROUTE_DIN;
            assert(thy_engine_arp_configure(&high_level,0u,&config)==THY_OK);
            if(mock_case==6u) assert(thy_engine_clock_configure(&high_level,120000u,
                THY_CLOCK_SOURCE_USB,4u,4u,THY_TRANSPORT_BAR)==THY_OK);
        }
        if(mock_case==5u && mock_iteration==220u) {
            uint32_t generation=high_level.live_generation[0];
            thy_instrument_app_usb_reset(mock_now);
            assert(high_level.live_generation[0]==generation && high_level.arps[0].held_count==1u);
        }
        if(mock_case==6u) {
            thy_midi_event pulse={THY_USB0,0xf8u,0u,0u};
            if(mock_iteration%10u==0u && (mock_iteration<250u || mock_iteration>=350u))
                thy_instrument_app_midi(&pulse,mock_now);
            if(mock_iteration==210u) {
                mock_burst_attacks=high_level.arps[0].attacks;
                for(unsigned n=0;n<32u;++n) thy_instrument_app_midi(&pulse,mock_now);
                assert(high_level.arps[0].attacks==mock_burst_attacks); /* No per-packet replay. */
            }
            if(mock_iteration==211u) assert(high_level.arps[0].attacks<=mock_burst_attacks+1u);
            if(mock_iteration==250u) {
                thy_instrument_app_usb_reset(mock_now);
                assert(high_level.musical_clock.status==THY_CLOCK_WAITING);
                assert(!high_level.arps[0].held_count && !core.generated_count);
            }
            if(mock_iteration==351u) assert(!high_level.arps[0].held_count && !core.generated_count);
        }
    }
    if (mock_iteration == 140u) {
        /* All other pads are qualified and usable despite one boot-held pad.
         * This assertion fails if app_ready globally waits for all96 inputs. */
        assert(thy_instrument_app_ready() && instrument.input_enabled);
        assert(instrument.sensors[0].warmup_count == 8u);
        assert(instrument.sensors[5].warmup_count == (mock_case>=7u?8u:0u));
        assert(instrument.sensors[5].blocked == (mock_case>=7u?0u:1u) && !instrument.sensors[5].held);
        assert(instrument.sensors[0].detector_profile == THY_DETECTOR_FACTORY_LINEAR);
        assert(instrument.destinations[0] == (THY_ROUTE_DIN | THY_ROUTE_USB0));
        assert(instrument.destinations[1] == (THY_ROUTE_DIN | THY_ROUTE_USB0));
        assert(instrument.channels[0] == (mock_case==4u?2u:0u) && instrument.channels[1] == 1u);
        if(mock_case==4u) {
            assert(settings.loaded && settings.status==THY_SETTINGS_SAVED && !settings.errors);
            assert(instrument.route_velocity[1]==94u && instrument.excluded_slots[0]==2u);
            assert(instrument.center_poll_ms==10u && instrument.light_key_contrast_percent==17u);
        }
        assert(mock_din_count == 48u && !mock_usb_count);
        if (mock_case == 3u) assert(!mock_boot_ready_calls && mock_boot_skip_calls);
        else assert(mock_boot_ready_calls != 0u); /* No USB host required. */
    }
    if (mock_case >= 7u) {
        if (mock_iteration == 150u) restore_during_main();
        if (mock_iteration == 200u) commit_restore_during_main();
        if (mock_case==8u && mock_iteration>=210u) {
            uint8_t observed=settings.observed;uint32_t changed=settings.changed_ms;
            thy_instrument_app_console_reset();
            assert(settings.observed==observed&&settings.changed_ms==changed);
        }
        if (mock_case==7u && backup_transfer.restore_verified && backup_transfer.restore_active)
            complete_restore_during_main();
        mock_controls.banks[2].valid=1u;
        mock_controls.banks[2].logical_pressed=
            (mock_iteration>=155u && mock_iteration<3980u)||mock_iteration==3990u?4u:
            mock_iteration==3992u?1u:0u;
        if(mock_iteration>=156u && mock_iteration<3980u)assert(instrument.performance.root==0u);
        if(mock_iteration==3991u)assert(instrument.performance.root==2u);
        if(mock_iteration==3993u)assert(instrument.performance.root==0u);
        /* A later in-flight acquisition interrupts an ongoing save. Its
         * pending journal words must wait rather than program over ADC work. */
        mock_pad_busy=(mock_iteration>=1800u && mock_iteration<=2000u);
    }
    if (mock_case == 2u) {
        if (mock_iteration == 180u || mock_iteration == 220u) {
            mock_usb.mounted = true;
            ++mock_epoch;
        } else if (mock_iteration == 200u) {
            mock_usb.mounted = false;
            ++mock_epoch;
        }
        if (mock_iteration == 250u) {
            assert(thy_instrument_app_ready() && instrument.input_enabled);
            assert(instrument.sensors[0].held && !instrument.sensors[0].blocked);
            assert(thy_core_pad_held(&core, 0u) && core.sounding_count == 6u);
            assert(mock_din_count == 51u); /* No panic/Off at USB transitions. */
            assert(instrument.presses == 1u && instrument.releases == 0u);
            for (unsigned i = 0u; i < mock_usb_count; ++i)
                usb_cleanup_packet(i); /* No held Note On replay. */
        }
    }
    unsigned selection = (mock_iteration - 1u) & 15u;
    for (unsigned bank = 0u; bank < 6u; ++bank) {
        unsigned slot = bank * 16u + selection;
        mock_pads.raw[slot] = raw_value(slot);
    }
    ++mock_pads.scans;
    mock_pads.index = (selection + 1u) & 15u;
    if (mock_pads.index == 0u) ++mock_pads.frames;
#ifdef THY_INSTRUMENT_APP_TEST_TURN
    THY_INSTRUMENT_APP_TEST_TURN();
#endif
}

static void din_message(unsigned index, unsigned status, unsigned note, unsigned value)
{
    assert(index < mock_din_count);
    assert(mock_din_messages[index][0] == status);
    assert(mock_din_messages[index][1] == note);
    assert(mock_din_messages[index][2] == value);
}
static void run_case(unsigned scenario, unsigned limit)
{
    mock_case = scenario;
    mock_limit = limit;
    mock_iteration = mock_now = mock_epoch = 0u;
    mock_restore_delay_ms = mock_restore_catalog_checks = mock_elapsed_ms = mock_pad_stop_stuck = 0u;
    mock_display_calls = 0u;
    mock_din_count = mock_usb_count = 0u;
    mock_boot_stage_count = mock_boot_ready_calls = mock_boot_skip_calls = 0u;
    mock_boot_stage = THY_STUDIO_BOOT_DISPLAY;
    mock_usb = (thy_diag_usb_stats){0};
    /* Model a new application entry without substituting its main loop. */
    last_scan = last_layout_revision = 0u;
    scale_index = 256u;
    group_index = 21u;
    initialized = acquisition_failed = false;
    restore_guard_seen=restore_terminal_phase=restore_controls_wait=0u;
    memset(restore_suppressed_controls,0,sizeof restore_suppressed_controls);
    if (setjmp(mock_escape) == 0) (void)thy_test_app_main();
    assert(mock_light_calls == limit);
    assert(mock_display_calls == limit);
    assert(mock_control_light_calls == limit + 1u); /* Startup plus every music turn. */
    assert(!core.queue_count && !transport.din_queue.count && !transport.pending_valid);
    assert(!core.sounding_count && !core.pending_keys && !instrument.errors);
    assert(!instrument.sensors[0].held && !instrument.sensors[48].held);
    /* These real musical presses all occur before the 840 ms brand reveal
     * could finish. Presentation cannot hold up acquisition or DIN output. */
    assert((mock_case>=7u || mock_now < 840u) && mock_boot_skip_calls >= (mock_case==4u || mock_case>=7u?1u:2u));
    if (mock_case == 3u) assert(!mock_boot_ready_calls && !mock_boot_stage_count);
    else assert(mock_boot_ready_calls && mock_boot_stage_count == 3u);
}

static void catalog_control(unsigned bank, unsigned index)
{
    assert(thy_instrument_control(&instrument,(uint8_t)bank,(uint8_t)index,1)==THY_OK);
    assert(thy_instrument_control(&instrument,(uint8_t)bank,(uint8_t)index,0)==THY_OK);
    if(last_layout_revision!=instrument.layout_revision) refresh_layout();
    assert(last_layout_revision==instrument.layout_revision);
}

static void same_catalog_refresh_preserves_lower_preferences(void)
{
    thy_core_init(&core);
    thy_instrument_init(&instrument,&core);
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&high_level,&instrument,21u,70u, undo_workspace_1);
    thy_engine_bind_controls(&high_level);
    thy_instrument_set_action_handler(&instrument,action,NULL);
    group_index=21u; scale_index=256u; last_layout_revision=0u;
    acquisition_failed=false;
    unsigned loads=mock_catalog_loads;
    refresh_layout();
    assert(mock_catalog_loads==loads+1u && instrument.layout==layout);
    assert(scale_index==256u && !acquisition_failed); /* NULL forced first load. */
    instrument.slot_scope=THY_SLOTS_SELECTED_PAD;
    instrument.selected_chord=0u;
    instrument.pad_octave_layers[0]=72u;
    instrument.excluded_slots[0]=2u;
    instrument.octave_layers=24u;
    instrument.global_excluded_slots=4u;
    loads=mock_catalog_loads;
    catalog_control(2u,0u); /* C again. */
    assert(instrument.pad_octave_layers[0]==72u && instrument.excluded_slots[0]==2u);
    uint8_t melody=instrument.performance.melody_layout;
    catalog_control(3u,1u); /* 12 cycles only the RIGHT layout. */
    assert(instrument.performance.melody_layout!=melody);
    assert(instrument.pad_octave_layers[0]==72u && instrument.excluded_slots[0]==2u);
    assert(!instrument.users_valid);
    assert(thy_instrument_set_scale_shortcut(&instrument,9u,21u)==THY_OK);
    catalog_control(3u,9u); /* Mapping to the current scale is a catalog no-op. */
    assert(mock_catalog_loads==loads && instrument.pad_octave_layers[0]==72u);
    assert(thy_instrument_set_lower_mode(&instrument,THY_LOWER_STORED_NOTES)==THY_OK);
    instrument.slot_scope=THY_SLOTS_LEFT_SIDE;
    refresh_layout();
    assert(mock_catalog_loads==loads && instrument.excluded_slots[0]==2u);
    assert(thy_instrument_set_lower_mode(&instrument,THY_LOWER_OCTAVE_LAYERS)==THY_OK);
    instrument.slot_scope=THY_SLOTS_SELECTED_PAD;
    assert(thy_instrument_lower_mask(&instrument)==72u);
    catalog_control(2u,2u); /* A genuinely different root reloads the catalog. */
    assert(mock_catalog_loads==loads+1u && scale_index==258u);
    assert(instrument.pad_octave_layers[0]==8u && !instrument.excluded_slots[0]);
    assert(instrument.octave_layers==24u && instrument.global_excluded_slots==4u);
    instrument.pad_octave_layers[0]=9u; instrument.excluded_slots[0]=1u;
    catalog_control(3u,3u); /* M2 changes the scale group, preserving root D. */
    assert(high_level.scale==22u && mock_catalog_loads==loads+2u && scale_index==270u);
    assert(instrument.pad_octave_layers[0]==8u && !instrument.excluded_slots[0]);
    assert(instrument.octave_layers==24u && instrument.global_excluded_slots==4u);
    for (unsigned key=9u; key<12u; ++key) {
        assert(thy_instrument_set_scale_shortcut(&instrument,(uint8_t)key,
            thy_instrument_default_scale_shortcut((uint8_t)key))==THY_OK);
        catalog_control(3u,key);
        assert(high_level.scale==thy_instrument_default_scale_shortcut((uint8_t)key));
        assert(scale_index==4u+high_level.scale*12u+2u);
    }
    assert(mock_catalog_loads==loads+5u);
    assert(!instrument.errors && !acquisition_failed && !core.queue_count && !core.sounding_count);
    puts("instrument catalog refresh regression passed: first load, same root, melody layout, mapped shortcuts, all U scales, mode/scope and true root/group changes");
}

static void calibration_callback_mapping_tests(void)
{
    /* The real app registered this callback during startup. Exercise that
     * callback, rather than a replacement that happens to draw pad zero. */
    assert(mock_color_override==calibration_colors && mock_color_context==&high_level);
    thy_core_init(&core);thy_instrument_init(&instrument,&core);
    static thy_seq_event undo_workspace_2[THY_SEQ_EVENTS];
    thy_engine_init(&high_level,&instrument,21u,70u, undo_workspace_2);
    thy_engine_bind_calibration(&high_level,&calibration);
    uint8_t mapping[96];
    for(unsigned p=0;p<96u;++p)mapping[p]=(uint8_t)((p*37u+11u)%96u);
    assert(thy_instrument_set_mapping(&instrument,mapping)==THY_OK);
    for(unsigned side=0u;side<2u;++side){
        high_level.sensitivity_target=(uint8_t)(side?97u:0u);
        assert(thy_engine_calibration_open(&high_level)==THY_STATE_OK);
        assert(calibration.target==side*48u && calibration.scope_count==48u);
        for(unsigned index=0;index<48u;++index){
            unsigned logical=side*48u+(index%12u)*4u+index/12u;
            calibration.target=calibration.selected=(uint8_t)logical;
            uint32_t colors[96];
            assert(mock_color_override(mock_color_context,300u,colors)==1);
            unsigned selected_count=0u;
            for(unsigned electrical=0;electrical<96u;++electrical){
                int selected=mapping[electrical]==logical;
                uint32_t expected=mapping[electrical]/48u!=side?0u:selected?0x040800u:0x010101u;
                assert(colors[mapping[electrical]]==expected);
                selected_count+=(unsigned)selected;
            }
            assert(selected_count==1u);
        }
        thy_engine_calibration_close(&high_level);
        thy_calibration_cancel(&calibration);
    }
    uint32_t colors[96];
    assert(mock_color_override(mock_color_context,300u,colors)==0);
    assert(!core.sounding_count && !core.queue_count && !instrument.errors);
}

static void backup_and_companion_integration(void)
{
    static uint8_t candidate[THY_BACKUP_FILE_MAX],encoded[THY_SETTINGS_PAYLOAD_MAX];
    char reply[384],command[128];
    thy_core_init(&core);thy_instrument_init(&instrument,&core);
    static thy_seq_event undo_workspace_3[THY_SEQ_EVENTS];
    thy_engine_init(&high_level,&instrument,21u,70u, undo_workspace_3);thy_engine_bind_expression(&high_level,&expression);
    thy_engine_bind_channel_expression(&high_level,&channel_expression);
    (void)thy_instrument_set_detector_profile(&instrument,THY_DETECTOR_FACTORY_LINEAR);
    memset(mock_settings_flash,255u,sizeof mock_settings_flash);
    thy_settings_io backend;assert(thy_instrument_settings_io(&backend));
    assert(thy_settings_journal_init(&settings,&backend,70u));
    memset(&transport,0,sizeof transport);memset(&mock_din,0,sizeof mock_din);
    initialized=true;acquisition_failed=false;scale_index=256u;instrument.layout=NULL;
    refresh_layout();init_services();song_transfer.state=THY_SONG_IDLE;
    thy_song_transfer_init(&song_transfer,settings.record,sizeof settings.record);
    assert(musical_idle()&&settings_idle());
    settings.observed=1u;settings.changed_ms=123u;
    thy_instrument_app_console_reset();thy_instrument_app_console_reset();
    assert(settings.observed==1u&&settings.changed_ms==123u);
    settings.observed=0u;
    high_level.scale=22u;instrument.performance.root=3u;refresh_layout();
    instrument.slot_scope=1u;instrument.excluded_slots[0]=2u;instrument.center_poll_ms=12u;
    assert(thy_instrument_app_backup_command("backup export",reply,sizeof reply));
    if(strstr(reply,"BACKUP export bytes=")!=reply)fprintf(stderr,"app backup export: %s",reply);
    assert(strstr(reply,"BACKUP export bytes=")==reply&&!settings_idle());
    uint32_t count=backup_transfer.bytes;memcpy(candidate,backup_transfer.header,64u);
    memcpy(candidate+64u,backup_transfer.buffer,count-64u);
    assert(thy_instrument_app_song_command("song export",reply,sizeof reply));
    assert(strstr(reply,"ERR song-busy")==reply);
    assert(thy_instrument_app_companion_command("companion snapshot",reply,sizeof reply));
    assert(strstr(reply,"COMPANION/1 ERR busy")==reply);
    thy_instrument_app_console_reset();assert(!backup_transfer.state&&settings_idle());
    assert(thy_instrument_app_companion_command("companion snapshot",reply,sizeof reply)&&companion.active);
    assert(thy_instrument_app_backup_command("backup export",reply,sizeof reply));
    assert(strstr(reply,"ERR backup-busy")==reply);
    thy_instrument_app_console_reset();assert(!companion.active);
    high_level.scale=21u;instrument.performance.root=0u;refresh_layout();instrument.excluded_slots[0]=0u;
    (void)snprintf(command,sizeof command,"backup begin %u %u",count,thy_settings_crc32(candidate,count));
    assert(thy_instrument_app_backup_command(command,reply,sizeof reply)&&strstr(reply,"BACKUP ready")==reply);
    for(uint32_t at=0u;at<count;at+=32u){
        unsigned bytes=count-at;if(bytes>32u)bytes=32u;
        int prefix=snprintf(command,sizeof command,"backup put %u ",at);
        for(unsigned n=0;n<bytes;++n)(void)snprintf(command+prefix+n*2u,sizeof command-(unsigned)prefix-n*2u,"%02x",candidate[at+n]);
        assert(thy_instrument_app_backup_command(command,reply,sizeof reply)&&strstr(reply,"BACKUP received=")==reply);
    }
    core.owners[0].held=1u;
    assert(thy_instrument_app_backup_command("backup commit",reply,sizeof reply)&&strstr(reply,"ERR backup-busy")==reply);
    assert(core.owners[0].held&&high_level.scale==21u&&instrument.performance.root==0u);
    core.owners[0].held=0u;
    uint32_t before=thy_settings_crc32((const uint8_t *)&instrument,sizeof instrument);
    uint32_t expected_crc=backup_transfer.crc;backup_transfer.crc^=1u;
    unsigned starts=mock_pad_starts,stops=mock_pad_stops;
    assert(thy_instrument_app_backup_command("backup commit",reply,sizeof reply)&&strstr(reply,"ERR backup-crc-or-format")==reply);
    assert(before==thy_settings_crc32((const uint8_t *)&instrument,sizeof instrument));
    assert(mock_pad_starts==starts+1u && mock_pad_stops==stops+1u && mock_pad_active);
    backup_transfer.crc=expected_crc;
    mock_restore_delay_ms=25u;mock_restore_catalog_checks=0u;
    assert(thy_instrument_app_backup_command("backup commit",reply,sizeof reply)&&strstr(reply,"BACKUP applied")==reply);
    mock_restore_delay_ms=0u;
    assert(high_level.scale==22u&&instrument.performance.root==3u&&scale_index==271u);
    assert(layout[0].notes[0]==63&&instrument.excluded_slots[0]==2u&&board_touch_response_ms()==12u);
    size_t encoded_bytes=0u;assert(thy_settings_encode(&high_level,encoded,sizeof encoded,&encoded_bytes));
    assert(encoded_bytes==count-64u&&!memcmp(encoded,candidate+64u,encoded_bytes));
    assert(!core.queue_count&&!core.sounding_count&&!core.generated_count);
    settings.observed=1u;settings.changed_ms=123u;
    thy_instrument_app_console_reset();thy_instrument_app_console_reset();
    assert(thy_settings_transfer_busy(&backup_transfer)&&backup_transfer.restore_active);
    assert(settings.observed==1u&&settings.changed_ms==123u);
}

static void restore_during_main(void)
{
    static uint8_t file[THY_BACKUP_FILE_MAX];char reply[384],line[128];
    assert(musical_idle() && instrument.sensors[0].warmup_count==8u);
    instrument.center_poll_ms=12u;
    assert(thy_instrument_app_backup_command("backup export",reply,sizeof reply)&&strstr(reply,"BACKUP export ")==reply);
    uint32_t bytes=backup_transfer.bytes;
    memcpy(file,backup_transfer.header,64u);memcpy(file+64u,backup_transfer.buffer,bytes-64u);
    mock_restored_crc=backup_transfer.payload_crc;
    assert(thy_instrument_app_backup_command("backup abort",reply,sizeof reply)&&strstr(reply,"BACKUP aborted")==reply);
    instrument.center_poll_ms=8u;
    (void)snprintf(line,sizeof line,"backup begin %u %u",bytes,thy_settings_crc32(file,bytes));
    assert(thy_instrument_app_backup_command(line,reply,sizeof reply)&&strstr(reply,"BACKUP ready")==reply);
    for(uint32_t at=0u;at<bytes;at+=32u){
        unsigned count=bytes-at;if(count>32u)count=32u;
        int prefix=snprintf(line,sizeof line,"backup put %u ",at);
        for(unsigned n=0u;n<count;++n)(void)snprintf(line+prefix+n*2u,sizeof line-(unsigned)prefix-n*2u,"%02x",file[at+n]);
        assert(thy_instrument_app_backup_command(line,reply,sizeof reply)&&strstr(reply,"BACKUP received=")==reply);
    }
    assert(backup_transfer.restore_active&&!instrument.input_enabled&&!thy_instrument_app_ready());
    const thy_restore_progress *progress=thy_instrument_app_restore_progress();
    assert(progress->active&&progress->pads_locked&&progress->phase==THY_RESTORE_RECEIVING&&progress->completed==bytes&&progress->total==bytes);
    assert(!mock_pad_stops&&mock_pad_starts==1u);
}

static void commit_restore_during_main(void)
{
    char reply[384];
    assert(backup_transfer.restore_active&&!instrument.input_enabled&&!instrument.sensors[0].held);
    assert(instrument.performance.root==0u); /* Physical D was suppressed. */
    mock_pad_busy=1u;mock_pads.state=DIAG_PADS_ACQUIRE;
    mock_restore_delay_ms=25u;mock_restore_catalog_checks=0u;
    assert(thy_instrument_app_backup_command("backup commit",reply,sizeof reply)&&strstr(reply,"BACKUP applied ")==reply);
    mock_restore_delay_ms=0u;
    assert(mock_pad_stops==1u && mock_pad_starts==2u && mock_pad_stop_services>=2u);
    assert(mock_pad_active && !mock_pad_busy && !acquisition_failed && !last_scan);
    assert(instrument.center_poll_ms==12u && instrument.sensors[0].warmup_count==0u);
}

static void complete_restore_during_main(void)
{
    char reply[384],line[64];
    assert(backup_transfer.restore_active&&backup_transfer.restore_verified&&!instrument.input_enabled);
    assert(backup_transfer.restore_crc==mock_restored_crc);
    /* A faulted scanner cannot turn byte-correct readback into success. */
    acquisition_failed=true;
    (void)snprintf(line,sizeof line,"backup complete %u",mock_restored_crc);
    assert(thy_instrument_app_backup_command(line,reply,sizeof reply)&&strstr(reply,"ERR backup-complete")==reply);
    assert(backup_transfer.restore_active&&!backup_transfer.restore_host_verified&&!thy_instrument_app_ready());
    acquisition_failed=false; /* A separate healthy-admission test, not a production retry. */
    assert(thy_instrument_app_backup_command(line,reply,sizeof reply)&&strstr(reply,"BACKUP complete ")==reply);
    assert(!backup_transfer.restore_active&&backup_transfer.restore_host_verified);
    const thy_restore_progress *progress=thy_instrument_app_restore_progress();
    assert(progress->active&&!progress->pads_locked&&progress->phase==THY_RESTORE_SUCCESS);
}

static void restore_cleanup_failure_stays_latched(void)
{
    char reply[384];unsigned starts=mock_pad_starts;
    backup_transfer.state=THY_BACKUP_UPLOAD;backup_transfer.bytes=backup_transfer.received=64u;
    mock_pad_stop_stuck=1u;mock_pad_busy=1u;mock_pads.state=DIAG_PADS_ACQUIRE;
    unsigned services=mock_pad_stop_services;
    uint32_t before=thy_settings_crc32((const uint8_t *)&instrument,sizeof instrument);
    assert(thy_instrument_app_backup_command("backup commit",reply,sizeof reply)&&strstr(reply,"ERR backup-busy")==reply);
    assert(acquisition_failed && mock_pad_starts==starts && mock_pad_stop_services-services<=5u);
    assert(before==thy_settings_crc32((const uint8_t *)&instrument,sizeof instrument));
    services=mock_pad_stop_services;
    assert(thy_instrument_app_backup_command("backup commit",reply,sizeof reply)&&strstr(reply,"ERR backup-busy")==reply);
    assert(mock_pad_stop_services==services && mock_pad_starts==starts); /* No fault retry. */
}

#ifndef THY_INSTRUMENT_APP_TEST_MAIN
#define THY_INSTRUMENT_APP_TEST_MAIN main
#endif
int THY_INSTRUMENT_APP_TEST_MAIN(void)
{
    static const unsigned notes[] = {60u, 64u, 67u};
    memset(mock_settings_flash,255u,sizeof mock_settings_flash);
    run_case(1u, 325u);
    assert(instrument.presses == 2u && instrument.releases == 2u);
    assert(mock_din_count == 56u && !mock_usb_count);
    for (unsigned i = 0u; i < 3u; ++i) {
        din_message(48u + i, 0x90u, notes[i], 96u);
        din_message(51u + i, 0x80u, notes[i], 0u);
    }
    din_message(54u, 0x91u, 60u, 96u);
    din_message(55u, 0x81u, 60u, 0u);

    run_case(2u, 470u);
    assert(instrument.presses == 2u && instrument.releases == 2u);
    const unsigned cleanup_packets=2u*THY_INSTRUMENT_TRANSPORT_RECONCILE_EVENTS;
    assert(mock_din_count == 60u && mock_usb_count == cleanup_packets+6u);
    assert(transport.session_changes == 4u);
    for (unsigned trial = 0u; trial < 2u; ++trial)
        for (unsigned i = 0u; i < 3u; ++i) {
            din_message(48u + 6u * trial + i, 0x90u, notes[i], 96u);
            din_message(51u + 6u * trial + i, 0x80u, notes[i], 0u);
        }
    for (unsigned i = 0u; i < cleanup_packets; ++i)usb_cleanup_packet(i);
    for (unsigned i = 0u; i < 3u; ++i) {
        assert(mock_usb_packets[cleanup_packets + i][0] == 0x09u);
        assert(mock_usb_packets[cleanup_packets + i][1] == 0x90u);
        assert(mock_usb_packets[cleanup_packets + i][2] == notes[i]);
        assert(mock_usb_packets[cleanup_packets + i][3] == 96u);
        assert(mock_usb_packets[cleanup_packets + 3u + i][0] == 0x08u);
        assert(mock_usb_packets[cleanup_packets + 3u + i][1] == 0x80u);
        assert(mock_usb_packets[cleanup_packets + 3u + i][2] == notes[i]);
        assert(mock_usb_packets[cleanup_packets + 3u + i][3] == 0u);
    }
    run_case(3u, 470u);
    assert(instrument.presses == 2u && instrument.releases == 2u);
    assert(mock_din_count == 60u && !mock_usb_count);
    assert(mock_boot_skip_calls >= mock_limit);
    /* Real app boot restores an explicitly serialized committed route and slot
     * mask after catalog load. The flash mock is the only substituted layer. */
    assert(thy_instrument_set_pad_route(&instrument,1u,2u,5u)==THY_OK);
    assert(thy_instrument_set_route_velocity(&instrument,1u,94u)==THY_OK);
    assert(thy_instrument_set_lower_mode(&instrument,THY_LOWER_STORED_NOTES)==THY_OK);
    instrument.excluded_slots[0]=2u; instrument.slot_scope=1u;
    instrument.center_poll_ms=10u; instrument.light_key_contrast_percent=17u;
    thy_settings_journal saved; thy_settings_io backend;
    assert(thy_instrument_settings_io(&backend)); assert(thy_settings_journal_init(&saved,&backend,70u));
    thy_settings_journal_task(&saved,&high_level,1000u,1);
    thy_settings_journal_task(&saved,&high_level,2500u,1);
    for(unsigned n=0;n<2000u && saved.writing;++n) thy_settings_journal_task(&saved,&high_level,2501u+n,1);
    assert(saved.status==THY_SETTINGS_SAVED && saved.loaded);
    unsigned writes=mock_settings_programs;
    run_case(4u,345u);
    assert(mock_settings_programs==writes && settings.loaded && !settings.errors);
    assert(instrument.presses==1u && instrument.releases==1u && mock_din_count==52u);
    din_message(48u,0x92u,60u,94u);din_message(49u,0x92u,67u,94u);
    din_message(50u,0x82u,60u,0u);din_message(51u,0x82u,67u,0u);
    same_catalog_refresh_preserves_lower_preferences();
    memset(mock_settings_flash,255u,sizeof mock_settings_flash);
    run_case(5u,470u);
    assert(high_level.arps[0].attacks>=3u && !high_level.arps[0].failures);
    unsigned ons=0u,offs=0u;
    for(unsigned n=48u;n<mock_din_count;++n) {
        assert(mock_din_messages[n][0]==0x92u || mock_din_messages[n][0]==0x82u);
        if(mock_din_messages[n][0]==0x92u) ++ons; else ++offs;
    }
    assert(ons==offs && ons>=3u); /* Foreground timing, no direct CH1 chord copy. */
    memset(mock_settings_flash,255u,sizeof mock_settings_flash);
    run_case(6u,470u);
    assert(high_level.arps[0].attacks>=2u && !high_level.arps[0].failures);
    assert(high_level.arps[0].skipped>0u && !high_level.arps[0].held_count);
    calibration_callback_mapping_tests();
    backup_and_companion_integration();
    memset(mock_settings_flash,255u,sizeof mock_settings_flash);
    run_case(7u,4300u);
    assert(thy_instrument_app_ready() && instrument.input_enabled && !acquisition_failed);
    assert(instrument.presses==1u && instrument.releases==1u && mock_din_count==54u);
    for(unsigned n=0u;n<96u;++n)assert(instrument.sensors[n].warmup_count==8u);
    assert(settings.status==THY_SETTINGS_SAVED && !settings.errors && settings.saved_crc==mock_restored_crc);
    assert(backup_transfer.state==THY_BACKUP_SAVED_VERIFIED);
    assert(backup_transfer.restore_host_verified&&!backup_transfer.restore_active);
    memset(mock_settings_flash,255u,sizeof mock_settings_flash);
    run_case(8u,4300u);
    assert(thy_instrument_app_ready()&&instrument.input_enabled&&!acquisition_failed);
    assert(instrument.presses==1u&&instrument.releases==1u&&mock_din_count==54u);
    assert(settings.status==THY_SETTINGS_SAVED&&!settings.errors&&settings.saved_crc==mock_restored_crc);
    assert(backup_transfer.restore_verified&&!backup_transfer.restore_active&&!backup_transfer.restore_host_verified);
    assert(backup_transfer.restore_phase==THY_RESTORE_ERROR);
    restore_cleanup_failure_stays_latched();
    puts("instrument actual app tests passed: 9 scenarios, DIN standalone, per-pad warmup, USB isolation, fresh-note recovery, boot/settings restoration, timed Arps, deferred USB clock bursts, guarded restoration, suppressed inputs, host completion and disconnected durable saving");
    return 0;
}
