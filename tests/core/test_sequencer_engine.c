/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_engine.h"

static thy_core core;
static thy_instrument instrument;
static thy_engine engine;
static thy_instrument_pad layout[96];
static thy_midi_event events[THY_QUEUE_CAPACITY];
static uint8_t import_image[THY_SEQ_SONG_MAX];
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);return 0;} }while(0)
static void init(void)
{
    const int16_t degrees[]={0,2,4,5,7,9,11};
    thy_core_init(&core);thy_instrument_init(&instrument,&core);
    for(unsigned p=0;p<96;++p)layout[p]=(thy_instrument_pad){.notes={60,64,67},.valid_mask=p<48?7u:1u,.color=1u};
    layout[48].notes[0]=62;
    (void)thy_instrument_set_layout(&instrument,layout,60,degrees,7);
    thy_engine_init(&engine,&instrument,21,70);(void)thy_engine_advance(&engine,0);
    for(unsigned t=0;t<21;++t)engine.sequencer.tracks[t].destinations=THY_ROUTE_USB0;
}
static unsigned drain(void)
{unsigned count=0;while(thy_core_poll(&core,&events[count]))++count;return count;}
static int advance(unsigned ms)
{CHECK(thy_engine_advance(&engine,ms)==THY_OK);return 1;}
static int arm(unsigned source,int bounded)
{
    engine.sequencer.tracks[engine.sequencer.selected_track].record_source=(uint8_t)source;
    engine.sequencer_ui.region_enabled=(uint8_t)bounded;engine.sequencer_ui.region_start=0;engine.sequencer_ui.region_end=960;
    CHECK(thy_engine_sequencer_record(&engine)==THY_OK);return 1;
}
static int arp(int layer)
{
    thy_arp_config c=engine.arps[0].config;c.enabled=1;c.layer=(uint8_t)layer;c.destinations=THY_ROUTE_USB0;
    CHECK(thy_engine_arp_configure(&engine,0,&c)==THY_OK);return 1;
}
static int pads_capture_snapshot_and_target_lock(void)
{
    init();CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);CHECK(drain()==3);
    CHECK(advance(100));CHECK(arm(THY_SEQ_SOURCE_PADS,1));CHECK(engine.sequencer.capture.state==THY_SEQ_ARMED);
    CHECK(thy_engine_transport_start(&engine)==THY_OK);CHECK(engine.sequencer.capture.count==3);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);CHECK(engine.sequencer.capture.count==3);
    CHECK(thy_sequencer_select(&engine.sequencer,3)==THY_OK);CHECK(advance(350));
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);CHECK(drain()==3);
    CHECK(advance(600));CHECK(engine.sequencer.count==3 && engine.sequencer.capture.state==THY_SEQ_CAPTURE_OFF);
    uint16_t group=engine.sequencer.events[0].group;
    for(unsigned n=0;n<3;++n){const thy_seq_event *e=&engine.sequencer.events[n];CHECK(e->track==0 && e->tick==0 && e->duration==480);
        CHECK(e->group==group && (e->kind&128u));}
    return 1;
}
static int generator_capture_is_final_output_once(void)
{
    init();CHECK(arp(0));CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);drain();
    CHECK(arm(THY_SEQ_SOURCE_ARP1,1));CHECK(thy_engine_transport_start(&engine)==THY_OK);CHECK(engine.sequencer.capture.count==1);
    CHECK(advance(63));drain();CHECK(advance(125));drain();CHECK(advance(188));drain();CHECK(advance(250));drain();
    CHECK(advance(313));drain();CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);CHECK(advance(500));
    CHECK(engine.sequencer.count==3);
    CHECK(engine.sequencer.events[0].pitch==60 && engine.sequencer.events[1].pitch==64 && engine.sequencer.events[2].pitch==67);
    for(unsigned n=0;n<3;++n)CHECK(engine.sequencer.events[n].duration>=120 && engine.sequencer.events[n].duration<=121);
    CHECK(!core.generated_count && !core.sounding_count);return 1;
}
static int live_mix_keeps_duplicate_pitch_contributions(void)
{
    init();CHECK(arp(1));CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);drain();
    CHECK(arm(THY_SEQ_SOURCE_LIVE,1));CHECK(thy_engine_transport_start(&engine)==THY_OK);
    CHECK(engine.sequencer.capture.count==4);unsigned c_notes=0;
    for(unsigned n=0;n<engine.sequencer.capture.count;++n)c_notes+=engine.sequencer.take[n].pitch==60;
    CHECK(c_notes==2);CHECK(advance(100));drain();CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain();
    CHECK(advance(500));CHECK(engine.sequencer.count==4);return 1;
}
static int growing_first_take_finish_does_not_release_live(void)
{
    init();CHECK(arm(THY_SEQ_SOURCE_PADS,0));CHECK(thy_engine_transport_start(&engine)==THY_OK);
    CHECK(advance(50));CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);drain();
    CHECK(advance(200));CHECK(thy_engine_sequencer_record(&engine)==THY_OK);
    CHECK(engine.sequencer.capture.state==THY_SEQ_FINISHING && engine.sequencer.capture.end_position==3840);
    CHECK(advance(2000));CHECK(engine.sequencer.count==1 && engine.sequencer.tracks[0].loop_end==3840);
    CHECK(thy_core_pad_held(&core,48));CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);
    CHECK(!thy_core_pad_held(&core,48));return 1;
}
static int held_source_survives_sequencer_view_changes(void)
{
    init();CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);drain();
    CHECK(thy_sequencer_create(&engine.sequencer,0)==THY_OK);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS)==THY_STATE_OK);
    CHECK(thy_core_pad_held(&core,48));
    CHECK(thy_sequencer_ui_configure(&engine.sequencer_ui,THY_SEQ_UI_INPUT,THY_SEQ_INPUT_CHORDS)==THY_OK);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(thy_engine_sequencer_close(&engine)==THY_STATE_OK);
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);CHECK(!core.sounding_count && drain()==1);return 1;
}
static int split_step_input_records_while_take_is_active(void)
{
    init();CHECK(thy_sequencer_create(&engine.sequencer,0)==THY_OK);
    CHECK(arm(THY_SEQ_SOURCE_PADS,0));CHECK(thy_engine_transport_start(&engine)==THY_OK);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS)==THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);CHECK(!core.owners[0].held && !drain());
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);
    /* The latched step editor yields to this track's active live recorder. */
    CHECK(engine.sequencer.capture.count==1 && !engine.input_assignment[48]);
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);
    CHECK(thy_engine_transport_stop(&engine)==THY_OK);CHECK(!core.sounding_count);return 1;
}
static int per_track_ownership_shares_live_pitch_safely(void)
{
    init();thy_seq_event e={.tick=0,.duration=480,.kind=THY_SEQ_NOTE,.pitch=60,.value=90};
    CHECK(thy_sequencer_add(&engine.sequencer,0,&e,1)==THY_OK);CHECK(thy_sequencer_add(&engine.sequencer,1,&e,1)==THY_OK);
    engine.sequencer.tracks[1].channel=0;
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);CHECK(drain()==3);
    CHECK(thy_engine_transport_start(&engine)==THY_OK);CHECK(engine.sequencer.voice_count==2 && !drain());
    CHECK(thy_engine_transport_stop(&engine)==THY_OK);CHECK(core.owners[0].held && drain()==0);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);CHECK(drain()==3);return 1;
}
static int remote_usb_capture_filters_without_echo(void)
{
    init();CHECK(arm(THY_SEQ_SOURCE_USB,1));CHECK(thy_engine_transport_start(&engine)==THY_OK);
    const uint8_t on[]={0x92,60,87},off[]={0x82,60,0};
    CHECK(thy_engine_receive_midi(&engine,on,3,10)==THY_OK);CHECK(!engine.sequencer.capture.count && !drain());
    engine.remote_record=1;engine.remote_channel=4;
    CHECK(thy_engine_receive_midi(&engine,on,3,20)==THY_OK);CHECK(!engine.sequencer.capture.count && !drain());
    engine.remote_channel=3;
    CHECK(thy_engine_receive_midi(&engine,on,3,30)==THY_OK);CHECK(engine.sequencer.capture.count==1 && !drain());
    CHECK(thy_engine_receive_midi(&engine,off,3,130)==THY_OK);CHECK(!drain());CHECK(advance(500));
    CHECK(engine.sequencer.count==1 && engine.sequencer.events[0].pitch==60 && engine.sequencer.events[0].value==87);
    CHECK(engine.sequencer.events[0].duration==192);return 1;
}
static int source_release_that_crosses_capture_boundary(void)
{
    init();CHECK(thy_engine_transport_start(&engine)==THY_OK);CHECK(advance(1000));
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);drain();
    engine.sequencer_ui.region_enabled=1;engine.sequencer_ui.region_start=0;engine.sequencer_ui.region_end=960;
    CHECK(thy_engine_sequencer_record(&engine)==THY_OK);CHECK(engine.sequencer.capture.queued_position==3840);
    /* Physical sampling advances core time before its release callback. The
     * boundary service must snapshot the held note BEFORE that callback clears it. */
    CHECK(thy_core_advance(&core,2001)==THY_OK);CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);drain();
    CHECK(engine.sequencer.capture.count==1);CHECK(advance(2500));CHECK(engine.sequencer.count==1);
    CHECK(engine.sequencer.events[0].tick==0 && engine.sequencer.events[0].duration==1);return 1;
}
static int assigned_arp_input_stays_excluded_across_capture_start(void)
{
    init();CHECK(thy_sequencer_create(&engine.sequencer,0)==THY_OK);
    CHECK(arp(0));
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS)==THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument,4)==THY_OK); /* editor column 2, tick 240 */
    CHECK(thy_instrument_release_pad(&instrument,4)==THY_OK); /* Selection is latched. */
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK); /* assigned D is auditioned through Arp */
    CHECK(engine.input_assignment[48] && engine.sequencer.count==1);drain();
    CHECK(thy_engine_sequencer_close(&engine)==THY_STATE_OK);
    CHECK(engine.input_assignment[48] && core.owners[48].held && !engine.sequencer.transaction_open);
    CHECK(thy_sequencer_select(&engine.sequencer,1)==THY_OK);CHECK(arm(THY_SEQ_SOURCE_ARP1,1));
    CHECK(thy_engine_transport_start(&engine)==THY_OK);CHECK(!engine.sequencer.capture.count);
    CHECK(advance(63));drain();CHECK(advance(125));drain();
    CHECK(!engine.sequencer.capture.count); /* Assigned input remains excluded after leaving editor. */
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);
    CHECK(!engine.sequencer.capture.count); /* Its generated tail must not become newly eligible. */
    CHECK(advance(188));drain();
    CHECK(advance(500));
    CHECK(engine.sequencer.count==1);unsigned recorded_d=0,assigned_d=0;
    for(unsigned n=0;n<engine.sequencer.count;++n){const thy_seq_event *e=&engine.sequencer.events[n];
        recorded_d+=e->track==1 && e->pitch==62;
        assigned_d+=e->track==0 && e->pitch==62 && e->tick==240;}
    CHECK(!recorded_d && assigned_d==1 && !core.sounding_count);return 1;
}
static int queued_replace_rejects_position_seek_and_restart(void)
{
    init();thy_seq_event old={.tick=0,.duration=960,.kind=THY_SEQ_NOTE,.pitch=67,.value=90};
    CHECK(thy_sequencer_add(&engine.sequencer,0,&old,1)==THY_OK);CHECK(thy_engine_transport_start(&engine)==THY_OK);
    CHECK(advance(100));CHECK(arm(THY_SEQ_SOURCE_PADS,1));CHECK(engine.sequencer.capture.state==THY_SEQ_QUEUED);
    uint64_t position=engine.rhythm_transport.position,queued=engine.sequencer.capture.queued_position;
    uint32_t generation=engine.rhythm_transport.generation;engine.remote_position=1;
    const uint8_t seek[]={0xf2,64,0};
    CHECK(thy_engine_receive_midi(&engine,seek,3,101)==THY_SEQ_BUSY);
    CHECK(thy_engine_transport_start(&engine)==THY_SEQ_BUSY);
    CHECK(engine.rhythm_transport.position==position && engine.rhythm_transport.generation==generation);
    CHECK(engine.sequencer.capture.state==THY_SEQ_QUEUED && engine.sequencer.capture.queued_position==queued);
    CHECK(engine.sequencer.count==1 && engine.sequencer.events[0].pitch==67 && engine.sequencer.events[0].duration==960);
    CHECK(thy_engine_transport_stop(&engine)==THY_OK);CHECK(engine.sequencer.capture.state==THY_SEQ_CAPTURE_OFF);return 1;
}
static int external_seek_restart_guards_and_stop_closes_capture(void)
{
    init();CHECK(thy_engine_clock_configure(&engine,120000,THY_CLOCK_SOURCE_USB,4,4,THY_TRANSPORT_BAR)==THY_OK);
    CHECK(arm(THY_SEQ_SOURCE_PADS,1));
    const uint8_t seek[]={0xf2,64,0},start[]={0xfa},pulse[]={0xf8},stop[]={0xfc};
    CHECK(thy_engine_receive_time(&engine,seek,3,0)==THY_SEQ_BUSY);CHECK(engine.sequencer.capture.state==THY_SEQ_ARMED);
    CHECK(thy_engine_receive_time(&engine,start,1,0)==THY_OK); /* ARM may start normally. */
    CHECK(thy_engine_receive_time(&engine,pulse,1,10)==THY_OK);CHECK(engine.sequencer.capture.state==THY_SEQ_RECORDING);
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);drain();
    CHECK(thy_engine_receive_time(&engine,pulse,1,20)==THY_OK);uint64_t position=engine.rhythm_transport.position;
    CHECK(thy_engine_receive_time(&engine,start,1,20)==THY_SEQ_BUSY);
    CHECK(thy_engine_receive_time_deferred(&engine,seek,3,20)==THY_SEQ_BUSY);
    CHECK(engine.rhythm_transport.position==position && engine.sequencer.capture.state==THY_SEQ_RECORDING);
    CHECK(thy_engine_receive_time(&engine,stop,1,20)==THY_OK);
    CHECK(engine.rhythm_transport.state==THY_TRANSPORT_PAUSED && engine.sequencer.capture.state==THY_SEQ_CAPTURE_OFF);
    CHECK(engine.sequencer.count==1 && engine.sequencer.events[0].pitch==62 && engine.sequencer.events[0].duration==40);
    CHECK(core.owners[48].held);CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);return 1;
}
static int song_import_rejects_armed_and_recording_take_without_mutation(void)
{
    init();thy_seq_event original={.tick=0,.duration=960,.kind=THY_SEQ_NOTE,.pitch=67,.value=90};
    CHECK(thy_sequencer_add(&engine.sequencer,0,&original,1)==THY_OK);
    size_t bytes=0;CHECK(thy_sequencer_encode(&engine.sequencer,import_image,sizeof(import_image),&bytes)==THY_OK);
    CHECK(thy_sequencer_validate(import_image,bytes)==THY_OK);
    CHECK(thy_sequencer_update(&engine.sequencer,engine.sequencer.events[0].id,0,960,72,90)==THY_OK);
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);drain();CHECK(arm(THY_SEQ_SOURCE_PADS,1));
    CHECK(thy_engine_sequencer_song_import(&engine,import_image,bytes)==THY_SEQ_BUSY);
    CHECK(engine.sequencer.capture.state==THY_SEQ_ARMED && engine.sequencer.count==1 && engine.sequencer.events[0].pitch==72);
    CHECK(core.owners[48].held && !drain());
    CHECK(thy_engine_transport_start(&engine)==THY_OK);drain();CHECK(advance(125));
    uint64_t position=engine.rhythm_transport.position;uint16_t voices=engine.sequencer.voice_count;
    CHECK(engine.sequencer.capture.state==THY_SEQ_RECORDING && engine.sequencer.capture.count==1);
    CHECK(thy_engine_sequencer_song_import(&engine,import_image,bytes)==THY_SEQ_BUSY);
    CHECK(engine.sequencer.capture.state==THY_SEQ_RECORDING && engine.sequencer.capture.count==1 &&
        engine.sequencer.take[0].pitch==62 && engine.sequencer.events[0].pitch==72);
    CHECK(engine.rhythm_transport.state==THY_TRANSPORT_PLAYING && engine.rhythm_transport.position==position &&
        engine.sequencer.voice_count==voices && core.owners[48].held && !drain());
    CHECK(thy_engine_transport_stop(&engine)==THY_OK);CHECK(core.owners[48].held);
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);return 1;
}
static int strum_capture_excludes_dry_pads_and_freezes_admission(void)
{
    init();CHECK(arm(THY_SEQ_SOURCE_STRUM,1));CHECK(thy_engine_transport_start(&engine)==THY_OK);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);drain();
    CHECK(!engine.sequencer.capture.count);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain();
    instrument.performance.strum=1u;instrument.performance.strum_ms=24u;
    CHECK(advance(100));CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);drain();
    CHECK(engine.sequencer.capture.count==1 && engine.input_strummed[0]);
    instrument.performance.strum=0u;
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);drain();
    CHECK(!engine.input_strummed[48] && engine.sequencer.capture.count==1);
    CHECK(advance(124));drain();CHECK(advance(148));drain();
    CHECK(engine.sequencer.capture.count==3);
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);drain();
    CHECK(advance(200));CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);drain();
    CHECK(advance(500));CHECK(engine.sequencer.count==3 && !engine.input_strummed[0]);
    CHECK(engine.sequencer.events[0].tick<engine.sequencer.events[1].tick &&
        engine.sequencer.events[1].tick<engine.sequencer.events[2].tick);
    return 1;
}
static int simplified_actions_and_latched_chord_entry(void)
{
    init();engine.rhythm_transport.tracks[0].generation=UINT32_MAX;
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_CREATE_TRACK,0)==THY_HANDLE_EXHAUSTED);
    CHECK(!thy_sequencer_track_exists(&engine.sequencer,0));
    engine.rhythm_transport.tracks[0].generation=1u;
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_CREATE_TRACK,255u)==THY_OK);
    CHECK(engine.sequencer.selected_track==0u && thy_sequencer_track_exists(&engine.sequencer,0));
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS)==THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument,0)==THY_OK);CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);
    CHECK(!engine.sequencer.count && !drain());
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);drain();
    CHECK(engine.sequencer.count==1 && engine.sequencer.events[0].tick==0u);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_DELETE_TRACK,0)==THY_SEQ_BUSY);
    CHECK(thy_instrument_press_pad(&instrument,4)==THY_OK);CHECK(thy_instrument_release_pad(&instrument,4)==THY_OK);
    CHECK(engine.sequencer_ui.selected_step==1u);
    CHECK(thy_instrument_press_pad(&instrument,52)==THY_OK);drain();
    CHECK(engine.sequencer.count==2u && engine.sequencer.events[1].tick==0u);
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);CHECK(thy_instrument_release_pad(&instrument,52)==THY_OK);drain();
    CHECK(thy_instrument_press_pad(&instrument,52)==THY_OK);CHECK(thy_instrument_release_pad(&instrument,52)==THY_OK);drain();
    CHECK(engine.sequencer.count==3u && engine.sequencer.events[2].tick==240u);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_CLEAR_STEP,0)==THY_OK && engine.sequencer.count==2u);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_INPUT_TOGGLE,0)==THY_OK);
    CHECK(engine.sequencer_ui.input_layout==THY_SEQ_INPUT_CHORDS);
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);drain();
    CHECK(engine.sequencer.count==5u);
    for(unsigned n=2u;n<5u;++n)CHECK(engine.sequencer.events[n].tick==240u && (engine.sequencer.events[n].kind&128u));
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_OVERDUB,0)==THY_OK && engine.sequencer_ui.overdub);
    CHECK(engine.sequencer.tracks[0].record_policy==THY_SEQ_OVERDUB);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_OCTAVE_UP,0)==THY_STATE_OK && instrument.performance.chord_octave==1);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_OCTAVE_DOWN,0)==THY_STATE_OK && !instrument.performance.chord_octave);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_HELP,0)==THY_OK && engine.sequencer_ui.help_open);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK && !engine.sequencer_ui.help_open);
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK && !engine.sequencer_ui.open);
    CHECK(!thy_state_row_visible(&engine.state,THY_ROW_SEQUENCER_TRACK));
    CHECK(!thy_state_row_visible(&engine.state,THY_ROW_SEQUENCER_LAYOUT));
    CHECK(!thy_state_row_visible(&engine.state,THY_ROW_SEQUENCER_LENGTH));
    CHECK(thy_state_row_visible(&engine.state,THY_ROW_SEQUENCER_HELP));return 1;
}
static int isolated_all_playback_and_delete_keep_live_owners(void)
{
    init();thy_seq_event a={.tick=0,.duration=480,.kind=THY_SEQ_NOTE,.pitch=60,.value=90};
    CHECK(thy_sequencer_add(&engine.sequencer,0,&a,1)==THY_OK);a.pitch=64;
    CHECK(thy_sequencer_add(&engine.sequencer,1,&a,1)==THY_OK);
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);drain();
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_PLAY_TRACK,0)==THY_OK);
    CHECK(engine.rhythm_transport.tracks[0].running && !engine.rhythm_transport.tracks[1].running && engine.sequencer.voice_count==1u);drain();
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_PLAY_ALL,0)==THY_OK);
    CHECK(engine.rhythm_transport.tracks[0].running && engine.rhythm_transport.tracks[1].running && engine.sequencer.voice_count==2u);drain();
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_DELETE_TRACK,0)==THY_OK);
    CHECK(!thy_sequencer_track_exists(&engine.sequencer,0) && engine.sequencer.voice_count==1u && engine.rhythm_transport.tracks[1].running);
    CHECK(core.owners[48].held);drain();
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_PLAY_ALL,1)==THY_OK);
    CHECK(engine.rhythm_transport.state==THY_TRANSPORT_STOPPED && !engine.sequencer.voice_count && core.owners[48].held);drain();
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK && !core.sounding_count);return 1;
}
static int direct_record_and_queued_new_track_autoplay(void)
{
    init();CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_CREATE_TRACK,0)==THY_OK);
    CHECK(thy_engine_sequencer_loop(&engine,0,0,960)==THY_OK);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS)==THY_STATE_OK);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_RECORD,0)==THY_OK);
    CHECK(engine.sequencer.capture.state==THY_SEQ_RECORDING && engine.rhythm_transport.state==THY_TRANSPORT_PLAYING);
    CHECK(thy_instrument_press_pad(&instrument,48)==THY_OK);drain();CHECK(advance(100));
    CHECK(thy_instrument_release_pad(&instrument,48)==THY_OK);drain();CHECK(advance(500));drain();
    CHECK(engine.sequencer.capture.state==THY_SEQ_CAPTURE_OFF && engine.sequencer.count==1u && engine.rhythm_transport.tracks[0].running);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_PLAY_TRACK,0)==THY_OK);drain();
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_CREATE_TRACK,1)==THY_OK);
    CHECK(thy_engine_sequencer_loop(&engine,1,0,960)==THY_OK);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_PLAY_ALL,1)==THY_OK);drain();CHECK(advance(600));drain();
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_RECORD,1)==THY_OK);
    CHECK(engine.sequencer.capture.state==THY_SEQ_QUEUED && engine.rhythm_transport.tracks[1].pending==THY_TRANSPORT_PENDING_START);
    CHECK(engine.sequencer.capture.queued_position==engine.rhythm_transport.tracks[1].pending_position);
    CHECK(advance(2500));drain();CHECK(engine.sequencer.capture.state==THY_SEQ_RECORDING);
    CHECK(thy_instrument_press_pad(&instrument,52)==THY_OK);drain();CHECK(advance(2600));
    CHECK(thy_instrument_release_pad(&instrument,52)==THY_OK);drain();CHECK(advance(3000));drain();
    CHECK(engine.sequencer.capture.state==THY_SEQ_CAPTURE_OFF && thy_sequencer_track_count(&engine.sequencer,1)==1u);
    CHECK(engine.rhythm_transport.tracks[1].running && engine.sequencer.voice_count>=1u);
    CHECK(thy_engine_transport_stop(&engine)==THY_OK);CHECK(!core.sounding_count);return 1;
}
static int track_options_preserve_step_input_colors_and_held_releases(void)
{
    init();CHECK(thy_sequencer_create(&engine.sequencer,0u)==THY_OK);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS)==THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument,4u)==THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,4u)==THY_OK);
    CHECK(engine.sequencer_ui.selected_step==1u && !drain());
    CHECK(thy_instrument_press_pad(&instrument,48u)==THY_OK);
    CHECK(engine.sequencer.count==1u && engine.input_assignment[48]);
    CHECK(drain()==1u);
    const thy_pad_owner owner=core.owners[48];
    uint16_t before[3],after[3];
    CHECK(thy_engine_sequencer_color(&engine,4u,before));
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(engine.sequencer_ui.options_open && engine.sequencer_ui.open);
    CHECK(thy_engine_sequencer_color(&engine,4u,after));
    CHECK(!memcmp(before,after,sizeof before));
    CHECK(!memcmp(&owner,&core.owners[48],sizeof owner) && !drain());
    /* The OLED changes, but LEFT remains a silent step selector. */
    CHECK(thy_instrument_press_pad(&instrument,8u)==THY_OK);
    CHECK(thy_instrument_release_pad(&instrument,8u)==THY_OK);
    CHECK(engine.sequencer_ui.selected_step==2u && !core.owners[8].held && !drain());
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(engine.sequencer_ui.open && !engine.sequencer_ui.options_open &&
        engine.sequencer_ui.view==THY_SEQ_VIEW_STEPS && engine.sequencer_ui.selected_step==2u);
    CHECK(!memcmp(&owner,&core.owners[48],sizeof owner));
    CHECK(thy_instrument_release_pad(&instrument,48u)==THY_OK);
    CHECK(drain()==1u && (events[0].status&0xf0u)==0x80u && events[0].data1==62u);
    CHECK(!core.sounding_count && !engine.sequencer.transaction_open);
    /* Newly acquired input inside options is still recorded into the step. */
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(thy_instrument_press_pad(&instrument,52u)==THY_OK);
    CHECK(engine.input_assignment[52] && engine.sequencer.count==2u);
    CHECK(thy_instrument_release_pad(&instrument,52u)==THY_OK);drain();
    CHECK(thy_sequencer_ui_configure(&engine.sequencer_ui,THY_SEQ_UI_INPUT,THY_SEQ_INPUT_CHORDS)==THY_OK);
    CHECK(thy_instrument_press_pad(&instrument,48u)==THY_OK);
    CHECK(core.owners[48].held && core.owners[48].count==3u && engine.input_assignment[48]);
    CHECK(engine.sequencer.count==4u);
    unsigned chord_notes=0u;
    for(unsigned n=0u;n<engine.sequencer.count;++n)
        chord_notes+=engine.sequencer.events[n].tick==480u && !!(engine.sequencer.events[n].kind&128u);
    CHECK(chord_notes==3u);CHECK(drain()==3u);
    const thy_pad_owner chord_owner=core.owners[48];
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK);
    CHECK(!engine.sequencer_ui.open && !engine.sequencer_ui.options_open);
    CHECK(!memcmp(&chord_owner,&core.owners[48],sizeof chord_owner) && !drain());
    CHECK(thy_instrument_release_pad(&instrument,48u)==THY_OK);
    CHECK(drain()==3u && !core.sounding_count && !core.generated_count && !engine.sequencer.transaction_open);
    for(unsigned n=0u;n<3u;++n)CHECK((events[n].status&0xf0u)==0x80u);
    return 1;
}
static int split_profile_records_upper_aftertouch_and_legacy_stays_unchanged(void)
{
    for(unsigned split=0u;split<2u;++split) {
        init();
        const thy_instrument_response response={200u,150u,100u,1100u,0u};
        const thy_expression_profile profile={(uint8_t)split,50u,0u,0u};
        CHECK(thy_instrument_set_cluster_response(&instrument,0u,&response)==THY_OK);
        CHECK(thy_instrument_set_cluster_expression_profile(&instrument,0u,&profile)==THY_OK);
        engine.sequencer.tracks[0].flags|=THY_SEQ_FLAG_SEND_PRESSURE;
        CHECK(arm(THY_SEQ_SOURCE_PADS,1));
        CHECK(thy_engine_transport_start(&engine)==THY_OK);
        CHECK(thy_instrument_press_pad(&instrument,0u)==THY_OK);drain();
        instrument.sensors[0].raw=850u;
        instrument.sensors[0].pressure=127u;
        CHECK(advance(30u));
        unsigned pressures=0u;
        for(unsigned n=0u;n<engine.sequencer.capture.count;++n) {
            const thy_seq_event *event=&engine.sequencer.take[n];
            if(event->kind==THY_SEQ_PRESSURE) {
                CHECK(event->value==(split?63u:127u));++pressures;
            }
        }
        CHECK(pressures==3u);
        CHECK(thy_instrument_release_pad(&instrument,0u)==THY_OK);
        CHECK(thy_engine_transport_stop(&engine)==THY_OK);drain();
    }
    return 1;
}
static int track_options_leave_sequencer_playback_running(void)
{
    init();thy_seq_event note={.tick=0u,.duration=120u,.kind=THY_SEQ_NOTE,.pitch=60u,.value=90u};
    CHECK(thy_sequencer_add(&engine.sequencer,0u,&note,1u)==THY_OK);
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS)==THY_STATE_OK);
    CHECK(thy_engine_sequencer_action(&engine,THY_SEQ_UI_PLAY_TRACK,0u)==THY_OK);
    CHECK(engine.sequencer.voice_count==1u && drain()==1u);
    CHECK(thy_engine_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(engine.sequencer_ui.options_open && engine.rhythm_transport.tracks[0].running);
    CHECK(advance(100u));
    CHECK(!engine.sequencer.voice_count && engine.rhythm_transport.tracks[0].running);
    CHECK(drain()==1u && (events[0].status&0xf0u)==0x80u);
    CHECK(thy_engine_navigate(&engine,THY_STATE_LEFT)==THY_STATE_OK);
    CHECK(engine.sequencer_ui.open && !engine.sequencer_ui.options_open && engine.rhythm_transport.tracks[0].running);
    CHECK(thy_engine_transport_stop(&engine)==THY_OK && !core.sounding_count);
    return 1;
}
int main(void)
{
    int(*tests[])(void)={pads_capture_snapshot_and_target_lock,generator_capture_is_final_output_once,
        live_mix_keeps_duplicate_pitch_contributions,growing_first_take_finish_does_not_release_live,
        held_source_survives_sequencer_view_changes,split_step_input_records_while_take_is_active,
        per_track_ownership_shares_live_pitch_safely,remote_usb_capture_filters_without_echo,
        source_release_that_crosses_capture_boundary,assigned_arp_input_stays_excluded_across_capture_start,
        queued_replace_rejects_position_seek_and_restart,external_seek_restart_guards_and_stop_closes_capture,
        song_import_rejects_armed_and_recording_take_without_mutation,
        strum_capture_excludes_dry_pads_and_freezes_admission,simplified_actions_and_latched_chord_entry,
        isolated_all_playback_and_delete_keep_live_owners,direct_record_and_queued_new_track_autoplay,
        track_options_preserve_step_input_colors_and_held_releases,
        track_options_leave_sequencer_playback_running,
        split_profile_records_upper_aftertouch_and_legacy_stays_unchanged};
    for(unsigned n=0;n<sizeof(tests)/sizeof(tests[0]);++n)if(!tests[n]())return 1;
    printf("%u sequencer engine C tests passed (%u assertions)\n",(unsigned)(sizeof(tests)/sizeof(tests[0])),assertions);return 0;
}
