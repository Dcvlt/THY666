/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "thy_engine.h"

/* These tests exercise the application boundary: authored data, live owners,
 * the menu transaction and external references must agree. The pure codec
 * and individual editor fields have separate, exhaustive test suites. */
static thy_core core, before_core;
static thy_instrument instrument, before_instrument;
static thy_engine engine, before_engine;
static thy_mpe mpe;
static thy_channel_expression ordinary;
static thy_instrument_pad layout[THY_PAD_COUNT];
static thy_library incoming;
static uint8_t file[THY_LIBRARY_FILE_BYTES], file_before[THY_LIBRARY_FILE_BYTES];
static unsigned assertions;
#define CHECK(x) do { ++assertions; if(!(x)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 0; } } while(0)

static void init(void)
{
    thy_core_init(&core);
    thy_instrument_init(&instrument,&core);
    for(unsigned p=0;p<THY_PAD_COUNT;++p)
        layout[p]=(thy_instrument_pad){.notes={60,64,67,71,74,77,81},
            .valid_mask=p<48u?7u:1u,.color=1u};
    const int16_t degrees[]={0,2,4,5,7,9,11};
    (void)thy_instrument_set_layout(&instrument,layout,60,degrees,7);
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&engine,&instrument,21,70, undo_workspace_1);
    thy_engine_bind_expression(&engine,&mpe);
    thy_engine_bind_channel_expression(&engine,&ordinary);
    thy_engine_bind_controls(&engine);
    (void)thy_engine_advance(&engine,0);
}

static void drain(void)
{
    thy_midi_event event;
    while(thy_core_poll(&core,&event)) {}
}

static void snapshot(void)
{
    before_instrument=instrument;
    before_engine=engine;
    before_core=core;
}

static int unchanged(void)
{
    CHECK(!memcmp(&instrument,&before_instrument,sizeof instrument));
    CHECK(!memcmp(&engine,&before_engine,sizeof engine));
    CHECK(!memcmp(&core,&before_core,sizeof core));
    return 1;
}

static thy_library_scale scale(const char *name,uint16_t mask)
{
    thy_library_scale value={.used=1u};
    (void)thy_library_name_set(value.name,name);
    (void)thy_library_scale_set_mask(&value,mask);
    return value;
}

static thy_library_chord chord(const char *name,uint8_t root)
{
    thy_library_chord value={.root=root,.notes={0,64,67,71,74,77,81},
        .valid_mask=7u,.used=1u};
    value.notes[0]=root;
    (void)thy_library_name_set(value.name,name);
    return value;
}

static thy_library_six six(const char *name)
{
    thy_library_six value={.used=1u};
    (void)thy_library_name_set(value.name,name);
    memset(value.chords,THY_LIBRARY_EMPTY_CHORD,sizeof value.chords);
    return value;
}

static int save_scale(uint8_t slot,const char *name,uint16_t mask)
{
    thy_library_scale value=scale(name,mask);
    CHECK(thy_engine_library_set(&engine,THY_LIBRARY_UI_SCALE,slot,&value,sizeof value)==THY_OK);
    return 1;
}

static int save_chord(uint8_t slot,const char *name,uint8_t root)
{
    thy_library_chord value=chord(name,root);
    CHECK(thy_engine_library_set(&engine,THY_LIBRARY_UI_CHORD,slot,&value,sizeof value)==THY_OK);
    return 1;
}

static int encode_incoming(void)
{
    size_t written=0;
    CHECK(thy_library_encode(&incoming,file,sizeof file,&written)==THY_LIBRARY_OK);
    CHECK(written==sizeof file);
    memcpy(file_before,file,sizeof file);
    return 1;
}

static int navigate(uint8_t action)
{
    CHECK(thy_engine_navigate(&engine,action)==THY_STATE_OK);
    return 1;
}

static int down(unsigned count)
{
    for(unsigned n=0;n<count;++n) CHECK(navigate(THY_STATE_DOWN));
    return 1;
}

static int press_control(uint8_t bank,uint8_t index)
{
    CHECK(thy_instrument_control(&instrument,bank,index,1)==THY_OK);
    CHECK(thy_instrument_control(&instrument,bank,index,0)==THY_OK);
    return 1;
}

static int preview(uint8_t pad,const int16_t *expected,size_t expected_count)
{
    int16_t notes[THY_MAX_NOTES];size_t count=99;
    CHECK(thy_instrument_preview_source(&instrument,pad,notes,&count)==THY_OK);
    CHECK(count==expected_count);
    for(size_t n=0;n<count;++n) CHECK(notes[n]==expected[n]);
    return 1;
}

