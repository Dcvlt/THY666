/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "thy_channel_expression.h"
#include "thy_arp.h"
#include "thy_sequencer.h"

static thy_core core;
static thy_channel_expression expression;
static thy_arp arp;
static thy_sequencer sequencer;
static thy_clock clock_;
static thy_transport transport;
static thy_midi_event events[THY_QUEUE_CAPACITY];
static unsigned assertions,calls,event_count;
#define CHECK(x) do{++assertions;if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static int guard(void *context,uint8_t channel,uint8_t destinations)
{
    ++calls;thy_channel_expression *m=context;
    int result=thy_channel_expression_prepare_attack(m,channel,destinations,0u);
    if(result==THY_OK)thy_channel_expression_commit_prepared(m,channel,destinations,0u);
    return result;
}
static int reject(void *context,uint8_t channel,uint8_t destinations)
{(void)channel;(void)destinations;++calls;return *(int *)context;}
static unsigned drain(void)
{event_count=0;while(event_count<THY_QUEUE_CAPACITY&&thy_core_poll(&core,&events[event_count]))++event_count;return event_count;}
static void initialize(void)
{thy_core_init(&core);thy_channel_expression_init(&expression,&core);calls=event_count=0;}
static void fill_to(unsigned count)
{while(thy_core_queued(&core)<count && thy_core_cc(&core,THY_ROUTE_USB0,15,1,0)==THY_OK){} }
static int leave_dirty_channel(void)
{
    initialize();int16_t note=60;uint8_t flags=thy_channel_expression_flags(2,1);
    CHECK(thy_channel_expression_prepare_attack(&expression,2,4,flags)==THY_OK);
    CHECK(thy_core_press(&core,0,&note,1,0,2,4,96)==THY_OK);
    CHECK(thy_channel_expression_capture(&expression,0,flags)==THY_OK);drain();
    CHECK(thy_channel_expression_pressure_axes(&expression,0,100,127)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
    CHECK(thy_core_release(&core,0)==THY_OK);thy_channel_expression_release(&expression,0);drain();
    CHECK(expression.sent_pressure[34]==100&&expression.sent_bend[34]==16383);
    thy_core_set_generated_guard(&core,guard,&expression);return 1;
}
static int no_guard_and_validation(void)
{
    initialize();thy_generated_handle handle=999;
    CHECK(thy_core_prepare_generated(&core,2,4)==THY_OK);
    CHECK(thy_core_generated_acquire(&core,1,1,60,2,4,96,&handle)==THY_OK);
    CHECK(drain()==1&&events[0].status==0x92&&events[0].destination==THY_DIN);
    CHECK(thy_core_generated_release(&core,handle)==THY_OK);drain();
    int result=THY_QUEUE_FULL;thy_core_set_generated_guard(&core,reject,&result);handle=999;
    CHECK(thy_core_generated_acquire(&core,1,1,60,2,4,96,&handle)==THY_QUEUE_FULL);
    CHECK(calls==1&&handle==999&&!core.generated_count&&!drain());
    CHECK(thy_core_prepare_generated(&core,16,4)==THY_INVALID_ARGUMENT&&calls==1);
    CHECK(thy_core_prepare_generated(&core,2,0)==THY_INVALID_ARGUMENT&&calls==1);
    CHECK(thy_core_prepare_generated(&core,2,8)==THY_INVALID_ARGUMENT&&calls==1);
    CHECK(thy_core_generated_acquire(&core,0,1,60,2,4,96,&handle)==THY_INVALID_ARGUMENT&&calls==1);
    CHECK(thy_core_generated_acquire(&core,1,0,60,2,4,96,&handle)==THY_INVALID_ARGUMENT&&calls==1);
    CHECK(thy_core_generated_acquire(&core,1,1,128,2,4,96,&handle)==THY_INVALID_ARGUMENT&&calls==1);
    result=-1;CHECK(thy_core_prepare_generated(&core,2,4)==THY_INVALID_ARGUMENT);
    result=255;CHECK(thy_core_generated_acquire(&core,1,1,60,2,4,96,&handle)==THY_INVALID_ARGUMENT);
    CHECK(!core.generated_count&&handle==999&&!drain());
    thy_core_set_generated_guard(&core,NULL,&result);
    CHECK(!core.generated_guard&&!core.generated_guard_context);
    CHECK(thy_core_generated_acquire(&core,1,1,60,2,4,96,&handle)==THY_OK&&drain()==1);
    thy_core_init(&core);CHECK(!core.generated_guard&&!core.generated_guard_context);return 1;
}
static int pool_and_handle_preflight_do_not_call_guard(void)
{
    initialize();thy_generated_handle handle=0;
    for(unsigned n=0;n<THY_GENERATED_OWNER_COUNT;++n){CHECK(thy_core_generated_acquire(&core,1,1,60,0,1,96,&handle)==THY_OK);drain();}
    int result=THY_QUEUE_FULL;thy_core_set_generated_guard(&core,reject,&result);handle=999;
    CHECK(thy_core_generated_acquire(&core,1,1,60,0,1,96,&handle)==THY_OWNER_LIMIT);
    CHECK(!calls&&handle==999&&!drain());
    initialize();core.generated_last_handle=UINT32_MAX;thy_core_set_generated_guard(&core,reject,&result);
    CHECK(thy_core_generated_acquire(&core,1,1,60,0,1,96,&handle)==THY_HANDLE_EXHAUSTED);
    CHECK(!calls&&handle==999&&!drain());return 1;
}
static int direct_core_call_cleans_before_note_on(void)
{
    CHECK(leave_dirty_channel());thy_generated_handle handle=999;
    CHECK(thy_core_generated_acquire(&core,1,1,64,2,4,96,&handle)==THY_OK);
    CHECK(calls==1&&drain()==3);
    CHECK(events[0].destination==2&&events[0].status==0xd2&&events[0].data1==0);
    CHECK(events[1].destination==2&&events[1].status==0xe2&&events[1].data1==0&&events[1].data2==64);
    CHECK(events[2].destination==2&&events[2].status==0x92&&events[2].data1==64);
    CHECK(expression.sent_pressure[34]==0&&expression.sent_bend[34]==8192);
    CHECK(thy_core_generated_acquire(&core,16,1,67,2,4,96,&handle)==THY_OK);
    CHECK(calls==2&&drain()==1&&events[0].status==0x92);
    CHECK(thy_core_generated_acquire(&core,64,1,69,3,1,96,&handle)==THY_OK);
    CHECK(calls==3&&drain()==1&&events[0].destination==0&&events[0].status==0x93);return 1;
}
static int queue_pressure_and_independent_cleanup(void)
{
    CHECK(leave_dirty_channel());fill_to(THY_QUEUE_CAPACITY);thy_generated_handle handle=999;
    CHECK(thy_core_generated_acquire(&core,16,1,64,2,4,96,&handle)==THY_QUEUE_FULL);
    CHECK(handle==999&&!core.generated_count&&core.generated_last_handle==0);
    CHECK(expression.sent_pressure[34]==100&&expression.sent_bend[34]==16383);
    thy_midi_event discarded;
    for(unsigned n=0;n<3;++n)CHECK(thy_core_poll(&core,&discarded));
    CHECK(thy_core_generated_acquire(&core,16,1,64,2,4,96,&handle)==THY_QUEUE_FULL);
    CHECK(!core.generated_count&&core.generated_last_handle==0&&handle==999);
    CHECK(core.queue_count==THY_QUEUE_CAPACITY-1);
    CHECK(expression.sent_pressure[34]==0&&expression.sent_bend[34]==8192);
    drain();CHECK(events[event_count-2].status==0xd2&&events[event_count-1].status==0xe2);
    for(unsigned n=0;n<event_count;++n)CHECK((events[n].status&0xf0)!=0x90);
    CHECK(thy_core_generated_acquire(&core,16,1,64,2,4,96,&handle)==THY_OK);
    CHECK(drain()==1&&events[0].status==0x92);return 1;
}
static int arp_batch_preflights_cleanup_before_capacity(void)
{
    CHECK(leave_dirty_channel());thy_arp_config config;thy_arp_default_config(&config);
    config.enabled=1;config.type=THY_ARP_CHORD;config.channel=2;config.destinations=4;
    thy_arp_init(&arp,1);CHECK(thy_arp_configure(&arp,&core,&config,0,1,1)==THY_OK);
    int16_t notes[]={60,64,67};fill_to(THY_QUEUE_CAPACITY-7u);
    CHECK(thy_arp_press(&arp,&core,0,1,notes,3,96,0,1,1)==THY_OK);
    CHECK(!core.generated_count&&!arp.output_count&&!arp.attacks&&arp.last_error==THY_QUEUE_FULL);
    CHECK(calls==1&&core.queue_count==THY_QUEUE_CAPACITY-5u);
    drain();CHECK(events[event_count-2].status==0xd2&&events[event_count-1].status==0xe2);
    for(unsigned n=0;n<event_count;++n)CHECK((events[n].status&0xf0)!=0x90);
    CHECK(thy_arp_advance(&arp,&core,config.rate_ticks,1,1)==THY_OK);
    CHECK(core.generated_count==3&&arp.output_count==3&&drain()==3);
    CHECK(calls==5); /* One new batch preparation, three per-owner checks. */
    CHECK(sizeof(thy_arp_output)==16u);return 1;
}
static int sequencer_batch_preflights_cleanup_before_capacity(void)
{
    CHECK(leave_dirty_channel());thy_clock_init(&clock_,0);thy_transport_init(&transport,&clock_);
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_sequencer_init(&sequencer, undo_workspace_1);sequencer.tracks[0].channel=2;sequencer.tracks[0].destinations=4;
    thy_seq_event notes[3]={
        {.tick=0,.duration=240,.pitch=60,.value=96,.group=1},
        {.tick=0,.duration=240,.pitch=64,.value=96,.group=1},
        {.tick=0,.duration=240,.pitch=67,.value=96,.group=1}};
    CHECK(thy_sequencer_add(&sequencer,0,notes,3)==THY_OK);
    CHECK(thy_transport_set_enabled(&transport,0,1)==THY_TIME_OK);
    CHECK(thy_transport_start(&transport,&clock_)==THY_TIME_OK);fill_to(THY_QUEUE_CAPACITY-7u);
    CHECK(thy_sequencer_advance(&sequencer,&core,&transport,&clock_,NULL)==THY_QUEUE_FULL);
    CHECK(!core.generated_count&&!sequencer.voice_count&&!sequencer.attacks&&calls==1);
    CHECK(core.queue_count==THY_QUEUE_CAPACITY-5u);
    drain();CHECK(events[event_count-2].status==0xd2&&events[event_count-1].status==0xe2);
    for(unsigned n=0;n<event_count;++n)CHECK((events[n].status&0xf0)!=0x90);
    clock_.tick=3840;CHECK(thy_transport_advance(&transport,&clock_)==THY_TIME_OK);
    CHECK(thy_sequencer_advance(&sequencer,&core,&transport,&clock_,NULL)==THY_OK);
    CHECK(core.generated_count==3&&sequencer.voice_count==3&&drain()==3&&calls==5);return 1;
}
int main(void)
{
    if(!no_guard_and_validation()||!pool_and_handle_preflight_do_not_call_guard()||
        !direct_core_call_cleans_before_note_on()||!queue_pressure_and_independent_cleanup()||
        !arp_batch_preflights_cleanup_before_capacity()||!sequencer_batch_preflights_cleanup_before_capacity())return 1;
    printf("Generated expression guard: 6 scenarios passed (%u assertions; arp output %zu bytes)\n",assertions,sizeof(thy_arp_output));return 0;
}
