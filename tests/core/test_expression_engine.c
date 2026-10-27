/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "thy_engine.h"
#include "thy_settings.h"
static thy_core core;
static thy_instrument instrument;
static thy_engine engine;
static thy_mpe mpe;
static thy_channel_expression ordinary;
static thy_instrument_pad layout[96];
static unsigned assertions;
#define CHECK(x) do { ++assertions; if(!(x)) {fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);return 0;} } while(0)
static void init(void)
{
    thy_core_init(&core);thy_instrument_init(&instrument,&core);
    thy_engine_init(&engine,&instrument,21,70);thy_engine_bind_expression(&engine,&mpe);
}
static unsigned drain(unsigned kind)
{
    thy_midi_event event;unsigned n=0;
    while(thy_core_poll(&core,&event))if(!kind || (event.status&0xf0)==kind)++n;
    return n;
}
static int setup(void)
{
    init();CHECK(!engine.expression_config.enabled && !drain(0));
    thy_mpe_config c=engine.expression_config;c.enabled=1;
    CHECK(thy_engine_expression_configure(&engine,&c)==THY_OK);
    CHECK(!drain(0));CHECK(engine.expression_config.enabled && mpe.config.enabled);
    CHECK(thy_engine_transport_start(&engine)==THY_SEQ_BUSY);
    const uint8_t start=0xfau, resume=0xfbu;
    CHECK(thy_engine_receive_time(&engine,&start,1,0)==THY_SEQ_BUSY);
    CHECK(thy_engine_receive_time_deferred(&engine,&resume,1,0)==THY_SEQ_BUSY);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_TRACKS)==THY_STATE_BLOCKED);
    thy_arp_config arp=engine.arps[0].config;arp.enabled=1;
    CHECK(thy_engine_arp_configure(&engine,0,&arp)==THY_SEQ_BUSY);
    CHECK(thy_engine_advance(&engine,0)==THY_OK);
    thy_generated_handle handle=99;
    CHECK(thy_engine_generated_acquire(&engine,THY_ENGINE_ARP1,
        thy_engine_source_generation(&engine,THY_ENGINE_ARP1),60,0,5,96,&handle)==THY_SEQ_BUSY);
    CHECK(handle==99);return 1;
}
static int chord_pressure_routes_and_panic(void)
{
    CHECK(setup());const int16_t chord[]={60,64,67};
    CHECK(thy_engine_press_shaped(&engine,0,chord,3,0,0,5,96,NULL)==THY_OK);
    CHECK(thy_mpe_active(&mpe)==3 && thy_core_sounding(&core)==6);
    CHECK(drain(0x90)==6);
    CHECK(thy_engine_expression_pressure(&engine,0,100)==THY_OK);
    CHECK(!drain(0));CHECK(thy_engine_advance(&engine,8)==THY_OK);
    CHECK(drain(0xd0)==4);CHECK(thy_engine_advance(&engine,16)==THY_OK);
    CHECK(drain(0xd0)==2);
    thy_mpe_config changed=engine.expression_config;changed.upper_zone=1;
    CHECK(thy_engine_expression_configure(&engine,&changed)==THY_SEQ_BUSY);
    CHECK(thy_instrument_set_route(&instrument,0,9,1,0)==THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);
    CHECK(drain(0x80)==6);
    CHECK(thy_mpe_active(&mpe)==0);
    CHECK(thy_engine_expression_configure(&engine,&changed)==THY_OK);
    CHECK(thy_engine_press_shaped(&engine,4,chord,3,0,0,5,96,NULL)==THY_OK);
    drain(0);thy_engine_panic(&engine);CHECK(thy_core_sounding(&core)==0);
    drain(0);CHECK(thy_engine_advance(&engine,24)==THY_OK);drain(0);
    CHECK(thy_mpe_active(&mpe)==0 && thy_mpe_quiet(&mpe));return 1;
}
static int modifier_capture_and_live_only(void)
{
    CHECK(setup());thy_mpe_config c=engine.expression_config;c.glide=1;
    CHECK(thy_engine_expression_configure(&engine,&c)==THY_OK);
    const int16_t chord[]={60,64,67}, neighbor[]={62,65,69};
    CHECK(thy_engine_press_shaped(&engine,0,chord,3,0,0,5,96,NULL)==THY_OK);
    drain(0);CHECK(thy_engine_expression_pressure(&engine,0,80)==THY_OK);
    CHECK(thy_engine_press_shaped(&engine,4,neighbor,3,0,0,5,96,NULL)==THY_OK);
    CHECK(thy_mpe_consumed(&mpe,4) && thy_mpe_active(&mpe)==3 && !drain(0x90));
    CHECK(thy_engine_expression_pressure(&engine,4,80)==THY_OK);
    CHECK(thy_engine_advance(&engine,8)==THY_OK);drain(0);
    for(unsigned n=0;n<15;n++)if(mpe.voices[n].state==THY_MPE_HELD)
        CHECK(mpe.voices[n].bend==8362 || mpe.voices[n].bend==8363);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain(0);
    CHECK(thy_mpe_consumed(&mpe,4));
    CHECK(thy_engine_expression_configure(&engine,&c)==THY_SEQ_BUSY);
    CHECK(thy_instrument_release_pad(&instrument,4)==THY_OK);
    CHECK(!thy_mpe_consumed(&mpe,4) && !thy_core_sounding(&core));
    CHECK(thy_engine_expression_configure(&engine,&c)==THY_OK);return 1;
}
static int alternating_strum_keeps_glide_reference(void)
{
    CHECK(setup());thy_mpe_config c=engine.expression_config;c.glide=1;
    CHECK(thy_engine_expression_configure(&engine,&c)==THY_OK);
    const int16_t chord[]={60,64,67}, neighbor[]={62,65,69};
    thy_shape shape={0};shape.strum_order=THY_STRUM_ALTERNATING;shape.strum_ms=25;
    CHECK(thy_engine_press_shaped(&engine,0,chord,3,0,0,5,96,&shape)==THY_OK);
    drain(0);CHECK(core.alternating_down==1);
    CHECK(thy_engine_press_shaped(&engine,4,neighbor,3,0,0,5,96,&shape)==THY_OK);
    CHECK(thy_mpe_consumed(&mpe,4) && core.alternating_down==1 && thy_mpe_active(&mpe)==3);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,4)==THY_OK);return 1;
}
static int sensor_pressure_callback_and_standard_compatibility(void)
{
    CHECK(setup());const int16_t note=60;
    CHECK(thy_engine_press_shaped(&engine,0,&note,1,0,0,4,96,NULL)==THY_OK);drain(0);
    CHECK(!instrument.held_pressure_enabled[0]);
    CHECK(instrument.musical_pressure(instrument.musical_context,0,73,73)==THY_OK);
    CHECK(thy_engine_advance(&engine,8)==THY_OK);CHECK(drain(0xd0)==1);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain(0);
    thy_mpe_config c=engine.expression_config;c.enabled=0;
    CHECK(thy_engine_expression_configure(&engine,&c)==THY_OK);
    CHECK(thy_engine_press_shaped(&engine,0,&note,1,0,7,4,96,NULL)==THY_OK);drain(0);
    CHECK(instrument.musical_pressure(instrument.musical_context,0,73,73)==THY_OK);CHECK(!drain(0));
    instrument.held_pressure_enabled[0]=1;
    CHECK(instrument.musical_pressure(instrument.musical_context,0,73,73)==THY_OK);CHECK(drain(0xa0)==1);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);return 1;
}
static int ordinary_capture_and_independent_axes(void)
{
    init();thy_engine_bind_channel_expression(&engine,&ordinary);
    CHECK(!instrument.pressure_enabled); /* Explicit applications need no legacy master. */
    thy_engine_channel_config policy={3,1,2};CHECK(thy_engine_channel_configure(&engine,1,&policy)==THY_OK);
    const int16_t note=60;
    CHECK(thy_engine_press_shaped(&engine,0,&note,1,0,3,5,96,NULL)==THY_OK);drain(0);
    CHECK(thy_engine_expression_pressure_axes(&engine,0,64,100)==THY_OK);
    CHECK(thy_engine_advance(&engine,8)==THY_OK);CHECK(drain(0xd0)==2);
    CHECK(ordinary.sent_pressure[3]==100 && ordinary.sent_pressure[35]==100);
    CHECK(ordinary.sent_bend[3]==12319 && ordinary.sent_bend[35]==12319);
    CHECK(thy_engine_press_shaped(&engine,4,&note,1,0,3,5,96,NULL)==THY_OK);drain(0);
    CHECK(thy_engine_expression_pressure_axes(&engine,4,127,40)==THY_OK);
    CHECK(thy_engine_advance(&engine,16)==THY_OK);drain(0);
    CHECK(ordinary.sent_pressure[3]==100 && ordinary.sent_bend[3]==16383);
    policy=(thy_engine_channel_config){1,2,2};CHECK(thy_engine_channel_configure(&engine,1,&policy)==THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,4)==THY_OK);drain(0);
    CHECK(thy_engine_advance(&engine,24)==THY_OK);drain(0);
    CHECK(ordinary.sent_pressure[3]==100 && ordinary.sent_bend[3]==12319);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain(0);
    CHECK(thy_engine_advance(&engine,32)==THY_OK);CHECK(drain(0xd0)==2);
    CHECK(ordinary.sent_pressure[3]==0 && ordinary.sent_bend[3]==8192 && thy_engine_channel_quiet(&engine));
    return 1;
}
static int raw_dual_stage_feeds_mpe_and_preserves_release(void)
{
    init();thy_engine_bind_channel_expression(&engine,&ordinary);
    for(unsigned p=0;p<96u;++p){layout[p]=(thy_instrument_pad){.notes={60,64,67},.valid_mask=7,.color=1};}
    const int16_t degrees[]={0,2,4,5,7,9,11};
    CHECK(thy_instrument_set_layout(&instrument,layout,60,degrees,7)==THY_OK);
    thy_instrument_response response={200,150,100,1100,0};
    CHECK(thy_instrument_set_response(&instrument,&response)==THY_OK);
    thy_expression_profile profile={1,50,0,0};
    CHECK(thy_instrument_set_cluster_expression_profile(&instrument,0,&profile)==THY_OK);
    thy_mpe_config config=engine.expression_config;config.enabled=1;
    CHECK(thy_engine_expression_configure(&engine,&config)==THY_OK);
    for(unsigned now=1;now<=8;++now)CHECK(thy_instrument_sample(&instrument,0,20,now)==THY_OK);
    CHECK(thy_instrument_sample(&instrument,0,300,10)==THY_OK);
    CHECK(thy_instrument_sample(&instrument,0,300,11)==THY_OK);
    CHECK(thy_mpe_active(&mpe)==3 && drain(0x90)==3);
    CHECK(thy_instrument_sample(&instrument,0,300,12)==THY_OK);
    CHECK(thy_engine_advance(&engine,16)==THY_OK);drain(0);
    CHECK(thy_instrument_sample(&instrument,0,850,17)==THY_OK);
    CHECK(thy_engine_advance(&engine,24)==THY_OK);drain(0);
    for(unsigned n=0;n<15;++n)if(mpe.voices[n].state==THY_MPE_HELD)
        CHECK(mpe.voices[n].pressure==127 && mpe.voices[n].timbre==63);
    /* Primary is already saturated; further raw motion must still update AT. */
    CHECK(thy_instrument_sample(&instrument,0,1000,25)==THY_OK);
    CHECK(thy_engine_advance(&engine,32)==THY_OK);drain(0);
    for(unsigned n=0;n<15;++n)if(mpe.voices[n].state==THY_MPE_HELD)
        CHECK(mpe.voices[n].pressure==127 && mpe.voices[n].timbre==101);
    CHECK(thy_instrument_sample(&instrument,0,20,33)==THY_OK);
    CHECK(thy_instrument_sample(&instrument,0,20,34)==THY_OK);
    CHECK(drain(0x80)==3 && !thy_core_sounding(&core) && !thy_mpe_active(&mpe));
    return 1;
}
static int menu_alias_uses_one_guarded_mpe_configuration_without_setup(void)
{
    init();
    const uint16_t aliases[]={THY_ROW_PERFORMANCE_MPE,THY_ROW_EXPRESSION_ENABLED};
    int32_t value=-1;
    CHECK(thy_menu_rows[aliases[0]].group!=thy_menu_rows[aliases[1]].group);
    CHECK(!thy_state_row_visible(&engine.state,THY_ROW_PERFORMANCE_AFTERTOUCH));
    CHECK(!thy_state_row_visible(&engine.state,THY_ROW_PERFORMANCE_VELOCITYSOURCE));
    for(unsigned n=0;n<2u;++n){
        CHECK(thy_state_set(&engine.state,aliases[n],1)==THY_STATE_OK && !drain(0));
        CHECK(thy_state_get(&engine.state,aliases[1u-n],&value)==THY_STATE_OK && value==1);
        const int16_t note=60;CHECK(thy_engine_press_shaped(&engine,0,&note,1,0,0,5,96,NULL)==THY_OK);drain(0);
        uint32_t revision=instrument.revision;
        CHECK(thy_state_set(&engine.state,aliases[n],0)==THY_STATE_BLOCKED);
        CHECK(instrument.revision==revision && engine.expression_config.enabled && mpe.config.enabled && !drain(0));
        CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain(0);
        CHECK(thy_state_set(&engine.state,aliases[1u-n],0)==THY_STATE_OK && !drain(0));
        thy_arp_config config=engine.arps[0].config;config.enabled=1;
        CHECK(thy_engine_arp_configure(&engine,0,&config)==THY_OK);
        CHECK(thy_state_set(&engine.state,aliases[n],1)==THY_STATE_BLOCKED && !engine.expression_config.enabled && !drain(0));
        config.enabled=0;CHECK(thy_engine_arp_configure(&engine,0,&config)==THY_OK);
    }
    CHECK(thy_state_set(&engine.state,THY_ROW_PERFORMANCE_MPE,1)==THY_STATE_OK);
    static uint8_t saved[THY_SETTINGS_PAYLOAD_MAX];size_t bytes=0;
    CHECK(thy_settings_encode(&engine,saved,sizeof saved,&bytes));
    CHECK(thy_state_set(&engine.state,THY_ROW_EXPRESSION_ENABLED,0)==THY_STATE_OK);
    instrument.input_enabled=0u;
    CHECK(thy_settings_apply(&engine,saved,bytes) && !drain(0));
    CHECK(thy_state_get(&engine.state,THY_ROW_PERFORMANCE_MPE,&value)==THY_STATE_OK && value==1);
    CHECK(thy_state_get(&engine.state,THY_ROW_EXPRESSION_ENABLED,&value)==THY_STATE_OK && value==1);
    /* Legacy ON remains visible and overridable as the real route's policy. */
    CHECK(thy_state_set(&engine.state,THY_ROW_PERFORMANCE_MPE,0)==THY_STATE_OK);
    CHECK(thy_instrument_set_expression(&instrument,96,1)==THY_OK);
    CHECK(thy_state_set(&engine.state,THY_ROW_MIDI_ROUTETARGET,1)==THY_STATE_OK);
    CHECK(thy_state_get(&engine.state,THY_ROW_MIDI_PRESSURE,&value)==THY_STATE_OK && value==THY_CHANNEL_PRESSURE_POLY);
    CHECK(thy_state_set(&engine.state,THY_ROW_MIDI_PRESSURE,THY_CHANNEL_PRESSURE_OFF)==THY_STATE_OK);
    CHECK(thy_state_get(&engine.state,THY_ROW_MIDI_PRESSURE,&value)==THY_STATE_OK && value==THY_CHANNEL_PRESSURE_OFF);
    CHECK(instrument.pressure_enabled && !drain(0));
    return 1;
}