static int canonical_crud_rejects_invalid_records_atomically(void)
{
    init();CHECK(thy_engine_library_quiet(&engine));
    CHECK(save_scale(7,"Sparse",0x0089u));
    CHECK(save_chord(23,"Last chord",60));
    thy_library_six value=six("Last set");value.chords[5]=23u;
    CHECK(thy_engine_library_set(&engine,2,2,&value,sizeof value)==THY_OK);
    CHECK(thy_library_valid(&instrument.library));
    CHECK(instrument.library.scales[7].used && instrument.library.chords[23].used);
    snapshot();
    thy_library_scale bad=scale("Bad",1u);bad.name[12]='X';
    CHECK(thy_engine_library_set(&engine,0,0,&bad,sizeof bad)==THY_INVALID_ARGUMENT);
    CHECK(unchanged());
    bad=scale("Bad",1u);bad.used=0u; /* An unused record must be entirely zero. */
    CHECK(thy_engine_library_set(&engine,0,0,&bad,sizeof bad)==THY_INVALID_ARGUMENT);
    CHECK(unchanged());
    CHECK(thy_engine_library_set(&engine,0,8,&bad,sizeof bad)==THY_INVALID_ARGUMENT);
    CHECK(thy_engine_library_set(&engine,3,0,&bad,sizeof bad)==THY_INVALID_ARGUMENT);
    CHECK(thy_engine_library_set(&engine,0,0,&bad,sizeof bad-1u)==THY_INVALID_ARGUMENT);
    CHECK(thy_engine_library_set(&engine,0,0,NULL,sizeof bad)==THY_INVALID_ARGUMENT);
    CHECK(unchanged());
    value.chords[0]=22u; /* Canonical slot, missing referenced chord. */
    CHECK(thy_engine_library_set(&engine,2,0,&value,sizeof value)==THY_INVALID_ARGUMENT);
    CHECK(unchanged());
    CHECK(thy_engine_library_delete(&engine,2,2)==THY_OK);
    CHECK(thy_engine_library_delete(&engine,1,23)==THY_OK);
    CHECK(thy_engine_library_delete(&engine,0,7)==THY_OK);
    thy_library empty={0};CHECK(!memcmp(&empty,&instrument.library,sizeof empty));
    CHECK(!thy_core_queued(&core));return 1;
}

static int external_selection_and_shortcuts_protect_deletions(void)
{
    init();CHECK(save_scale(0,"Triad",0x0091u));
    CHECK(thy_engine_user_scale(&engine,1)==THY_OK);
    snapshot();
    CHECK(thy_engine_library_delete(&engine,0,0)==THY_ENGINE_LIBRARY_IN_USE);
    thy_library_scale empty={0};
    CHECK(thy_engine_library_set(&engine,0,0,&empty,sizeof empty)==THY_ENGINE_LIBRARY_IN_USE);
    CHECK(unchanged());
    CHECK(thy_engine_user_scale(&engine,0)==THY_OK);
    CHECK(thy_instrument_set_scale_shortcut(&instrument,11,71)==THY_OK);
    snapshot();
    CHECK(thy_engine_library_delete(&engine,0,0)==THY_ENGINE_LIBRARY_IN_USE);
    CHECK(unchanged());
    CHECK(thy_instrument_set_scale_shortcut(&instrument,11,21)==THY_OK);
    CHECK(thy_engine_library_delete(&engine,0,0)==THY_OK);
    CHECK(thy_engine_user_scale(&engine,1)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_scale_shortcut(&instrument,11,71)==THY_INVALID_ARGUMENT);
    CHECK(save_chord(0,"C",60));
    thy_library_six set=six("Set");set.chords[0]=0u;
    CHECK(thy_engine_library_set(&engine,2,0,&set,sizeof set)==THY_OK);
    snapshot();
    CHECK(thy_engine_library_delete(&engine,1,0)==THY_ENGINE_LIBRARY_IN_USE);
    CHECK(unchanged());
    CHECK(thy_engine_library_delete(&engine,2,0)==THY_OK);
    instrument.chord_layout=THY_CHORD_LAYOUT_USER;
    snapshot();
    CHECK(thy_engine_library_delete(&engine,1,0)==THY_ENGINE_LIBRARY_IN_USE);
    CHECK(unchanged());
    instrument.chord_layout=THY_CHORD_LAYOUT_SIX_USER;
    CHECK(thy_engine_library_set(&engine,2,0,&set,sizeof set)==THY_OK);
    snapshot();
    CHECK(thy_engine_library_delete(&engine,2,0)==THY_ENGINE_LIBRARY_IN_USE);
    CHECK(unchanged());
    instrument.chord_layout=THY_CHORD_LAYOUT_CLASSIC;
    CHECK(thy_engine_library_delete(&engine,2,0)==THY_OK);
    CHECK(thy_engine_library_delete(&engine,1,0)==THY_OK);return 1;
}

static int live_owners_and_background_work_guard_all_writes(void)
{
    init();CHECK(save_scale(0,"Keep",0x0091u));
    thy_library_scale replacement=scale("New",0x0089u);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);drain();snapshot();
    CHECK(!thy_engine_library_quiet(&engine));
    CHECK(thy_engine_library_set(&engine,0,0,&replacement,sizeof replacement)==THY_ENGINE_LIBRARY_BUSY);
    CHECK(thy_engine_library_delete(&engine,0,0)==THY_ENGINE_LIBRARY_BUSY);
    CHECK(unchanged());
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);
    /* Reserved NoteOffs retain their old routes; they need not block data edits. */
    CHECK(thy_core_queued(&core)>0 && thy_engine_library_quiet(&engine));
    CHECK(thy_engine_library_set(&engine,0,0,&replacement,sizeof replacement)==THY_OK);drain();
    for(unsigned state=0;state<5u;++state) {
        init();
        if(state==0u)instrument.sensors[0].held=1u;
        if(state==1u)instrument.context_held[95]=1u;
        if(state==2u)engine.calibration_open=1u;
        if(state==3u)engine.rhythm_transport.state=THY_TRANSPORT_PLAYING;
        if(state==4u)engine.sequencer.capture.state=THY_SEQ_ARMED;
        snapshot();
        CHECK(!thy_engine_library_quiet(&engine));
        CHECK(thy_engine_library_set(&engine,0,0,&replacement,sizeof replacement)==THY_ENGINE_LIBRARY_BUSY);
        CHECK(unchanged());
    }
    init();thy_generated_handle handle=0;
    CHECK(thy_core_generated_acquire(&core,64,1,60,0,THY_ROUTE_DIN,90,&handle)==THY_OK);
    drain();snapshot();
    CHECK(thy_engine_library_set(&engine,0,0,&replacement,sizeof replacement)==THY_ENGINE_LIBRARY_BUSY);
    CHECK(unchanged());CHECK(thy_core_generated_release(&core,handle)==THY_OK);drain();
    CHECK(thy_core_sustain(&core,THY_ROUTE_DIN,0,1)==THY_OK);drain();snapshot();
    CHECK(thy_engine_library_set(&engine,0,0,&replacement,sizeof replacement)==THY_ENGINE_LIBRARY_BUSY);
    CHECK(unchanged());CHECK(thy_core_sustain(&core,THY_ROUTE_DIN,0,0)==THY_OK);drain();
    CHECK(thy_engine_library_set(&engine,0,0,&replacement,sizeof replacement)==THY_OK);return 1;
}

