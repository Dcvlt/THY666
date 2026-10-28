/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_engine.h"
#include "../../core/thy_lighting.h"

static thy_core core;
static thy_instrument instrument;
static thy_engine engine;
static thy_instrument_pad layout[96];
static thy_seq_event history[THY_SEQ_EVENTS];
static thy_instrument_sensor saved_sensors[96];
static uint8_t saved_mapping[96],saved_velocity_flags[96];
static thy_instrument_velocity saved_velocity[96];
static uint16_t saved_guards[96];
static thy_midi_event events[THY_QUEUE_CAPACITY];
static thy_lighting lighting;
static uint32_t before_rgb[96],after_rgb[96];
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)

static unsigned drain(void)
{unsigned n=0;while(n<THY_QUEUE_CAPACITY&&thy_core_poll(&core,&events[n]))++n;return n;}
static void init(int bind)
{
    const int16_t degrees[]={0,2,4,5,7,9,11};
    thy_core_init(&core);thy_instrument_init(&instrument,&core);
    for(unsigned p=0;p<96u;++p)layout[p]=(thy_instrument_pad){.notes={60,64,67},.valid_mask=p<48u?7u:1u,.color=(uint8_t)(p/48u)};
    for(unsigned p=48u;p<96u;++p)layout[p].notes[0]=74;
    (void)thy_instrument_set_layout(&instrument,layout,60,degrees,7);
    (void)thy_instrument_set_route(&instrument,0,2,THY_ROUTE_USB0,0);
    (void)thy_instrument_set_route(&instrument,1,8,THY_ROUTE_USB0,0);
    if(bind){thy_engine_init(&engine,&instrument,0,70,history);(void)thy_engine_advance(&engine,0);}
    drain();
}

static int physical_profiles_and_routes_follow_different_coordinates(void)
{
    init(0);
    uint8_t mapping[96];for(unsigned p=0;p<96u;++p)mapping[p]=(uint8_t)((p/48u)*48u+47u-p%48u);
    CHECK(thy_instrument_set_mapping(&instrument,mapping)==THY_OK);
    thy_instrument_velocity velocity=instrument.cluster_velocity[0];velocity.mode=THY_VELOCITY_STRIKE;velocity.soft_rate=3u;
    CHECK(thy_instrument_set_pad_velocity(&instrument,5,&velocity)==THY_OK);
    instrument.sensitivity_guard[5]=1200u;
    memcpy(saved_sensors,instrument.sensors,sizeof saved_sensors);
    memcpy(saved_mapping,instrument.mapping,sizeof saved_mapping);
    memcpy(saved_velocity,instrument.pad_velocity,sizeof saved_velocity);
    memcpy(saved_velocity_flags,instrument.velocity_overridden,sizeof saved_velocity_flags);
    memcpy(saved_guards,instrument.sensitivity_guard,sizeof saved_guards);
    thy_instrument_response left=instrument.cluster_response[0],right=instrument.cluster_response[1];
    CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_OK);
    CHECK(!memcmp(saved_sensors,instrument.sensors,sizeof saved_sensors));
    CHECK(!memcmp(saved_mapping,instrument.mapping,sizeof saved_mapping));
    CHECK(!memcmp(saved_velocity,instrument.pad_velocity,sizeof saved_velocity));
    CHECK(!memcmp(saved_velocity_flags,instrument.velocity_overridden,sizeof saved_velocity_flags));
    CHECK(!memcmp(saved_guards,instrument.sensitivity_guard,sizeof saved_guards));
    CHECK(!memcmp(&left,&instrument.cluster_response[0],sizeof left));
    CHECK(!memcmp(&right,&instrument.cluster_response[1],sizeof right));
    for(unsigned p=0;p<96u;++p){
        CHECK(thy_instrument_role_pad(&instrument,(uint8_t)p)==(p+48u)%96u);
        CHECK(thy_instrument_physical_pad(&instrument,thy_instrument_role_pad(&instrument,(uint8_t)p))==p);
    }
    CHECK(thy_instrument_role_pad(NULL,0)==UINT8_MAX);
    CHECK(thy_instrument_physical_pad(&instrument,96)==UINT8_MAX);
    CHECK(thy_instrument_set_cluster_swap(&instrument,2)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);
    CHECK(core.owners[0].held&&core.owners[0].count==1u);
    CHECK(drain()==1u&&events[0].status==0x98u&&events[0].data1==74u);
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);
    CHECK(core.owners[48].held&&core.owners[48].count==3u);
    CHECK(drain()==3u);for(unsigned n=0;n<3u;++n)CHECK(events[n].status==0x92u);
    CHECK(thy_instrument_set_cluster_swap(&instrument,0)==THY_INVALID_ARGUMENT);
    CHECK(instrument.cluster_swap==1u&&!memcmp(saved_mapping,instrument.mapping,sizeof saved_mapping));
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);
    CHECK(drain()==1u&&events[0].status==0x88u&&events[0].data1==74u);
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);
    CHECK(drain()==3u);for(unsigned n=0;n<3u;++n)CHECK(events[n].status==0x82u);
    CHECK(thy_instrument_set_cluster_swap(&instrument,0)==THY_OK);
    return 1;
}

