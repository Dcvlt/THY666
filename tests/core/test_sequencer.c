/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_sequencer.h"

static thy_core core;
static thy_sequencer seq, backup;
static thy_clock clock_;
static thy_transport transport;
static thy_arp_config presets[2];
static thy_midi_event output[THY_QUEUE_CAPACITY];
static uint8_t song[THY_SEQ_SONG_MAX];
static thy_seq_event full_batch[THY_SEQ_EVENTS];
static unsigned assertions;
#define CHECK(x) do { ++assertions; if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);return 0;} }while(0)
static void init(void)
{
    thy_core_init(&core);thy_clock_init(&clock_,0u);thy_transport_init(&transport,&clock_);thy_sequencer_init(&seq);
    for(unsigned t=0;t<THY_SEQ_TRACKS;++t)seq.tracks[t].destinations=THY_ROUTE_USB0;
    for(unsigned a=0;a<2u;++a){thy_arp_default_config(&presets[a]);presets[a].enabled=1u;}
}
static unsigned drain(void)
{unsigned count=0;while(thy_core_poll(&core,&output[count]))++count;return count;}
static thy_seq_event note(uint32_t tick,uint32_t duration,unsigned pitch,unsigned group)
{return(thy_seq_event){.tick=tick,.duration=duration,.pitch=(uint8_t)pitch,.value=96u,.group=(uint16_t)group};}
static int advance(uint64_t tick)
{clock_.tick=tick;CHECK(thy_transport_advance(&transport,&clock_)==THY_TIME_OK);CHECK(thy_sequencer_advance(&seq,&core,&transport,&clock_,presets)==THY_OK);return 1;}
static int start(unsigned track)
{CHECK(thy_transport_set_enabled(&transport,(uint8_t)track,1u)==THY_TIME_OK);CHECK(thy_transport_start(&transport,&clock_)==THY_TIME_OK);return advance(clock_.tick);}
static int sparse_edits_and_ids(void)
{
    init();CHECK(sizeof(seq)<=30720u);CHECK(sizeof(thy_seq_event)==16u);
    thy_seq_event chord[]={note(0,240,60,1),note(0,240,64,1),note(0,240,67,1)};
    for(unsigned n=0;n<3;++n)chord[n].kind=128u;
    CHECK(thy_sequencer_add(&seq,0,chord,3)==THY_OK);CHECK(seq.count==3);
    CHECK(seq.events[0].group==seq.events[2].group && seq.events[0].id!=seq.events[2].id);
    CHECK(thy_sequencer_add(&seq,1,chord,3)==THY_OK);
    CHECK(seq.events[0].group!=seq.events[3].group);
    CHECK(thy_sequencer_track_count(&seq,0)==3 && thy_sequencer_event_at(&seq,1,2)->pitch==67);
    uint16_t id=seq.events[1].id;CHECK(thy_sequencer_update(&seq,id,600,120,65,80)==THY_OK);
    CHECK(thy_sequencer_find(&seq,id)->tick==600);CHECK(thy_sequencer_remove(&seq,id)==THY_OK);
    CHECK(!thy_sequencer_find(&seq,id) && seq.count==5);return 1;
}
static int overlap_ownership_and_captured_route(void)
{
    init();thy_seq_event e=note(0,240,60,1);CHECK(thy_sequencer_add(&seq,0,&e,1)==THY_OK);
    CHECK(thy_sequencer_add(&seq,1,&e,1)==THY_OK);seq.tracks[1].channel=0;
    int16_t pitch=60;CHECK(thy_core_press(&core,0,&pitch,1,0,0,1,100)==THY_OK);CHECK(drain()==1);
    CHECK(thy_transport_set_enabled(&transport,1,1)==THY_TIME_OK);CHECK(start(0));
    CHECK(seq.voice_count==2 && drain()==0);seq.tracks[0].channel=5;seq.tracks[0].destinations=THY_ROUTE_DIN;
    CHECK(advance(240));CHECK(seq.voice_count==0 && drain()==0);
    CHECK(thy_core_release(&core,0)==THY_OK);CHECK(drain()==1 && output[0].status==0x80);
    CHECK(advance(3840));CHECK(drain()==2 && output[0].destination==THY_DIN && output[0].status==0x95);return 1;
}
static int duration_edit_and_delete_cleanup(void)
{
    init();thy_seq_event e=note(0,960,60,1);CHECK(thy_sequencer_add(&seq,0,&e,1)==THY_OK);uint16_t id=seq.events[0].id;
    CHECK(start(0));CHECK(drain()==1);CHECK(advance(100));
    CHECK(thy_sequencer_update(&seq,id,0,1200,60,96)==THY_OK);CHECK(advance(960));CHECK(drain()==0 && seq.voice_count==1);
    CHECK(thy_sequencer_update(&seq,id,0,200,60,96)==THY_OK);CHECK(advance(961));CHECK(drain()==1 && seq.voice_count==0);
    CHECK(advance(3840));CHECK(drain()==1);CHECK(thy_sequencer_remove(&seq,id)==THY_OK);CHECK(advance(3841));CHECK(drain()==1 && seq.voice_count==0);return 1;
}
static int loop_clip_late_and_stop(void)
{
    init();thy_seq_event e=note(900,500,60,1);CHECK(thy_sequencer_add(&seq,0,&e,1)==THY_OK);
    CHECK(thy_sequencer_loop(&seq,&transport,0,0,960)==THY_OK);CHECK(start(0));CHECK(advance(900));CHECK(drain()==1);
    CHECK(advance(960));CHECK(drain()==1 && !seq.voice_count);
    CHECK(advance(4000));CHECK(seq.skipped>0 && drain()==0);
    CHECK(thy_sequencer_stop(&seq,&core,&transport,&clock_)==THY_OK);CHECK(thy_transport_stop(&transport,&clock_)==THY_TIME_OK);
    CHECK(advance(8000));CHECK(drain()==0);return 1;
}
static int group_admission_is_atomic(void)
{
    init();thy_seq_event chord[]={note(0,960,60,1),note(0,960,64,1),note(0,960,67,1)};
    CHECK(thy_sequencer_add(&seq,0,chord,3)==THY_OK);
    for(unsigned n=0;n<126u;++n){thy_generated_handle handle;CHECK(thy_core_generated_acquire(&core,64,1,(uint8_t)(n%128u),0,1,96,&handle)==THY_OK);}
    drain();CHECK(thy_transport_set_enabled(&transport,0,1)==THY_TIME_OK);CHECK(thy_transport_start(&transport,&clock_)==THY_TIME_OK);
    CHECK(thy_sequencer_advance(&seq,&core,&transport,&clock_,presets)==THY_OWNER_LIMIT);
    CHECK(!seq.voice_count && !drain() && thy_core_generated_count(&core)==126);return 1;
}
static int capture_arm_snapshot_and_one_pass(void)
{
    init();CHECK(thy_sequencer_record(&seq,&transport,&clock_,0,THY_SEQ_SOURCE_PADS,THY_SEQ_REPLACE,0,960)==THY_OK);
    CHECK(seq.capture.state==THY_SEQ_ARMED && !seq.count);CHECK(start(0));
    CHECK(seq.capture.state==THY_SEQ_RECORDING && seq.capture.start_epoch==1);
    CHECK(thy_sequencer_input_on(&seq,0,10,55,1,60,90,0)==THY_OK);
    CHECK(thy_sequencer_input_on(&seq,0,10,55,1,60,90,0)==THY_OK);CHECK(seq.capture.count==1);
    CHECK(thy_sequencer_input_on(&seq,0,11,55,1,64,91,0)==THY_OK);
    CHECK(thy_sequencer_input_off(&seq,0,10,480)==THY_OK);CHECK(advance(960));
    CHECK(seq.capture.state==THY_SEQ_CAPTURE_OFF && seq.count==2);
    CHECK(seq.events[0].duration==480 && seq.events[1].duration==960);
    CHECK(seq.events[0].group==seq.events[1].group && (seq.events[1].kind&128u));
    CHECK(advance(1920));CHECK(seq.count==2);return 1;
}
static int replace_edges_and_undo(void)
{
    init();thy_seq_event e=note(0,3000,60,1);CHECK(thy_sequencer_add(&seq,0,&e,1)==THY_OK);
    thy_seq_event pc={.tick=1500,.duration=1,.kind=THY_SEQ_PROGRAM,.pitch=9};CHECK(thy_sequencer_add(&seq,0,&pc,1)==THY_OK);
    CHECK(thy_sequencer_record(&seq,&transport,&clock_,0,0,0,960,1920)==THY_OK);CHECK(start(0));drain();
    CHECK(thy_sequencer_input_on(&seq,0,1,1,0,67,100,0)==THY_OK);CHECK(thy_sequencer_input_off(&seq,0,1,480)==THY_OK);
    CHECK(advance(960));CHECK(seq.count==4);unsigned left=0,right=0,newnote=0,program=0;
    for(unsigned n=0;n<seq.count;++n){thy_seq_event *v=&seq.events[n];left+=v->pitch==60 && v->tick==0 && v->duration==960;
        right+=v->pitch==60 && v->tick==1920 && v->duration==1080;newnote+=v->pitch==67 && v->tick==960 && v->duration==480;program+=(v->kind&127u)==THY_SEQ_PROGRAM;}
    CHECK(left==1 && right==1 && newnote==1 && program==1);
    CHECK(thy_sequencer_history(&seq,&core,&transport,0)==THY_OK);CHECK(seq.count==2 && seq.events[0].duration==3000);
    CHECK(thy_sequencer_history(&seq,&core,&transport,1)==THY_OK);CHECK(seq.count==4);return 1;
}
static int growing_finish_and_locked_target(void)
{
    init();CHECK(thy_sequencer_record(&seq,&transport,&clock_,2,0,0,0,0)==THY_OK);CHECK(start(2));
    CHECK(seq.capture.growing);CHECK(thy_sequencer_select(&seq,4)==THY_OK);
    CHECK(thy_sequencer_input_on(&seq,THY_SEQ_SOURCE_ARP1,88,1,0,55,100,0)==THY_OK);CHECK(!seq.capture.count);
    CHECK(thy_sequencer_input_on(&seq,0,1,1,0,60,100,10)==THY_OK);
    CHECK(advance(1000));CHECK(thy_sequencer_finish(&seq,&transport,&clock_,0)==THY_OK);
    CHECK(seq.capture.end_position==3840);CHECK(advance(3840));CHECK(seq.count==1 && seq.events[0].track==2);
    CHECK(seq.events[0].tick==10 && seq.events[0].duration==3830 && seq.tracks[2].loop_end==3840);return 1;
}
static int take_overflow_preserves_original(void)
{
    init();thy_seq_event e=note(0,960,60,1);CHECK(thy_sequencer_add(&seq,0,&e,1)==THY_OK);backup=seq;
    CHECK(thy_sequencer_record(&seq,&transport,&clock_,0,0,0,0,960)==THY_OK);CHECK(start(0));drain();
    for(unsigned n=0;n<THY_SEQ_TAKE_EVENTS;++n){CHECK(thy_sequencer_input_on(&seq,0,n+1,1,0,(uint8_t)(n%128u),90,0)==THY_OK);
        CHECK(thy_sequencer_input_off(&seq,0,n+1,10)==THY_OK);}
    CHECK(thy_sequencer_input_on(&seq,0,1000,1,0,62,90,0)==THY_SEQ_FULL);
    CHECK(thy_sequencer_finish(&seq,&transport,&clock_,1)==THY_SEQ_CAPTURE_FAILED);
    CHECK(seq.count==backup.count && !memcmp(seq.events,backup.events,seq.count*sizeof(seq.events[0])));
    CHECK(seq.capture.state==THY_SEQ_FAILED);
    CHECK(thy_sequencer_stop(&seq,&core,&transport,&clock_)==THY_OK);
    CHECK(seq.capture.state==THY_SEQ_CAPTURE_OFF && seq.count==backup.count);
    return 1;
}
static int assignment_grouped_history_and_content(void)
{
    init();thy_seq_event e=note(0,240,60,1);CHECK(thy_sequencer_add(&seq,0,&e,1)==THY_OK);
    CHECK(thy_sequencer_transaction_begin(&seq)==THY_OK);
    e.pitch=64;CHECK(thy_sequencer_assign(&seq,0,0,480,&e,1,1)==THY_OK);
    e.pitch=67;CHECK(thy_sequencer_assign(&seq,0,0,480,&e,1,0)==THY_OK);thy_sequencer_transaction_end(&seq);
    CHECK(seq.count==2);CHECK(thy_sequencer_history(&seq,&core,&transport,0)==THY_OK);CHECK(seq.count==1 && seq.events[0].pitch==60);
    CHECK(thy_sequencer_history(&seq,&core,&transport,1)==THY_OK);CHECK(seq.count==2);
    CHECK(thy_sequencer_duplicate(&seq,0,0,960,7680,1)==THY_OK);CHECK(seq.count==4 && seq.tracks[0].loop_end==8640);
    CHECK(seq.events[0].group!=seq.events[2].group);
    CHECK(thy_sequencer_loop(&seq,&transport,0,0,960)==THY_OK);CHECK(seq.count==4 && seq.tracks[0].content_end>=8640);
    CHECK(thy_sequencer_trim(&seq,0,0,960)==THY_OK);CHECK(seq.count==2 && seq.tracks[0].content_end==960);return 1;
}
static int codec_roundtrip_and_atomic_rejection(void)
{
    init();thy_seq_event e=note(7680,960,0,1);CHECK(thy_sequencer_add(&seq,20,&e,1)==THY_OK);seq.tracks[20].processor=THY_SEQ_ARP2;
    size_t written=0;CHECK(thy_sequencer_encode(&seq,song,sizeof(song),&written)==THY_OK);
    CHECK(written==636 && song[4]==2u && thy_sequencer_validate(song,written)==THY_OK);backup=seq;
    CHECK(thy_sequencer_decode(&seq,song,written)==THY_OK);CHECK(seq.count==1 && seq.events[0].pitch==0 && seq.tracks[20].processor==2);
    CHECK(!seq.voice_count && !seq.history_valid && seq.capture.state==0);
    song[written-1]^=1;backup=seq;CHECK(thy_sequencer_decode(&seq,song,written)==THY_SEQ_CORRUPT);
    CHECK(!memcmp(&seq,&backup,sizeof(seq)));CHECK(thy_sequencer_validate(song,written-1)==THY_SEQ_CORRUPT);return 1;
}
static int compact_track_arp_and_isolated_cleanup(void)
{
    init();thy_seq_event chord[]={note(0,960,60,1),note(0,960,64,1),note(0,960,67,1),note(0,960,62,2)};
    for(unsigned n=0;n<3;++n)chord[n].kind=128u;
    CHECK(thy_sequencer_add(&seq,0,chord,4)==THY_OK);seq.tracks[0].processor=THY_SEQ_ARP1;
    presets[0].rate_ticks=240;presets[0].gate_percent=50;CHECK(start(0));CHECK(drain()==1 && output[0].data1==60);
    CHECK(advance(120));CHECK(drain()==1 && output[0].status==0x80);
    CHECK(advance(240));CHECK(drain()==1 && output[0].data1==62);
    thy_generated_handle live;CHECK(thy_core_generated_acquire(&core,1,1,70,0,1,100,&live)==THY_OK);drain();
    CHECK(thy_sequencer_stop(&seq,&core,&transport,&clock_)==THY_OK);CHECK(thy_core_generated_find(&core,live));
    CHECK(drain()==1 && output[0].data1==62);return 1;
}
static int quantization_controller_lanes_and_oneshot(void)
{
    init();seq.tracks[0].flags=(3u<<THY_SEQ_QUANTIZE_SHIFT)|THY_SEQ_FLAG_ONESHOT|THY_SEQ_FLAG_RECORD_CONTROLLERS;
    CHECK(thy_sequencer_loop(&seq,&transport,0,0,960)==THY_OK);
    CHECK(thy_sequencer_record(&seq,&transport,&clock_,0,0,0,0,960)==THY_OK);CHECK(start(0));
    CHECK(thy_sequencer_input_on(&seq,0,1,1,0,60,96,130)==THY_OK);CHECK(thy_sequencer_input_off(&seq,0,1,500)==THY_OK);
    CHECK(thy_sequencer_input_event(&seq,0,THY_SEQ_CC,74,100,600)==THY_OK);CHECK(advance(960));
    CHECK(seq.count==2 && seq.events[0].tick==240 && seq.events[0].duration==240);
    CHECK(start(0));drain();
    CHECK(advance(1200));CHECK(drain()==1 && output[0].data1==60);
    CHECK(advance(1440));CHECK(drain()==1 && output[0].status==0x80);
    CHECK(advance(1560));CHECK(drain()==1 && output[0].status==0xb0 && output[0].data1==74);
    CHECK(advance(1920));CHECK(!transport.tracks[0].running && !transport.tracks[0].enabled && !seq.voice_count);return 1;
}
static int queued_cancel_and_stop_partial_replace(void)
{
    init();thy_seq_event old=note(0,3840,60,1);CHECK(thy_sequencer_add(&seq,0,&old,1)==THY_OK);CHECK(start(0));drain();
    CHECK(thy_sequencer_record(&seq,&transport,&clock_,0,0,0,0,3840)==THY_OK);CHECK(seq.capture.state==THY_SEQ_QUEUED);
    CHECK(thy_sequencer_record(&seq,&transport,&clock_,1,0,0,0,3840)==THY_OK);CHECK(seq.capture.state==0 && seq.count==1);
    transport.launch_quantize=THY_TRANSPORT_IMMEDIATE;
    CHECK(thy_sequencer_record(&seq,&transport,&clock_,0,0,0,0,3840)==THY_OK);CHECK(advance(0));
    CHECK(thy_sequencer_input_on(&seq,0,100,1,0,67,80,0)==THY_OK);CHECK(advance(240));
    CHECK(thy_sequencer_stop(&seq,&core,&transport,&clock_)==THY_OK);CHECK(seq.count==2);
    CHECK(seq.events[0].pitch==60 && seq.events[0].tick==240 && seq.events[0].duration==3600);
    CHECK(seq.events[1].pitch==67 && seq.events[1].duration==240);return 1;
}
static int full_capacity_and_non_destructive_limits(void)
{
    init();for(unsigned n=0;n<THY_SEQ_EVENTS;++n)full_batch[n]=note(n*60u,30u,n%128u,n+1u);
    CHECK(thy_sequencer_add(&seq,0,full_batch,THY_SEQ_EVENTS)==THY_OK);CHECK(seq.count==THY_SEQ_EVENTS);
    backup=seq;thy_seq_event extra=note(0,240,60,1);
    CHECK(thy_sequencer_add(&seq,0,&extra,1)==THY_SEQ_FULL);CHECK(seq.count==backup.count &&
        !memcmp(seq.events,backup.events,sizeof(seq.events)));
    CHECK(thy_sequencer_duplicate(&seq,0,0,60,60000,0)==THY_SEQ_FULL);
    CHECK(!memcmp(seq.events,backup.events,sizeof(seq.events)));
    size_t written=0;CHECK(thy_sequencer_encode(&seq,song,sizeof(song),&written)==THY_OK);
    CHECK(written==THY_SEQ_SONG_MAX && thy_sequencer_validate(song,written)==THY_OK);
    CHECK(thy_sequencer_decode(&seq,song,written)==THY_OK);CHECK(seq.count==THY_SEQ_EVENTS);return 1;
}
static int controller_event_delivery(void)
{
    init();thy_seq_event events[]={
        {.tick=0,.duration=1,.kind=THY_SEQ_PROGRAM,.pitch=12},
        {.tick=120,.duration=1,.kind=THY_SEQ_CHANNEL_PRESSURE,.pitch=80},
        {.tick=240,.duration=1,.kind=THY_SEQ_PRESSURE,.pitch=60,.value=73},
        {.tick=360,.duration=1,.kind=THY_SEQ_BEND,.pitch=0,.value=64},
        {.tick=480,.duration=1,.kind=THY_SEQ_CC,.pitch=74,.value=99}};
    seq.tracks[0].channel=3;seq.tracks[0].flags=THY_SEQ_FLAG_SEND_PRESSURE;CHECK(thy_sequencer_add(&seq,0,events,5)==THY_OK);
    CHECK(start(0));CHECK(drain()==1 && output[0].status==0xc3 && output[0].data1==12);
    CHECK(advance(120));CHECK(drain()==1 && output[0].status==0xd3 && output[0].data1==80);
    CHECK(advance(240));CHECK(drain()==1 && output[0].status==0xa3 && output[0].data1==60 && output[0].data2==73);
    CHECK(advance(360));CHECK(drain()==1 && output[0].status==0xe3 && output[0].data2==64);
    CHECK(advance(480));CHECK(drain()==1 && output[0].status==0xb3 && output[0].data1==74 && output[0].data2==99);return 1;
}
static int compact_arp_each_layer_and_restart(void)
{
    init();thy_seq_event events[]={note(0,960,60,1),note(0,960,64,1),note(0,960,67,1),note(100,500,62,2)};
    for(unsigned n=0;n<3;++n)events[n].kind=128u;
    CHECK(thy_sequencer_add(&seq,0,events,4)==THY_OK);seq.tracks[0].processor=THY_SEQ_ARP1;
    presets[0].grouping=THY_ARP_EACH;CHECK(start(0));CHECK(drain()==1 && output[0].data1==60);
    CHECK(advance(100));CHECK(drain()==1 && output[0].data1==62);CHECK(seq.voice_count==2);
    CHECK(advance(120));CHECK(drain()==1 && output[0].data1==60);CHECK(advance(220));CHECK(drain()==1 && output[0].data1==62);
    CHECK(thy_sequencer_stop(&seq,&core,&transport,&clock_)==THY_OK);drain();
    presets[0].grouping=THY_ARP_HYBRID;presets[0].restart_on_change=1;presets[0].layer=1;
    CHECK(start(0));CHECK(drain()==3 && seq.voice_count==4); /* one arp plus three dry owners; shared C emits once. */
    CHECK(advance(320));CHECK(seq.voice_count>=4); /* new melody restarts phrase, dry D coexists. */
    CHECK(thy_sequencer_stop(&seq,&core,&transport,&clock_)==THY_OK);CHECK(!seq.voice_count);return 1;
}
static int mute_preserves_phase_and_oneshot_end(void)
{
    init();thy_seq_event e=note(0,800,60,1);CHECK(thy_sequencer_add(&seq,0,&e,1)==THY_OK);
    CHECK(thy_sequencer_loop(&seq,&transport,0,0,960)==THY_OK);seq.tracks[0].flags=THY_SEQ_FLAG_ONESHOT;
    CHECK(start(0));CHECK(drain()==1);CHECK(advance(100));seq.tracks[0].muted=1;
    CHECK(advance(101));CHECK(drain()==1 && !seq.voice_count);CHECK(advance(960));
    CHECK(!transport.tracks[0].running);seq.tracks[0].muted=0;CHECK(advance(1000));CHECK(!drain());return 1;
}
static int quantized_track_launch_with_small_service_delay(void)
{
    init();thy_seq_event e=note(0,240,60,1);CHECK(thy_sequencer_add(&seq,0,&e,1)==THY_OK);
    CHECK(thy_transport_start(&transport,&clock_)==THY_TIME_OK);CHECK(advance(100));
    CHECK(thy_transport_request_track(&transport,0,1,THY_TRANSPORT_BAR,&clock_)==THY_TIME_OK);
    CHECK(transport.tracks[0].pending_position==3840);
    CHECK(advance(3842));CHECK(drain()==1 && output[0].status==0x90 && output[0].data1==60);
    CHECK(seq.voices[0].on_position==3840 && seq.voices[0].off_position==4080);
    CHECK(advance(4080));CHECK(drain()==1 && !seq.voice_count);return 1;
}
static uint32_t test_crc(const uint8_t *p,size_t count)
{uint32_t value=UINT32_MAX;for(size_t n=0;n<count;++n){value^=p[n];for(unsigned bit=0;bit<8;++bit)value=(value>>1)^((0u-(value&1u))&0xedb88320u);}return ~value;}
static void repair_crc(size_t size)
{uint32_t value=test_crc(song+16,size-16);for(unsigned n=0;n<4;++n)song[12+n]=(uint8_t)(value>>(8u*n));}
static int codec_rejects_crc_valid_structural_corruption(void)
{
    init();thy_seq_event notes[]={note(0,240,60,1),note(0,240,64,1)};CHECK(thy_sequencer_add(&seq,0,notes,2)==THY_OK);
    size_t size=0;CHECK(thy_sequencer_encode(&seq,song,sizeof(song),&size)==THY_OK);
    unsigned event_offset=32u+THY_SEQ_TRACKS*28u;uint8_t saved=song[event_offset+16+8];
    song[event_offset+16+8]=song[event_offset+8];repair_crc(size);
    CHECK(thy_sequencer_validate(song,size)==THY_SEQ_CORRUPT);
    song[event_offset+16+8]=saved;repair_crc(size);CHECK(thy_sequencer_validate(song,size)==THY_OK);
    song[32+23]=0u;repair_crc(size);CHECK(thy_sequencer_validate(song,size)==THY_SEQ_CORRUPT);
    song[32+23]=THY_SEQ_FLAG_CREATED;repair_crc(size);song[event_offset+12]=THY_SEQ_TRACKS;repair_crc(size);
    CHECK(thy_sequencer_validate(song,size)==THY_SEQ_CORRUPT);
    seq.tracks[0].muted=2;size_t untouched=123u;
    CHECK(thy_sequencer_encode(&seq,song,sizeof(song),&untouched)==THY_SEQ_CORRUPT && untouched==123u);
    return 1;
}
static int sustain_track_leases_and_captured_routes(void)
{
    init();thy_seq_event a[]={
        {.tick=0,.duration=1,.kind=THY_SEQ_CC,.pitch=64,.value=127},note(0,120,60,1),
        {.tick=240,.duration=1,.kind=THY_SEQ_CC,.pitch=64,.value=0}};
    CHECK(thy_sequencer_add(&seq,0,a,3)==THY_OK);a[1].pitch=64;a[2].tick=360;
    CHECK(thy_sequencer_add(&seq,1,a,3)==THY_OK);seq.tracks[1].channel=0;
    CHECK(thy_transport_set_enabled(&transport,1,1)==THY_TIME_OK);CHECK(start(0));CHECK(drain()==3);
    CHECK(advance(120));CHECK(!seq.voice_count && core.sustain[0] && core.sounding_count==2 && !drain());
    CHECK(advance(240));CHECK(core.sustain[0] && !drain());CHECK(advance(360));
    CHECK(!core.sustain[0] && !core.sounding_count && drain()==3);
    CHECK(advance(3840));drain();seq.tracks[0].channel=5;seq.tracks[0].destinations=THY_ROUTE_DIN;
    CHECK(thy_sequencer_stop(&seq,&core,&transport,&clock_)==THY_OK);CHECK(!core.sustain[0] && !seq.voice_count);
    unsigned count=drain();CHECK(count>=3);
    for(unsigned n=0;n<count;++n)CHECK(output[n].destination==THY_USB0 && (output[n].status&15u)==0u);
    return 1;
}
static int sustain_cleanup_keeps_live_pedal_and_full_queue_reservations(void)
{
    init();CHECK(thy_core_sustain(&core,1,0,1)==THY_OK);int16_t note70=70;
    CHECK(thy_core_press(&core,0,&note70,1,0,0,1,90)==THY_OK);CHECK(thy_core_release(&core,0)==THY_OK);drain();
    thy_seq_event a[]={ {.tick=0,.duration=1,.kind=THY_SEQ_CC,.pitch=64,.value=127},note(0,120,60,1)};
    CHECK(thy_sequencer_add(&seq,0,a,2)==THY_OK);CHECK(start(0));drain();
    CHECK(thy_sequencer_stop(&seq,&core,&transport,&clock_)==THY_OK);
    CHECK(core.sustain[0] && core.sounding_count==2 && !drain());
    CHECK(thy_core_sustain(&core,1,0,0)==THY_OK);CHECK(drain()==3 && !core.sounding_count);
    init();CHECK(thy_sequencer_add(&seq,0,a,2)==THY_OK);CHECK(start(0));drain();
    unsigned filled=0;while(thy_core_cc(&core,1,2,74,100)==THY_OK)++filled;
    CHECK(filled>900 && thy_core_queued(&core)+thy_core_reserved(&core)==THY_QUEUE_CAPACITY);
    CHECK(thy_sequencer_stop(&seq,&core,&transport,&clock_)==THY_OK);
    CHECK(!core.sustain[0] && !core.sounding_count && !seq.voice_count && thy_core_queued(&core)<=THY_QUEUE_CAPACITY);
    return 1;
}
static int continuous_capture_thinning_keeps_edges(void)
{
    init();seq.tracks[0].flags=THY_SEQ_FLAG_SEND_PRESSURE|THY_SEQ_FLAG_RECORD_CONTROLLERS;
    CHECK(thy_sequencer_record(&seq,&transport,&clock_,0,0,0,0,960)==THY_OK);CHECK(start(0));
    for(unsigned tick=0;tick<600;++tick)CHECK(thy_sequencer_input_event(&seq,0,THY_SEQ_PRESSURE,60,(uint8_t)((tick%120u)+1u),tick)==THY_OK);
    CHECK(seq.capture.count==10);
    unsigned before=seq.capture.count;
    CHECK(thy_sequencer_input_event(&seq,0,THY_SEQ_CC,64,127,610)==THY_OK);
    CHECK(thy_sequencer_input_event(&seq,0,THY_SEQ_CC,64,0,611)==THY_OK);
    CHECK(thy_sequencer_input_event(&seq,0,THY_SEQ_PROGRAM,5,0,612)==THY_OK);
    CHECK(thy_sequencer_input_event(&seq,0,THY_SEQ_PROGRAM,6,0,613)==THY_OK);
    CHECK(seq.capture.count==before+4);CHECK(advance(960));CHECK(seq.count==before+4);return 1;
}
static int live_loop_extension_preserves_phase_and_owners(void)
{
    init();thy_seq_event notes[]={note(0,2000,60,1),note(1500,120,64,2)};
    CHECK(thy_sequencer_add(&seq,0,notes,2)==THY_OK);CHECK(thy_sequencer_loop(&seq,&transport,0,0,960)==THY_OK);
    CHECK(start(0));CHECK(drain()==1);CHECK(advance(900));uint32_t generation=transport.tracks[0].generation;
    CHECK(thy_sequencer_loop(&seq,&transport,0,0,1920)==THY_OK);
    CHECK(transport.tracks[0].generation==generation && transport.tracks[0].playhead==900 && seq.voice_count==1);
    CHECK(advance(1000));CHECK(transport.tracks[0].playhead==1000 && seq.voice_count==1 && !drain());
    CHECK(advance(1500));CHECK(drain()==1 && output[0].data1==64);CHECK(seq.voice_count==2);
    CHECK(advance(1620));CHECK(drain()==1 && output[0].data1==64);
    CHECK(advance(1920));CHECK(drain()==2 && output[0].status==0x80 && output[1].status==0x90);
    /* A duplicate can extend the canonical loop before transport's next call.
     * Even if that call crossed the OLD end, restore the intended phase. */
    init();thy_seq_event e=note(0,240,60,1);CHECK(thy_sequencer_add(&seq,0,&e,1)==THY_OK);
    CHECK(thy_sequencer_loop(&seq,&transport,0,0,960)==THY_OK);CHECK(start(0));drain();CHECK(advance(900));drain();
    CHECK(thy_sequencer_duplicate(&seq,0,0,960,960,1)==THY_OK);generation=transport.tracks[0].generation;
    CHECK(advance(1000));CHECK(transport.tracks[0].playhead==1000 && transport.tracks[0].generation==generation);
    CHECK(drain()==1 && output[0].data1==60);return 1;
}
static int delayed_multiple_loops_release_before_new_attack(void)
{
    init();thy_seq_event notes[]={note(0,2400,60,1),note(960,120,64,2)};
    CHECK(thy_sequencer_add(&seq,0,notes,2)==THY_OK);CHECK(start(0));CHECK(drain()==1);
    CHECK(advance(15360));CHECK(drain()==2 && output[0].status==0x80 && output[0].data1==60 &&
        output[1].status==0x90 && output[1].data1==60);
    CHECK(seq.skipped>0 && seq.voice_count==1);return 1;
}
static int created_track_lifecycle_and_legacy_migration(void)
{
    init();CHECK(!thy_sequencer_track_exists(&seq,0));
    CHECK(thy_sequencer_create(&seq,4)==THY_OK && seq.selected_track==4);
    CHECK(thy_sequencer_track_exists(&seq,4) && seq.tracks[4].grid_ticks==240u);
    CHECK(thy_sequencer_create(&seq,4)==THY_SEQ_BUSY);
    size_t size=0;CHECK(thy_sequencer_encode(&seq,song,sizeof song,&size)==THY_OK);
    CHECK(thy_sequencer_decode(&seq,song,size)==THY_OK && thy_sequencer_track_exists(&seq,4));
    CHECK(thy_sequencer_delete(&seq,4)==THY_OK && !thy_sequencer_track_exists(&seq,4));
    CHECK(thy_sequencer_history(&seq,&core,&transport,0)==THY_OK && thy_sequencer_track_exists(&seq,4));
    CHECK(thy_sequencer_history(&seq,&core,&transport,1)==THY_OK && !thy_sequencer_track_exists(&seq,4));
    thy_seq_event e=note(240,180,67,1);CHECK(thy_sequencer_add(&seq,2,&e,1)==THY_OK);
    CHECK(thy_sequencer_encode(&seq,song,sizeof song,&size)==THY_OK);
    /* Build a real v1 stream from v2: preserve its tracks/events and strip only
     * the v2 created marker/grid extension, then repair length and CRC. */
    for(unsigned t=0;t<THY_SEQ_TRACKS;++t){memmove(song+32u+t*24u,song+32u+t*28u,24u);song[32u+t*24u+23u]&=127u;}
    memmove(song+32u+THY_SEQ_TRACKS*24u,song+32u+THY_SEQ_TRACKS*28u,seq.count*16u);
    size-=THY_SEQ_TRACKS*4u;song[4]=1u;
    for(unsigned b=0;b<4u;++b)song[8u+b]=(uint8_t)(size>>(b*8u));repair_crc(size);
    CHECK(thy_sequencer_validate(song,size)==THY_OK);
    CHECK(thy_sequencer_decode(&seq,song,size)==THY_OK);
    CHECK(thy_sequencer_track_exists(&seq,2) && !thy_sequencer_track_exists(&seq,4));
    CHECK(seq.count==1u && seq.events[0].tick==240u && seq.events[0].duration==180u && seq.events[0].pitch==67u);
    CHECK(thy_sequencer_encode(&seq,song,sizeof song,&size)==THY_OK && song[4]==2u);
    return 1;
}
static int step_bucket_replace_and_grid_retime_are_undoable(void)
{
    init();thy_seq_event e[]={note(250,180,60,1),note(470,12,64,2),note(480,120,67,3),
        {.tick=300,.duration=1,.kind=THY_SEQ_CC,.pitch=1,.value=80}};
    CHECK(thy_sequencer_add(&seq,0,e,4)==THY_OK);
    thy_seq_event input=note(0,1,72,1);
    CHECK(thy_sequencer_assign(&seq,0,240,240,&input,1,2)==THY_OK);
    CHECK(seq.count==3u && seq.events[0].pitch==67u && seq.events[1].kind==THY_SEQ_CC && seq.events[2].pitch==72u);
    CHECK(thy_sequencer_history(&seq,&core,&transport,0)==THY_OK && seq.count==4u);
    CHECK(thy_sequencer_grid(&seq,&transport,0,480)==THY_OK);
    CHECK(seq.tracks[0].grid_ticks==480u && seq.tracks[0].loop_end==7680u);
    CHECK(seq.events[0].tick==500u && seq.events[0].duration==360u && seq.events[3].tick==600u);
    CHECK(thy_sequencer_history(&seq,&core,&transport,0)==THY_OK);
    CHECK(seq.tracks[0].grid_ticks==240u && seq.events[0].tick==250u);
    CHECK(thy_sequencer_record(&seq,&transport,&clock_,0,0,0,0,960)==THY_OK);
    CHECK(thy_sequencer_delete(&seq,0)==THY_SEQ_BUSY);
    CHECK(thy_sequencer_grid(&seq,&transport,0,120)==THY_SEQ_BUSY);
    init();input=note(0,2500,60,1);CHECK(thy_sequencer_add(&seq,0,&input,1)==THY_OK);
    CHECK(thy_sequencer_loop(&seq,&transport,0,960,2880)==THY_OK);
    CHECK(thy_sequencer_grid(&seq,&transport,0,120)==THY_OK);
    CHECK(seq.events[0].duration==2500u && seq.tracks[0].content_end>=2500u);
    size_t bytes=0;CHECK(thy_sequencer_encode(&seq,song,sizeof song,&bytes)==THY_OK);
    return 1;
}
int main(void)
{
    int (*tests[])(void)={sparse_edits_and_ids,overlap_ownership_and_captured_route,duration_edit_and_delete_cleanup,
        loop_clip_late_and_stop,group_admission_is_atomic,capture_arm_snapshot_and_one_pass,replace_edges_and_undo,
        growing_finish_and_locked_target,take_overflow_preserves_original,assignment_grouped_history_and_content,
        codec_roundtrip_and_atomic_rejection,compact_track_arp_and_isolated_cleanup,
        quantization_controller_lanes_and_oneshot,queued_cancel_and_stop_partial_replace,
        full_capacity_and_non_destructive_limits,controller_event_delivery,
        compact_arp_each_layer_and_restart,mute_preserves_phase_and_oneshot_end,
        quantized_track_launch_with_small_service_delay,codec_rejects_crc_valid_structural_corruption,
        sustain_track_leases_and_captured_routes,sustain_cleanup_keeps_live_pedal_and_full_queue_reservations,
        continuous_capture_thinning_keeps_edges,live_loop_extension_preserves_phase_and_owners,
        delayed_multiple_loops_release_before_new_attack,created_track_lifecycle_and_legacy_migration,
        step_bucket_replace_and_grid_retime_are_undoable};
    for(unsigned n=0;n<sizeof(tests)/sizeof(tests[0]);++n)if(!tests[n]())return 1;
    printf("%u sequencer C tests passed (%u assertions), sizeof=%zu\n",(unsigned)(sizeof(tests)/sizeof(tests[0])),assertions,sizeof(seq));return 0;
}