static int custom_scales_apply_restore_and_preserve_held_notes(void)
{
    init();instrument.performance.melody_layout=THY_LAYOUT_CONDENSED;
    CHECK(save_scale(0,"Minor triad",0x0089u));
    const int16_t before[]={62},after[]={63};
    CHECK(preview(52,before,1));
    CHECK(thy_instrument_press_pad(&instrument,52)==THY_OK);drain();
    CHECK(thy_engine_user_scale(&engine,1)==THY_OK);
    CHECK(instrument.degree_count==3u && instrument.degrees[0]==0 &&
        instrument.degrees[1]==3 && instrument.degrees[2]==7);
    CHECK(preview(52,after,1));
    CHECK((thy_core_owner_key(&core,52,0)&127u)==62u);
    CHECK(thy_instrument_release_pad(&instrument,52)==THY_OK);drain();
    CHECK(!thy_core_sounding(&core));
    instrument.live_degree_count=1u;instrument.live_degrees[0]=11;
    CHECK(thy_engine_user_scale(&engine,0)==THY_OK);
    CHECK(!instrument.live_degree_count && instrument.degree_count==7u);
    CHECK(preview(52,before,1));
    CHECK(save_scale(0,"Edited triad",0x0091u));
    CHECK(thy_engine_user_scale(&engine,1)==THY_OK);
    CHECK(instrument.degrees[1]==4);return 1;
}

static int custom_shortcuts_cover_all_slots_and_return_to_builtin(void)
{
    init();instrument.performance.melody_layout=THY_LAYOUT_CONDENSED;
    for(unsigned n=0;n<8u;++n) {
        CHECK(save_scale((uint8_t)n,"Custom",(uint16_t)(1u|(1u<<(n+1u)))));
        CHECK(thy_instrument_set_scale_shortcut(&instrument,(uint8_t)n,(uint8_t)(71u+n))==THY_OK);
        CHECK(press_control(3,(uint8_t)n));
        CHECK(instrument.user_scale==n+1u && instrument.degree_count==2u && instrument.degrees[1]==(int)n+1);
    }
    CHECK(thy_instrument_set_scale_shortcut(&instrument,8,21)==THY_OK);
    CHECK(press_control(3,8));CHECK(!instrument.user_scale && instrument.degree_count==7u);
    CHECK(thy_instrument_set_scale_shortcut(&instrument,8,70)==THY_OK);
    CHECK(thy_engine_user_scale(&engine,8)==THY_OK);
    uint8_t old_layout=instrument.performance.melody_layout;
    CHECK(press_control(3,8));
    CHECK(instrument.performance.melody_layout!=(int)old_layout && instrument.user_scale==8u);
    instrument.keep_transpose=0u;instrument.transpose[0]=instrument.transpose[1]=7;
    CHECK(thy_engine_user_scale(&engine,7)==THY_OK);
    CHECK(!instrument.transpose[0] && !instrument.transpose[1]);
    CHECK(thy_instrument_set_scale_shortcut(&instrument,0,79)==THY_INVALID_ARGUMENT);
    CHECK(!thy_core_sounding(&core));return 1;
}

static int authored_capture_excludes_performance_and_retains_slot_holes(void)
{
    init();layout[0]=(thy_instrument_pad){.notes={0,64,67,1,2,3,127},.valid_mask=69u,.color=0};
    instrument.transpose[0]=5;instrument.performance.inversion=2;
    instrument.performance.spread=3;instrument.performance.chord_octave=1;
    instrument.global_excluded_slots=4u;
    thy_library_ui_draft captured;
    CHECK(thy_engine_library_capture(&engine,1,23,&captured)==THY_OK);
    CHECK(!strcmp((const char *)captured.chord.name,"Chord 24"));
    CHECK(captured.chord.root==0 && captured.chord.valid_mask==69u);
    for(unsigned n=0;n<7u;++n)CHECK(captured.chord.notes[n]==layout[0].notes[n]);
    CHECK(thy_engine_library_capture(&engine,0,7,&captured)==THY_OK);
    CHECK(thy_library_scale_mask(&captured.scale)==0x0ab5u);
    CHECK(!strcmp((const char *)captured.scale.name,"Scale 8"));
    memset(&captured,0x5a,sizeof captured);thy_library_ui_draft before=captured;
    CHECK(thy_engine_library_capture(&engine,2,0,&captured)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&captured,&before,sizeof captured));
    CHECK(thy_engine_library_capture(&engine,3,0,&captured)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&captured,&before,sizeof captured));
    layout[0].valid_mask=0u;
    CHECK(thy_engine_library_capture(&engine,1,0,&captured)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&captured,&before,sizeof captured));return 1;
}

