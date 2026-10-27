/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_sequencer_ui.h"

static thy_sequencer seq;
static thy_transport transport;
static thy_clock clock_state;
static thy_core core;
static thy_sequencer_ui ui;
static unsigned assertions,commands;
static uint8_t last_command,last_track;
#define CHECK(x) do { ++assertions;if(!(x)) {fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);return 0;} } while(0)
static uint8_t pad(unsigned side,unsigned row,unsigned col)
{ return (uint8_t)(side*48u+col*4u+row); }
static int callback(void *ctx,uint8_t kind,uint8_t track)
{
    (void)ctx;++commands;
    if(kind!=THY_SEQ_UI_LOOP_CHANGED) {last_command=kind;last_track=track;}
    switch(kind) {
    case THY_SEQ_UI_CREATE_TRACK:
        for(unsigned t=0;t<THY_SEQ_TRACKS;++t) if(!thy_sequencer_track_exists(&seq,(uint8_t)t)) {
            int r=thy_sequencer_create(&seq,(uint8_t)t);
            if(r==THY_OK) (void)thy_sequencer_select(&seq,(uint8_t)t);
            return r;
        }
        return THY_SEQ_FULL;
    case THY_SEQ_UI_DELETE_TRACK:return thy_sequencer_delete(&seq,track);
    case THY_SEQ_UI_CLEAR_STEP:return thy_sequencer_ui_clear_step(&ui);
    case THY_SEQ_UI_OVERDUB:return thy_sequencer_ui_set_overdub(&ui,!ui.overdub);
    case THY_SEQ_UI_INPUT_TOGGLE:return thy_sequencer_ui_configure(&ui,THY_SEQ_UI_INPUT,!ui.input_layout);
    case THY_SEQ_UI_UNDO:return thy_sequencer_history(&seq,&core,&transport,0);
    case THY_SEQ_UI_REDO:return thy_sequencer_history(&seq,&core,&transport,1);
    default:return THY_OK;
    }
}
static void init(unsigned view,int created)
{
    thy_core_init(&core);thy_clock_init(&clock_state,0u);thy_transport_init(&transport,&clock_state);
    thy_sequencer_init(&seq);
    if(created) (void)thy_sequencer_create(&seq,0u);
    thy_sequencer_ui_init(&ui,&seq,&transport,&clock_state,callback,NULL);
    thy_sequencer_ui_open(&ui,(uint8_t)view);
    commands=0u;last_command=last_track=255u;
}
static int add(unsigned track,unsigned tick,unsigned duration,unsigned pitch)
{
    thy_seq_event e={(uint32_t)tick,(uint32_t)duration,0u,0u,(uint8_t)track,THY_SEQ_NOTE,(uint8_t)pitch,96u};
    return thy_sequencer_add(&seq,(uint8_t)track,&e,1u);
}
static int tap(uint8_t p)
{int a=thy_sequencer_ui_pad(&ui,p,1),b=thy_sequencer_ui_pad(&ui,p,0);return a&&b;}
static int key(unsigned index)
{int a=thy_sequencer_ui_control(&ui,3u,(uint8_t)index,1),b=thy_sequencer_ui_control(&ui,3u,(uint8_t)index,0);return a&&b;}
static int note(uint8_t p,int16_t pitch)
{
    if(thy_sequencer_ui_pad(&ui,p,1)) return 0;
    int r=thy_sequencer_ui_assign(&ui,p,&pitch,1u,96u,0u);
    if(thy_sequencer_ui_pad(&ui,p,0)) return 0;
    return r;
}
static int test_empty_tracks_and_explicit_creation(void)
{
    init(THY_SEQ_VIEW_STEPS,0);
    CHECK(ui.open && ui.view==THY_SEQ_VIEW_TRACKS && ui.track_new);
    for(unsigned p=0;p<96u;++p) {
        CHECK(tap((uint8_t)p));
        CHECK(!thy_sequencer_ui_input(&ui,(uint8_t)p));
        uint16_t rgb[3]={1,1,1};CHECK(thy_sequencer_ui_color(&ui,(uint8_t)p,rgb));
        CHECK(!(rgb[0]|rgb[1]|rgb[2]));
    }
    CHECK(!seq.count && !commands);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_RIGHT));
    CHECK(last_command==THY_SEQ_UI_CREATE_TRACK && thy_sequencer_track_exists(&seq,0u));
    CHECK(ui.view==THY_SEQ_VIEW_STEPS && !ui.track_new && ui.selected_step==0u);
    CHECK(!thy_sequencer_track_exists(&seq,1u));
    return 1;
}
static int test_tracks_list_ignores_uncreated_slots(void)
{
    init(THY_SEQ_VIEW_TRACKS,1);
    CHECK(thy_sequencer_create(&seq,7u)==THY_OK && thy_sequencer_create(&seq,20u)==THY_OK);
    CHECK(thy_sequencer_select(&seq,0u)==THY_OK);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_DOWN) && seq.selected_track==7u && !ui.track_new);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_DOWN) && seq.selected_track==20u);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_DOWN) && ui.track_new);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_DOWN) && seq.selected_track==0u && !ui.track_new);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_UP) && ui.track_new);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_UP) && seq.selected_track==20u && !ui.track_new);
    for(unsigned t=1;t<20u;++t) if(t!=7u) CHECK(thy_sequencer_create(&seq,(uint8_t)t)==THY_OK);
    CHECK(thy_sequencer_select(&seq,20u)==THY_OK);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_DOWN) && !ui.track_new && seq.selected_track==0u);
    return 1;
}
static int test_all_48_steps_are_row_major_and_selection_only(void)
{
    init(THY_SEQ_VIEW_STEPS,1);
    for(unsigned row=0;row<4u;++row) for(unsigned col=0;col<12u;++col) {
        unsigned step=row*12u+col;
        CHECK(tap(pad(0,row,col)));
        CHECK(ui.selected_step==step && ui.selected_pad==pad(0,row,col));
        uint8_t track,pitch;uint32_t tick;
        CHECK(thy_sequencer_ui_pad_role(&ui,pad(0,row,col),&track,&tick,&pitch)==THY_SEQ_PAD_EDITOR);
        CHECK(!track && tick==step*240u);
        CHECK(seq.count==0u);
    }
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_DOWN) && ui.selected_step==0u);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_UP) && ui.selected_step==47u);
    return 1;
}
static int test_latched_chord_assignment_and_single_undo(void)
{
    init(THY_SEQ_VIEW_STEPS,1);CHECK(add(0u,240u*14u+15u,100u,42u)==THY_OK);
    CHECK(tap(pad(0,1,2)) && ui.selected_step==14u);
    uint8_t input=pad(1,0,0);int16_t chord[]={60,64,67,60};
    CHECK(!thy_sequencer_ui_pad(&ui,input,1));
    CHECK(thy_sequencer_ui_assign(&ui,input,chord,4u,100u,1u));
    CHECK(seq.count==3u); /* Replacement includes an off-grid captured onset. */
    CHECK(seq.events[0].group==seq.events[1].group && seq.events[1].group==seq.events[2].group);
    for(unsigned n=0;n<3u;++n) {
        CHECK(seq.events[n].tick==14u*240u && seq.events[n].duration==240u);
        CHECK((seq.events[n].kind&128u) && seq.events[n].value==100u);
    }
    CHECK(!thy_sequencer_ui_pad(&ui,input,0) && !seq.transaction_open);
    CHECK(ui.selected_step==14u && !ui.pads[pad(0,1,2)].held);
    CHECK(thy_sequencer_history(&seq,&core,&transport,0)==THY_OK && seq.count==1u && seq.events[0].pitch==42u);
    CHECK(thy_sequencer_history(&seq,&core,&transport,1)==THY_OK && seq.count==3u);
    return 1;
}
static int test_new_gesture_replace_and_explicit_overdub(void)
{
    init(THY_SEQ_VIEW_STEPS,1);
    CHECK(note(pad(1,0,0),60) && seq.count==1u);
    CHECK(note(pad(1,0,1),62) && seq.count==1u && seq.events[0].pitch==62u);
    CHECK(key(4u) && ui.overdub && seq.tracks[0].record_policy==THY_SEQ_OVERDUB);
    CHECK(note(pad(1,0,2),64) && seq.count==2u);
    CHECK(thy_sequencer_history(&seq,&core,&transport,0)==THY_OK);
    CHECK(seq.count==1u && seq.events[0].pitch==62u);
    CHECK(key(4u) && !ui.overdub);
    return 1;
}
static int test_overlap_freezes_target_role_and_one_transaction(void)
{
    init(THY_SEQ_VIEW_STEPS,1);CHECK(thy_sequencer_create(&seq,1u)==THY_OK);
    CHECK(thy_sequencer_select(&seq,0u)==THY_OK);
    CHECK(tap(pad(0,0,2)));
    uint8_t a=pad(1,0,0),b=pad(1,0,1);int16_t c=60,d=62;
    CHECK(!thy_sequencer_ui_pad(&ui,a,1));
    CHECK(thy_sequencer_ui_assign(&ui,a,&c,1u,96u,0u));
    CHECK(tap(pad(0,2,5)) && ui.selected_step==29u);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_LEFT));
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_DOWN) && seq.selected_track==1u);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_RIGHT));
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_INPUT,THY_SEQ_INPUT_CHORDS)==THY_OK);
    CHECK(thy_sequencer_ui_input(&ui,a)==1);
    CHECK(!thy_sequencer_ui_pad(&ui,b,1) && thy_sequencer_ui_input(&ui,b)==2);
    CHECK(thy_sequencer_ui_assign(&ui,b,&d,1u,96u,1u));
    CHECK(seq.count==2u);
    for(unsigned n=0;n<2u;++n) CHECK(seq.events[n].track==0u && seq.events[n].tick==480u);
    CHECK(!thy_sequencer_ui_pad(&ui,a,0) && seq.transaction_open);
    CHECK(!thy_sequencer_ui_pad(&ui,b,0) && !seq.transaction_open);
    CHECK(thy_sequencer_history(&seq,&core,&transport,0)==THY_OK && seq.count==0u);
    CHECK(note(a,67) && seq.count==1u && seq.events[0].track==1u && seq.events[0].tick==0u);
    return 1;
}
static int test_clear_is_explicit_bucket_wide_and_undoable(void)
{
    init(THY_SEQ_VIEW_STEPS,1);
    CHECK(add(0u,0u,240u,60u)==THY_OK && add(0u,20u,100u,64u)==THY_OK && add(0u,240u,240u,67u)==THY_OK);
    CHECK(tap(pad(0,0,0)) && seq.count==3u);
    CHECK(key(3u) && seq.count==1u && seq.events[0].pitch==67u);
    CHECK(thy_sequencer_history(&seq,&core,&transport,0)==THY_OK && seq.count==3u);
    CHECK(!thy_sequencer_ui_pad(&ui,pad(1,0,0),1));
    CHECK(thy_sequencer_ui_clear_step(&ui)==THY_SEQ_BUSY && seq.count==3u);
    CHECK(!thy_sequencer_ui_pad(&ui,pad(1,0,0),0));
    return 1;
}
static int test_live_capture_bypasses_step_entry(void)
{
    init(THY_SEQ_VIEW_STEPS,1);
    for(unsigned state=THY_SEQ_ARMED;state<=THY_SEQ_FINISHING;++state) {
        seq.capture.state=(uint8_t)state;
        uint8_t input=pad(1,0,0);int16_t note_value=60;
        CHECK(!thy_sequencer_ui_pad(&ui,input,1));
        CHECK(thy_sequencer_ui_input(&ui,input)==1);
        CHECK(!thy_sequencer_ui_assign(&ui,input,&note_value,1u,96u,0u));
        CHECK(!thy_sequencer_ui_pad(&ui,input,0) && !seq.count && !seq.transaction_open);
    }
    seq.capture.state=THY_SEQ_CAPTURE_OFF;
    CHECK(note(pad(1,0,0),60) && seq.count==1u);
    CHECK(thy_sequencer_create(&seq,1u)==THY_OK && thy_sequencer_select(&seq,0u)==THY_OK);
    seq.capture.track=1u;seq.capture.state=THY_SEQ_ARMED;
    CHECK(!thy_sequencer_ui_pad(&ui,pad(1,0,1),1));
    int16_t value=64;
    CHECK(!thy_sequencer_ui_assign(&ui,pad(1,0,1),&value,1u,96u,0u));
    CHECK(!seq.transaction_open && seq.count==1u);
    CHECK(thy_sequencer_ui_clear_step(&ui)==THY_SEQ_BUSY);
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_STEP_COUNT,8u)==THY_SEQ_BUSY);
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_GRID,480u)==THY_SEQ_BUSY);
    CHECK(thy_sequencer_ui_set_overdub(&ui,1)==THY_SEQ_BUSY);
    CHECK(!thy_sequencer_ui_pad(&ui,pad(1,0,1),0));
    return 1;
}
static int test_control_map_and_release_after_leaving(void)
{
    init(THY_SEQ_VIEW_STEPS,1);
    CHECK(key(0u) && last_command==THY_SEQ_UI_PLAY_ALL);
    CHECK(key(1u) && last_command==THY_SEQ_UI_RECORD);
    CHECK(key(2u) && last_command==THY_SEQ_UI_PLAY_TRACK);
    CHECK(key(5u) && ui.input_layout==THY_SEQ_INPUT_CHORDS);
    CHECK(key(9u) && last_command==THY_SEQ_UI_OCTAVE_DOWN);
    CHECK(key(10u) && last_command==THY_SEQ_UI_OCTAVE_UP);
    CHECK(key(11u) && last_command==THY_SEQ_UI_INPUT_CONTEXT);
    CHECK(!thy_sequencer_ui_control(&ui,1u,0u,1) && !thy_sequencer_ui_control(&ui,2u,0u,1));
    CHECK(thy_sequencer_ui_control(&ui,3u,0u,1));unsigned before=commands;
    CHECK(thy_sequencer_ui_control(&ui,3u,0u,1) && commands==before);
    uint8_t input=pad(1,0,0);CHECK(!thy_sequencer_ui_pad(&ui,input,1));
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_SELECT) && !ui.open && last_command==THY_SEQ_UI_PLAY);
    CHECK(!seq.transaction_open && thy_sequencer_ui_input(&ui,input)==2);
    CHECK(!thy_sequencer_ui_pad(&ui,input,0));
    CHECK(thy_sequencer_ui_control(&ui,3u,0u,0)); /* Frozen control release is consumed. */
    CHECK(!thy_sequencer_ui_control(&ui,3u,0u,1));
    return 1;
}
static int test_delete_confirmation_and_held_input_guard(void)
{
    init(THY_SEQ_VIEW_TRACKS,1);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_LEFT) && ui.delete_pending);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_LEFT) && !ui.delete_pending && thy_sequencer_track_exists(&seq,0u));
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_RIGHT) && ui.view==THY_SEQ_VIEW_STEPS);
    CHECK(!thy_sequencer_ui_pad(&ui,pad(1,0,0),1));
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_LEFT));
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_LEFT) && ui.delete_pending);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_RIGHT) && ui.last_error==THY_SEQ_BUSY);
    CHECK(thy_sequencer_track_exists(&seq,0u));
    CHECK(!thy_sequencer_ui_pad(&ui,pad(1,0,0),0));
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_LEFT));
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_RIGHT));
    CHECK(!thy_sequencer_track_exists(&seq,0u) && ui.track_new);
    return 1;
}
static int test_step_count_grid_and_far_step_extension(void)
{
    init(THY_SEQ_VIEW_STEPS,1);
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_SELECTED_STEP,47u)==THY_OK);
    CHECK(note(pad(1,0,0),60));
    CHECK(seq.events[0].tick==47u*240u && seq.tracks[0].loop_end==48u*240u);
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_STEP_COUNT,12u)==THY_OK);
    CHECK(seq.tracks[0].loop_end==2880u && seq.count==1u); /* Shortening keeps content. */
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_STEP_COUNT,49u)==THY_INVALID_ARGUMENT);
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_SELECTED_STEP,48u)==THY_INVALID_ARGUMENT);
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_SWAPPED,1u)==THY_INVALID_ARGUMENT);
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_LAYOUT,THY_SEQ_FULL_TIME)==THY_INVALID_ARGUMENT);
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_WINDOW,1u)==THY_INVALID_ARGUMENT);
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_GRID,480u)==THY_OK && seq.tracks[0].grid_ticks==480u);
    CHECK(!thy_sequencer_ui_pad(&ui,pad(1,0,1),1));
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_GRID,240u)==THY_SEQ_BUSY);
    CHECK(thy_sequencer_ui_configure(&ui,THY_SEQ_UI_OVERDUB_VALUE,1u)==THY_SEQ_BUSY);
    CHECK(!thy_sequencer_ui_pad(&ui,pad(1,0,1),0));
    return 1;
}
static int test_note_colors_and_tracks_dark_even_for_frozen_roles(void)
{
    init(THY_SEQ_VIEW_STEPS,1);
    CHECK(add(0u,0u,240u,60u)==THY_OK && add(0u,240u,240u,67u)==THY_OK);
    uint16_t a[3],b[3];uint8_t pitch;
    CHECK(thy_sequencer_ui_step_note(&ui,0u,&pitch) && pitch==60u);
    CHECK(thy_sequencer_ui_step_note(&ui,1u,&pitch) && pitch==67u);
    CHECK(!thy_sequencer_ui_step_note(&ui,2u,&pitch));
    CHECK(thy_sequencer_ui_color(&ui,pad(0,0,0),a));
    CHECK(thy_sequencer_ui_color(&ui,pad(0,0,1),b) && memcmp(a,b,sizeof a));
    thy_sequencer_ui before=ui;
    CHECK(thy_sequencer_ui_color(&ui,pad(0,0,0),a) && !memcmp(&before,&ui,sizeof ui));
    CHECK(!thy_sequencer_ui_color(&ui,pad(1,0,0),a));
    CHECK(thy_sequencer_ui_pad(&ui,pad(0,0,0),1));
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_LEFT));
    CHECK(thy_sequencer_ui_color(&ui,pad(0,0,0),a) && !(a[0]|a[1]|a[2]));
    CHECK(thy_sequencer_ui_pad(&ui,pad(0,0,0),0));
    return 1;
}
static int test_help_pages_and_immediate_transport_control(void)
{
    init(THY_SEQ_VIEW_STEPS,1);
    CHECK(key(8u) && ui.help_open && ui.help_page==0u);
    CHECK(thy_sequencer_ui_scene_key(&ui)==0x70000u);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_UP) && ui.help_page==4u);
    CHECK(thy_sequencer_ui_navigate(&ui,THY_STATE_DOWN) && ui.help_page==0u);
    CHECK(key(0u) && last_command==THY_SEQ_UI_PLAY_ALL && !ui.help_open);
    CHECK(key(8u) && thy_sequencer_ui_navigate(&ui,THY_STATE_LEFT) && !ui.help_open);
    CHECK(thy_sequencer_ui_scene_key(&ui)==0x60001u);
    return 1;
}
int main(void)
{
    int (*tests[])(void)={test_empty_tracks_and_explicit_creation,test_tracks_list_ignores_uncreated_slots,
        test_all_48_steps_are_row_major_and_selection_only,test_latched_chord_assignment_and_single_undo,
        test_new_gesture_replace_and_explicit_overdub,test_overlap_freezes_target_role_and_one_transaction,
        test_clear_is_explicit_bucket_wide_and_undoable,test_live_capture_bypasses_step_entry,
        test_control_map_and_release_after_leaving,test_delete_confirmation_and_held_input_guard,
        test_step_count_grid_and_far_step_extension,test_note_colors_and_tracks_dark_even_for_frozen_roles,
        test_help_pages_and_immediate_transport_control};
    unsigned count=(unsigned)(sizeof tests/sizeof tests[0]);
    for(unsigned n=0;n<count;++n) if(!tests[n]()) return 1;
    printf("%u sequencer UI tests passed (%u assertions)\n",count,assertions);return 0;
}
