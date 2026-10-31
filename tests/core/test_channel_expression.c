/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "thy_channel_expression.h"

static thy_core core;
static thy_channel_expression expression,saved;
static thy_midi_event events[THY_QUEUE_CAPACITY];
static unsigned event_count,assertions;
#define CHECK(x) do {++assertions;if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static uint8_t flags(unsigned p,unsigned b)
{return thy_channel_expression_flags((uint8_t)p,(uint8_t)b);}
static void init(void)
{thy_core_init(&core);thy_channel_expression_init(&expression,&core);event_count=0;}
static unsigned drain(void)
{event_count=0;while(event_count<THY_QUEUE_CAPACITY && thy_core_poll(&core,&events[event_count]))++event_count;return event_count;}
static int has(unsigned dest,unsigned status,unsigned a,unsigned b)
{for(unsigned n=0;n<event_count;++n)if(events[n].destination==dest&&events[n].status==status&&events[n].data1==a&&events[n].data2==b)return 1;return 0;}
static int press(unsigned pad,unsigned pitch,unsigned channel,unsigned dests,uint8_t policy)
{
    int result=thy_channel_expression_prepare_attack(&expression,(uint8_t)channel,(uint8_t)dests,policy);
    if(result!=THY_OK)return result;
    int16_t note=(int16_t)pitch;
    result=thy_core_press(&core,(uint8_t)pad,&note,1,0,(uint8_t)channel,(uint8_t)dests,96);
    if(result!=THY_OK)return result;
    return thy_channel_expression_capture(&expression,(uint8_t)pad,policy);
}
static int release(unsigned pad)
{int r=thy_core_release(&core,(uint8_t)pad);thy_channel_expression_release(&expression,(uint8_t)pad);return r;}
static void fill(void)
{while(thy_core_cc(&core,THY_ROUTE_USB0,15,1,0)==THY_OK){} }