static int user_layout_places_twenty_four_chords_in_two_octave_pairs(void)
{
    init();
    for(unsigned n=0;n<24u;++n) {
        thy_library_chord value=chord("User",(uint8_t)(48u+n));
        value.notes[1]=(uint8_t)(52u+n);value.notes[2]=(uint8_t)(55u+n);
        CHECK(thy_engine_library_set(&engine,1,(uint8_t)n,&value,sizeof value)==THY_OK);
    }
    instrument.chord_layout=THY_CHORD_LAYOUT_USER;instrument.performance.root=2u;
    for(unsigned p=0;p<48u;++p) {
        unsigned slot=(p/4u)*2u+p%2u;
        int16_t root=(int16_t)(48u+slot+2u-(p%4u>=2u?12u:0u));
        int16_t expected[]={root,(int16_t)(root+4),(int16_t)(root+7)};
        CHECK(preview((uint8_t)p,expected,3));
        thy_chord_cell cell;
        CHECK(thy_instrument_chord_cell(&instrument,(uint8_t)p,&cell)==THY_OK);
        CHECK(cell.root==root && cell.degree==THY_CHORD_NO_DEGREE);
    }
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);
    CHECK((thy_core_owner_key(&core,0,0)&127u)==50u);
    instrument.performance.root=5u;
    CHECK((thy_core_owner_key(&core,0,0)&127u)==50u);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain();
    CHECK(thy_engine_library_quiet(&engine));return 1;
}

static int custom_slots_drive_all_seven_physical_note_buttons(void)
{
    init();thy_library_chord value=chord("Seven voices",60);value.valid_mask=127u;
    CHECK(thy_engine_library_set(&engine,1,0,&value,sizeof value)==THY_OK);
    instrument.chord_layout=THY_CHORD_LAYOUT_USER;
    CHECK(thy_instrument_set_lower_mode(&instrument,THY_LOWER_STORED_NOTES)==THY_OK);
    CHECK(instrument.lower_mode==THY_LOWER_STORED_NOTES);
    CHECK(thy_instrument_lower_available(&instrument)==127u);
    const int16_t full[]={60,64,67,71,74,77,81};
    CHECK(preview(0,full,7));
    for(unsigned n=0;n<7u;++n) {
        CHECK(press_control(1,(uint8_t)(7u+n)));
        CHECK(instrument.global_excluded_slots==(1u<<(n+1u))-1u);
    }
    CHECK(preview(0,NULL,0));
    for(unsigned n=0;n<7u;++n)CHECK(press_control(1,(uint8_t)(7u+n)));
    CHECK(!instrument.global_excluded_slots);CHECK(preview(0,full,7));
    instrument.slot_scope=THY_SLOTS_SELECTED_PAD;instrument.selected_chord=4u;
    CHECK(!thy_instrument_lower_available(&instrument));
    CHECK(press_control(1,13));CHECK(!instrument.excluded_slots[4]);
    instrument.selected_chord=0u;CHECK(thy_instrument_lower_available(&instrument)==127u);
    CHECK(press_control(1,13));CHECK(instrument.excluded_slots[0]==64u);
    const int16_t six_notes[]={60,64,67,71,74,77};
    CHECK(preview(0,six_notes,6));return 1;
}

static int six_sets_use_authored_intervals_active_scale_and_empty_slots(void)
{
    init();CHECK(save_chord(2,"Minor",60));
    thy_library_chord value=chord("Minor",60);value.notes[1]=63u;
    CHECK(thy_engine_library_set(&engine,1,2,&value,sizeof value)==THY_OK);
    thy_library_six set=six("Sparse six");set.chords[0]=2u;
    CHECK(thy_engine_library_set(&engine,2,1,&set,sizeof set)==THY_OK);
    instrument.six_bank=1u;instrument.chord_layout=THY_CHORD_LAYOUT_SIX_USER;
    const int16_t c[]={60,63,67},e[]={64,67,71},d[]={62,65,69};
    CHECK(preview(0,c,3));CHECK(preview(1,e,3));CHECK(preview(24,d,3));
    CHECK(preview(4,NULL,0));
    CHECK(thy_instrument_press_pad(&instrument,4)==THY_OK);
    CHECK(thy_core_pad_held(&core,4) && !thy_core_sounding(&core));
    CHECK(thy_instrument_release_pad(&instrument,4)==THY_OK);
    CHECK(save_scale(0,"Triad",0x0089u));CHECK(thy_engine_user_scale(&engine,1)==THY_OK);
    const int16_t g[]={67,70,74},eb[]={63,66,70};
    CHECK(preview(1,g,3));CHECK(preview(24,eb,3));
    thy_library_ui_draft copy;
    CHECK(thy_engine_library_capture(&engine,2,2,&copy)==THY_OK);
    CHECK(copy.six.chords[0]==2u && copy.six.chords[1]==255u);
    CHECK(!strcmp((const char *)copy.six.name,"Set 3"));
    init();instrument.chord_layout=THY_CHORD_LAYOUT_USER;
    CHECK(preview(0,NULL,0));return 1;
}

