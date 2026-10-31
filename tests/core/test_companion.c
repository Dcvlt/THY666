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
    CHECK(strstr(response.text," role=instrument backup=1 song=1 companion=1 harmony=1 harmony_bytes=800 reset=1 pad_map=1 pad_map_bytes=1672 performance=1 performance_bytes=1024\r\n"));
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
static int optional_harmony_snapshot_reuses_staging_and_preserves_visual_cadence(void)
{
    init();thy_harmony_workspace *h=&engine.harmony;
    h->revision=UINT32_C(0x12345678);h->key_mask=UINT16_C(0xab5);h->input_mask=UINT16_C(0x091);h->target_mask=UINT16_C(0xad5);
    h->comparison=(thy_modulation){.common=0x895,.added=0x240,.removed=0x20,.pivot=0x091,.pivot_root=0,.current_fits=1};
    h->source_root=0;h->source_family=0;h->target_root=7;h->target_family=0;h->target_catalog=4;h->can_apply=1;
    h->current_root=0;h->current_pad=5;h->selected_suggestion=1;h->page=THY_HARMONY_CHORDS;h->cursor=1;h->count=2;
    h->pinned=h->cache_ready=h->highlights=h->motion=1;h->pattern=3;
    h->base_mask=0x091;h->right_mask=4;h->view=2;h->control_bank=1;h->octave_target=1;
    h->right_mode=1;h->analysis=0;h->bass_root=1;h->low_note=36;h->high_note=100;h->preview_scale=4;
    instrument.cluster_swap=1;instrument.octave_layers=0x2a;instrument.voicing_mode=2;
    instrument.performance.chord_octave=-1;instrument.performance.melody_octave=2;
    h->pad_roles[5]=THY_HARMONY_CURRENT;h->pad_roles[9]=THY_HARMONY_SELECTED;h->pad_roles[47]=THY_HARMONY_SUGGESTED;
    h->choices[0]=(thy_harmony_choice){.mask=0x091,.root=0,.shared=3,.degree=1,.reason=THY_HARMONY_IN_KEY,.pad=5};
    h->choices[1]=(thy_harmony_choice){.mask=0x221,.root=5,.shared=2,.degree=4,.reason=THY_HARMONY_PATTERN_NEXT,.pad=9};
    CHECK(strstr(command(1,10,"companion snapshot"),"SNAPSHOT seq=1"));
    CHECK(get32(THY_COMPANION_HARMONY_REVISION)==UINT32_C(0x12345678));
    CHECK(memory.stage[THY_COMPANION_CLUSTER_SWAP]==1u);
    CHECK(strstr(command(1,10,"companion harmony snapshot"),"ERR snapshot-active"));
    CHECK(strstr(command(1,11,"companion release 1"),"RELEASED"));
    memset(memory.stage+THY_COMPANION_HARMONY_BYTES,0x6a,sizeof memory.stage-THY_COMPANION_HARMONY_BYTES);
    uint32_t revision=instrument.revision;unsigned calls=visual_calls;
    CHECK(strstr(command(1,11,"companion harmony snapshot"),"HARMONY seq=2 bytes=800 crc32="));
    CHECK(!memcmp(memory.stage,"THYHRM1",8) && get16(8)==4 && get16(10)==800 && get32(12)==2 && get32(16)==11);
    CHECK(!memcmp(memory.stage+32,"0123456789abcdef",16) && !memcmp(memory.stage+48,"00112233445566778899aabb",24));
    CHECK(!memcmp(memory.stage+72,THY_MENU_SCHEMA_SHA256,64));
    CHECK(get32(136)==UINT32_C(0x12345678) && get16(140)==0xab5 && get16(142)==0x091 && get16(144)==0xad5);
    CHECK(get16(146)==0x895 && get16(148)==0x240 && get16(150)==0x20 && get16(152)==0x091);
    CHECK(memory.stage[157]==THY_HARMONY_CHORDS && memory.stage[160]==7 && memory.stage[162]==1 && memory.stage[163]==2);
    CHECK(memory.stage[164]==3 && memory.stage[166]==1 && memory.stage[167]==1 && memory.stage[168]==5 && memory.stage[169]==1 && memory.stage[170]==4);
    CHECK(memory.stage[172]==(1u<<5u) && memory.stage[179]==2u && memory.stage[189]==128u);
    CHECK(memory.stage[190]==3 && memory.stage[191]==3);
    char label[32];thy_engine_harmony_label(h->input_mask,h->current_root,label);
    CHECK(!strcmp((char *)memory.stage+192,label));
    thy_engine_harmony_label(h->choices[1].mask,h->choices[1].root,label);
    CHECK(!strcmp((char *)memory.stage+288,label));
    CHECK(get16(320)==0x091 && memory.stage[322]==0 && memory.stage[323]==3 && memory.stage[324]==1 && memory.stage[325]==THY_HARMONY_IN_KEY && memory.stage[326]==5 && memory.stage[327]==1);
    CHECK(get16(328)==0x221 && memory.stage[330]==5 && memory.stage[333]==THY_HARMONY_PATTERN_NEXT && memory.stage[334]==9 && memory.stage[335]==1);
    for(unsigned n=336;n<512u;++n)CHECK(memory.stage[n]==0u);
    CHECK(get16(512)==0x091 && get16(514)==4 && memory.stage[516]==2 && memory.stage[517]==1 && memory.stage[518]==1);
    CHECK(memory.stage[519]==1 && memory.stage[520]==0 && memory.stage[521]==1 && memory.stage[522]==36 && memory.stage[523]==100);
    CHECK(memory.stage[524]==1 && memory.stage[525]==0x2a && memory.stage[526]==32);
    CHECK(memory.stage[534]==32 && memory.stage[547]==2 && memory.stage[563]==128);
    for(unsigned n=0;n<7u;++n)CHECK(memory.stage[564+n]==thy_instrument_scale_shortcut(&instrument,(uint8_t)(n+2u)));
    CHECK(memory.stage[571]==4 && memory.stage[572]==2 && memory.stage[573]==2 && memory.stage[574]==255 && memory.stage[575]==2);
    thy_engine_harmony_label(h->base_mask,h->current_root,label);CHECK(!strcmp((char *)memory.stage+576,label));
    thy_engine_harmony_melody_label(&engine,label);CHECK(!strcmp((char *)memory.stage+608,label));
    for(unsigned n=800;n<sizeof memory.stage;++n)CHECK(memory.stage[n]==0x6a);
    CHECK(companion.captured_bytes==800 && companion.crc==thy_settings_crc32(memory.stage,800));
    CHECK(visual_calls==calls && instrument.revision==revision && !core.queue_count && !core.sounding_count);
    CHECK(strstr(command(1,12,"companion get 2 639 1"),"DATA seq=2 offset=639 hex=00"));
    CHECK(strstr(command(1,12,"companion get 2 799 2"),"ERR offset"));
    CHECK(strstr(command(1,12,"companion get 2 800 1"),"ERR offset"));
    CHECK(strstr(command(1,12,"companion get 1 0 1"),"ERR generation"));
    CHECK(strstr(command(0,12,"companion release 2"),"RELEASED"));
    /* The extension has its own lease timestamp, never the visual rate timer. */
    CHECK(strstr(command(1,59,"companion snapshot"),"ERR rate"));
    CHECK(strstr(command(1,60,"companion snapshot"),"SNAPSHOT seq=3 bytes=1680"));
    CHECK(companion.captured_bytes==1680 && visual_calls==calls+1u);
    CHECK(strstr(command(1,61,"companion get 3 1679 1"),"DATA seq=3 offset=1679"));
    CHECK(guards());return 1;
}
static int harmony_scale_rows_keys_and_actions_are_bounded_and_schema_scoped(void)
{
    init();thy_harmony_workspace *h=&engine.harmony;h->page=THY_HARMONY_SCALES;h->scale_count=24;h->scale_total=37;h->cursor=23;
    for(unsigned n=0;n<24u;++n)h->scales[n]=(thy_scale_match){.mask=(uint16_t)(1u<<(n%12u)),.root=(uint8_t)(n%12u),.family=0,.extra=5};
    CHECK(strstr(command(1,0,"companion harmony snapshot"),"HARMONY seq=1 bytes=800"));
    CHECK(get16(154)==37 && memory.stage[163]==24 && get16(504)==(1u<<11u));
    CHECK(memory.stage[506]==11 && memory.stage[507]==0 && memory.stage[508]==5 && memory.stage[509]==255 && memory.stage[510]==255 && memory.stage[511]==1);
    CHECK(strstr((char *)memory.stage+288,thy_guidance_scale_name(0)) && memory.stage[319]==0u);
    CHECK(strstr(command(1,1,"companion release 1"),"RELEASED"));
    h->page=THY_HARMONY_KEY;
    CHECK(strstr(command(1,1,"companion harmony snapshot"),"HARMONY seq=2 bytes=800"));
    CHECK(memory.stage[163]==0 && memory.stage[288]==0);
    for(unsigned n=320;n<512u;++n)CHECK(memory.stage[n]==0);
    CHECK(strstr(command(1,THY_COMPANION_IDLE_MS+1u,"companion get 2 0 1"),"ERR generation"));
    CHECK(strstr(command(0,3000,"companion harmony snapshot"),"ERR busy"));
    char request[180];uint32_t revision=h->revision;
    (void)snprintf(request,sizeof request,"companion harmony %s 11 0",THY_MENU_SCHEMA_SHA256);
    CHECK(strstr(command(1,3000,request),"ERR action") && h->revision==revision);
    (void)snprintf(request,sizeof request,"companion harmony %s 0 1",THY_MENU_SCHEMA_SHA256);
    char wrong[180];memcpy(wrong,request,sizeof wrong);wrong[18]=wrong[18]=='a'?'b':'a';
    CHECK(strstr(command(1,3000,wrong),"ERR command-or-schema") && h->revision==revision);
    CHECK(strstr(command(0,3000,request),"ERR busy") && h->revision==revision);
    CHECK(strstr(command(1,3000,request),"HARMONY result=0 action=0 value=1") && thy_engine_harmony_active(&engine));
    h->page=THY_HARMONY_SCALES;h->scale_count=1u;
    h->scales[0]=(thy_scale_match){.mask=0xad5u,.root=7u,.family=0u,.extra=0u};
    (void)snprintf(request,sizeof request,"companion harmony %s 10 0",THY_MENU_SCHEMA_SHA256);
    CHECK(strstr(command(1,3000,request),"HARMONY result=0 action=10 value=0") &&
        h->page==THY_HARMONY_KEY && h->target_root==7u && h->target_family==0u);
    (void)snprintf(request,sizeof request,"companion harmony %s 1 1 garbage",THY_MENU_SCHEMA_SHA256);
    CHECK(strstr(command(1,3000,request),"ERR command-or-schema"));
    (void)snprintf(request,sizeof request,"companion harmony %s 0 0",THY_MENU_SCHEMA_SHA256);
    engine.state.editing=1;
    CHECK(strstr(command(1,3000,request),"ERR draft-active") && thy_engine_harmony_active(&engine));
    engine.state.editing=0;
    CHECK(strstr(command(1,3000,request),"HARMONY result=0 action=0 value=0") && !thy_engine_harmony_active(&engine));
    CHECK(!core.queue_count && !core.sounding_count && guards());return 1;
}
static int harmony_controls_are_scoped_and_do_not_release_physical_holds(void)
{
    init();CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_ACTIVATE,1)==THY_STATE_OK);
    char request[180];uint8_t root=instrument.performance.root;uint16_t scale=engine.scale;
    (void)snprintf(request,sizeof request,"companion harmony-control %s 2 7",THY_MENU_SCHEMA_SHA256);
    CHECK(strstr(command(1,0,request),"ERR harmony-context") && instrument.performance.root==root && !instrument.control_masks[2]);
    thy_engine_bind_controls(&engine);
    CHECK(strstr(command(1,0,request),"HCONTROL result=0 bank=2 index=7"));
    CHECK(engine.harmony.target_root==7u && instrument.performance.root==root && engine.scale==scale && !instrument.control_masks[2]);
    engine.state.level=THY_STATE_ROWS;
    (void)snprintf(request,sizeof request,"companion harmony-control %s 2 2",THY_MENU_SCHEMA_SHA256);
    CHECK(strstr(command(1,1,request),"ERR harmony-context"));
    CHECK(engine.harmony.target_root==7u && instrument.performance.root==root && engine.scale==scale && !instrument.control_masks[2]);
    engine.state.level=THY_STATE_PLAY;engine.arp_quick_open=1u;
    CHECK(strstr(command(1,2,request),"ERR harmony-context") && !instrument.control_masks[2]);
    engine.arp_quick_open=0;engine.calibration_open=1u;
    CHECK(strstr(command(1,3,request),"ERR harmony-context") && !instrument.control_masks[2]);
    engine.calibration_open=0;engine.state.editing=1u;
    CHECK(strstr(command(1,4,request),"ERR harmony-context") && !instrument.control_masks[2]);
    engine.state.editing=0;engine.sequencer_ui.open=1u;
    CHECK(strstr(command(1,5,request),"ERR harmony-context") && !instrument.control_masks[2]);
    engine.sequencer_ui.open=0;instrument.control_masks[2]=1u<<2u;
    CHECK(strstr(command(1,6,request),"ERR control-held") && instrument.control_masks[2]==(1u<<2u));
    CHECK(engine.harmony.target_root==7u && instrument.performance.root==root && engine.scale==scale);
    instrument.control_masks[2]=0;
    (void)snprintf(request,sizeof request,"companion harmony-control %s 1 0",THY_MENU_SCHEMA_SHA256);
    CHECK(strstr(command(1,7,request),"ERR harmony-control") && !instrument.control_masks[1]);
    (void)snprintf(request,sizeof request,"companion harmony-control %s 0 2",THY_MENU_SCHEMA_SHA256);
    CHECK(strstr(command(1,8,request),"ERR harmony-control") && engine.state.level==THY_STATE_PLAY);
    (void)snprintf(request,sizeof request,"companion harmony-control %s 3 0",THY_MENU_SCHEMA_SHA256);
    uint8_t bank=engine.harmony.control_bank;
    CHECK(strstr(command(1,9,request),"HCONTROL result=0 bank=3 index=0") && engine.harmony.control_bank==(bank^1u));
    CHECK(!instrument.control_masks[3] && instrument.performance.root==root && engine.scale==scale);
    CHECK(!core.queue_count && !core.sounding_count && guards());return 1;
}
static int reset_confirmation_uses_c_scope_and_rejects_stale_remote_navigation(void)
{
    init();thy_engine_bind_controls(&engine);int32_t enabled=1;
    CHECK(thy_state_set(&engine.state,THY_ROW_LIGHTS_ENABLED,0)==THY_STATE_OK);
    CHECK(thy_state_reset_begin(&engine.state,THY_ROW_LIGHTS_RESETOPTIONS)==THY_STATE_OK);
    uint32_t epoch=engine.state.reset_epoch;CHECK(epoch && engine.state.reset_open && !engine.state.reset_confirm);
    CHECK(strstr(command(1,0,"companion snapshot"),"SNAPSHOT seq=1"));
    CHECK(memory.stage[1672]==1 && memory.stage[1673]==0 && memory.stage[1674]==thy_menu_rows[THY_ROW_LIGHTS_RESETOPTIONS].group);
    CHECK(memory.stage[1675]==UINT8_MAX && get32(1676)==epoch);
    CHECK(strstr(command(1,0,"companion release 1"),"RELEASED"));
    char request[180];
    (void)snprintf(request,sizeof request,"companion nav %s %u",THY_MENU_SCHEMA_SHA256,THY_STATE_RIGHT);
    engine.state.reset_confirm=1u;
    CHECK(strstr(command(1,1,request),"ERR reset-active") && engine.state.reset_open);
    (void)snprintf(request,sizeof request,"companion control %s 0 4",THY_MENU_SCHEMA_SHA256);
    CHECK(strstr(command(1,2,request),"ERR reset-active") && !instrument.control_masks[0]);
    (void)snprintf(request,sizeof request,"companion reset-confirm %s %lu 1",THY_MENU_SCHEMA_SHA256,(unsigned long)(epoch+1u));
    CHECK(strstr(command(1,3,request),"ERR reset-context") && engine.state.reset_open);
    CHECK(thy_state_get(&engine.state,THY_ROW_LIGHTS_ENABLED,&enabled)==THY_STATE_OK && !enabled);
    (void)snprintf(request,sizeof request,"companion reset-confirm %s %lu 0",THY_MENU_SCHEMA_SHA256,(unsigned long)epoch);
    CHECK(strstr(command(1,4,request),"RESET result=0") && !engine.state.reset_open);
    CHECK(thy_state_get(&engine.state,THY_ROW_LIGHTS_ENABLED,&enabled)==THY_STATE_OK && !enabled);
    CHECK(thy_state_reset_begin(&engine.state,THY_ROW_LIGHTS_RESETOPTIONS)==THY_STATE_OK && engine.state.reset_epoch!=epoch);
    (void)snprintf(request,sizeof request,"companion reset-confirm %s %lu 1",THY_MENU_SCHEMA_SHA256,(unsigned long)epoch);
    CHECK(strstr(command(1,5,request),"ERR reset-context") && engine.state.reset_open);
    epoch=engine.state.reset_epoch;instrument.control_masks[0]=1u<<4u;
    (void)snprintf(request,sizeof request,"companion reset-confirm %s %lu 1",THY_MENU_SCHEMA_SHA256,(unsigned long)epoch);
    CHECK(!strstr(command(1,6,request),"RESET result=0") && engine.state.reset_open && instrument.control_masks[0]==(1u<<4u));
    CHECK(thy_state_get(&engine.state,THY_ROW_LIGHTS_ENABLED,&enabled)==THY_STATE_OK && !enabled);
    instrument.control_masks[0]=0;
    CHECK(strstr(command(1,7,request),"RESET result=0") && !engine.state.reset_open);
    CHECK(thy_state_get(&engine.state,THY_ROW_LIGHTS_ENABLED,&enabled)==THY_STATE_OK && enabled==1);
    CHECK(!core.queue_count && !core.sounding_count && guards());return 1;
}
static int complete_pad_map(uint32_t now)
{
    CHECK(strstr(command(1,now,"companion pad-map"),"PADMAP pending=1 seq="));
    for(unsigned n=0u;n<96u;++n)CHECK(thy_companion_service(&companion,&engine,now+n+1u)==(n==95u));
    CHECK(strstr(command(1,now+97u,"companion pad-map"),"PADMAP seq="));
    CHECK(companion.captured_bytes==THY_COMPANION_PAD_MAP_BYTES);return 1;
}
static int pad_map_cooperative_publication_is_readonly_and_leased(void)
{
    init();instrument.performance.melody_layout=THY_LAYOUT_CHROMATIC;
    static thy_engine before_engine;static thy_instrument before_instrument;static thy_core before_core;
    before_engine=engine;before_instrument=instrument;before_core=core;
    CHECK(strstr(command(1,0,"companion pad-map"),"PADMAP pending=1 seq=1"));
    CHECK(companion.active&&companion.map_cursor==0u&&companion.captured_bytes==0u&&!visual_calls);
    CHECK(!memcmp(memory.stage,"THYMAP1",8)&&get16(8)==1u&&get16(10)==1672u);
    CHECK(strstr(command(1,1,"companion snapshot"),"ERR snapshot-active"));
    CHECK(strstr(command(1,2,"companion get 1 136 16"),"ERR offset"));
    for(unsigned n=0;n<95u;++n){
        CHECK(!thy_companion_service(&companion,&engine,3u+n));
        CHECK(companion.map_cursor==n+1u&&companion.captured_bytes==0u);
        CHECK(memory.stage[136u+16u*n]==(n<48u?THY_COMPANION_PAD_CHORD:THY_COMPANION_PAD_NOTE));
        CHECK(memory.stage[136u+16u*(n+1u)]==0u);
    }
    CHECK(thy_companion_service(&companion,&engine,100u));
    CHECK(strstr(command(1,101,"companion pad-map"),"PADMAP seq=1 bytes=1672 crc32="));
    CHECK(companion.crc==thy_settings_crc32(memory.stage,1672u));
    CHECK(!strcmp((char *)memory.stage+137u,"C MAJ"));
    CHECK(!strcmp((char *)memory.stage+136u+48u*16u+1u,"C4"));
    for(unsigned n=0;n<96u;++n){
        unsigned ended=0u;
        for(unsigned k=1u;k<16u;++k){unsigned c=memory.stage[136u+16u*n+k];if(!c)ended=1u;else CHECK(!ended&&c>=32u&&c<127u);}
        CHECK(ended);
    }
    for(unsigned n=1672u;n<1680u;++n)CHECK(memory.stage[n]==0xa5);
    CHECK(!memcmp(&engine,&before_engine,sizeof engine));CHECK(!memcmp(&instrument,&before_instrument,sizeof instrument));CHECK(!memcmp(&core,&before_core,sizeof core));
    CHECK(strstr(command(1,102,"companion get 1 136 16"),"DATA seq=1 offset=136 hex="));
    CHECK(strstr(command(1,103,"companion release 1"),"RELEASED"));
    uint32_t key=get32(20);char request[80];(void)snprintf(request,sizeof request,"companion pad-map %lu",(unsigned long)key);
    CHECK(strstr(command(1,104,request),"PADMAP unchanged=1 revision="));CHECK(!companion.active&&companion.sequence==1u);
    CHECK(guards());return 1;
}
static int pad_map_cancellation_changes_and_physical_roles(void)
{
    init();instrument.performance.melody_layout=THY_LAYOUT_PIANO;
    CHECK(complete_pad_map(0u));
    CHECK(memory.stage[136u+48u*16u]==THY_COMPANION_PAD_HOLE);
    CHECK(memory.stage[136u+52u*16u]==THY_COMPANION_PAD_NOTE&&!strcmp((char *)memory.stage+136u+52u*16u+1u,"C#4"));
    CHECK(strstr(command(1,100u,"companion release 1"),"RELEASED"));
    CHECK(thy_instrument_set_cluster_swap(&instrument,1u)==THY_OK);CHECK(complete_pad_map(101u));
    CHECK(memory.stage[136u]==THY_COMPANION_PAD_HOLE);
    CHECK(memory.stage[136u+48u*16u]==THY_COMPANION_PAD_CHORD);
    CHECK(strstr(command(1,200u,"companion release 2"),"RELEASED"));
    CHECK(strstr(command(1,201u,"companion pad-map"),"pending=1 seq=3"));
    CHECK(!thy_companion_service(&companion,&engine,202u));
    ++instrument.layout_revision;CHECK(!thy_companion_service(&companion,&engine,203u));CHECK(!companion.active&&!companion.map_state);
    CHECK(strstr(command(1,204u,"companion pad-map"),"pending=1 seq=4"));
    CHECK(strstr(command(0,205u,"companion release 4"),"RELEASED"));CHECK(!thy_companion_service(&companion,&engine,206u));
    CHECK(strstr(command(1,207u,"companion pad-map"),"pending=1 seq=5"));CHECK(thy_companion_expire(&companion,2207u));
    CHECK(!companion.active&&!companion.map_state);
    companion.capacity=1671u;CHECK(strstr(command(1,2208u,"companion pad-map"),"ERR snapshot-unavailable"));
    CHECK(guards());return 1;
}
static int pad_map_sequencer_controls_are_contextual(void)
{
    init();instrument.performance.melody_layout=THY_LAYOUT_CHROMATIC;
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_TRACKS)==THY_STATE_OK);
    CHECK(complete_pad_map(0u));for(unsigned p=0;p<96u;++p)CHECK(memory.stage[136u+16u*p]==THY_COMPANION_PAD_HOLE);
    CHECK(strstr(command(1,100u,"companion release 1"),"RELEASED"));
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_CREATE_TRACK,0)==THY_OK);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS)==THY_STATE_OK);
    CHECK(complete_pad_map(101u));
    CHECK(memory.stage[136u]==THY_COMPANION_PAD_STEP&&!strcmp((char *)memory.stage+137u,"Step 1"));
    CHECK(!strcmp((char *)memory.stage+136u+1u*16u+1u,"Step 13"));
    CHECK(memory.stage[136u+48u*16u]==THY_COMPANION_PAD_NOTE);
    CHECK(strstr(command(1,201u,"companion release 2"),"RELEASED"));
    CHECK(thy_sequencer_ui_configure(&engine.sequencer_ui,THY_SEQ_UI_INPUT,THY_SEQ_INPUT_CHORDS)==THY_OK);
    CHECK(complete_pad_map(202u));CHECK(memory.stage[136u+48u*16u]==THY_COMPANION_PAD_CHORD);
    CHECK(strstr(command(1,302u,"companion release 3"),"RELEASED"));
    engine.sequencer_ui.pads[48].held=1u;engine.sequencer_ui.pads[48].role=THY_SEQ_PAD_INPUT_CHORDS;
    CHECK(thy_sequencer_ui_configure(&engine.sequencer_ui,THY_SEQ_UI_INPUT,THY_SEQ_INPUT_NOTES)==THY_OK);
    CHECK(complete_pad_map(303u));CHECK(memory.stage[136u+48u*16u]==THY_COMPANION_PAD_NOTE);
    CHECK(engine.sequencer_ui.pads[48].held&&engine.sequencer_ui.pads[48].role==THY_SEQ_PAD_INPUT_CHORDS);
    uint32_t key=get32(20);CHECK(strstr(command(1,403u,"companion release 4"),"RELEASED"));
    engine.sequencer_ui.pads[48].held=0u;char query[80];(void)snprintf(query,sizeof query,"companion pad-map %lu",(unsigned long)key);
    CHECK(strstr(command(1,404u,query),"PADMAP unchanged=1"));
    thy_sequencer_ui_close(&engine.sequencer_ui);instrument.context_controls=1u;instrument.context_location=0u;instrument.context_mask=63u;++instrument.revision;
    CHECK(complete_pad_map(405u));CHECK(memory.stage[136u+32u*16u]==THY_COMPANION_PAD_CONTROL);
    CHECK(!strcmp((char *)memory.stage+136u+32u*16u+1u,"Chord oct -"));CHECK(guards());return 1;
}
static int pad_map_key_ignores_acquisition_pressure_and_gate_activity(void)
{
    init();instrument.performance.melody_layout=THY_LAYOUT_CHROMATIC;
    static thy_mpe mpe;thy_engine_bind_expression(&engine,&mpe);
    thy_mpe_config config=engine.expression_config;config.enabled=1u;
    CHECK(thy_engine_expression_configure(&engine,&config)==THY_OK);
    unsigned electrical=0u;while(electrical<96u&&instrument.mapping[electrical]!=48u)++electrical;CHECK(electrical<96u);
    for(unsigned n=0;n<THY_INSTRUMENT_WARMUP_SAMPLES+2u;++n)CHECK(thy_instrument_sample(&instrument,(uint8_t)electrical,0u,n)==THY_OK);
    CHECK(complete_pad_map(50u));uint32_t key=get32(20);
    CHECK(strstr(command(1,150u,"companion release 1"),"RELEASED"));
    char query[80];(void)snprintf(query,sizeof query,"companion pad-map %lu",(unsigned long)key);
    thy_midi_event event;while(thy_core_poll(&core,&event)){}
    for(unsigned n=0;n<8u;++n){CHECK(thy_instrument_sample(&instrument,(uint8_t)electrical,4095u,200u+n*5u)==THY_OK);while(thy_core_poll(&core,&event)){} }
    CHECK(instrument.presses==1u&&instrument.sensors[electrical].held);
    CHECK(strstr(command(1,250u,query),"PADMAP unchanged=1"));
    for(unsigned n=0;n<20u;++n){
        CHECK(thy_instrument_sample(&instrument,(uint8_t)electrical,(uint16_t)(2500u+n*70u),300u+n*8u)==THY_OK);
        thy_engine_expression_tick(&engine,300u+n*8u);while(thy_core_poll(&core,&event)){}
        CHECK(strstr(command(1,301u+n*8u,query),"PADMAP unchanged=1"));
    }
    for(unsigned n=0;n<8u;++n){CHECK(thy_instrument_sample(&instrument,(uint8_t)electrical,0u,500u+n*5u)==THY_OK);while(thy_core_poll(&core,&event)){} }
    CHECK(instrument.releases==1u&&!instrument.sensors[electrical].held);
    CHECK(strstr(command(1,550u,query),"PADMAP unchanged=1"));
    instrument.input_enabled=0u;
    for(unsigned n=0;n<8u;++n)CHECK(thy_instrument_sample(&instrument,(uint8_t)electrical,4095u,600u+n*5u)==THY_OK);
    CHECK(instrument.presses==2u&&instrument.sensors[electrical].blocked);
    for(unsigned n=0;n<8u;++n)CHECK(thy_instrument_sample(&instrument,(uint8_t)electrical,0u,700u+n*5u)==THY_OK);
    CHECK(instrument.releases==2u);CHECK(strstr(command(1,750u,query),"PADMAP unchanged=1"));
    CHECK(!companion.active&&companion.sequence==1u);
    ++instrument.layout_revision;CHECK(strstr(command(1,800u,query),"PADMAP pending=1 seq=2"));
    companion.active=0u;CHECK(!thy_companion_service(&companion,&engine,801u)&&!companion.map_state);
    init();instrument.performance.melody_layout=THY_LAYOUT_CHROMATIC;
    CHECK(complete_pad_map(0u));key=get32(20);CHECK(strstr(command(1,100u,"companion release 1"),"RELEASED"));
    (void)snprintf(query,sizeof query,"companion pad-map %lu",(unsigned long)key);
    for(unsigned n=0u;n<16u;++n){
        thy_generated_handle handle;
        CHECK(thy_core_generated_acquire(&core,1u,1u,(uint8_t)(60u+n%7u),0u,THY_ROUTE_DIN,96u,&handle)==THY_OK);
        CHECK(strstr(command(1,200u+n*2u,query),"PADMAP unchanged=1"));
        CHECK(thy_core_generated_release(&core,handle)==THY_OK);while(thy_core_poll(&core,&event)){}
        CHECK(strstr(command(1,201u+n*2u,query),"PADMAP unchanged=1"));
    }
    CHECK(guards());return 1;
}
static int capture_performance(uint32_t now)
{
    if(companion.active){char release[64];(void)snprintf(release,sizeof release,"companion release %lu",(unsigned long)companion.sequence);
        CHECK(strstr(command(0,now,release),"RELEASED"));}
    CHECK(strstr(command(1,now,"companion performance snapshot"),"PERFORMANCE seq="));return 1;
}
static void drain_events(void)
{thy_midi_event event;while(thy_core_poll(&core,&event)){} }
static int performance_snapshot_is_frozen_exclusive_and_readonly(void)
{
    init();provider_available=0;
    CHECK(strstr(command(0,0,"companion performance snapshot"),"ERR busy"));
    companion.capacity=THY_COMPANION_PERFORMANCE_BYTES-1u;
    CHECK(strstr(command(1,0,"companion performance snapshot"),"ERR snapshot-unavailable"));
    companion.capacity=sizeof memory.stage;
    uint32_t engine_before=thy_settings_crc32((const uint8_t *)&engine,sizeof engine);
    uint32_t core_before=thy_settings_crc32((const uint8_t *)&core,sizeof core);
    CHECK(capture_performance(10u));
    CHECK(!memcmp(memory.stage,"THYPER1",8u));CHECK(get16(8)==1u&&get16(10)==1024u);
    CHECK(get16(28)==THY_CLOCK_PPQN&&memory.stage[31]==UINT8_MAX&&memory.stage[157]==UINT8_MAX);
    CHECK(get32(20)==engine.sequencer.revision&&get32(24)==engine.rhythm_transport.generation);
    CHECK(companion.crc==thy_settings_crc32(memory.stage,THY_COMPANION_PERFORMANCE_BYTES));
    CHECK(engine_before==thy_settings_crc32((const uint8_t *)&engine,sizeof engine));
    CHECK(core_before==thy_settings_crc32((const uint8_t *)&core,sizeof core)&&!visual_calls);
    for(unsigned n=0;n<THY_MPE_MEMBERS;++n){unsigned at=160u+n*24u;
        CHECK(!memory.stage[at]&&memory.stage[at+1u]==UINT8_MAX&&memory.stage[at+19u]==UINT8_MAX);}
    for(unsigned n=520u;n<1024u;++n)CHECK(!memory.stage[n]);
    memcpy(frozen,memory.stage,1024u);engine.rhythm_transport.position=UINT64_C(0x1122334455667788);
    CHECK(strstr(command(1,11u,"companion performance snapshot"),"ERR snapshot-active"));
    CHECK(strstr(command(1,11u,"companion snapshot"),"ERR snapshot-active"));
    CHECK(strstr(command(1,11u,"companion harmony snapshot"),"ERR snapshot-active"));
    CHECK(strstr(command(1,11u,"companion pad-map"),"ERR snapshot-active"));
    CHECK(strstr(command(1,11u,"companion get 1 1023 1"),"DATA seq=1"));
    CHECK(strstr(command(1,11u,"companion get 1 1023 2"),"ERR offset"));
    CHECK(!memcmp(frozen,memory.stage,1024u));
    CHECK(capture_performance(12u));CHECK(get32(136)==0x55667788u&&get32(140)==0x11223344u);
    CHECK(thy_companion_expire(&companion,5013u)&&!companion.active);
    CHECK(strstr(command(1,5014u,"companion get 2 0 1"),"ERR generation"));
    provider_available=1;CHECK(strstr(command(1,5015u,"companion snapshot"),"SNAPSHOT seq=3"));
    CHECK(strstr(command(1,5016u,"companion release 3"),"RELEASED"));CHECK(capture_performance(5017u));
    CHECK(strstr(command(1,5018u,"companion release 4"),"RELEASED"));
    CHECK(strstr(command(1,5019u,"companion snapshot"),"ERR rate"));
    CHECK(guards());return 1;
}
static int performance_mpe_reports_real_admission_backpressure_and_retirement(void)
{
    init();static thy_mpe mpe;thy_engine_bind_expression(&engine,&mpe);
    thy_mpe_config config=engine.expression_config;config.enabled=1u;config.pressure_output[0]=THY_MPE_CC74;
    CHECK(thy_engine_expression_configure(&engine,&config)==THY_OK);CHECK(sizeof(thy_mpe_voice)==24u);
    const int16_t chord[]={60,64,67};int consumed=-1;
    CHECK(thy_core_advance(&core,0u)==THY_OK);
    CHECK(thy_mpe_press_ex(&mpe,0u,0u,60u,chord,3u,THY_ROUTE_USB0,91u,30u,&consumed,12u)==THY_OK);
    CHECK(thy_mpe_pressure(&mpe,0u,107u)==THY_OK&&thy_mpe_bend(&mpe,0u,10000u)==THY_OK);
    CHECK(capture_performance(1u));CHECK(memory.stage[152]&THY_COMPANION_PERF_MPE_ENABLED);
    CHECK(memory.stage[160]==THY_MPE_HELD&&memory.stage[166]==THY_ROUTE_USB0);
    CHECK(memory.stage[168]==91u&&memory.stage[169]==THY_MPE_CC74);
    CHECK(memory.stage[170]==0u&&memory.stage[171]==12u&&get16(172)==8192u);
    CHECK(memory.stage[174]==107u&&get16(176)==10000u);
    CHECK(memory.stage[167]&THY_COMPANION_VOICE_EXPRESSION_PENDING);
    for(unsigned n=1;n<3u;++n){unsigned at=160u+n*24u;
        CHECK(memory.stage[at]==THY_MPE_HELD&&!memory.stage[at+6u]);
        CHECK(memory.stage[at+7u]&THY_COMPANION_VOICE_PENDING);}
    thy_midi_event cc={0u,0xb0u,1u,0u};unsigned filled=0;
    while(thy_core_emit_batch(&core,&cc,1u)==THY_OK)++filled;
    CHECK(filled>100u);uint16_t queue=core.queue_count;
    CHECK(thy_mpe_service(&mpe)==THY_QUEUE_FULL);
    CHECK(capture_performance(2u));CHECK(core.queue_count==queue&&memory.stage[170]==0u&&memory.stage[174]==107u);
    drain_events();for(unsigned n=0;n<15u;++n)CHECK(thy_mpe_service(&mpe)==THY_OK);
    drain_events();CHECK(thy_core_advance(&core,60u)==THY_OK);
    CHECK(capture_performance(3u));
    for(unsigned n=0;n<3u;++n){unsigned at=160u+n*24u;CHECK(memory.stage[at+6u]==THY_ROUTE_USB0);
        CHECK(!(memory.stage[at+7u]&(THY_COMPANION_VOICE_PENDING|THY_COMPANION_VOICE_EXPRESSION_PENDING)));
        CHECK(memory.stage[at+10u]==107u&&get16(at+12u)==10000u);}
    drain_events();CHECK(thy_core_sustain(&core,THY_ROUTE_USB0,mpe.voices[0].channel,1)==THY_OK);
    CHECK(thy_mpe_release(&mpe,0u)==THY_OK);CHECK(capture_performance(4u));
    CHECK(memory.stage[160]==THY_MPE_RETIRING&&(memory.stage[167]&THY_COMPANION_VOICE_SUSTAINED));
    CHECK(memory.stage[168]==91u&&memory.stage[169]==THY_MPE_CC74);
    const int16_t next[]={72};CHECK(thy_mpe_press(&mpe,0u,0u,72u,next,1u,THY_ROUTE_USB0,44u,0u,&consumed)==THY_OK);
    CHECK(capture_performance(5u));CHECK(memory.stage[168]==91u&&memory.stage[192]==44u);
    CHECK(thy_mpe_release(&mpe,0u)==THY_OK);drain_events();
    CHECK(thy_core_sustain(&core,THY_ROUTE_USB0,mpe.voices[0].channel,0)==THY_OK);
    for(unsigned n=0;n<15u;++n)CHECK(thy_mpe_service(&mpe)==THY_OK);
    CHECK(capture_performance(6u));for(unsigned n=0;n<THY_MPE_MEMBERS;++n){unsigned at=160u+n*24u;
        CHECK(memory.stage[at]==THY_MPE_FREE&&memory.stage[at+1u]==UINT8_MAX&&!memory.stage[at+10u]);}
    drain_events();config.upper_zone=1u;config.glide=1u;config.pressure_output[1]=THY_MPE_CC1;
    CHECK(thy_engine_expression_configure(&engine,&config)==THY_OK);
    const int16_t root[]={60},neighbor[]={62};
    CHECK(thy_mpe_press(&mpe,48u,48u,60u,root,1u,THY_ROUTE_ALL,77u,0u,&consumed)==THY_OK);
    CHECK(thy_mpe_press(&mpe,52u,52u,62u,neighbor,1u,THY_ROUTE_ALL,77u,0u,&consumed)==THY_OK&&consumed);
    CHECK(thy_mpe_pressure(&mpe,48u,20u)==THY_OK&&thy_mpe_pressure(&mpe,52u,90u)==THY_OK);
    CHECK(capture_performance(7u));CHECK(memory.stage[31]==15u&&memory.stage[161]==14u);
    CHECK(memory.stage[163]==48u&&memory.stage[164]==48u&&memory.stage[165]==THY_ROUTE_ALL);
    CHECK(memory.stage[169]==THY_MPE_CC1&&memory.stage[179]==52u);
    CHECK(memory.stage[167]&THY_COMPANION_VOICE_MODIFIER);
    CHECK(get16(176)==thy_mpe_glide_bend(20u,90u,2,config.bend_range));
    CHECK(guards());return 1;
}
static int performance_playheads_copy_transport_loop_pause_queue_and_clock_loss(void)
{
    init();thy_transport *t=&engine.rhythm_transport;thy_clock *clock=&engine.musical_clock;thy_sequencer *seq=&engine.sequencer;
    CHECK(thy_sequencer_create(seq,0u)==THY_OK&&thy_sequencer_create(seq,20u)==THY_OK);
    CHECK(thy_sequencer_loop(seq,t,0u,960u,4800u)==THY_OK);
    CHECK(thy_sequencer_loop(seq,t,20u,120u,1080u)==THY_OK);
    seq->tracks[20].muted=1u;seq->tracks[20].flags|=THY_SEQ_FLAG_ONESHOT;
    CHECK(thy_transport_set_enabled(t,0u,1u)==THY_TIME_OK&&thy_transport_set_enabled(t,20u,1u)==THY_TIME_OK);
    CHECK(thy_sequencer_record(seq,t,clock,0u,THY_SEQ_SOURCE_PADS,THY_SEQ_REPLACE,960u,4800u)==THY_OK);
    CHECK(capture_performance(1u));CHECK(memory.stage[156]==THY_SEQ_ARMED&&memory.stage[157]==0u&&memory.stage[543]==THY_SEQ_ARMED);
    CHECK(thy_transport_start(t,clock)==THY_TIME_OK);CHECK(thy_sequencer_capture_advance(seq,t,clock)==THY_OK);
    clock->tick=1000u;CHECK(thy_transport_advance(t,clock)==THY_TIME_OK);
    CHECK(capture_performance(2u));CHECK(memory.stage[153]==THY_TRANSPORT_PLAYING);
    CHECK(get32(520)==960u&&get32(524)==3840u&&get32(528)==1960u&&get32(532)==240u);
    CHECK(memory.stage[540]==(THY_COMPANION_TRACK_CREATED|THY_COMPANION_TRACK_ENABLED|THY_COMPANION_TRACK_RUNNING|THY_COMPANION_TRACK_POSITION));
    CHECK(get32(520u+20u*24u+8u)==160u);
    CHECK(memory.stage[520u+20u*24u+20u]&(THY_COMPANION_TRACK_MUTED|THY_COMPANION_TRACK_ONESHOT));
    CHECK(memory.stage[156]==THY_SEQ_RECORDING&&memory.stage[543]==THY_SEQ_RECORDING);
    thy_sequencer_cancel(seq);CHECK(thy_transport_pause(t,clock)==THY_TIME_OK);uint32_t phase=t->tracks[0].playhead;
    clock->tick+=1000u;CHECK(thy_transport_advance(t,clock)==THY_TIME_OK);
    CHECK(capture_performance(3u));CHECK(memory.stage[153]==THY_TRANSPORT_PAUSED&&get32(528)==phase&&get32(136)==1000u);
    CHECK(thy_transport_continue(t,clock)==THY_TIME_OK);
    CHECK(thy_clock_set_source(clock,THY_CLOCK_SOURCE_USB,0u)==THY_TIME_OK);
    CHECK(thy_transport_advance(t,clock)==THY_TIME_OK);CHECK(capture_performance(999999u));
    CHECK(memory.stage[152]&THY_COMPANION_PERF_SUSPENDED);CHECK(!(memory.stage[152]&THY_COMPANION_PERF_CLOCK_READY));
    CHECK(get32(528)==phase&&memory.stage[155]==THY_CLOCK_WAITING);
    CHECK(thy_clock_midi_pulse(clock,1u)==THY_TIME_OK&&thy_transport_advance(t,clock)==THY_TIME_OK);
    CHECK(capture_performance(1000000u));CHECK(!(memory.stage[152]&THY_COMPANION_PERF_SUSPENDED));CHECK(get32(528)==phase+40u);
    CHECK(thy_transport_request_track(t,20u,0u,THY_TRANSPORT_BEAT,clock)==THY_TIME_OK);
    CHECK(capture_performance(1000001u));CHECK(memory.stage[520u+20u*24u+21u]==THY_TRANSPORT_PENDING_STOP);
    CHECK(thy_transport_stop(t,clock)==THY_TIME_OK);CHECK(capture_performance(1000002u));
    CHECK(memory.stage[153]==THY_TRANSPORT_STOPPED&&get32(136)==0u&&get32(528)==960u);
    CHECK(thy_clock_set_source(clock,THY_CLOCK_SOURCE_INTERNAL,2u)==THY_TIME_OK);
    CHECK(thy_transport_set_enabled(t,0u,0u)==THY_TIME_OK&&thy_transport_set_enabled(t,20u,1u)==THY_TIME_OK);
    CHECK(thy_transport_start(t,clock)==THY_TIME_OK&&thy_sequencer_advance(seq,&core,t,clock,NULL)==THY_OK);
    clock->tick+=1000u;CHECK(thy_transport_advance(t,clock)==THY_TIME_OK);
    CHECK(thy_sequencer_advance(seq,&core,t,clock,NULL)==THY_OK&&capture_performance(1000003u));
    CHECK(!(memory.stage[520u+20u*24u+20u]&(THY_COMPANION_TRACK_ENABLED|THY_COMPANION_TRACK_RUNNING)));
    CHECK(memory.stage[520u+20u*24u+20u]&THY_COMPANION_TRACK_ONESHOT);
    CHECK(get32(520u+20u*24u+8u)==t->tracks[20].playhead);
    CHECK(!core.queue_count&&!visual_calls);CHECK(guards());return 1;
}
int main(void)
{
    if(!frozen_snapshot_and_runtime_are_actual()||!exclusive_staging_expiry_and_readonly_pad_selection()||
       !schema_scoped_parameters_navigation_and_explicit_actions()||!remote_controls_are_taps_and_cannot_release_a_physical_control()||
       !maximum_note_set_and_long_labels_keep_wire_fields_separate()||
       !optional_harmony_snapshot_reuses_staging_and_preserves_visual_cadence()||
       !harmony_scale_rows_keys_and_actions_are_bounded_and_schema_scoped()||
       !harmony_controls_are_scoped_and_do_not_release_physical_holds()||
       !reset_confirmation_uses_c_scope_and_rejects_stale_remote_navigation()||
       !pad_map_cooperative_publication_is_readonly_and_leased()||
       !pad_map_cancellation_changes_and_physical_roles()||!pad_map_sequencer_controls_are_contextual()||
       !pad_map_key_ignores_acquisition_pressure_and_gate_activity()||
       !performance_snapshot_is_frozen_exclusive_and_readonly()||
       !performance_mpe_reports_real_admission_backpressure_and_retirement()||
       !performance_playheads_copy_transport_loop_pause_queue_and_clock_loss())return 1;
    printf("Companion:16 scenarios passed (%u assertions); service%zu bytes, caller-owned staging%u bytes.\n",assertions,sizeof(thy_companion),THY_COMPANION_SNAPSHOT_BYTES);return 0;
}
