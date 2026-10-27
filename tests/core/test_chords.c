/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include "../../core/thy_engine.h"
#include "../../core/thy_lighting.h"
static thy_core core;
static thy_instrument i;
static thy_engine e;
static thy_instrument_pad pads[96];
static thy_mpe mpe;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static void drain(void){thy_midi_event event;while(thy_core_poll(&core,&event)){} }
static void init(void){
    thy_core_init(&core);thy_instrument_init(&i,&core);
    for(unsigned p=0;p<96u;++p)pads[p]=(thy_instrument_pad){.notes={60,64,67},.valid_mask=p<48u?7u:1u,.color=1u};
    pads[4]=(thy_instrument_pad){.notes={57,62,65},.valid_mask=7u,.color=2u};
    pads[52].notes[0]=62;
    const int16_t degrees[]={0,2,4,5,7,9,11};
    (void)thy_instrument_set_layout(&i,pads,60,degrees,7);
    thy_engine_init(&e,&i,21,70);thy_engine_bind_expression(&e,&mpe);
    (void)thy_engine_advance(&e,0);
}
static int pitch(unsigned pad,int expected){int16_t notes[64];size_t count;
    CHECK(thy_instrument_preview_source(&i,(uint8_t)pad,notes,&count)==THY_OK);
    CHECK(count==1u && notes[0]==expected);return 1;}