static int file_import_is_atomic_and_reapplies_selected_scale(void)
{
    init();CHECK(save_scale(0,"Old",0x0091u));
    CHECK(thy_engine_user_scale(&engine,1)==THY_OK);
    incoming=instrument.library;incoming.scales[0]=scale("New",0x0089u);
    CHECK(encode_incoming());
    instrument.live_degree_count=1u;instrument.live_degrees[0]=11;
    CHECK(thy_engine_library_import(&engine,file,sizeof file)==THY_OK);
    CHECK(!memcmp(&incoming,&instrument.library,sizeof incoming));
    CHECK(instrument.user_scale==1u && instrument.degrees[1]==3 && !instrument.live_degree_count);
    CHECK(!memcmp(file,file_before,sizeof file));CHECK(!thy_core_queued(&core));
    snapshot();file[THY_LIBRARY_HEADER_BYTES+1u]^=1u;
    CHECK(thy_engine_library_import(&engine,file,sizeof file)==THY_INVALID_ARGUMENT);CHECK(unchanged());
    memcpy(file,file_before,sizeof file);file[4]=2u;
    CHECK(thy_engine_library_import(&engine,file,sizeof file)==THY_INVALID_ARGUMENT);CHECK(unchanged());
    memcpy(file,file_before,sizeof file);
    CHECK(thy_engine_library_import(&engine,file,sizeof file-1u)==THY_INVALID_ARGUMENT);CHECK(unchanged());
    CHECK(thy_engine_library_import(&engine,file,sizeof file+1u)==THY_INVALID_ARGUMENT);CHECK(unchanged());
    CHECK(thy_engine_library_import(&engine,NULL,0)==THY_INVALID_ARGUMENT);CHECK(unchanged());return 1;
}

static int file_import_respects_external_references_and_active_editors(void)
{
    init();CHECK(save_scale(0,"Selected",0x0091u));
    CHECK(save_scale(7,"Shortcut",0x0089u));
    CHECK(thy_engine_user_scale(&engine,1)==THY_OK);
    CHECK(thy_instrument_set_scale_shortcut(&instrument,0,78)==THY_OK);
    thy_library_six set=six("Empty set");
    CHECK(thy_engine_library_set(&engine,2,2,&set,sizeof set)==THY_OK);
    instrument.six_bank=2u;instrument.chord_layout=THY_CHORD_LAYOUT_SIX_USER;
    for(unsigned missing=0;missing<3u;++missing) {
        incoming=instrument.library;
        if(missing==0u)incoming.scales[0]=(thy_library_scale){0};
        if(missing==1u)incoming.scales[7]=(thy_library_scale){0};
        if(missing==2u)incoming.six[2]=(thy_library_six){0};
        CHECK(encode_incoming());snapshot();
        CHECK(thy_engine_library_import(&engine,file,sizeof file)==THY_ENGINE_LIBRARY_IN_USE);
        CHECK(unchanged());CHECK(!memcmp(file,file_before,sizeof file));
    }
    incoming=instrument.library;CHECK(encode_incoming());
    CHECK(thy_engine_library_open(&engine,0,1)==THY_STATE_OK);snapshot();
    CHECK(thy_engine_library_import(&engine,file,sizeof file)==THY_ENGINE_LIBRARY_BUSY);CHECK(unchanged());
    thy_engine_library_close(&engine);
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[THY_ROW_PERFORMANCE_ROOT].group,
        THY_ROW_PERFORMANCE_ROOT)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_PERFORMANCE_ROOT)==THY_STATE_OK);snapshot();
    CHECK(thy_engine_library_import(&engine,file,sizeof file)==THY_ENGINE_LIBRARY_BUSY);CHECK(unchanged());
    CHECK(thy_state_cancel(&engine.state)==THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);snapshot();
    CHECK(thy_engine_library_import(&engine,file,sizeof file)==THY_ENGINE_LIBRARY_BUSY);CHECK(unchanged());
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain();
    CHECK(thy_engine_library_import(&engine,file,sizeof file)==THY_OK);return 1;
}

static int malformed_runtime_is_rejected_before_capture_or_import_mutation(void)
{
    init();thy_library_ui_draft captured,before;
    const int16_t invalid[][3]={{1,2,4},{0,-1,4},{0,2,12},{0,2,2},{0,4,2}};
    for(unsigned n=0;n<sizeof invalid/sizeof invalid[0];++n) {
        instrument.degree_count=3u;
        memcpy(instrument.degrees,invalid[n],sizeof invalid[n]);
        memset(&captured,0x5a,sizeof captured);before=captured;snapshot();
        CHECK(thy_engine_library_capture(&engine,0,0,&captured)==THY_INVALID_ARGUMENT);
        CHECK(!memcmp(&captured,&before,sizeof captured));CHECK(unchanged());
    }
    for(unsigned state=0;state<3u;++state) {
        init();incoming=instrument.library;CHECK(encode_incoming());
        if(state==0u)instrument.user_scale=9u;
        if(state==1u)instrument.six_bank=3u;
        if(state==2u)instrument.scale_shortcuts[11]=79u;
        snapshot();
        CHECK(thy_engine_library_import(&engine,file,sizeof file)==THY_INVALID_ARGUMENT);
        CHECK(unchanged());CHECK(!memcmp(file,file_before,sizeof file));
    }
    return 1;
}

