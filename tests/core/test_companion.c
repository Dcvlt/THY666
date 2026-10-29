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
    CHECK(strstr(response.text," role=instrument backup=1 song=1 companion=1 harmony=1 harmony_bytes=800 reset=1\r\n"));
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
int main(void)
{
    if(!frozen_snapshot_and_runtime_are_actual()||!exclusive_staging_expiry_and_readonly_pad_selection()||
       !schema_scoped_parameters_navigation_and_explicit_actions()||!remote_controls_are_taps_and_cannot_release_a_physical_control()||
       !maximum_note_set_and_long_labels_keep_wire_fields_separate()||
       !optional_harmony_snapshot_reuses_staging_and_preserves_visual_cadence()||
       !harmony_scale_rows_keys_and_actions_are_bounded_and_schema_scoped()||
       !harmony_controls_are_scoped_and_do_not_release_physical_holds()||
       !reset_confirmation_uses_c_scope_and_rejects_stale_remote_navigation())return 1;
    printf("Companion:9 scenarios passed (%u assertions); service%zu bytes, caller-owned staging%u bytes.\n",assertions,sizeof(thy_companion),THY_COMPANION_SNAPSHOT_BYTES);return 0;
}
