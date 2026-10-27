/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "thy_companion.h"
#include "thy_settings.h"
static thy_core core;
static thy_instrument instrument;
static thy_engine engine;
static thy_companion companion;
static struct {uint8_t before[16],stage[THY_COMPANION_SNAPSHOT_BYTES],after[16];} memory;
static struct {uint8_t before[16];char text[THY_COMPANION_RESPONSE_BYTES];uint8_t after[16];} response;
static uint8_t frozen[THY_COMPANION_SNAPSHOT_BYTES];
static thy_instrument_pad layout[96];
static uint32_t generation;
static unsigned assertions,visual_calls;
static int provider_available;
#define CHECK(x) do{++assertions;if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static uint16_t get16(unsigned at){return (uint16_t)(memory.stage[at]|((uint16_t)memory.stage[at+1u]<<8u));}
static uint32_t get32(unsigned at){uint32_t v=0;for(unsigned n=0;n<4u;++n)v|=(uint32_t)memory.stage[at+n]<<(8u*n);return v;}
static int visual(void *ctx,uint8_t oled[1024],uint8_t rgb[288],uint8_t center[19],uint32_t *ms,uint8_t *visible)
{
    if(ctx!=&generation)return 0;
    ++visual_calls;if(!provider_available)return 0;
    for(unsigned n=0;n<1024u;++n)oled[n]=(uint8_t)n;
    for(unsigned n=0;n<288u;++n)rgb[n]=(uint8_t)(n%9u);
    for(unsigned n=0;n<19u;++n)center[n]=(uint8_t)(n*11u);
    *ms=123u;*visible=1u;return 1;
}
static void init(void)
{
    thy_core_init(&core);thy_instrument_init(&instrument,&core);
    static const int16_t degrees[]={0,2,4,5,7,9,11};
    for(unsigned n=0;n<96u;++n)layout[n]=(thy_instrument_pad){.notes={60,64,67},.valid_mask=7,.color=1};
    (void)thy_instrument_set_layout(&instrument,layout,60,degrees,7);
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&engine,&instrument,21u,70u, undo_workspace_1);
    memset(&memory,0xa5,sizeof memory);memset(&response,0x5a,sizeof response);
    generation=42u;visual_calls=0;provider_available=1;
    const thy_companion_identity id={"0123456789abcdef","00112233445566778899aabb",&generation};
    thy_companion_init(&companion,memory.stage,sizeof memory.stage,&id,visual,&generation);
}
static const char *command(int available,uint32_t now,const char *line)
{(void)thy_companion_command(&companion,&engine,available,now,line,response.text,sizeof response.text);return response.text;}
static void line(char *out,const char *verb,unsigned row,unsigned instance,int32_t value,int with_value)
{if(with_value)(void)snprintf(out,180,"companion %s %s %u %u %ld",verb,THY_MENU_SCHEMA_SHA256,row,instance,(long)value);
 else(void)snprintf(out,180,"companion %s %s %u %u",verb,THY_MENU_SCHEMA_SHA256,row,instance);}
