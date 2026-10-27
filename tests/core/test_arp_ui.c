/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_engine.h"
#include "../../core/thy_studio_ui.h"

static thy_core core;
static thy_instrument instrument;
static thy_instrument_pad pads[THY_PAD_COUNT];
static thy_engine engine, before;
static thy_studio_ui ui;
static unsigned assertions;
#define CHECK(x) do { ++assertions; if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 0; } } while (0)

static void initialize(void)
{
    static const int16_t degrees[] = {0,2,4,5,7,9,11};
    thy_core_init(&core); thy_instrument_init(&instrument,&core);
    for (unsigned n=0u;n<THY_PAD_COUNT;++n)
        pads[n]=(thy_instrument_pad){.notes={60,64,67},.valid_mask=7u,.color=1u};
    (void)thy_instrument_set_layout(&instrument,pads,60,degrees,7u);
    thy_engine_init(&engine,&instrument,21u,70u);
    thy_engine_bind_controls(&engine);
    (void)thy_state_set_design_mode(&engine.state,0u);
    (void)thy_engine_advance(&engine,0u);
    thy_studio_ui_init(&ui); ui.animations_enabled=0u;
}
static int set(uint16_t row,int32_t value) { return thy_state_set(&engine.state,row,value); }
static int32_t get(uint16_t row)
{
    int32_t value=INT32_MIN;
    (void)thy_state_get(&engine.state,row,&value); return value;
}
static int tap(unsigned index)
{
    int first=thy_instrument_control(&instrument,3u,(uint8_t)index,1);
    int last=thy_instrument_control(&instrument,3u,(uint8_t)index,0);
    return first==THY_OK&&last==THY_OK;
}

static int menu_configures_real_independent_arps(void)
{
    initialize();
    CHECK(engine.arp_target==0u&&engine.arp_quick_keys==1u);
    CHECK(get(THY_ROW_ARPS_CHANNEL)==3);
    CHECK(set(THY_ROW_ARPS_ENABLED,1)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_TYPE,THY_ARP_CHORD)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_GROUPING,THY_ARP_EACH)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_SOURCEZONES,1)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_SOURCEZONE6,1)==THY_STATE_OK);
    CHECK(engine.arps[0].config.source_zones==33u);
    CHECK(set(THY_ROW_ARPS_NOTEVALUE,5)==THY_STATE_OK);
    CHECK(engine.arps[0].config.rate_ticks==480u);
    CHECK(set(THY_ROW_ARPS_RATETICKS,333)==THY_STATE_OK);
    CHECK(get(THY_ROW_ARPS_NOTEVALUE)==9);
    CHECK(set(THY_ROW_ARPS_GATE,83)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_SWING,17)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_LAYER,1)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_SYNC,THY_ARP_GRID)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_RESTART,1)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_PATTERN,THY_ARP_AS_PLAYED)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_STEP3,0)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_OFFSET8,-24)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_CHANNEL,16)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_DESTINATIONS,6)==THY_STATE_OK);
    CHECK(engine.arps[0].config.destinations==7u&&engine.arps[0].config.channel==15u);
    CHECK(engine.arps[0].config.offsets[7]==-24&&engine.arps[0].config.step_mask==251u);
    CHECK(set(THY_ROW_ARPS_TARGET,1)==THY_STATE_OK);
    CHECK(!engine.arps[1].config.enabled&&get(THY_ROW_ARPS_GATE)==50);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_ARPS_GATE)==THY_STATE_OK);
    CHECK(thy_state_edit_set(&engine.state,61)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_TARGET,0)==THY_STATE_OK);
    CHECK(thy_state_commit(&engine.state)==THY_STATE_OK);
    CHECK(engine.arps[1].config.gate_percent==61u&&get(THY_ROW_ARPS_GATE)==83);
    before=engine;
    CHECK(set(THY_ROW_ARPS_GATE,101)==THY_STATE_INVALID);
    CHECK(set(THY_ROW_ARPS_OFFSET8,-25)==THY_STATE_INVALID);
    CHECK(engine.state.revision==before.state.revision);
    CHECK(memcmp(&engine.arps,&before.arps,sizeof engine.arps)==0);
    CHECK(!thy_state_row_visible(&engine.state,THY_ROW_ARPS_RELEASETAIL));
    CHECK(!thy_state_row_visible(&engine.state,THY_ROW_ARPS_KEEPKEY));
    CHECK(thy_state_row_visible(&engine.state,THY_ROW_ARPS_TRIGGER));
    CHECK(strcmp(thy_state_get_text(&engine.state,THY_ROW_ARPS_TRIGGER),"Hold only; Latch unavailable")==0);
    return 1;
}