static int device_editor_save_cancel_name_and_copy_are_transactions(void)
{
    init();CHECK(thy_engine_library_open(&engine,1,0)==THY_STATE_OK);
    CHECK(down(1));CHECK(navigate(THY_STATE_RIGHT));
    CHECK(instrument.library_editor.page==THY_LIBRARY_UI_EDIT);
    CHECK(down(1));CHECK(navigate(THY_STATE_RIGHT)); /* Root edit. */
    CHECK(navigate(THY_STATE_UP));CHECK(navigate(THY_STATE_LEFT));
    CHECK(instrument.library_editor.draft.chord.root==60u && !instrument.library.chords[0].used);
    CHECK(navigate(THY_STATE_RIGHT));CHECK(navigate(THY_STATE_UP));CHECK(navigate(THY_STATE_RIGHT));
    CHECK(navigate(THY_STATE_UP));CHECK(navigate(THY_STATE_RIGHT)); /* Name edit. */
    CHECK(navigate(THY_STATE_UP));
    CHECK(instrument.library_editor.name_changed && !instrument.library.chords[0].used);
    for(unsigned n=0;n<13u;++n)CHECK(navigate(THY_STATE_RIGHT));
    CHECK(instrument.library_editor.page==THY_LIBRARY_UI_EDIT);
    CHECK(down(9));CHECK(navigate(THY_STATE_RIGHT)); /* Save. */
    CHECK(instrument.library.chords[0].used && instrument.library.chords[0].root==61u);
    CHECK(instrument.library.chords[0].name[0]=='D');
    CHECK(instrument.library_editor.notice==THY_LIBRARY_UI_NOTICE_SAVED);
    CHECK(navigate(THY_STATE_RIGHT));CHECK(down(1));CHECK(navigate(THY_STATE_RIGHT));
    CHECK(navigate(THY_STATE_UP));CHECK(navigate(THY_STATE_RIGHT));
    CHECK(navigate(THY_STATE_LEFT)); /* Cancel record, not save a draft root 62. */
    CHECK(instrument.library.chords[0].root==61u);
    CHECK(navigate(THY_STATE_DOWN));CHECK(navigate(THY_STATE_RIGHT)); /* Copy current. */
    CHECK(instrument.library_editor.page==THY_LIBRARY_UI_EDIT);
    CHECK(instrument.library_editor.draft.chord.root==60u);
    CHECK(!strcmp((const char *)instrument.library_editor.draft.chord.name,"Chord 1"));
    CHECK(instrument.library.chords[0].root==61u);
    CHECK(navigate(THY_STATE_SELECT));
    CHECK(!instrument.library_editor.open && engine.state.level==THY_STATE_PLAY);
    CHECK(instrument.library.chords[0].root==61u);return 1;
}

static int device_editor_reports_busy_and_in_use_without_losing_draft(void)
{
    init();CHECK(thy_engine_library_open(&engine,1,0)==THY_STATE_OK);
    CHECK(down(1));CHECK(navigate(THY_STATE_RIGHT));CHECK(down(9));
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);drain();
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_BLOCKED);
    CHECK(instrument.library_editor.page==THY_LIBRARY_UI_EDIT);
    CHECK(instrument.library_editor.notice==THY_LIBRARY_UI_NOTICE_RELEASE);
    CHECK(!instrument.library.chords[0].used);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain();
    CHECK(navigate(THY_STATE_RIGHT));CHECK(instrument.library.chords[0].used);
    CHECK(down(2));CHECK(navigate(THY_STATE_RIGHT));CHECK(down(1)); /* Confirm delete. */
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);drain();
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_BLOCKED);
    CHECK(instrument.library_editor.page==THY_LIBRARY_UI_DELETE_CONFIRM);
    CHECK(instrument.library_editor.notice==THY_LIBRARY_UI_NOTICE_RELEASE);
    CHECK(instrument.library.chords[0].used);
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);drain();
    instrument.chord_layout=THY_CHORD_LAYOUT_USER;
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_BLOCKED);
    CHECK(instrument.library_editor.notice==THY_LIBRARY_UI_NOTICE_IN_USE);
    instrument.chord_layout=THY_CHORD_LAYOUT_CLASSIC;
    CHECK(navigate(THY_STATE_RIGHT));
    CHECK(!instrument.library.chords[0].used);
    CHECK(instrument.library_editor.notice==THY_LIBRARY_UI_NOTICE_DELETED);
    CHECK(navigate(THY_STATE_SELECT));CHECK(!instrument.library_editor.open);return 1;
}