static int quiet_swap_rejects_contacts_outputs_and_adapter_activity(void)
{
    init(1);
    instrument.sensors[7].blocked=1u;
    CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_INVALID_ARGUMENT);
    instrument.sensors[7].blocked=0u;instrument.sensors[7].attack_count=1u;
    CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_INVALID_ARGUMENT);
    instrument.sensors[7].attack_count=0u;
    engine.arps[0].inputs[7].held=1u;
    CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_INVALID_ARGUMENT);
    engine.arps[0].inputs[7].held=0u;
    engine.sequencer_ui.pads[7].held=1u;
    CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_INVALID_ARGUMENT);
    engine.sequencer_ui.pads[7].held=0u;
    CHECK(thy_core_sustain(&core,THY_ROUTE_USB0,2,127)==THY_OK);drain();
    CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_INVALID_ARGUMENT);
    CHECK(thy_core_sustain(&core,THY_ROUTE_USB0,2,0)==THY_OK);
    CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_INVALID_ARGUMENT); /* queued cleanup */
    drain();CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_OK);
    return 1;
}

static int physical_preview_holes_context_and_rgb_follow_roles(void)
{
    init(0);thy_lighting_config config;thy_lighting_defaults(&config);
    config.fade_ms=0;config.key_contrast=0;config.selected_enabled=0;config.chord_color_mode=THY_LIGHTING_CHORD_BASS;
    thy_lighting_init(&lighting);
    CHECK(thy_lighting_render(&lighting,&instrument,&config,0,before_rgb)==THY_OK);
    CHECK(before_rgb[0]!=before_rgb[48]);
    CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_OK);
    CHECK(thy_lighting_render(&lighting,&instrument,&config,1,after_rgb)==THY_OK);
    for(unsigned p=0;p<96u;++p)CHECK(after_rgb[p]==before_rgb[(p+48u)%96u]);
    instrument.performance.melody_layout=THY_LAYOUT_PIANO;
    int16_t notes[THY_MAX_NOTES];size_t count=99u;
    CHECK(thy_instrument_preview_pad(&instrument,0,notes,&count)==THY_OK&&count==0u);
    CHECK(thy_instrument_preview_pad(&instrument,4,notes,&count)==THY_OK&&count==1u&&notes[0]==61);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK&&drain()==0u);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain();
    CHECK(thy_instrument_set_context_mask(&instrument,1u)==THY_OK);
    CHECK(thy_instrument_set_context_controls(&instrument,1u)==THY_OK);
    CHECK(thy_instrument_preview_pad(&instrument,80,notes,&count)==THY_OK&&count==0u);
    CHECK(thy_instrument_press_pad(&instrument,80)==THY_OK&&instrument.context_held[80]);
    CHECK(instrument.performance.chord_octave==-1&&drain()==0u);
    CHECK(thy_instrument_set_cluster_swap(&instrument,0)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_release_pad(&instrument,80)==THY_OK);
    CHECK(thy_instrument_set_cluster_swap(&instrument,0)==THY_OK);
    return 1;
}