static int quick_navigation_and_temporary_keys(void)
{
    initialize();
    instrument.play_nav[3]=THY_PLAY_ARPS;
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(engine.arp_quick_open);
    uint32_t transition=engine.state.transition_revision;
    CHECK(thy_engine_navigate(&engine,THY_STATE_DOWN)==THY_STATE_OK);
    CHECK(engine.arp_quick_cursor==1u&&engine.state.transition_revision==transition);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_UP)==THY_STATE_OK);
    CHECK(!engine.arps[0].config.enabled);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(engine.arps[0].config.enabled&&engine.state.transition_revision==transition);
    uint8_t shortcut=instrument.performance.shortcut;
    CHECK(tap(2u)); CHECK(!engine.arps[0].config.enabled);
    CHECK(tap(3u)); CHECK(engine.arps[1].config.enabled);
    CHECK(instrument.performance.shortcut==shortcut);
    CHECK(tap(4u)); CHECK(engine.state.notice==THY_NOTICE_HOLD_ONLY);
    CHECK(tap(5u)); CHECK(engine.state.notice==THY_NOTICE_HOLD_ONLY);
    CHECK(!engine.arps[0].config.enabled&&engine.arps[1].config.enabled);
    CHECK(tap(8u)); CHECK(engine.arp_target==1u);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_UP)==THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(!engine.state.editing&&engine.arp_quick_open&&engine.arps[1].config.enabled);
    CHECK(set(THY_ROW_ARPS_QUICKKEYS,0)==THY_STATE_OK);
    CHECK(tap(2u)); CHECK(instrument.performance.shortcut==2u);
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK);
    CHECK(!engine.arp_quick_open&&engine.state.level==THY_STATE_PLAY);
    CHECK(thy_engine_control(&engine,3u,3u,1)==0);
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK);
    CHECK(engine.state.level==THY_STATE_GROUPS);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_ARPS_OPEN)==THY_STATE_OK);
    CHECK(engine.arp_quick_open&&engine.state.level==THY_STATE_PLAY);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(!engine.arp_quick_open);
    return 1;
}

static int clear_is_scoped_and_clock_menu_is_live(void)
{
    initialize();
    CHECK(set(THY_ROW_ARPS_ENABLED,1)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_TARGET,1)==THY_STATE_OK);
    CHECK(set(THY_ROW_ARPS_ENABLED,1)==THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument,0u)==THY_OK);
    CHECK(engine.arps[0].held_count==1u&&engine.arps[1].held_count==1u);
    thy_arp_config saved=engine.arps[1].config;
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_ARPS_CLEAR)==THY_STATE_OK);
    CHECK(engine.arps[0].held_count==1u&&!engine.arps[1].held_count);
    CHECK(memcmp(&saved,&engine.arps[1].config,sizeof saved)==0);
    CHECK(thy_instrument_release_pad(&instrument,0u)==THY_OK);
    CHECK(set(THY_ROW_MIDI_TEMPO,137)==THY_STATE_OK);
    CHECK(engine.musical_clock.tempo_milli_bpm==137000u);
    CHECK(set(THY_ROW_MIDI_CLOCK,THY_CLOCK_SOURCE_USB)==THY_STATE_OK);
    CHECK(engine.musical_clock.status==THY_CLOCK_WAITING);
    CHECK(set(THY_ROW_MIDI_TEMPO,81)==THY_STATE_OK);
    CHECK(engine.musical_clock.source==THY_CLOCK_SOURCE_USB&&engine.musical_clock.tempo_milli_bpm==81000u);
    CHECK(set(THY_ROW_MIDI_METERNUMERATOR,7)==THY_STATE_OK);
    CHECK(set(THY_ROW_MIDI_METERDENOMINATOR,3)==THY_STATE_OK);
    CHECK(engine.musical_clock.numerator==7u&&engine.musical_clock.denominator==8u);
    return 1;
}

static int quick_renderer_uses_runtime_without_mutation(void)
{
    struct { uint8_t pre[16],frame[THY_UI_FRAME_BYTES],post[16]; } pixels;
    uint8_t first[THY_UI_FRAME_BYTES];
    initialize(); CHECK(thy_engine_arp_quick_open(&engine)==THY_STATE_OK);
    memset(&pixels,0xa5,sizeof pixels); before=engine;
    thy_studio_ui_render(&ui,&engine.state,"MAJOR C",1,1,0u,pixels.frame);
    CHECK(memcmp(&before,&engine,sizeof engine)==0);
    for (unsigned n=0u;n<16u;++n) CHECK(pixels.pre[n]==0xa5u&&pixels.post[n]==0xa5u);
    memcpy(first,pixels.frame,sizeof first);
    engine.arps[0].phrases[0].active=1u; engine.arps[0].phrases[0].ordinal=3u;
    thy_studio_ui_render(&ui,&engine.state,"MAJOR C",1,1,1u,pixels.frame);
    CHECK(memcmp(first,pixels.frame,sizeof first)!=0);
    memcpy(first,pixels.frame,sizeof first);
    CHECK(set(THY_ROW_ARPS_QUICKKEYS,0)==THY_STATE_OK);
    thy_studio_ui_render(&ui,&engine.state,"MAJOR C",1,1,2u,pixels.frame);
    CHECK(memcmp(first,pixels.frame,sizeof first)!=0);
    return 1;
}

int main(void)
{
    if (!menu_configures_real_independent_arps()||!quick_navigation_and_temporary_keys()||
        !clear_is_scoped_and_clock_menu_is_live()||!quick_renderer_uses_runtime_without_mutation()) return 1;
    printf("4 Arp menu/quick-control scenarios passed (%u assertions).\n",assertions); return 0;
}
