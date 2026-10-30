/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_engine.h"
#include "../../core/thy_settings.h"
#include "../../core/thy_studio_ui.h"
#include "../../core/thy_lighting.h"
static thy_core core;
static thy_instrument instrument;
static thy_engine engine;
static thy_instrument_pad layout[96];
static thy_seq_event history[THY_SEQ_EVENTS];
static uint8_t before[THY_SETTINGS_PAYLOAD_MAX],after[THY_SETTINGS_PAYLOAD_MAX];
static unsigned checks,scenarios;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static int init(void)
{
    int16_t degrees[]={0,2,4,5,7,9,11};thy_core_init(&core);thy_instrument_init(&instrument,&core);
    for(unsigned p=0;p<96u;++p)layout[p]=(thy_instrument_pad){{60,64,67},7u,1u};
    CHECK(thy_instrument_set_layout(&instrument,layout,60,degrees,7)==THY_OK);
    thy_engine_init(&engine,&instrument,21,70,history);thy_engine_bind_controls(&engine);
    CHECK(thy_state_set_design_mode(&engine.state,0)==THY_STATE_OK);return 1;
}
static uint16_t row(const char *id){return thy_state_find_row(id);}
static int open(const char *id)
{
    uint16_t r=row(id);CHECK(r<THY_MENU_ROW_COUNT);
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[r].group,r)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,r)==THY_STATE_OK);
    CHECK(engine.state.reset_open&&!engine.state.reset_confirm);return 1;
}
static int confirm(void){CHECK(thy_state_reset_resolve(&engine.state,engine.state.reset_epoch,1)==THY_STATE_OK);CHECK(!engine.state.reset_open);return 1;}
static int snapshot(uint8_t *out,size_t *used){CHECK(thy_settings_encode(&engine,out,THY_SETTINGS_PAYLOAD_MAX,used));return 1;}
static int terminal_rows_and_cancel(void)
{
    ++scenarios;CHECK(init());
    for(unsigned g=0;g<THY_MENU_GROUP_COUNT;++g){const thy_menu_group *group=&thy_menu_groups[g];
        if(g==thy_menu_rows[THY_ROW_ENGINES_PLAY].group){CHECK(group->row_count==6u);continue;}
        if(g==thy_menu_rows[THY_ROW_ENGINESETTINGS_HARMONY].group){CHECK(group->row_count==2u);continue;}
        CHECK(thy_state_reset_row((uint16_t)(group->first_row+group->row_count-1u)));
        CHECK(!strcmp(thy_menu_rows[group->first_row+group->row_count-1u].label,"Reset options"));}
    instrument.light_idle=7;instrument.performance.root=5;size_t n,m;CHECK(snapshot(before,&n));
    CHECK(open("lights.resetOptions"));uint32_t epoch=engine.state.reset_epoch;
    thy_studio_ui ui;thy_studio_ui_init(&ui);thy_studio_ui_boot_skip(&ui);thy_studio_ui_set_navigation_animations(&ui,0);
    uint8_t frame[THY_UI_FRAME_BYTES],expected[THY_UI_FRAME_BYTES]={0};
    thy_studio_ui_render(&ui,&engine.state,"MAJOR C",1,1,1,frame);
    thy_ui_draw_text(expected,4,36,"> CANCEL",20,0);
    CHECK((thy_studio_ui_scene_key(&engine.state)&UINT32_C(0xF0000))==UINT32_C(0xE0000));
    for(unsigned y=36;y<41u;++y)for(unsigned x=4;x<54u;++x)
        CHECK(!!(frame[(y/8u)*128u+x]&(1u<<(y%8u)))==!!(expected[(y/8u)*128u+x]&(1u<<(y%8u))));
    CHECK(thy_state_navigate(&engine.state,THY_STATE_RIGHT)==THY_STATE_OK);CHECK(!engine.state.reset_open);
    CHECK(snapshot(after,&m));CHECK(n==m&&!memcmp(before,after,n));
    CHECK(open("lights.resetOptions"));CHECK(engine.state.reset_epoch>epoch);
    CHECK(thy_state_reset_resolve(&engine.state,epoch,1)==THY_STATE_BLOCKED);
    CHECK(thy_state_set(&engine.state,THY_ROW_LIGHTS_IDLE,1)==THY_STATE_BLOCKED);
    CHECK(thy_state_navigate(&engine.state,THY_STATE_DOWN)==THY_STATE_OK&&engine.state.reset_confirm);
    CHECK(thy_state_navigate(&engine.state,THY_STATE_LEFT)==THY_STATE_OK&&!engine.state.reset_open);
    CHECK(open("lights.resetOptions"));CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK);
    CHECK(engine.state.level==THY_STATE_PLAY&&!engine.state.reset_open);CHECK(snapshot(after,&m));CHECK(n==m&&!memcmp(before,after,n));
    return 1;
}
static int lights_views_and_aliases(void)
{
    ++scenarios;CHECK(init());instrument.performance.root=7;instrument.light_idle=7;instrument.light_held=2;
    instrument.light_key_contrast=0;instrument.light_key_contrast_percent=88;instrument.center_brightness=1;
    CHECK(open("lights.resetOptions"));CHECK(confirm());
    thy_lighting_config cfg;thy_lighting_defaults(&cfg);
    CHECK(instrument.light_idle==cfg.idle_brightness&&instrument.light_held==cfg.held_brightness);
    CHECK(instrument.light_key_contrast==cfg.key_contrast&&instrument.light_key_contrast_percent==cfg.key_dim_percent);
    CHECK(instrument.performance.root==7&&instrument.center_brightness==8);
    CHECK(thy_state_set(&engine.state,THY_ROW_PERFORMANCE_KEYCONTRAST,0)==THY_STATE_OK);
    int32_t value=1;CHECK(thy_state_get(&engine.state,THY_ROW_LIGHTS_KEYCONTRAST,&value)==THY_STATE_OK&&!value);
    instrument.display_view=THY_VIEW_PIANO;instrument.display_config[THY_VIEW_PIANO].slots[0]=THY_VIEW_INFO_OFF;
    instrument.display_config[THY_VIEW_SIMPLE].slots[0]=THY_VIEW_INFO_OFF;
    CHECK(open("views.resetOptions"));CHECK(confirm());
    CHECK(instrument.display_view==THY_VIEW_PIANO&&instrument.display_config[THY_VIEW_PIANO].slots[0]==THY_VIEW_INFO_MUSICAL_NAMES);
    CHECK(instrument.display_config[THY_VIEW_SIMPLE].slots[0]==THY_VIEW_INFO_OFF);return 1;
}
static int scoped_arp_and_harmony(void)
{
    ++scenarios;CHECK(init());engine.arp_target=0;engine.arps[0].config.gate_percent=91;engine.arps[1].config.gate_percent=88;
    CHECK(open("arps.resetOptions"));engine.arp_target=1;CHECK(confirm());
    CHECK(engine.arps[0].config.gate_percent==50&&engine.arps[1].config.gate_percent==88);
    engine.harmony.pattern=3;engine.harmony.right_mode=1;engine.harmony.high_note=90;instrument.voicing_mode=2;
    CHECK(open("harmony.resetOptions"));CHECK(confirm());
    CHECK(engine.harmony.pattern==1&&engine.harmony.high_note==127&&!engine.harmony.right_mode&&!instrument.voicing_mode);
    CHECK(engine.arps[1].config.gate_percent==88);return 1;
}
static int controls_preserve_other_shortcuts_and_harmony(void)
{
    ++scenarios;CHECK(init());
    engine.shortcut_target=2u;instrument.scale_shortcuts[2]=69u;instrument.scale_shortcuts[3]=68u;
    engine.scale=17u;instrument.performance.root=5u;engine.harmony.pattern=3u;
    CHECK(open("controls.resetOptions"));engine.shortcut_target=3u;CHECK(confirm());
    CHECK(instrument.scale_shortcuts[2]==thy_instrument_default_scale_shortcut(2u));
    CHECK(instrument.scale_shortcuts[3]==68u);
    CHECK(engine.scale==17u&&instrument.performance.root==5u&&engine.harmony.pattern==3u);
    return 1;
}
static int quiet_failure_is_atomic(void)
{
    ++scenarios;CHECK(init());instrument.light_idle=7;CHECK(open("lights.resetOptions"));
    const int16_t notes[]={60,64,67};CHECK(thy_engine_press_shaped(&engine,0,notes,3,0,1,5,90,NULL)==THY_OK);
    thy_midi_event event;while(thy_core_poll(&core,&event)){}thy_pad_owner owner=core.owners[0];size_t n,m;
    CHECK(snapshot(before,&n));CHECK(thy_state_reset_resolve(&engine.state,engine.state.reset_epoch,1)==THY_STATE_BLOCKED);
    CHECK(engine.state.reset_open&&instrument.light_idle==7);CHECK(!memcmp(&owner,&core.owners[0],sizeof owner));
    CHECK(snapshot(after,&m));CHECK(n==m&&!memcmp(before,after,n));
    CHECK(instrument.musical_release(instrument.musical_context,0)==THY_OK);while(thy_core_poll(&core,&event)){}
    CHECK(confirm());CHECK(instrument.light_idle==3);
    CHECK(open("lights.resetOptions"));engine.rhythm_transport.state=THY_TRANSPORT_PLAYING;
    CHECK(thy_state_reset_resolve(&engine.state,engine.state.reset_epoch,1)==THY_STATE_BLOCKED);
    CHECK(engine.state.reset_open&&instrument.light_idle==3);engine.rhythm_transport.state=THY_TRANSPORT_STOPPED;
    CHECK(thy_state_reset_resolve(&engine.state,engine.state.reset_epoch,0)==THY_STATE_OK);return 1;
}
static int performance_and_midi_scopes(void)
{
    ++scenarios;CHECK(init());engine.velocity_target=1u;engine.route_target=2u;
    thy_instrument_velocity velocity;thy_instrument_velocity_defaults(&velocity);velocity.mode=THY_VELOCITY_STRIKE;
    CHECK(thy_instrument_set_cluster_velocity(&instrument,0,&velocity)==THY_OK);
    CHECK(thy_instrument_set_cluster_velocity(&instrument,1,&velocity)==THY_OK);
    instrument.pressure_enabled=1u;instrument.fixed_velocity=80u;
    engine.channel_routes[1]=(thy_engine_channel_config){3,2,24};engine.channel_routes[2]=(thy_engine_channel_config){3,1,12};
    CHECK(open("performance.resetOptions"));CHECK(confirm());
    CHECK(instrument.cluster_velocity[0].mode==THY_VELOCITY_FIXED&&instrument.cluster_velocity[1].mode==THY_VELOCITY_STRIKE);
    CHECK(instrument.fixed_velocity==80u&&engine.performance[0].velocity==96u&&!engine.performance[1].velocity);
    CHECK(engine.performance[0].aftertouch==1u&&!engine.performance[1].aftertouch);
    CHECK(thy_engine_channel_aftertouch(&engine,2)==THY_CHANNEL_PRESSURE_CHANNEL);
    CHECK(engine.channel_routes[1].bend==2u&&engine.channel_routes[1].range==24u);
    instrument.channels[0]=7;instrument.channels[1]=9;instrument.route_velocity[1]=77;instrument.route_velocity[2]=88;
    instrument.route_pressure[1]=2u;instrument.route_pressure[2]=1u;
    const thy_engine_channel_config left_response=engine.channel_routes[1],right_response=engine.channel_routes[2];
    engine.usb_input_velocity[2][60]=91u;
    engine.route_target=2u;CHECK(open("midi.resetOptions"));engine.route_target=1u;CHECK(confirm());
    CHECK(instrument.channels[0]==7&&instrument.channels[1]==1);
    CHECK(instrument.route_velocity[1]==77&&instrument.route_velocity[2]==88);
    CHECK(instrument.route_pressure[1]==2u&&instrument.route_pressure[2]==1u);
    CHECK(!memcmp(&engine.channel_routes[1],&left_response,sizeof left_response));
    CHECK(!memcmp(&engine.channel_routes[2],&right_response,sizeof right_response));
    CHECK(engine.musical_clock.source==THY_CLOCK_SOURCE_INTERNAL&&engine.musical_clock.tempo_milli_bpm==120000u);
    CHECK(thy_engine_channel_aftertouch(&engine,2)==THY_CHANNEL_PRESSURE_CHANNEL);
    CHECK(!engine.usb_input_velocity[2][60]);return 1;
}
static int system_reset_respects_partial_contacts(void)
{
    ++scenarios;CHECK(init());CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_OK);
    CHECK(open("system.resetOptions"));instrument.sensors[0].raw=instrument.sensors[0].response.release_raw+1u;
    instrument.sensors[0].delta=instrument.sensors[0].calibration.release_delta+1u;
    CHECK(thy_state_reset_resolve(&engine.state,engine.state.reset_epoch,1)==THY_STATE_BLOCKED);
    CHECK(engine.state.reset_open&&instrument.cluster_swap==1u);
    instrument.sensors[0].raw=instrument.sensors[0].delta=0u;CHECK(confirm());CHECK(instrument.cluster_swap==0u);return 1;
}
static int sensitivity_keeps_calibration_and_overrides(void)
{
    ++scenarios;CHECK(init());
    thy_instrument_response tuned={1500,1477,1353,4095,1};CHECK(thy_instrument_set_cluster_response(&instrument,1,&tuned)==THY_OK);
    thy_instrument_response custom={1600,1577,1353,4095,2};CHECK(thy_instrument_set_pad_response(&instrument,52,&custom)==THY_OK);
    instrument.sensitivity_guard[52]=900;instrument.sensors[52].calibration.baseline=123;
    CHECK(thy_state_set(&engine.state,THY_ROW_SENSITIVITY_TARGET,53)==THY_STATE_OK);
    CHECK(open("sensitivity.resetOptions"));CHECK(confirm());
    CHECK(instrument.cluster_response[1].attack_raw==1377);
    CHECK(instrument.sensors[52].response_overridden&&instrument.sensors[52].response.attack_raw==1600);
    CHECK(instrument.sensors[52].calibration.baseline==123&&instrument.sensitivity_guard[52]==900);
    CHECK(instrument.cluster_response[0].attack_raw==1377);return 1;
}
static int track_reset_keeps_material(void)
{
    ++scenarios;CHECK(init());CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_TRACKS)==THY_STATE_OK);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_CREATE_TRACK,0)==THY_OK);
    thy_seq_event note={.tick=480,.duration=240,.kind=THY_SEQ_NOTE,.pitch=67,.value=92};
    CHECK(thy_sequencer_add(&engine.sequencer,0,&note,1)==THY_OK);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS)==THY_STATE_OK);
    engine.sequencer.tracks[0].grid_ticks=480u;
    CHECK(thy_sequencer_ui_configure(&engine.sequencer_ui,THY_SEQ_UI_SELECTED_STEP,1)==THY_OK);
    CHECK(engine.sequencer_ui.selected_event==engine.sequencer.events[0].id);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_TRACK_OPTIONS,0)==THY_OK);
    thy_seq_event saved=engine.sequencer.events[0];engine.sequencer.tracks[0].channel=8;
    CHECK(thy_state_row_visible(&engine.state,THY_ROW_SEQUENCER_RESETTRACK));
    CHECK(!thy_state_row_visible(&engine.state,THY_ROW_SEQUENCER_RESETOPTIONS));
    CHECK(open("sequencer.resetTrack"));CHECK(engine.state.reset_track==0);CHECK(confirm());
    CHECK(engine.sequencer.count==1&&!memcmp(&saved,&engine.sequencer.events[0],sizeof saved));
    CHECK(engine.sequencer.tracks[0].channel==0&&engine.sequencer.tracks[0].loop_end==3840);
    CHECK(engine.sequencer_ui.options_open&&engine.state.level==THY_STATE_ROWS);
    CHECK(engine.sequencer_ui.selected_step==1&&!engine.sequencer_ui.selected_event);
    CHECK(open("sequencer.resetTrack"));
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK);
    CHECK(!engine.state.reset_open&&engine.sequencer_ui.open&&!engine.sequencer_ui.options_open);
    CHECK(engine.state.level==THY_STATE_PLAY);return 1;
}
int main(void)
{
    if(!terminal_rows_and_cancel()||!lights_views_and_aliases()||!scoped_arp_and_harmony()||
       !controls_preserve_other_shortcuts_and_harmony()||!quiet_failure_is_atomic()||!sensitivity_keeps_calibration_and_overrides()||!track_reset_keeps_material()||!performance_and_midi_scopes()||!system_reset_respects_partial_contacts())return 1;
    printf("%u scoped menu reset scenarios passed (%u assertions).\n",scenarios,checks);return 0;
}
