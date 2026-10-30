/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_engine.h"
#include "../../core/thy_settings.h"
#include "../../core/thy_lighting.h"
static thy_core core;
static thy_instrument instrument;
static thy_engine engine;
static thy_instrument_pad layout[96];
static thy_seq_event history[THY_SEQ_EVENTS];
static thy_mpe expression;
static unsigned checks;
static unsigned planner_calls;
static int counted_plan(void *ctx,uint8_t pad,const int16_t notes[7],uint8_t mask,int16_t out[7])
{++planner_calls;return thy_engine_harmony_voicing_plan(ctx,pad,notes,mask,out);}
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static void drain(void){thy_midi_event ev;while(thy_core_poll(&core,&ev)){} }
static void settle(void){for(unsigned n=0;n<52u;++n)(void)thy_engine_advance(&engine,core.now_ms+1u);}
static int init(void)
{
    const int16_t degrees[]={0,2,4,5,7,9,11};thy_core_init(&core);thy_instrument_init(&instrument,&core);
    for(unsigned p=0;p<96u;++p)layout[p]=(thy_instrument_pad){{60,64,67},7,1};
    CHECK(thy_instrument_set_layout(&instrument,layout,60,degrees,7)==THY_OK);
    thy_engine_init(&engine,&instrument,21,70,history);thy_engine_bind_music(&engine);
    instrument.chord_layout=THY_CHORD_LAYOUT_DIATONIC;engine.harmony.palette=1u;
    CHECK(thy_engine_workspace(&engine,THY_PLAY_ENGINE_HARMONY)==THY_STATE_OK);settle();
    CHECK(engine.harmony.cache_ready&&engine.harmony.key_mask==0xab5u);return 1;
}
static int attack(unsigned p)
{
    int16_t notes[THY_MAX_NOTES];size_t count=0;
    CHECK(thy_instrument_preview_pad(&instrument,(uint8_t)p,notes,&count)==THY_OK);
    CHECK(thy_engine_press_shaped(&engine,(uint8_t)p,notes,count,0,1,5,80,NULL)==THY_OK);drain();settle();return 1;
}
static int release(unsigned p)
{CHECK(instrument.musical_release(instrument.musical_context,(uint8_t)p)==THY_OK);drain();settle();return 1;}
static int live_and_playable(void)
{
    CHECK(init());CHECK(attack(0));CHECK(engine.harmony.input_mask==0x091u);CHECK(release(0));
    CHECK(engine.harmony.input_mask==0x091u&&engine.harmony.current_pad==0u);
    CHECK(engine.harmony.count&&engine.harmony.choices[engine.harmony.selected_suggestion].reason==THY_HARMONY_PATTERN_NEXT);
    CHECK(engine.harmony.choices[engine.harmony.selected_suggestion].root==5u); /* Familiar C -> F. */
    for(unsigned row=0;row<engine.harmony.count;++row){
        const thy_harmony_choice *choice=&engine.harmony.choices[row];CHECK(choice->pads!=0u);
        CHECK(choice->mask<=4095u);
        for(unsigned p=0;p<48u;++p)if(choice->pads&(UINT64_C(1)<<p)){
            int16_t notes[THY_MAX_NOTES];size_t count=0;uint16_t mask=0;
            CHECK(thy_instrument_preview_pad(&instrument,(uint8_t)p,notes,&count)==THY_OK);
            for(size_t n=0;n<count;++n)mask|=(uint16_t)(1u<<(notes[n]%12));CHECK(mask==choice->mask);
        }
    }
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_PIN_INPUT,1)==THY_STATE_OK);
    CHECK(attack(8));CHECK(engine.harmony.input_mask==0x091u);CHECK(release(8));
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_PIN_INPUT,0)==THY_STATE_OK);
    CHECK(attack(8));CHECK(engine.harmony.input_mask==0x244u);CHECK(release(8));
    CHECK(engine.harmony.choices[engine.harmony.selected_suggestion].root==7u);return 1;
}
static int arp_and_mpe_input(void)
{
    CHECK(init());thy_arp_config config=engine.arps[0].config;config.enabled=1;config.layer=0;
    CHECK(thy_engine_arp_configure(&engine,0,&config)==THY_OK);CHECK(attack(0));
    CHECK(engine.harmony.input_mask==0x091u);
    for(unsigned n=0;n<80u;++n){CHECK(thy_engine_advance(&engine,core.now_ms+10u)==THY_OK);drain();CHECK(engine.harmony.input_mask==0x091u);}
    thy_engine_panic(&engine);drain();
    CHECK(engine.harmony.input_mask==0x091u); /* Panic doesn't fabricate a new chord. */
    CHECK(init());thy_engine_bind_expression(&engine,&expression);thy_mpe_config mpe=engine.expression_config;mpe.enabled=1;
    CHECK(thy_engine_expression_configure(&engine,&mpe)==THY_OK);CHECK(attack(0));
    CHECK(engine.harmony.input_mask==0x091u);CHECK(thy_mpe_release(&expression,0)==THY_OK);drain();
    CHECK(engine.harmony.input_mask==0x091u);return 1;
}
static int explicit_key_and_owners(void)
{
    CHECK(init());CHECK(attack(0));
    instrument.voicing_previous[0]=60;instrument.voicing_previous[1]=64;instrument.voicing_previous[2]=67;
    instrument.voicing_previous_mask=7u;
    thy_pad_owner before=core.owners[0];uint32_t revision=instrument.revision;
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_TARGET_ROOT,7)==THY_STATE_OK);
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_TARGET_FAMILY,0)==THY_STATE_OK);
    CHECK(instrument.performance.root==0u&&instrument.revision==revision);
    CHECK(engine.harmony.comparison.added==(1u<<6)&&engine.harmony.comparison.removed==(1u<<5));
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_APPLY_KEY,0)==THY_STATE_INVALID);
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_APPLY_KEY,1)==THY_STATE_OK);
    CHECK(instrument.performance.root==7u&&engine.scale==21u);
    const int16_t degrees[]={0,2,4,5,7,9,11};
    CHECK(thy_instrument_set_layout(&instrument,layout,60,degrees,7)==THY_OK);
    CHECK(instrument.voicing_previous_mask==7u&&instrument.voicing_previous[1]==64);
    CHECK(memcmp(&before,&core.owners[0],sizeof before)==0);CHECK(release(0));CHECK(core.sounding_count==0u);
    unsigned unsupported=0;while(unsupported<thy_guidance_scale_count()&&thy_guidance_playable_catalog(unsupported)>=0)++unsupported;
    CHECK(unsupported<thy_guidance_scale_count());revision=instrument.revision;
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_TARGET_FAMILY,(int32_t)unsupported)==THY_STATE_OK);
    CHECK(!engine.harmony.can_apply);CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_APPLY_KEY,1)==THY_STATE_UNAVAILABLE);
    CHECK(instrument.revision==revision);return 1;
}
static int discovery_holes_and_lifecycle(void)
{
    CHECK(init());CHECK(attack(0));CHECK(release(0));
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_PAGE,THY_HARMONY_SCALES)==THY_STATE_OK);settle();
    CHECK(engine.harmony.scale_count&&engine.harmony.scale_total>=engine.harmony.scale_count);
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_COMPARE_SCALE,0)==THY_STATE_OK);
    CHECK(engine.harmony.page==THY_HARMONY_KEY&&instrument.performance.root==0u);
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_CLEAR_INPUT,0)==THY_STATE_OK);
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_PAGE,THY_HARMONY_SCALES)==THY_STATE_OK);settle();
    CHECK(!engine.harmony.scale_count&&!engine.harmony.scale_total);
    CHECK(thy_instrument_set_context_controls(&instrument,1)==THY_OK);settle();
    for(unsigned p=0;p<48u;++p)CHECK(!thy_instrument_context_pad(&instrument,(uint8_t)p)&&engine.harmony.pad_masks[p]);
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK);CHECK(engine.state.level==THY_STATE_GROUPS);
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK);CHECK(engine.state.level==THY_STATE_PLAY);
    CHECK(thy_engine_harmony_active(&engine));
    CHECK(thy_engine_workspace(&engine,THY_PLAY_ENGINE_PLAY)==THY_STATE_OK);CHECK(!thy_engine_harmony_active(&engine));return 1;
}
static int saved_configuration_and_label(void)
{
    CHECK(init());engine.harmony.pattern=3;engine.harmony.highlights=0;engine.harmony.motion=0;
    engine.harmony.view=2;engine.harmony.control_bank=1;engine.harmony.octave_target=0;
    engine.harmony.analysis=2;engine.harmony.right_mode=1;engine.harmony.bass_root=1;
    engine.harmony.low_note=36;engine.harmony.high_note=84;
    CHECK(attack(0));CHECK(release(0));
    static uint8_t encoded[THY_SETTINGS_PAYLOAD_MAX];size_t used=0;
    CHECK(thy_settings_encode(&engine,encoded,sizeof encoded,&used));CHECK(thy_settings_payload_version(encoded,used)==THY_SETTINGS_VERSION);
    CHECK(thy_settings_validate(encoded,used,70));CHECK(thy_instrument_set_input_enabled(&instrument,0)==THY_OK);
    engine.state.reset_open=1u;CHECK(!thy_settings_can_apply(&engine,encoded,used));
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_ACTIVATE,0)==THY_STATE_BLOCKED);
    CHECK(!thy_settings_apply(&engine,encoded,used));engine.state.reset_open=0u;
    CHECK(thy_settings_apply(&engine,encoded,used));
    CHECK(thy_engine_harmony_active(&engine)&&engine.harmony.pattern==3&&!engine.harmony.highlights&&!engine.harmony.motion);
    CHECK(!engine.harmony.input_mask&&engine.harmony.current_pad==255u);
    CHECK(engine.harmony.view==2&&engine.harmony.control_bank==1&&engine.harmony.octave_target==0);
    CHECK(engine.harmony.analysis==2&&engine.harmony.right_mode==1&&engine.harmony.bass_root==1);
    CHECK(engine.harmony.low_note==36&&engine.harmony.high_note==84);
    char label[32];thy_engine_harmony_label((1u<<5)|(1u<<9)|1u,5,label);CHECK(strcmp(label,"F MAJ")==0);return 1;
}
static int right_hand_analysis_and_controls(void)
{
    CHECK(init());CHECK(attack(0));CHECK(release(0));
    CHECK(attack(52));CHECK(engine.harmony.base_mask==0x091u&&engine.harmony.right_mask==2u);
    CHECK(engine.harmony.input_mask==0x093u&&engine.harmony.current_pad==0u);
    CHECK(release(52));CHECK(engine.harmony.input_mask==0x091u&&!engine.harmony.right_mask);
    CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_ANALYSIS,1)==THY_STATE_OK);
    CHECK(attack(52));CHECK(engine.harmony.input_mask==0x091u&&engine.harmony.right_mask==2u);
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_CAPTURE_HELD,0)==THY_STATE_OK);
    CHECK(engine.harmony.input_mask==0x093u&&engine.harmony.pinned);CHECK(release(52));
    CHECK(engine.harmony.input_mask==0x093u);CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_PIN_INPUT,0)==THY_STATE_OK);
    CHECK(engine.harmony.input_mask==0x091u);
    int16_t note;CHECK(thy_engine_harmony_melody_plan(&engine,52,&note)==1&&note==61);
    CHECK(thy_engine_control(&engine,3,1,1)==1);
    CHECK(thy_engine_harmony_melody_plan(&engine,52,&note)==1&&note==64);
    uint8_t root=instrument.performance.root;uint16_t scale=engine.scale;
    CHECK(thy_engine_control(&engine,2,7,1)==1);CHECK(engine.harmony.target_root==7u);
    CHECK(thy_engine_control(&engine,3,2,1)==1);CHECK(engine.harmony.preview_scale==0u);
    CHECK(instrument.performance.root==root&&engine.scale==scale);
    uint8_t selected=instrument.selected_chord;CHECK(thy_engine_control(&engine,1,8,1)==1);
    CHECK(engine.harmony.selected_suggestion==1u&&instrument.selected_chord==selected);
    CHECK(thy_engine_control(&engine,3,0,1)==1);CHECK(engine.harmony.control_bank==1u);
    CHECK(thy_engine_control(&engine,1,7,1)==1);CHECK(instrument.octave_layers==9u);
    CHECK(thy_engine_control(&engine,3,10,1)==1);CHECK(instrument.performance.melody_octave==1);
    CHECK(thy_engine_control(&engine,3,11,1)==1);CHECK(thy_engine_control(&engine,3,9,1)==1);
    CHECK(instrument.performance.chord_octave==-1&&instrument.performance.melody_octave==1);
    CHECK(!core.sounding_count);return 1;
}
static int cached_hints_avoid_voicing_search(void)
{
    CHECK(init());CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_VOICING,2)==THY_STATE_OK);settle();
    instrument.musical_voicing_plan=counted_plan;planner_calls=0;
    thy_lighting lights;thy_lighting_config config;uint32_t colors[96];
    thy_lighting_init(&lights);thy_lighting_defaults(&config);
    CHECK(thy_lighting_render(&lights,&instrument,&config,core.now_ms,colors)==THY_OK);
    CHECK(planner_calls==0u&&colors[0]);
    config.enabled=0;CHECK(thy_lighting_render(&lights,&instrument,&config,core.now_ms+1u,colors)==THY_OK);
    CHECK(planner_calls==0u);for(unsigned p=0;p<96u;++p)CHECK(!colors[p]);return 1;
}
static unsigned peak(uint32_t color)
{unsigned a=color&255u,b=(color>>8u)&255u,c=(color>>16u)&255u;return a>b?(a>c?a:c):(b>c?b:c);}
static int chromatic_contrast_in_both_workspaces(void)
{
    CHECK(init());thy_lighting lights;thy_lighting_config config;uint32_t off[96],on[96];
    CHECK(instrument.light_key_contrast==1u);
    CHECK(thy_state_set(&engine.state,THY_ROW_PERFORMANCE_KEYCONTRAST,0)==THY_STATE_OK);
    int32_t contrast=-1;CHECK(thy_state_get(&engine.state,THY_ROW_LIGHTS_KEYCONTRAST,&contrast)==THY_STATE_OK&&contrast==0);
    CHECK(thy_state_set(&engine.state,THY_ROW_LIGHTS_KEYCONTRAST,1)==THY_STATE_OK);
    CHECK(thy_state_get(&engine.state,THY_ROW_PERFORMANCE_KEYCONTRAST,&contrast)==THY_STATE_OK&&contrast==1);
    thy_lighting_defaults(&config);config.fade_ms=0;config.key_contrast=0;instrument.light_key_contrast=0;
    thy_lighting_init(&lights);CHECK(thy_lighting_render(&lights,&instrument,&config,core.now_ms,off)==THY_OK);
    config.key_contrast=1;instrument.light_key_contrast=1;thy_lighting_init(&lights);
    CHECK(thy_lighting_render(&lights,&instrument,&config,core.now_ms,on)==THY_OK);
    CHECK(peak(on[52])<peak(off[52])&&peak(on[52])>0);CHECK(on[48]==off[48]);
    CHECK(thy_engine_workspace(&engine,THY_PLAY_ENGINE_PLAY)==THY_STATE_OK);
    for(unsigned mode=0;mode<2u;++mode){
    instrument.performance.melody_layout=mode?THY_LAYOUT_ALL_LIT:THY_LAYOUT_CHROMATIC;
    config.key_contrast=0;thy_lighting_init(&lights);CHECK(thy_lighting_render(&lights,&instrument,&config,core.now_ms,off)==THY_OK);
    config.key_contrast=1;thy_lighting_init(&lights);CHECK(thy_lighting_render(&lights,&instrument,&config,core.now_ms,on)==THY_OK);
    for(unsigned column=0;column<12u;++column){unsigned p=48u+column*4u;
        int black=column==1u||column==3u||column==6u||column==8u||column==10u;
        CHECK(black?peak(on[p])<peak(off[p]):on[p]==off[p]);
    }}return 1;
}
static int exploration_stable_ownership(void)
{
    CHECK(init());CHECK(attack(0));CHECK(release(0));
    CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_PALETTE,0)==THY_STATE_OK);settle();
    uint16_t original[48];memcpy(original,engine.harmony.pad_masks,sizeof original);
    for(unsigned p=0;p<48u;++p){CHECK(engine.harmony.pad_masks[p]);CHECK(engine.harmony.pad_roots[p]==p/4u);}
    unsigned roots=0,borrowed=0;
    for(unsigned n=0;n<engine.harmony.count;++n){roots|=1u<<engine.harmony.choices[n].root;borrowed|=engine.harmony.choices[n].mask&~engine.harmony.key_mask;}
    unsigned unique=0;for(;roots;roots&=roots-1u)++unique;CHECK(unique>=5u);
    CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_LOCKMAP,1)==THY_STATE_OK);settle();
    unsigned pad=engine.harmony.choices[0].pad;CHECK(attack(pad));thy_pad_owner owner=core.owners[pad];
    thy_harmony_choice choices[9];memcpy(choices,engine.harmony.choices,sizeof choices);
    CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_PALETTE,2)==THY_STATE_BLOCKED);
    CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_DIRECTION,3)==THY_STATE_OK);settle();
    CHECK(!memcmp(choices,engine.harmony.choices,sizeof choices));CHECK(!memcmp(&owner,&core.owners[pad],sizeof owner));
    CHECK(release(pad));CHECK(!core.sounding_count&&!engine.harmony.chord_held);
    CHECK(!memcmp(original,engine.harmony.pad_masks,sizeof original));
    CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_LOCKMAP,0)==THY_STATE_OK);settle();
    CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_OUTSIDE,100)==THY_STATE_OK);settle();
    borrowed=0;for(unsigned n=0;n<engine.harmony.count;++n)borrowed|=engine.harmony.choices[n].mask&~engine.harmony.key_mask;CHECK(borrowed);
    CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_STAYINKEY,1)==THY_STATE_OK);settle();
    CHECK(engine.harmony.count);for(unsigned n=0;n<engine.harmony.count;++n)CHECK(!(engine.harmony.choices[n].mask&~engine.harmony.key_mask));
    for(unsigned p=0;p<48u;++p)CHECK(engine.harmony.pad_masks[p]); /* advice filter never disables musical pads */
    CHECK(thy_instrument_set_context_controls(&instrument,1)==THY_OK);
    CHECK(!thy_instrument_context_pad(&instrument,47));
    CHECK(thy_instrument_set_cluster_swap(&instrument,1)==THY_OK);
    CHECK(thy_instrument_role_pad(&instrument,95)==47);return 1;
}
static int published_hints_and_navigation(void)
{
    CHECK(init());CHECK(attack(0));CHECK(release(0));
    uint8_t count=engine.harmony.count,roles[48];memcpy(roles,engine.harmony.pad_roles,sizeof roles);
    ++instrument.layout_revision;thy_engine_harmony_tick(&engine);
    CHECK(engine.harmony.cache_cursor==4u&&engine.harmony.count==count);
    CHECK(!memcmp(roles,engine.harmony.pad_roles,sizeof roles));settle();
    CHECK(attack(52));CHECK(engine.harmony.ranking_mask==0x091u);
    CHECK(thy_engine_advance(&engine,core.now_ms+200u)==THY_OK);CHECK(engine.harmony.ranking_mask==0x093u);
    CHECK(release(52));CHECK(engine.harmony.input_mask==0x091u&&engine.harmony.ranking_mask==0x093u);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK&&engine.harmony.palette==2u);
    settle();CHECK(thy_engine_navigate(&engine,THY_STATE_DOWN)==THY_STATE_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK&&engine.state.level==THY_STATE_ROWS);
    CHECK(engine.state.group_index==thy_menu_rows[THY_ROW_HARMONY_VIEW].group);
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK&&engine.state.level==THY_STATE_PLAY);
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK&&engine.state.level==THY_STATE_GROUPS);
    unsigned visible=0;for(unsigned g=0;g<THY_MENU_GROUP_COUNT;++g)visible+=!!thy_state_group_visible((uint8_t)g);
    CHECK(visible==12u);CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK);
    CHECK(thy_engine_harmony_active(&engine)&&!core.sounding_count);
    CHECK(thy_engine_harmony_action(&engine,THY_HARMONY_CAPTURE_SELECTED,0)==THY_STATE_OK);
    CHECK(!engine.harmony.chord_held);CHECK(attack(0));thy_engine_panic(&engine);drain();
    CHECK(!engine.harmony.chord_held&&!core.sounding_count);return 1;
}
static int contextual_progressions_and_visible_surface(void)
{
    CHECK(init());CHECK(attack(0));CHECK(release(0));
    thy_harmony_choice c_major[9];memcpy(c_major,engine.harmony.choices,sizeof c_major);
    CHECK(attack(10));CHECK(release(10)); /* D7 -> G, even though D7 is outside C major. */
    CHECK(engine.harmony.current_root==2u);
    thy_chord_cell cell;CHECK(thy_instrument_chord_cell(&instrument,10,&cell)==THY_OK&&cell.root%12==2);
    CHECK(attack(10));thy_instrument_capture_pad_color(&instrument,10);CHECK(instrument.held_color_valid[10]&&instrument.held_color_root[10]%12==2);CHECK(release(10));
    CHECK(engine.harmony.choices[0].root==7u&&engine.harmony.choices[0].reason==THY_HARMONY_RESOLVE);
    CHECK(memcmp(c_major,engine.harmony.choices,sizeof c_major));
    CHECK(attack(30));CHECK(release(30)); /* G7 -> C. */
    CHECK(engine.harmony.choices[0].root==0u&&engine.harmony.choices[0].reason==THY_HARMONY_RESOLVE);
    CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_ANALYSIS,1)==THY_STATE_OK);
    uint16_t retained=engine.harmony.ranking_mask;CHECK(attack(52));
    CHECK(thy_engine_advance(&engine,core.now_ms+300u)==THY_OK);
    CHECK(engine.harmony.ranking_mask==retained);CHECK(release(52));
    CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_CONTRAST,100)==THY_STATE_OK);
    thy_lighting lights;thy_lighting_config config;uint32_t colors[96];
    thy_lighting_defaults(&config);config.fade_ms=0;thy_lighting_init(&lights);
    CHECK(thy_lighting_render(&lights,&instrument,&config,core.now_ms,colors)==THY_OK);
    /* At least one physical LED amplitude after eight-level quantization. */
    for(unsigned p=0;p<48u;++p)CHECK(peak(colors[p])>=1u);
    CHECK(thy_state_set(&engine.state,THY_ROW_HARMONY_HIGHLIGHTS,0)==THY_STATE_OK);
    CHECK(thy_lighting_render(&lights,&instrument,&config,core.now_ms+1u,colors)==THY_OK);
    for(unsigned p=0;p<48u;++p)CHECK(peak(colors[p])>=1u);
    return 1;
}
int main(void)
{
    int ok=live_and_playable()&&arp_and_mpe_input()&&explicit_key_and_owners()&&
        discovery_holes_and_lifecycle()&&saved_configuration_and_label()&&right_hand_analysis_and_controls()&&cached_hints_avoid_voicing_search()&&chromatic_contrast_in_both_workspaces()&&exploration_stable_ownership()&&published_hints_and_navigation();
    if(!ok||!contextual_progressions_and_visible_surface())return 1;printf("11 Harmony workspace scenarios passed (%u assertions)\n",checks);return 0;
}