static int follows_key_or_chord_and_retains_release(void){
    init();i.performance.melody_layout=THY_LAYOUT_CONDENSED;
    CHECK(thy_instrument_press_pad(&i,48)==THY_OK);drain();
    CHECK(thy_instrument_set_live_update(&i,1,1,0)==THY_OK);
    CHECK(thy_instrument_press_pad(&i,4)==THY_OK);
    CHECK(pitch(48,62));CHECK(pitch(52,64));CHECK(pitch(56,65));
    CHECK((thy_core_owner_key(&core, (uint8_t)(48), (uint8_t)(0))&127u)==60u);
    CHECK(thy_instrument_release_pad(&i,4)==THY_OK);CHECK(pitch(48,62));
    CHECK(thy_instrument_release_pad(&i,48)==THY_OK);CHECK(!core.sounding_count);drain();
    CHECK(thy_instrument_set_live_update(&i,1,0,0)==THY_OK);
    CHECK(thy_instrument_press_pad(&i,4)==THY_OK);
    CHECK(pitch(48,62));CHECK(pitch(52,65));CHECK(pitch(56,69));
    CHECK(thy_instrument_release_pad(&i,4)==THY_OK);drain();
    CHECK(thy_instrument_set_live_update(&i,0,0,0)==THY_OK);CHECK(pitch(48,60));return 1;
}
static int muted_chord_feeds_arp_without_direct_sound(void){
    init();CHECK(thy_instrument_set_live_update(&i,1,0,1)==THY_OK);
    CHECK(thy_instrument_press_pad(&i,0)==THY_OK);
    CHECK(core.owners[0].held && !core.owners[0].count && !core.sounding_count);
    CHECK(pitch(52,64));CHECK(thy_instrument_release_pad(&i,0)==THY_OK);
    thy_arp_config c=e.arps[0].config;c.enabled=1;c.source_zones=63u;c.layer=1;
    CHECK(thy_engine_arp_configure(&e,0,&c)==THY_OK);
    CHECK(thy_instrument_press_pad(&i,4)==THY_OK);
    CHECK(core.owners[4].held && !core.owners[4].count);
    CHECK(thy_engine_advance(&e,1)==THY_OK);CHECK(core.generated_count==1u);
    CHECK(thy_instrument_release_pad(&i,4)==THY_OK);drain();return 1;
}
static int failed_attack_does_not_select_and_transpose_contract(void){
    init();CHECK(thy_instrument_set_live_update(&i,1,0,0)==THY_OK);
    CHECK(thy_instrument_press_pad(&i,0)==THY_OK);CHECK(thy_instrument_release_pad(&i,0)==THY_OK);drain();
    core.queue_count=THY_QUEUE_CAPACITY;
    CHECK(thy_instrument_press_pad(&i,4)==THY_QUEUE_FULL);
    CHECK(i.selected_chord==0u);CHECK(pitch(48,60));core.queue_count=0u;
    i.transpose[0]=i.transpose[1]=7;
    CHECK(thy_state_set(&e.state,THY_ROW_PERFORMANCE_ROOT,2)==THY_STATE_OK);
    CHECK(i.transpose[0]==7 && !i.live_degree_count);
    CHECK(thy_state_set(&e.state,THY_ROW_PERFORMANCE_KEEPTRANSPOSE,0)==THY_STATE_OK);
    CHECK(thy_state_set(&e.state,THY_ROW_PERFORMANCE_ROOT,2)==THY_STATE_OK);CHECK(i.transpose[0]==7);
    CHECK(thy_state_set(&e.state,THY_ROW_PERFORMANCE_ROOT,3)==THY_STATE_OK);CHECK(!i.transpose[0]&&!i.transpose[1]);return 1;
}
static int mpe_muted_selection_needs_no_members(void){
    init();thy_mpe_config c=e.expression_config;c.enabled=1;c.members=1;
    CHECK(thy_engine_expression_configure(&e,&c)==THY_OK);
    CHECK(thy_instrument_set_live_update(&i,1,0,1)==THY_OK);
    CHECK(thy_instrument_press_pad(&i,0)==THY_OK);CHECK(!thy_mpe_active(&mpe));
    CHECK(pitch(52,64));CHECK(core.owners[0].held);
    CHECK(i.held_input_valid[0] && i.held_color_valid[0]);
    CHECK(thy_instrument_release_pad(&i,0)==THY_OK);CHECK(!core.owners[0].held);return 1;
}
static int generated_layouts_and_piano_share_the_actual_attack(void){
    init();i.chord_layout=THY_CHORD_LAYOUT_CHROMATIC;
    int16_t notes[64];size_t count;uint8_t degree;int16_t root;
    CHECK(thy_instrument_preview_source(&i,4,notes,&count)==THY_OK);
    CHECK(count==3u && notes[0]==61 && notes[1]==65 && notes[2]==68);
    CHECK(thy_instrument_press_pad(&i,4)==THY_OK);
    CHECK((thy_core_owner_key(&core, (uint8_t)(4), (uint8_t)(0))&127u)==61u && i.held_color_root[4]%12u==1u);
    i.chord_layout=THY_CHORD_LAYOUT_DIATONIC;
    CHECK(thy_instrument_preview_source(&i,4,notes,&count)==THY_OK);
    CHECK(count==3u && notes[0]==62 && notes[1]==65 && notes[2]==69);
    CHECK(thy_instrument_chord_degree(&i,4,&degree)==THY_OK && degree==1u);
    CHECK(thy_instrument_chord_root(&i,4,&root)==THY_OK && root%12==2);
    CHECK((thy_core_owner_key(&core, (uint8_t)(4), (uint8_t)(0))&127u)==61u);
    CHECK(thy_instrument_release_pad(&i,4)==THY_OK);drain();
    i.chord_layout=THY_CHORD_LAYOUT_SIX_DEFAULT;
    CHECK(thy_instrument_preview_source(&i,24,notes,&count)==THY_OK && count==3u);
    CHECK(notes[0]==62 && notes[1]==66 && notes[2]==69);
    i.performance.melody_layout=THY_LAYOUT_PIANO;
    CHECK(pitch(52,61));CHECK(pitch(49,60));CHECK(pitch(51,48));
    CHECK(thy_instrument_preview_source(&i,48,notes,&count)==THY_OK && !count);
    CHECK(thy_instrument_press_pad(&i,48)==THY_OK && core.owners[48].held && !core.owners[48].count);
    CHECK(thy_instrument_release_pad(&i,48)==THY_OK && !core.sounding_count);
    return 1;
}
static int dropped_voices_controls_are_independent_and_held_snapshot_stays(void){
    init();CHECK(thy_instrument_press_pad(&i,0)==THY_OK);drain();
    i.voicing_mode=1u;
    CHECK(thy_instrument_control(&i,1,0,1)==THY_OK);
    CHECK(thy_instrument_control(&i,1,0,0)==THY_OK);
    CHECK(thy_instrument_control(&i,1,1,1)==THY_OK);
    CHECK(thy_instrument_control(&i,1,1,0)==THY_OK);
    CHECK(i.drop_mask==3u && thy_instrument_drop_available(&i)==3u);
    CHECK((thy_core_owner_key(&core, (uint8_t)(0), (uint8_t)(0))&127u)==60u && (thy_core_owner_key(&core, (uint8_t)(0), (uint8_t)(1))&127u)==64u);
    int16_t notes[64];size_t count;
    CHECK(thy_instrument_preview_source(&i,0,notes,&count)==THY_OK && count==3u);
    CHECK(notes[0]==60 && notes[1]==52 && notes[2]==55);
    uint16_t levels[19];thy_engine_center_levels(&e,levels);
    CHECK(levels[5]>levels[7] && levels[6]>levels[7]);
    CHECK(thy_instrument_release_pad(&i,0)==THY_OK);drain();
    CHECK(thy_instrument_press_pad(&i,0)==THY_OK);CHECK((thy_core_owner_key(&core, (uint8_t)(0), (uint8_t)(1))&127u)==52u);
    CHECK(thy_instrument_release_pad(&i,0)==THY_OK);drain();
    i.voicing_mode=0u;i.performance.inversion=1u;
    CHECK(thy_instrument_preview_source(&i,0,notes,&count)==THY_OK && notes[0]==72 && notes[1]==64);
    return 1;
}
static int sequencer_chord_lighting_uses_generated_degrees(void){
    init();i.chord_layout=THY_CHORD_LAYOUT_DIATONIC;i.light_chord_mode=THY_LIGHTING_CHORD_DEGREE;
    CHECK(thy_sequencer_create(&e.sequencer,0)==THY_OK);
    CHECK(thy_engine_sequencer_open(&e,THY_SEQ_VIEW_STEPS)==THY_STATE_OK);
    CHECK(thy_sequencer_ui_configure(&e.sequencer_ui,THY_SEQ_UI_INPUT,THY_SEQ_INPUT_CHORDS)==THY_OK);
    uint16_t idle[3],held[3];CHECK(thy_engine_sequencer_color(&e,52,idle));
    uint32_t color=thy_lighting_note_color_palette(2,i.light_palette);
    for(unsigned c=0;c<3u;++c)CHECK(idle[c]==((color>>(16u-c*8u))&255u)*16u*i.light_idle/8u);
    CHECK(thy_instrument_press_pad(&i,52)==THY_OK);CHECK(thy_engine_sequencer_color(&e,52,held));
    for(unsigned c=0;c<3u;++c)CHECK(held[c]==((color>>(16u-c*8u))&255u)*16u*i.light_held/8u);
    CHECK(thy_instrument_release_pad(&i,52)==THY_OK);drain();return 1;
}
static int live_in_key_lighting_follows_the_effective_chord_pool(void){
    init();i.chord_layout=THY_CHORD_LAYOUT_CHROMATIC;i.performance.melody_layout=THY_LAYOUT_IN_KEY_LIT;
    CHECK(thy_instrument_set_live_update(&i,1,0,0)==THY_OK);
    CHECK(thy_instrument_press_pad(&i,4)==THY_OK);CHECK(thy_instrument_release_pad(&i,4)==THY_OK);drain();
    CHECK(thy_instrument_note_in_key(&i,61)&&thy_instrument_note_in_key(&i,65)&&thy_instrument_note_in_key(&i,68));
    CHECK(!thy_instrument_note_in_key(&i,60)&&!thy_instrument_note_in_key(&i,64));
    thy_lighting lights;thy_lighting_init(&lights);thy_lighting_config config;thy_lighting_defaults(&config);
    config.fade_ms=0;uint32_t rgb[96];CHECK(thy_lighting_render(&lights,&i,&config,0,rgb)==THY_OK);
    CHECK(rgb[48]&&rgb[52]&&rgb[56]);
    i.performance.melody_layout=THY_LAYOUT_PIANO;
    CHECK(thy_instrument_note_in_key(&i,60)&&!thy_instrument_note_in_key(&i,61));return 1;
}
static int chord_shortcut_resolves_the_active_voicing_policy(void){
    init();CHECK(thy_state_set_design_mode(&e.state,0)==THY_STATE_OK);
    i.play_nav[0]=THY_PLAY_CHORDS;i.voicing_mode=1u;
    CHECK(thy_engine_navigate(&e,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(thy_state_current_row(&e.state)==THY_ROW_PERFORMANCE_ROOT);
    CHECK(thy_engine_navigate(&e,THY_STATE_SELECT)==THY_STATE_OK);
    CHECK(e.state.level==THY_STATE_PLAY);i.voicing_mode=0u;
    CHECK(thy_engine_navigate(&e,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(thy_state_current_row(&e.state)==THY_ROW_PERFORMANCE_ROOT);return 1;
}
int main(void){int(*tests[])(void)={follows_key_or_chord_and_retains_release,muted_chord_feeds_arp_without_direct_sound,
    failed_attack_does_not_select_and_transpose_contract,mpe_muted_selection_needs_no_members,
    generated_layouts_and_piano_share_the_actual_attack,dropped_voices_controls_are_independent_and_held_snapshot_stays,
    sequencer_chord_lighting_uses_generated_degrees,live_in_key_lighting_follows_the_effective_chord_pool,
    chord_shortcut_resolves_the_active_voicing_policy};
    for(unsigned n=0;n<sizeof tests/sizeof tests[0];++n)if(!tests[n]())return 1;
    printf("Chords: %u scenarios passed (%u assertions)\n",(unsigned)(sizeof tests/sizeof tests[0]),checks);return 0;}