static int guards(void)
{for(unsigned n=0;n<16u;++n)CHECK(memory.before[n]==0xa5 && memory.after[n]==0xa5 && response.before[n]==0x5a && response.after[n]==0x5a);return 1;}
static int frozen_snapshot_and_runtime_are_actual(void)
{
    init();
    CHECK(strstr(command(1,0,"companion caps"),"COMPANION/1 CAPS build=0123456789abcdef uid=00112233445566778899aabb schema="));
    CHECK(strstr(response.text," bytes=1680 chunk=128 rows="));
    CHECK(strstr(response.text," role=instrument backup=1 song=1 companion=1\r\n"));
    const int16_t notes[]={60,64,67};
    CHECK(thy_engine_press_shaped(&engine,0,notes,3,0,0,5,80,NULL)==THY_OK);
    instrument.control_masks[1]=1u<<2u;instrument.sensors[0].pressure=93;
    instrument.sensors[0].raw=1732;instrument.sensors[0].signed_delta=-7;
    CHECK(strstr(command(1,10,"companion snapshot"),"SNAPSHOT seq=1 bytes=1680 crc32="));
    CHECK(visual_calls==1 && companion.active && !memcmp(memory.stage,"THYCMP1",8));
    CHECK(get16(8)==1 && get16(10)==1680 && get32(12)==1 && get32(16)==10 && get32(20)==123);
    CHECK(!memcmp(memory.stage+32,"0123456789abcdef",16) && !memcmp(memory.stage+72,THY_MENU_SCHEMA_SHA256,64));
    CHECK(get32(1654)==42 && memory.stage[1658]==1);
    for(unsigned n=0;n<1024u;++n)CHECK(memory.stage[THY_COMPANION_OLED+n]==(uint8_t)n);
    for(unsigned n=0;n<288u;++n)CHECK(memory.stage[THY_COMPANION_RGB+n]==n%9u);
    CHECK(memory.stage[THY_COMPANION_CENTER+18u]==198);
    CHECK(memory.stage[THY_COMPANION_HELD]&1u);
    CHECK(memory.stage[THY_COMPANION_CONTROLS]&(1u<<7u));
    for(unsigned n=0;n<3u;++n)CHECK(memory.stage[THY_COMPANION_NOTES+(unsigned)notes[n]/8u]&(1u<<((unsigned)notes[n]&7u)));
    CHECK(strstr((char *)memory.stage+THY_COMPANION_HARMONY,"C MAJ"));
    CHECK(memory.stage[1618]==1 && memory.stage[1619]==3 && get16(1620)==6);
    CHECK(get16(1634)==1732 && (int16_t)get16(1636)==-7 && memory.stage[1638]==93);
    CHECK(companion.crc==thy_settings_crc32(memory.stage,sizeof memory.stage));
    memcpy(frozen,memory.stage,sizeof frozen);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);generation=43;
    CHECK(strstr(command(1,20,"companion get 1 0 128"),"DATA seq=1 offset=0 hex=544859434d503100"));
    CHECK(visual_calls==1 && !memcmp(memory.stage,frozen,sizeof frozen));
    CHECK(strstr(command(1,25,"companion get 1 1679 2"),"ERR offset"));
    CHECK(strstr(command(1,25,"companion get 1 0 129"),"ERR offset"));
    CHECK(strstr(command(1,25,"companion snapshot"),"ERR snapshot-active"));
    CHECK(strstr(command(1,26,"companion release 2"),"ERR generation") && companion.active);
    CHECK(strstr(command(0,26,"companion release 1"),"RELEASED") && !companion.active);
    CHECK(strstr(command(1,27,"companion snapshot"),"ERR rate"));
    CHECK(strstr(command(1,60,"companion snapshot"),"SNAPSHOT seq=2"));
    CHECK(visual_calls==2 && get32(1654)==43 && !memory.stage[1618] && !memory.stage[1619]);
    CHECK(guards());return 1;
}
static int exclusive_staging_expiry_and_readonly_pad_selection(void)
{
    init();uint8_t selected=instrument.selected_pad;
    CHECK(strstr(command(1,0,"companion pad 95"),"PAD selected=95"));
    CHECK(instrument.selected_pad==selected && !core.sounding_count && !core.queue_count);
    CHECK(strstr(command(1,0,"companion pad 96"),"ERR pad") && companion.selected_pad==95);
    CHECK(strstr(command(0,0,"companion snapshot"),"ERR busy") && !visual_calls);
    CHECK(strstr(command(0,0,"companion pad 0"),"ERR busy") && companion.selected_pad==95);
    CHECK(strstr(command(0,0,"companion caps"),"writable=0"));
    CHECK(strstr(command(1,UINT32_MAX-100u,"companion snapshot"),"SNAPSHOT seq=1"));
    CHECK(!thy_companion_expire(&companion,100u));
    CHECK(thy_companion_expire(&companion,2000u) && !companion.active);
    CHECK(strstr(command(1,2001,"companion get 1 0 1"),"ERR generation"));
    CHECK(strstr(command(1,2100,"companion snapshot"),"SNAPSHOT seq=2"));
    for(unsigned n=1;n<=4u;++n)CHECK(strstr(command(1,2100+n*1000u,"companion get 2 0 1"),"DATA seq=2"));
    CHECK(strstr(command(1,7100,"companion get 2 0 1"),"ERR generation") && !companion.active);
    provider_available=0;
    CHECK(strstr(command(1,7200,"companion snapshot"),"ERR snapshot-unavailable") && !companion.active);
    CHECK(guards());return 1;
}
static int schema_scoped_parameters_navigation_and_explicit_actions(void)
{
    init();char request[180];int32_t value=-1;
    unsigned channel=THY_ROW_MIDI_CHANNEL;
    CHECK(thy_state_set(&engine.state,THY_ROW_MIDI_ROUTETARGET,2)==THY_STATE_OK);
    line(request,"read",channel,255u,0,0);
    CHECK(strstr(command(1,0,request)," instance=2 value=2"));
    line(request,"set",channel,2u,4,1);
    char wrong[180];memcpy(wrong,request,sizeof wrong);wrong[14]=wrong[14]=='a'?'b':'a';
    uint32_t revision=instrument.revision;
    CHECK(strstr(command(1,0,wrong),"ERR command-or-schema") && instrument.revision==revision);
    CHECK(strstr(command(0,0,request),"ERR busy") && instrument.revision==revision);
    CHECK(strstr(command(1,0,request),"SET result=0"));
    CHECK(thy_state_get_instance(&engine.state,(uint16_t)channel,2,&value)==THY_STATE_OK && value==4);
    CHECK(thy_state_set(&engine.state,THY_ROW_MIDI_ROUTETARGET,1)==THY_STATE_OK);
    line(request,"read",channel,255u,0,0);CHECK(strstr(command(1,0,request)," instance=1 value=1"));
    line(request,"read",channel,2u,0,0);CHECK(strstr(command(1,0,request)," instance=2 value=4"));
    line(request,"set",channel,255u,3,1);CHECK(strstr(command(1,0,request),"ERR row"));
    line(request,"set",channel,1u,17,1);CHECK(strstr(command(1,0,request),"SET result=-1"));
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[channel].group,(uint16_t)channel)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,(uint16_t)channel)==THY_STATE_OK);
    line(request,"set",channel,2u,5,1);CHECK(strstr(command(1,0,request),"ERR draft-active"));
    (void)snprintf(request,sizeof request,"companion nav %s %u",THY_MENU_SCHEMA_SHA256,THY_STATE_SELECT);
    CHECK(strstr(command(1,0,request),"NAV result=0") && !engine.state.editing && engine.state.level==THY_STATE_PLAY);
    line(request,"set",THY_ROW_SYSTEM_SLEEP,0u,1,1);CHECK(strstr(command(1,0,request),"ERR row"));
    line(request,"action",THY_ROW_SYSTEM_ABOUT,0u,0,0);CHECK(strstr(command(1,0,request),"ACTION result=0") && engine.state.about_open);
    CHECK(!core.queue_count && !core.sounding_count);
    CHECK(guards());return 1;
}
static int remote_controls_are_taps_and_cannot_release_a_physical_control(void)
{
    init();thy_engine_bind_controls(&engine);char request[180];
    (void)snprintf(request,sizeof request,"companion control %s 0 2",THY_MENU_SCHEMA_SHA256);
    CHECK(strstr(command(1,0,request),"CONTROL result=0"));
    CHECK(engine.state.level==THY_STATE_GROUPS && !instrument.control_masks[0]);
    CHECK(thy_instrument_control(&instrument,0,2,1)==THY_OK);
    CHECK(strstr(command(1,0,request),"ERR control-held") && instrument.control_masks[0]==4u);
    CHECK(thy_instrument_control(&instrument,0,2,0)==THY_OK);
    (void)snprintf(request,sizeof request,"companion control %s 2 1",THY_MENU_SCHEMA_SHA256);
    CHECK(strstr(command(1,0,request),"CONTROL result=0") && !instrument.control_masks[2]);
    CHECK(!core.queue_count && !core.sounding_count);
    char short_response[4]={1,2,3,4};
    CHECK(!thy_companion_command(&companion,&engine,1,0,"companion caps",short_response,sizeof short_response));
    CHECK(short_response[0]==1 && short_response[3]==4);
    CHECK(strstr(command(1,0,"companion get 4294967296 0 1"),"ERR command-or-schema"));
    CHECK(strstr(command(1,0,"companion snapshot garbage"),"ERR command-or-schema"));
    CHECK(guards());return 1;
}
static int maximum_note_set_and_long_labels_keep_wire_fields_separate(void)
{
    init();
    /* Source identity text has its own32-byte field even when musical naming
     * reads all128 actual MIDI pitches through the shared formatter. */
    instrument.user_scale=1u;
    memset(instrument.library.scales[0].name,'S',sizeof instrument.library.scales[0].name);
    instrument.library.scales[0].name[sizeof instrument.library.scales[0].name-1u]=0u;
    for(unsigned note=0;note<128u;++note)core.refs[note]=1u;
    CHECK(strstr(command(1,0,"companion snapshot"),"SNAPSHOT seq=1"));
    CHECK(!strcmp((char *)memory.stage+THY_COMPANION_HARMONY,"128 NOTES"));
    CHECK(memory.stage[THY_COMPANION_HARMONY+31u]==0u);
    CHECK(memory.stage[THY_COMPANION_SCALE_NAME]=='S' && memory.stage[THY_COMPANION_SCALE_NAME+31u]==0u);
    CHECK(memory.stage[1619]==128u);
    for(unsigned n=0;n<16u;++n)CHECK(memory.stage[THY_COMPANION_NOTES+n]==255u);
    CHECK(guards());
    init();
    /* A captured authored root must retain a long, exact slash-chord label
     * without intruding into the next field or either input-note bitmap. */
    instrument.held_input_valid[0]=instrument.held_color_valid[0]=1u;
    instrument.held_color_root[0]=1u;
    core.refs[7]=1u;
    for(unsigned interval=0;interval<12u;++interval)
        if(UINT16_C(0x26d)&(1u<<interval))core.refs[13u+interval]=1u;
    CHECK(strstr(command(1,0,"companion snapshot"),"SNAPSHOT seq=1"));
    CHECK(!strcmp((char *)memory.stage+THY_COMPANION_HARMONY,"C# DIM7 ADD9 ADD11/G"));
    CHECK(memory.stage[THY_COMPANION_HARMONY+31u]==0u);
    CHECK(!strcmp((char *)memory.stage+THY_COMPANION_SCALE_NAME,thy_menu_scale_names[engine.scale]));
    for(unsigned n=0;n<16u;++n)CHECK(memory.stage[THY_COMPANION_POOL+n]==0u);
    CHECK(guards());return 1;
}
int main(void)
{
    if(!frozen_snapshot_and_runtime_are_actual()||!exclusive_staging_expiry_and_readonly_pad_selection()||
       !schema_scoped_parameters_navigation_and_explicit_actions()||!remote_controls_are_taps_and_cannot_release_a_physical_control()||
       !maximum_note_set_and_long_labels_keep_wire_fields_separate())return 1;
    printf("Companion:5 scenarios passed (%u assertions); service%zu bytes, caller-owned staging%u bytes.\n",assertions,sizeof(thy_companion),THY_COMPANION_SNAPSHOT_BYTES);return 0;
}