static int swapped_sequencer_editor_and_arp_use_role_ids(void)
{
    init(1);CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_OK);
    CHECK(thy_sequencer_create(&engine.sequencer,0)==THY_OK);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS)==THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument,52)==THY_OK);
    CHECK(engine.sequencer_ui.selected_step==1u&&!core.owners[52].held&&drain()==0u);
    CHECK(thy_instrument_release_pad(&instrument,52)==THY_OK);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);
    CHECK(core.owners[0].held&&engine.input_assignment[0]);
    CHECK(engine.sequencer.count==1u&&engine.sequencer.events[0].pitch==74u);
    CHECK(engine.sequencer_ui.pads[48].held);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain();
    uint16_t rgb[3];CHECK(thy_engine_sequencer_color(&engine,52,rgb));
    CHECK(rgb[0]||rgb[1]||rgb[2]);
    CHECK(thy_engine_sequencer_close(&engine)==THY_STATE_OK);drain();
    thy_arp_config cfg=engine.arps[0].config;cfg.enabled=1;cfg.layer=0;cfg.source_zones=1u;
    CHECK(thy_engine_arp_configure(&engine,0,&cfg)==THY_OK);drain();
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);
    CHECK(engine.arps[0].inputs[48].held&&core.owners[48].held&&!core.owners[48].count);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);
    CHECK(!engine.arps[0].inputs[0].held&&core.owners[0].count==1u);
    CHECK(thy_instrument_set_cluster_swap(&instrument,0)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain();
    CHECK(!core.sounding_count&&!core.generated_count);
    return 1;
}

static int melody_plan(void *context,uint8_t role,int16_t *out)
{int *mode=context;if(role<48u)return -1;if(*mode<0)return -1;if(!*mode)return 0;*out=79;return 1;}
static int voicing_plan(void *context,uint8_t role,const int16_t notes[7],uint8_t mask,int16_t out[7])
{int *mode=context;if(role>=48u)return -1;if(*mode<0)return -1;if(!*mode)return 0;for(unsigned n=0;n<7u;++n)out[n]=(mask&(1u<<n))?(int16_t)(notes[n]-12):notes[n];return 1;}
static int neutral_planning_callbacks_match_actual_attacks(void)
{
    init(0);int mode=1;instrument.musical_context=&mode;
    instrument.musical_melody_plan=melody_plan;instrument.musical_voicing_plan=voicing_plan;
    instrument.voicing_mode=2u;
    CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_OK);
    int16_t notes[THY_MAX_NOTES];size_t count=0;
    CHECK(thy_instrument_preview_pad(&instrument,0,notes,&count)==THY_OK&&count==1u&&notes[0]==79);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK&&drain()==1u&&events[0].data1==79);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain();
    CHECK(thy_instrument_preview_pad(&instrument,48,notes,&count)==THY_OK&&count==3u&&notes[0]==48&&notes[1]==52&&notes[2]==55);
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK&&drain()==3u&&events[0].data1==48);
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);drain();
    mode=-1;count=19u;notes[0]=-123;
    CHECK(thy_instrument_preview_pad(&instrument,0,notes,&count)==THY_INVALID_ARGUMENT&&count==19u&&notes[0]==-123);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_INVALID_ARGUMENT&&!core.owners[0].held&&drain()==0u);
    mode=0;CHECK(thy_instrument_preview_pad(&instrument,0,notes,&count)==THY_OK&&count==1u&&notes[0]==74);
    return 1;
}

int main(void)
{
    if(!physical_profiles_and_routes_follow_different_coordinates()||
       !quiet_swap_rejects_contacts_outputs_and_adapter_activity()||
       !physical_preview_holes_context_and_rgb_follow_roles()||
       !swapped_sequencer_editor_and_arp_use_role_ids()||
       !neutral_planning_callbacks_match_actual_attacks())return 1;
    printf("5 cluster role swap scenarios passed (%u assertions).\n",checks);
    return 0;
}