static int performance_policy_edits_preserve_captured_expression_and_release(void)
{
    init();thy_engine_bind_channel_expression(&engine,&ordinary);
    CHECK(thy_state_set(&engine.state,THY_ROW_PERFORMANCE_ROUTETARGET,1)==THY_STATE_OK);
    CHECK(thy_state_set(&engine.state,THY_ROW_PERFORMANCE_PRESSUREMODE,THY_CHANNEL_PRESSURE_CHANNEL)==THY_STATE_OK);
    CHECK(thy_state_set(&engine.state,THY_ROW_PERFORMANCE_PRESSUREBEND,THY_CHANNEL_BEND_UP)==THY_STATE_OK);
    CHECK(thy_state_set(&engine.state,THY_ROW_PERFORMANCE_BENDRANGE,2)==THY_STATE_OK);
    CHECK(!instrument.pressure_enabled && !drain(0));
    const int16_t first=60,next=62;
    CHECK(thy_engine_press_shaped(&engine,0,&first,1,0,3,5,96,NULL)==THY_OK);drain(0);
    const thy_pad_owner owner=core.owners[0];
    const uint8_t flags=ordinary.flags[0];
    CHECK((flags&3u)==THY_CHANNEL_PRESSURE_CHANNEL && (flags>>2u)==THY_CHANNEL_BEND_UP);
    CHECK(thy_engine_expression_pressure_axes(&engine,0,64,100)==THY_OK);
    CHECK(thy_engine_advance(&engine,8)==THY_OK);drain(0);
    CHECK(ordinary.sent_pressure[3]==100 && ordinary.sent_pressure[35]==100);
    CHECK(ordinary.sent_bend[3]==12319 && ordinary.sent_bend[35]==12319);
    CHECK(thy_state_set(&engine.state,THY_ROW_PERFORMANCE_PRESSUREMODE,THY_CHANNEL_PRESSURE_OFF)==THY_STATE_OK);
    CHECK(thy_state_set(&engine.state,THY_ROW_PERFORMANCE_PRESSUREBEND,THY_CHANNEL_BEND_DOWN)==THY_STATE_OK);
    CHECK(!memcmp(&owner,&core.owners[0],sizeof owner) && ordinary.flags[0]==flags && !drain(0));
    /* New left-side attacks use the new policy; the held owner's route and
     * contribution remain captured even while another editor target changes. */
    CHECK(thy_state_set(&engine.state,THY_ROW_PERFORMANCE_ROUTETARGET,2)==THY_STATE_OK);
    CHECK(thy_engine_press_shaped(&engine,4,&next,1,0,7,5,96,NULL)==THY_OK);drain(0);
    CHECK((ordinary.flags[4]&3u)==THY_CHANNEL_PRESSURE_OFF && (ordinary.flags[4]>>2u)==THY_CHANNEL_BEND_DOWN);
    CHECK(thy_engine_expression_pressure_axes(&engine,4,127,125)==THY_OK);
    CHECK(thy_engine_advance(&engine,16)==THY_OK);drain(0);
    CHECK(ordinary.sent_pressure[3]==100 && ordinary.sent_bend[3]==12319);
    CHECK(!ordinary.sent_pressure[7] && !ordinary.sent_pressure[39]);
    CHECK(ordinary.sent_bend[7]==0 && ordinary.sent_bend[39]==0);
    CHECK(thy_instrument_release_pad(&instrument,4)==THY_OK);drain(0);
    CHECK(thy_engine_advance(&engine,24)==THY_OK);drain(0);
    CHECK(ordinary.sent_bend[7]==8192 && ordinary.sent_bend[39]==8192);
    CHECK(ordinary.sent_pressure[3]==100 && ordinary.sent_bend[3]==12319);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);CHECK(drain(0x80)==2);
    CHECK(thy_engine_advance(&engine,32)==THY_OK);drain(0);
    CHECK(!ordinary.sent_pressure[3] && ordinary.sent_bend[3]==8192);
    CHECK(!thy_core_sounding(&core) && thy_engine_channel_quiet(&engine));
    return 1;
}

int main(void)
{
    if(!chord_pressure_routes_and_panic() || !modifier_capture_and_live_only() ||
        !alternating_strum_keeps_glide_reference() || !sensor_pressure_callback_and_standard_compatibility() ||
        !ordinary_capture_and_independent_axes() || !raw_dual_stage_feeds_mpe_and_preserves_release() ||
        !menu_alias_uses_one_guarded_mpe_configuration_without_setup() ||
        !performance_policy_edits_preserve_captured_expression_and_release())return 1;
    printf("Expression engine: 8 scenarios passed (%u assertions)\n",assertions);return 0;
}