static int editor_context_guards_and_center_discard(void)
{
    init();engine.calibration_open=1u;
    CHECK(thy_engine_library_open(&engine,0,0)==THY_STATE_BLOCKED);
    engine.calibration_open=0u;engine.sequencer_ui.open=1u;
    CHECK(thy_engine_library_open(&engine,0,0)==THY_STATE_BLOCKED);
    engine.sequencer_ui.open=0u;
    CHECK(thy_engine_library_open(&engine,3,0)==THY_STATE_INVALID);
    CHECK(thy_engine_library_open(&engine,0,8)==THY_STATE_INVALID);
    CHECK(thy_state_focus(&engine.state,thy_menu_rows[THY_ROW_PERFORMANCE_ROOT].group,
        THY_ROW_PERFORMANCE_ROOT)==THY_STATE_OK);
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_PERFORMANCE_ROOT)==THY_STATE_OK);
    CHECK(thy_state_adjust(&engine.state,1)==THY_STATE_OK);
    CHECK(thy_engine_library_open(&engine,0,0)==THY_STATE_OK);
    CHECK(!engine.state.editing && !instrument.performance.root);
    CHECK(down(1));CHECK(navigate(THY_STATE_RIGHT));CHECK(down(3));
    CHECK(navigate(THY_STATE_RIGHT));CHECK(navigate(THY_STATE_DOWN));
    CHECK(navigate(THY_STATE_SELECT));
    CHECK(!instrument.library_editor.open && engine.state.level==THY_STATE_PLAY);
    CHECK(!instrument.library.scales[0].used && !engine.state.editing);
    CHECK(thy_engine_library_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_UNAVAILABLE);return 1;
}

static int held_numeric_edits_are_bounded_cancelable_and_never_catch_up(void)
{
    init();CHECK(thy_engine_library_open(&engine,1,0)==THY_STATE_OK);
    CHECK(down(1));CHECK(navigate(THY_STATE_RIGHT));CHECK(down(1));CHECK(navigate(THY_STATE_RIGHT));
    CHECK(thy_instrument_control(&instrument,0,THY_STATE_UP,1)==THY_OK);
    CHECK(instrument.library_editor.draft.chord.root==61u && engine.navigation_repeat.action==THY_STATE_UP);
    CHECK(thy_engine_advance(&engine,399)==THY_OK && instrument.library_editor.draft.chord.root==61u);
    CHECK(thy_engine_advance(&engine,400)==THY_OK && instrument.library_editor.draft.chord.root==62u);
    CHECK(thy_engine_advance(&engine,10000)==THY_OK && instrument.library_editor.draft.chord.root==63u);
    CHECK(thy_engine_advance(&engine,10000)==THY_OK && instrument.library_editor.draft.chord.root==63u);
    for(unsigned n=1;n<=100u;++n)CHECK(thy_engine_advance(&engine,10000u+n*30u)==THY_OK);
    CHECK(instrument.library_editor.draft.chord.root==127u);
    CHECK(!instrument.library.chords[0].used && !thy_core_queued(&core));
    CHECK(thy_instrument_control(&instrument,0,THY_STATE_UP,0)==THY_OK);
    CHECK(thy_engine_advance(&engine,15000)==THY_OK && !engine.navigation_repeat.action);
    CHECK(press_control(0,THY_STATE_LEFT));
    CHECK(instrument.library_editor.draft.chord.root==60u && !instrument.library_editor.editing);
    CHECK(navigate(THY_STATE_RIGHT));
    CHECK(thy_instrument_control(&instrument,0,THY_STATE_DOWN,1)==THY_OK);
    CHECK(instrument.library_editor.draft.chord.root==59u);
    CHECK(press_control(0,THY_STATE_SELECT));
    CHECK(!instrument.library_editor.open && engine.state.level==THY_STATE_PLAY);
    CHECK(thy_engine_advance(&engine,17000)==THY_OK && !engine.navigation_repeat.action);
    CHECK(thy_instrument_control(&instrument,0,THY_STATE_DOWN,0)==THY_OK);
    CHECK(!instrument.library.chords[0].used);return 1;
}

static int non_c_capture_roundtrips_without_double_transposition(void)
{
    init();instrument.performance.root=2u;
    layout[0]=(thy_instrument_pad){.notes={62,66,69,9,11,0,127},.valid_mask=7u,.color=1u};
    layout[48].notes[0]=62;
    thy_library_ui_draft captured;
    CHECK(thy_engine_library_capture(&engine,1,0,&captured)==THY_OK);
    CHECK(captured.chord.root==60 && captured.chord.notes[0]==60 &&
        captured.chord.notes[1]==64 && captured.chord.notes[2]==67);
    for(unsigned n=3;n<7u;++n)CHECK(captured.chord.notes[n]==layout[0].notes[n]);
    CHECK(thy_engine_library_set(&engine,1,0,&captured.chord,sizeof captured.chord)==THY_OK);
    instrument.chord_layout=THY_CHORD_LAYOUT_USER;
    const int16_t expected[]={62,66,69};CHECK(preview(0,expected,3));
    thy_library_ui_draft again;CHECK(thy_engine_library_capture(&engine,1,1,&again)==THY_OK);
    CHECK(again.chord.root==captured.chord.root && !memcmp(again.chord.notes,captured.chord.notes,7));
    instrument.chord_layout=THY_CHORD_LAYOUT_CLASSIC;
    layout[0].valid_mask=1u;layout[0].notes[0]=0;layout[0].color=0;
    memset(&again,0x5a,sizeof again);thy_library_ui_draft before=again;
    CHECK(thy_engine_library_capture(&engine,1,1,&again)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&again,&before,sizeof again));return 1;
}