static int cold_default_and_validation(void)
{
    init();CHECK(sizeof expression<=768u);
    CHECK(thy_channel_expression_service(&expression)==THY_OK && !drain());
    CHECK(press(0,60,3,5,flags(1,0))==THY_OK);CHECK(drain()==2);
    CHECK(has(0,0x93,60,96)&&has(2,0x93,60,96));
    CHECK(thy_channel_expression_pressure_axes(&expression,0,127,127)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK && !drain());
    CHECK(release(0)==THY_OK);CHECK(drain()==2);
    for(unsigned p=0;p<3;++p)for(unsigned b=0;b<3;++b)CHECK(thy_channel_expression_flags_valid(flags(p,b)));
    for(unsigned f=0;f<256;++f)CHECK(thy_channel_expression_flags_valid((uint8_t)f)==(f<16&&(f&3)<3&&(f>>2)<3));
    CHECK(flags(3,0)==255 && flags(0,3)==255);
    saved=expression;
    CHECK(thy_channel_expression_prepare_attack(&expression,16,1,0)==THY_INVALID_ARGUMENT);
    CHECK(thy_channel_expression_prepare_attack(&expression,1,0,0)==THY_INVALID_ARGUMENT);
    CHECK(thy_channel_expression_prepare_attack(&expression,1,8,0)==THY_INVALID_ARGUMENT);
    CHECK(thy_channel_expression_prepare_attack(&expression,1,1,255)==THY_INVALID_ARGUMENT);
    CHECK(thy_channel_expression_capture(&expression,0,0)==THY_INVALID_ARGUMENT);
    CHECK(thy_channel_expression_pressure_axes(&expression,96,1,1)==THY_INVALID_ARGUMENT);
    CHECK(thy_channel_expression_pressure_axes(&expression,0,128,1)==THY_INVALID_ARGUMENT);
    CHECK(thy_channel_expression_pressure_axes(&expression,0,1,128)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&expression,&saved,sizeof expression)&&!drain());return 1;
}
static int max_pressure_and_captured_routes(void)
{
    init();CHECK(press(0,60,3,5,flags(2,0))==THY_OK);drain();
    CHECK(thy_channel_expression_pressure(&expression,0,80)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==2);
    CHECK(has(0,0xd3,80,0)&&has(2,0xd3,80,0));
    CHECK(press(1,60,3,1,flags(2,0))==THY_OK);CHECK(!drain());
    CHECK(thy_channel_expression_pressure(&expression,1,110)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==1&&has(0,0xd3,110,0));
    CHECK(thy_channel_expression_pressure(&expression,0,20)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==1&&has(2,0xd3,20,0));
    /* Preparing another route never changes this owner's captured route. */
    CHECK(thy_channel_expression_prepare_attack(&expression,9,4,0)==THY_OK&&!drain());
    CHECK(thy_channel_expression_pressure(&expression,0,35)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==1&&has(2,0xd3,35,0));
    CHECK(release(1)==THY_OK&&!drain());
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==1&&has(0,0xd3,35,0));
    CHECK(release(0)==THY_OK);drain();
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==2&&has(0,0xd3,0,0)&&has(2,0xd3,0,0));return 1;
}
static int newest_bend_fallback_and_independent_axes(void)
{
    init();CHECK(press(0,60,2,4,flags(2,1))==THY_OK);drain();
    CHECK(thy_channel_expression_pressure_axes(&expression,0,20,127)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==2);
    CHECK(has(2,0xd2,20,0)&&has(2,0xe2,127,127));
    CHECK(press(1,64,2,4,flags(2,2))==THY_OK);CHECK(drain()==2);
    CHECK(events[0].status==0xe2&&events[0].data1==0&&events[0].data2==64);
    CHECK(events[1].status==0x92);
    CHECK(thy_channel_expression_pressure_axes(&expression,1,100,127)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==2);
    CHECK(has(2,0xd2,100,0)&&has(2,0xe2,0,0));
    CHECK(thy_channel_expression_pressure_axes(&expression,0,50,40)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK&&!drain());
    CHECK(release(1)==THY_OK);drain();
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==2);
    unsigned bend=8192+40u*8191u/127u;
    CHECK(has(2,0xd2,50,0)&&has(2,0xe2,bend&127,bend>>7));
    CHECK(thy_channel_expression_bend_pressure(&expression,0,0)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==1&&has(2,0xe2,0,64));return 1;
}
static int sustain_spring_return_and_panic(void)
{
    init();CHECK(press(0,60,4,4,flags(2,1))==THY_OK);drain();
    CHECK(thy_channel_expression_pressure_axes(&expression,0,127,127)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
    CHECK(thy_core_sustain(&core,4,4,1)==THY_OK);drain();
    CHECK(release(0)==THY_OK&&!drain()&&thy_core_sounding(&core)==1);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==2&&has(2,0xd4,0,0)&&has(2,0xe4,0,64));
    CHECK(thy_core_sustain(&core,4,4,0)==THY_OK);CHECK(drain()==2&&has(2,0x84,60,0));
    CHECK(press(0,60,4,4,flags(2,2))==THY_OK);drain();
    CHECK(thy_channel_expression_pressure_axes(&expression,0,100,100)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
    thy_core_panic(&core);thy_channel_expression_release_all(&expression);drain();
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==2&&has(2,0xd4,0,0)&&has(2,0xe4,0,64));
    CHECK(thy_channel_expression_service(&expression)==THY_OK&&!drain());return 1;
}
static int full_queue_neutral_then_attack_and_atomic_prepare(void)
{
    init();CHECK(press(0,60,1,4,flags(2,1))==THY_OK);drain();
    CHECK(thy_channel_expression_pressure_axes(&expression,0,127,127)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();fill();
    CHECK(release(0)==THY_OK&&thy_core_queued(&core)==THY_QUEUE_CAPACITY);
    CHECK(thy_channel_expression_service(&expression)==THY_QUEUE_FULL);
    CHECK(thy_channel_expression_prepare_attack(&expression,1,4,0)==THY_QUEUE_FULL);
    CHECK(expression.sent_pressure[33]==127&&expression.sent_bend[33]==16383);
    drain();saved=expression;uint16_t queued=core.queue_count;
    CHECK(thy_channel_expression_prepare_attack(&expression,1,4,0)==THY_OK);
    CHECK(!memcmp(&saved,&expression,sizeof expression));
    int16_t invalid=128;CHECK(thy_core_press(&core,1,&invalid,1,0,1,4,96)==THY_INVALID_ARGUMENT);
    core.queue_count=queued;CHECK(!drain());
    CHECK(press(1,62,1,4,0)==THY_OK);CHECK(drain()==3);
    CHECK(events[0].status==0xd1&&events[0].data1==0);
    CHECK(events[1].status==0xe1&&events[1].data1==0&&events[1].data2==64);
    CHECK(events[2].status==0x91&&events[2].data1==62);
    CHECK(expression.sent_pressure[33]==0&&expression.sent_bend[33]==8192);
    CHECK(thy_channel_expression_service(&expression)==THY_OK&&!drain());return 1;
}
static int queued_desired_state_coalesces(void)
{
    init();CHECK(press(0,60,1,4,flags(2,1))==THY_OK);drain();fill();
    CHECK(thy_channel_expression_pressure_axes(&expression,0,20,30)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_QUEUE_FULL);
    CHECK(thy_channel_expression_pressure_axes(&expression,0,110,120)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_QUEUE_FULL);
    CHECK(expression.sent_pressure[33]==0&&expression.sent_bend[33]==8192);
    drain();CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==2);
    unsigned bend=8192+120u*8191u/127u;
    CHECK(has(2,0xd1,110,0)&&has(2,0xe1,bend&127,bend>>7));return 1;
}
static int activated_prefix_and_pending_cancellation(void)
{
    init();uint8_t f=flags(2,0),channels[2]={1,7};int16_t notes[2]={60,64};
    CHECK(thy_channel_expression_prepare_attack(&expression,1,4,f)==THY_OK);
    CHECK(thy_channel_expression_prepare_attack(&expression,7,4,f)==THY_OK);
    CHECK(thy_core_press_assigned(&core,0,notes,channels,2,4,96,10)==THY_OK);
    CHECK(thy_channel_expression_capture(&expression,0,f)==THY_OK);drain();
    CHECK(thy_channel_expression_pressure_axes(&expression,0,100,0)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==1&&has(2,0xd1,100,0));
    CHECK(thy_core_advance(&core,10)==THY_OK);CHECK(drain()==1&&has(2,0x97,64,96));
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==1&&has(2,0xd7,100,0));
    CHECK(release(0)==THY_OK);drain();CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
    CHECK(thy_channel_expression_prepare_attack(&expression,1,4,f)==THY_OK);
    CHECK(thy_channel_expression_prepare_attack(&expression,7,4,f)==THY_OK);
    CHECK(thy_core_press_assigned(&core,0,notes,channels,2,4,96,10)==THY_OK);
    CHECK(thy_channel_expression_capture(&expression,0,f)==THY_OK);drain();
    CHECK(release(0)==THY_OK);drain();
    CHECK(thy_core_advance(&core,20)==THY_OK);CHECK(thy_channel_expression_service(&expression)==THY_OK&&!drain());return 1;
}
static int bounded_fair_service_under_continuous_changes(void)
{
    init();uint16_t seen[THY_DEST_COUNT]={0u};
    for(unsigned p=0;p<16;++p)CHECK(press(p,60+p,p,7,flags(2,1))==THY_OK);
    drain();
    for(unsigned step=1;step<=8;++step) {
        for(unsigned p=0;p<16;++p)CHECK(thy_channel_expression_pressure_axes(&expression,p,(uint8_t)step,(uint8_t)(step+32))==THY_OK);
        CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
        unsigned count[THY_DEST_COUNT]={0u};
        for(unsigned e=0;e<event_count;++e){CHECK(events[e].destination<THY_DEST_COUNT);++count[events[e].destination];
            seen[events[e].destination]|=(uint16_t)(1u<<(events[e].status&15u));}
        for(unsigned d=0;d<THY_DEST_COUNT;++d)CHECK(count[d]<=4u);
    }
    for(unsigned d=0;d<THY_DEST_COUNT;++d)CHECK(seen[d]==UINT16_MAX);return 1;
}
static int rank_survives_many_retriggers(void)
{
    init();CHECK(press(0,60,0,4,flags(0,1))==THY_OK);drain();
    CHECK(thy_channel_expression_bend_pressure(&expression,0,20)==THY_OK);
    CHECK(press(1,64,0,4,flags(0,2))==THY_OK);drain();
    CHECK(thy_channel_expression_bend_pressure(&expression,1,100)==THY_OK);
    for(unsigned n=0;n<1000;++n){CHECK(press(2,67,0,4,flags(0,1))==THY_OK);drain();CHECK(release(2)==THY_OK);drain();}
    CHECK(expression.rank[1]<expression.rank[0]);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
    unsigned bend=8192-100u*8192u/127u;CHECK(has(2,0xe0,bend&127,bend>>7));
    CHECK(release(1)==THY_OK);drain();CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
    bend=8192+20u*8191u/127u;CHECK(has(2,0xe0,bend&127,bend>>7));return 1;
}
static int generated_attack_commit_and_external_panic(void)
{
    init();CHECK(press(0,60,3,4,flags(2,1))==THY_OK);drain();
    CHECK(thy_channel_expression_pressure_axes(&expression,0,100,100)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
    CHECK(release(0)==THY_OK);drain();
    CHECK(thy_channel_expression_prepare_attack(&expression,3,4,0)==THY_OK);
    thy_generated_handle handle=0;
    CHECK(thy_core_generated_acquire(&core,1,1,64,3,4,96,&handle)==THY_OK);
    thy_channel_expression_commit_prepared(&expression,3,4,0);CHECK(drain()==3);
    CHECK(events[0].status==0xd3&&events[1].status==0xe3&&events[2].status==0x93);
    CHECK(thy_channel_expression_service(&expression)==THY_OK&&!drain());
    CHECK(press(1,67,3,4,flags(2,1))==THY_OK);drain();
    CHECK(thy_channel_expression_pressure_axes(&expression,1,127,127)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
    thy_core_panic(&core);drain();
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==2&&has(2,0xd3,0,0)&&has(2,0xe3,0,64));return 1;
}
static int usb_epoch_excludes_old_contributors_but_preserves_din(void)
{
    init();CHECK(press(0,60,3,7,flags(2,1))==THY_OK);drain();
    CHECK(thy_channel_expression_pressure_axes(&expression,0,90,90)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
    uint16_t din_bend=expression.sent_bend[35];
    thy_channel_expression_usb_reset(&expression);
    CHECK(expression.excluded_destinations[0]==3u&&expression.excluded_destinations[1]==0u);
    CHECK(thy_core_sounding(&core)==3u&&thy_core_pad_held(&core,0));
    CHECK(expression.sent_pressure[3]==0&&expression.sent_pressure[19]==0);
    CHECK(expression.sent_bend[3]==8192&&expression.sent_bend[19]==8192);
    CHECK(expression.sent_pressure[35]==90&&expression.sent_bend[35]==din_bend);
    CHECK(thy_channel_expression_pressure_axes(&expression,0,110,127)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==2);
    CHECK(has(2,0xd3,110,0)&&has(2,0xe3,127,127));
    for(unsigned n=0;n<event_count;++n)CHECK(events[n].destination==THY_DIN);
    CHECK(press(1,62,3,7,flags(2,2))==THY_OK);drain();
    CHECK(thy_channel_expression_pressure_axes(&expression,1,40,127)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==5);
    CHECK(has(0,0xd3,40,0)&&has(1,0xd3,40,0)&&expression.sent_pressure[35]==110);
    CHECK(has(0,0xe3,0,0)&&has(1,0xe3,0,0)&&has(2,0xe3,0,0));
    CHECK(release(1)==THY_OK);drain();CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==5);
    CHECK(has(0,0xd3,0,0)&&has(1,0xd3,0,0)&&expression.sent_pressure[35]==110);
    CHECK(has(0,0xe3,0,64)&&has(1,0xe3,0,64)&&has(2,0xe3,127,127));
    CHECK(release(0)==THY_OK);drain();CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
    CHECK(press(0,60,3,7,flags(2,1))==THY_OK);drain();CHECK(!expression.excluded_destinations[0]);
    CHECK(thy_channel_expression_pressure_axes(&expression,0,70,0)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==3);
    CHECK(has(0,0xd3,70,0)&&has(1,0xd3,70,0)&&has(2,0xd3,70,0));return 1;
}
static int usb_epoch_marks_both_cables_and_pending_strums(void)
{
    init();int16_t notes[]={60,64};thy_shape shape={.strum_ms=10};
    CHECK(thy_core_press_shaped(&core,0,notes,2,0,1,7,96,&shape)==THY_OK);
    CHECK(thy_channel_expression_capture(&expression,0,0)==THY_OK);
    CHECK(core.owners[0].count>core.owners[0].activated);
    CHECK(press(1,62,2,1,0)==THY_OK);CHECK(press(2,65,2,2,0)==THY_OK);CHECK(press(3,67,2,4,0)==THY_OK);
    drain();thy_channel_expression_usb_reset(&expression);
    CHECK(expression.excluded_destinations[0]==3u&&expression.excluded_destinations[1]==1u);
    CHECK(expression.excluded_destinations[2]==2u&&expression.excluded_destinations[3]==0u);
    CHECK(core.pending_keys==3u&&thy_core_sounding(&core)==6u);
    CHECK(thy_core_advance(&core,10)==THY_OK);CHECK(drain()==3);
    CHECK(core.pending_keys==0u&&expression.excluded_destinations[0]==3u);
    CHECK(press(4,69,4,7,0)==THY_OK);CHECK(expression.excluded_destinations[4]==0);return 1;
}
static int transport_blocked_keys_exclude_suppressed_fresh_and_overrun_inputs(void)
{
    init();uint8_t bitmap[2u*16u*128u/8u]={0};
    CHECK(press(0,60,2,7,flags(2,0))==THY_OK);drain();
    CHECK(thy_channel_expression_pressure(&expression,0,100)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);drain();
    unsigned key=thy_core_key(THY_USB0,2,60);bitmap[key/8u]|=(uint8_t)(1u<<(key&7u));
    thy_channel_expression_usb_blocked(&expression,bitmap,sizeof bitmap-1u);
    CHECK(!expression.excluded_destinations[0]);
    thy_channel_expression_usb_blocked(&expression,bitmap,sizeof bitmap);
    CHECK(expression.excluded_destinations[0]==1u);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==1&&has(0,0xd2,0,0));
    CHECK(expression.sent_pressure[18]==100&&expression.sent_pressure[34]==100);
    CHECK(press(1,62,2,7,flags(2,0))==THY_OK);drain();
    CHECK(thy_channel_expression_pressure(&expression,1,40)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);CHECK(drain()==1&&has(0,0xd2,40,0));
    memset(bitmap,0,sizeof bitmap);
    thy_channel_expression_usb_blocked(&expression,bitmap,sizeof bitmap);
    CHECK(expression.excluded_destinations[0]==1u);
    CHECK(release(0)==THY_OK);drain();CHECK(press(0,60,2,7,flags(2,0))==THY_OK);
    CHECK(!expression.excluded_destinations[0]);return 1;
}
static int empty_transport_bitmap_preserves_all_expression_and_ownership(void)
{
    init();uint8_t bitmap[2u*16u*128u/8u]={0};
    int16_t notes[]={60,64,67};thy_shape shape={.strum_ms=10};
    CHECK(thy_core_press_shaped(&core,0,notes,3,0,2,7,96,&shape)==THY_OK);
    CHECK(thy_channel_expression_capture(&expression,0,flags(2,1))==THY_OK);
    CHECK(thy_channel_expression_pressure_axes(&expression,0,90,110)==THY_OK);
    thy_channel_expression_usb_reset(&expression); /* Exclusions survive an empty map. */
    CHECK(press(1,62,3,7,flags(2,2))==THY_OK);
    CHECK(thy_channel_expression_pressure_axes(&expression,1,45,70)==THY_OK);
    CHECK(thy_channel_expression_service(&expression)==THY_OK);
    CHECK(core.pending_keys!=0u&&expression.excluded_destinations[0]==3u&&
        expression.excluded_destinations[1]==0u);
    saved=expression;
    static thy_core before;before=core;
    for(unsigned repeat=0;repeat<8u;++repeat)
        thy_channel_expression_usb_blocked(&expression,bitmap,sizeof bitmap);
    CHECK(!memcmp(&expression,&saved,sizeof expression));
    CHECK(!memcmp(&core,&before,sizeof core));
    CHECK(thy_core_advance(&core,30u)==THY_OK);drain();
    CHECK(thy_core_sustain(&core,THY_ROUTE_USB0|THY_ROUTE_USB1|THY_ROUTE_DIN,2u,1)==THY_OK);
    CHECK(release(0)==THY_OK);
    CHECK(thy_core_deferred(&core,thy_core_key(THY_USB0,2u,60u)));
    saved=expression;before=core;
    thy_channel_expression_usb_blocked(&expression,bitmap,sizeof bitmap);
    CHECK(!memcmp(&expression,&saved,sizeof expression));
    CHECK(!memcmp(&core,&before,sizeof core));
    return 1;
}
int main(void)
{
    if(!cold_default_and_validation()||!max_pressure_and_captured_routes()||
        !newest_bend_fallback_and_independent_axes()||!sustain_spring_return_and_panic()||
        !full_queue_neutral_then_attack_and_atomic_prepare()||!queued_desired_state_coalesces()||
        !activated_prefix_and_pending_cancellation()||!bounded_fair_service_under_continuous_changes()||
        !rank_survives_many_retriggers()||!generated_attack_commit_and_external_panic()||
        !usb_epoch_excludes_old_contributors_but_preserves_din()||!usb_epoch_marks_both_cables_and_pending_strums()||
        !transport_blocked_keys_exclude_suppressed_fresh_and_overrun_inputs()||
        !empty_transport_bitmap_preserves_all_expression_and_ownership())return 1;
    printf("Channel expression: 14 scenarios passed (%u assertions; workspace %zu bytes)\n",assertions,sizeof expression);return 0;
}
