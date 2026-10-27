/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_channel_expression.h"

#define USED_PRESSURE 1u
#define USED_BEND 2u
_Static_assert(sizeof(thy_channel_expression)<=768u,"Channel expression workspace must stay compact");

uint8_t thy_channel_expression_flags(uint8_t pressure,uint8_t bend)
{
    return pressure<=THY_CHANNEL_PRESSURE_CHANNEL && bend<=THY_CHANNEL_BEND_DOWN ?
        (uint8_t)(pressure|(bend<<2u)):UINT8_MAX;
}
int thy_channel_expression_flags_valid(uint8_t flags)
{
    return !(flags&0xf0u) && (flags&3u)<=THY_CHANNEL_PRESSURE_CHANNEL &&
        ((flags>>2u)&3u)<=THY_CHANNEL_BEND_DOWN;
}
void thy_channel_expression_init(thy_channel_expression *m,thy_core *core)
{
    if(!m)return;
    uint8_t *bytes=(uint8_t *)m;
    for(size_t n=0;n<sizeof(*m);++n)bytes[n]=0u;
    m->core=core;
    for(unsigned n=0;n<THY_PAD_COUNT;++n)m->rank[n]=UINT8_MAX;
    for(unsigned n=0;n<THY_CHANNEL_EXPRESSION_ROUTES;++n)m->sent_bend[n]=8192u;
}
static int valid(const thy_channel_expression *m)
{return m && m->core;}
static int route_valid(uint8_t channel,uint8_t destinations)
{return channel<16u && destinations && !(destinations&~THY_ROUTE_ALL);}
static uint8_t use_flags(uint8_t flags)
{
    return (uint8_t)(((flags&3u)==THY_CHANNEL_PRESSURE_CHANNEL?USED_PRESSURE:0u)|
        ((flags>>2u)?USED_BEND:0u));
}
static int contributes(const thy_pad_owner *owner,unsigned route)
{
    if(!owner->held || !owner->activated)return 0;
    /* An ordinary pad has one channel across destinations. This quick test
     * avoids scanning large chords on unrelated channels; mixed-key owners
     * remain supported by the exact scan below. */
    for(unsigned n=0;n<owner->activated;++n)
        if(thy_pad_owner_key(owner, (uint8_t)(n))/128u==route)return 1;
    return 0;
}
static void desired(const thy_channel_expression *m,unsigned route,
    uint8_t *pressure,uint16_t *bend)
{
    uint8_t latest=UINT8_MAX;
    *pressure=0u;*bend=8192u;
    for(unsigned p=0;p<THY_PAD_COUNT;++p) {
        uint8_t flags=m->flags[p];
        if(m->rank[p]==UINT8_MAX || !flags ||
            (m->excluded_destinations[p]&(1u<<(route/16u))) ||
            !contributes(&m->core->owners[p],route))continue;
        if((flags&3u)==THY_CHANNEL_PRESSURE_CHANNEL && m->pressure[p]>*pressure)
            *pressure=m->pressure[p];
        uint8_t direction=(uint8_t)(flags>>2u);
        if(direction && m->rank[p]<latest) {
            latest=m->rank[p];
            if(direction==THY_CHANNEL_BEND_UP)
                *bend=(uint16_t)(8192u+((uint32_t)m->bend_pressure[p]*8191u)/127u);
            else *bend=(uint16_t)(8192u-((uint32_t)m->bend_pressure[p]*8192u)/127u);
        }
    }
}
static size_t events_for(const thy_channel_expression *m,unsigned route,uint8_t used,
    uint8_t pressure,uint16_t bend,thy_midi_event *events)
{
    size_t count=0u;uint8_t destination=(uint8_t)(route/16u),channel=(uint8_t)(route%16u);
    if((used&USED_PRESSURE) && (!(m->used[route]&USED_PRESSURE) || pressure!=m->sent_pressure[route]))
        events[count++]=(thy_midi_event){destination,(uint8_t)(0xd0u|channel),pressure,0u};
    if((used&USED_BEND) && (!(m->used[route]&USED_BEND) || bend!=m->sent_bend[route]))
        events[count++]=(thy_midi_event){destination,(uint8_t)(0xe0u|channel),
            (uint8_t)(bend&127u),(uint8_t)(bend>>7u)};
    return count;
}
int thy_channel_expression_prepare_attack(thy_channel_expression *m,uint8_t channel,
    uint8_t destinations,uint8_t flags)
{
    if(!valid(m) || !route_valid(channel,destinations) || !thy_channel_expression_flags_valid(flags))
        return THY_INVALID_ARGUMENT;
    thy_midi_event events[THY_DEST_COUNT*2u];size_t count=0u;
    for(unsigned destination=0;destination<THY_DEST_COUNT;++destination) {
        if(!(destinations&(1u<<destination)))continue;
        unsigned route=destination*16u+channel;
        uint8_t pressure,used=(uint8_t)(m->used[route]|use_flags(flags));uint16_t bend;
        desired(m,route,&pressure,&bend);
        if(flags>>2u)bend=8192u;
        count+=events_for(m,route,used,pressure,bend,events+count);
    }
    return thy_core_emit_batch(m->core,events,count);
}
void thy_channel_expression_commit_prepared(thy_channel_expression *m,uint8_t channel,
    uint8_t destinations,uint8_t flags)
{
    if(!valid(m) || !route_valid(channel,destinations) || !thy_channel_expression_flags_valid(flags))return;
    for(unsigned destination=0;destination<THY_DEST_COUNT;++destination) {
        if(!(destinations&(1u<<destination)))continue;
        unsigned route=destination*16u+channel;uint8_t pressure;uint16_t bend;
        desired(m,route,&pressure,&bend);
        if(flags>>2u)bend=8192u;
        m->used[route]|=use_flags(flags);
        m->sent_pressure[route]=pressure;m->sent_bend[route]=bend;
    }
}
int thy_channel_expression_capture(thy_channel_expression *m,uint8_t pad,uint8_t flags)
{
    if(!valid(m) || pad>=THY_PAD_COUNT || !thy_channel_expression_flags_valid(flags) ||
        !m->core->owners[pad].held)return THY_INVALID_ARGUMENT;
    for(unsigned p=0;p<THY_PAD_COUNT;++p)
        if(!m->core->owners[p].held)thy_channel_expression_release(m,(uint8_t)p);
    thy_channel_expression_release(m,pad);
    for(unsigned p=0;p<THY_PAD_COUNT;++p)
        if(m->rank[p]!=UINT8_MAX)++m->rank[p];
    m->flags[pad]=flags;m->pressure[pad]=m->bend_pressure[pad]=0u;m->rank[pad]=0u;
    m->excluded_destinations[pad]=0u;
    const thy_pad_owner *owner=&m->core->owners[pad];
    /* Commit only routes belonging to this immutable owner. Full keys include
     * pending strum destinations which were initialized by prepare_attack. */
    uint64_t seen=0u;
    for(unsigned n=0;n<owner->count;++n) {
        unsigned route=thy_pad_owner_key(owner, (uint8_t)(n))/128u;
        if(seen&(UINT64_C(1)<<route))continue;
        seen|=UINT64_C(1)<<route;
        thy_channel_expression_commit_prepared(m,(uint8_t)(route%16u),
            (uint8_t)(1u<<(route/16u)),flags);
    }
    return THY_OK;
}
int thy_channel_expression_pressure(thy_channel_expression *m,uint8_t pad,uint8_t value)
{
    if(!valid(m) || pad>=THY_PAD_COUNT || value>127u)return THY_INVALID_ARGUMENT;
    if(m->rank[pad]!=UINT8_MAX && m->core->owners[pad].held)m->pressure[pad]=value;
    return THY_OK;
}
int thy_channel_expression_bend_pressure(thy_channel_expression *m,uint8_t pad,uint8_t value)
{
    if(!valid(m) || pad>=THY_PAD_COUNT || value>127u)return THY_INVALID_ARGUMENT;
    if(m->rank[pad]!=UINT8_MAX && m->core->owners[pad].held)m->bend_pressure[pad]=value;
    return THY_OK;
}
int thy_channel_expression_pressure_axes(thy_channel_expression *m,uint8_t pad,
    uint8_t aftertouch,uint8_t primary)
{
    if(!valid(m) || pad>=THY_PAD_COUNT || aftertouch>127u || primary>127u)
        return THY_INVALID_ARGUMENT;
    if(m->rank[pad]!=UINT8_MAX && m->core->owners[pad].held) {
        m->pressure[pad]=aftertouch;m->bend_pressure[pad]=primary;
    }
    return THY_OK;
}
void thy_channel_expression_release(thy_channel_expression *m,uint8_t pad)
{
    if(!m || pad>=THY_PAD_COUNT)return;
    uint8_t rank=m->rank[pad];
    m->flags[pad]=m->pressure[pad]=m->bend_pressure[pad]=0u;m->rank[pad]=UINT8_MAX;
    /* Compact the bounded held order. Saturating old ranks would lose the
     * order of two long-held pads after many taps on a third pad. */
    if(rank!=UINT8_MAX)for(unsigned p=0;p<THY_PAD_COUNT;++p)
        if(m->rank[p]!=UINT8_MAX && m->rank[p]>rank)--m->rank[p];
}
void thy_channel_expression_release_all(thy_channel_expression *m)
{
    if(!m)return;
    for(unsigned p=0;p<THY_PAD_COUNT;++p)thy_channel_expression_release(m,(uint8_t)p);
}
void thy_channel_expression_usb_reset(thy_channel_expression *m)
{
    if(!valid(m))return;
    for(unsigned p=0;p<THY_PAD_COUNT;++p) {
        const thy_pad_owner *owner=&m->core->owners[p];
        if(!owner->held)continue;
        for(unsigned k=0;k<owner->count;++k) {
            unsigned destination=thy_pad_owner_key(owner, (uint8_t)(k))/(16u*128u);
            if(destination<THY_DIN)m->excluded_destinations[p]|=(uint8_t)(1u<<destination);
        }
    }
    for(unsigned route=0;route<THY_DIN*16u;++route) {
        m->sent_pressure[route]=0u;m->sent_bend[route]=8192u;
    }
}
void thy_channel_expression_usb_blocked(thy_channel_expression *m,const uint8_t *bitmap,
    size_t bytes)
{
    if(!valid(m) || !bitmap || bytes!=THY_DIN*16u*128u/8u)return;
    for(unsigned p=0;p<THY_PAD_COUNT;++p) {
        const thy_pad_owner *owner=&m->core->owners[p];
        if(!owner->held || (m->excluded_destinations[p]&3u)==3u)continue;
        for(unsigned k=0;k<owner->count;++k) {
            unsigned key=thy_pad_owner_key(owner, (uint8_t)(k)),destination=key/(16u*128u);
            if(destination>=THY_DIN || (m->excluded_destinations[p]&(1u<<destination)))continue;
            if(bitmap[key/8u]&(1u<<(key&7u)))
                m->excluded_destinations[p]|=(uint8_t)(1u<<destination);
        }
    }
}
int thy_channel_expression_service(thy_channel_expression *m)
{
    if(!valid(m))return THY_INVALID_ARGUMENT;
    for(unsigned p=0;p<THY_PAD_COUNT;++p)
        if(!m->core->owners[p].held)thy_channel_expression_release(m,(uint8_t)p);
    for(unsigned destination=0;destination<THY_DEST_COUNT;++destination) {
        uint8_t sent=0u,next=m->cursor[destination];int emitted=0;
        for(unsigned scanned=0;scanned<16u;++scanned) {
            unsigned route=destination*16u+m->cursor[destination];
            m->cursor[destination]=(uint8_t)((m->cursor[destination]+1u)%16u);
            if(!m->used[route] || sent>=4u)continue;
            uint8_t pressure;uint16_t bend;thy_midi_event events[2];
            desired(m,route,&pressure,&bend);
            size_t count=events_for(m,route,m->used[route],pressure,bend,events);
            if(!count || sent+count>4u)continue;
            int result=thy_core_emit_batch(m->core,events,count);
            if(result!=THY_OK)return result;
            m->sent_pressure[route]=pressure;m->sent_bend[route]=bend;
            sent=(uint8_t)(sent+count);
            next=m->cursor[destination];emitted=1;
        }
        if(emitted)m->cursor[destination]=next;
    }
    return THY_OK;
}