static int board_guidance_is_advisory_and_physical_controls_release(void)
{
    init();instrument.context_controls=0;
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);drain();
    before_core=core;thy_library saved=instrument.library;
    CHECK(thy_state_begin_edit(&engine.state,THY_ROW_CHORDS_GUIDANCE)==THY_STATE_OK);
    CHECK(instrument.guidance.key==0xab5u);
    CHECK(thy_engine_library_open(&engine,0,0)==THY_STATE_BLOCKED);
    CHECK(thy_engine_sequencer_open(&engine,0)==THY_STATE_BLOCKED);
    CHECK(thy_engine_arp_quick_open(&engine)==THY_STATE_BLOCKED);
    CHECK(thy_engine_calibration_open(&engine)==THY_STATE_BLOCKED);
    /* Three physical DOWN presses, then RIGHT: explicit held-note capture. */
    for(unsigned n=0;n<3u;++n){
        CHECK(thy_instrument_control(&instrument,0,THY_STATE_DOWN,1)==THY_OK);
        CHECK(thy_instrument_control(&instrument,0,THY_STATE_DOWN,0)==THY_OK);
    }
    CHECK(instrument.guidance.cursor==3u);
    CHECK(thy_instrument_control(&instrument,0,THY_STATE_RIGHT,1)==THY_OK);
    CHECK(thy_instrument_control(&instrument,0,THY_STATE_RIGHT,0)==THY_OK);
    CHECK(instrument.guidance.notes==0x91u);
    CHECK(!memcmp(&core,&before_core,sizeof core));
    /* A held Arp source contributes even with no dry output; a generated
     * note and an unheld input must not contribute. */
    engine.arps[0].inputs[2].held=1;
    engine.arps[0].inputs[2].pitches[1]=UINT64_C(1)<<2; /* F#4 */
    engine.arps[0].inputs[3].pitches[1]=UINT64_C(1)<<4; /* unheld G#4 */
    core.generated[0].count=1;core.generated[0].keys[0]=70; /* generated Bb */
    CHECK(navigate(THY_STATE_RIGHT));CHECK(instrument.guidance.notes==0xd1u);
    engine.arps[0].inputs[2].held=0;core=before_core;
    instrument.guidance.notes=0x91u;
    CHECK(!memcmp(&saved,&instrument.library,sizeof saved));
    /* Release still reaches the original owner while browsing. */
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);
    CHECK(!core.owners[0].held);drain();
    CHECK(instrument.guidance.notes==0x91u);
    for(unsigned n=0;n<3;++n)CHECK(navigate(THY_STATE_UP));
    CHECK(navigate(THY_STATE_RIGHT));CHECK(instrument.guidance.page==1u&&instrument.guidance.count);
    CHECK(navigate(THY_STATE_LEFT));CHECK(navigate(THY_STATE_DOWN));CHECK(navigate(THY_STATE_RIGHT));
    CHECK(instrument.guidance.page==2u&&instrument.guidance.total>=instrument.guidance.count);
    CHECK(navigate(THY_STATE_RIGHT));CHECK(instrument.guidance.page==3u);
    CHECK(navigate(THY_STATE_RIGHT));CHECK(instrument.guidance.editing);
    uint8_t root=instrument.performance.root;uint16_t key=instrument.guidance.key;
    CHECK(navigate(THY_STATE_UP));CHECK(instrument.performance.root==root&&instrument.guidance.key==key);
    CHECK(navigate(THY_STATE_LEFT));CHECK(!instrument.guidance.editing&&instrument.guidance.page==3u);
    CHECK(navigate(THY_STATE_SELECT));CHECK(!instrument.guidance.open&&engine.state.level==THY_STATE_PLAY);
    CHECK(!memcmp(&saved,&instrument.library,sizeof saved));
    printf("Board guidance workspace: %u bytes\n",(unsigned)sizeof(thy_guidance_ui));
    CHECK(thy_engine_guidance_open(&engine)==THY_STATE_OK);
    CHECK(!instrument.guidance.notes);CHECK(navigate(THY_STATE_DOWN));CHECK(navigate(THY_STATE_RIGHT));
    CHECK(!instrument.guidance.count);CHECK(navigate(THY_STATE_DOWN));CHECK(!instrument.guidance.cursor);
    CHECK(navigate(THY_STATE_SELECT));
    CHECK(thy_engine_library_open(&engine,0,0)==THY_STATE_OK);
    CHECK(thy_engine_guidance_open(&engine)==THY_STATE_BLOCKED);
    return 1;
}

int main(void)
{
    int (*tests[])(void)={
        board_guidance_is_advisory_and_physical_controls_release,
        canonical_crud_rejects_invalid_records_atomically,
        external_selection_and_shortcuts_protect_deletions,
        live_owners_and_background_work_guard_all_writes,
        custom_scales_apply_restore_and_preserve_held_notes,
        custom_shortcuts_cover_all_slots_and_return_to_builtin,
        authored_capture_excludes_performance_and_retains_slot_holes,
        non_c_capture_roundtrips_without_double_transposition,
        user_layout_places_twenty_four_chords_in_two_octave_pairs,
        custom_slots_drive_all_seven_physical_note_buttons,
        six_sets_use_authored_intervals_active_scale_and_empty_slots,
        file_import_is_atomic_and_reapplies_selected_scale,
        file_import_respects_external_references_and_active_editors,
        malformed_runtime_is_rejected_before_capture_or_import_mutation,
        device_editor_save_cancel_name_and_copy_are_transactions,
        device_editor_reports_busy_and_in_use_without_losing_draft,
        editor_context_guards_and_center_discard,
        held_numeric_edits_are_bounded_cancelable_and_never_catch_up
    };
    for(unsigned n=0;n<sizeof tests/sizeof tests[0];++n)if(!tests[n]())return 1;
    printf("%u library engine scenarios passed (%u assertions)\n",
        (unsigned)(sizeof tests/sizeof tests[0]),assertions);
    return 0;
}
