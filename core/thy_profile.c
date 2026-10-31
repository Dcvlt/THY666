/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_profile.h"

_Static_assert(THY_QUEUE_CAPACITY == 1024u,"profile queue stamp contract");
_Static_assert(THY_PROFILE_METRIC_COUNT == 19u,"profile metric wire order");
_Static_assert(sizeof(thy_profile) <= 6400u,"fixed profiling memory budget");

static void add(thy_profile *p,uint32_t *value,uint32_t amount)
{
    if(UINT32_MAX-*value<amount){*value=UINT32_MAX;p->saturated=1u;}
    else *value+=amount;
}
static int reject(thy_profile *p)
{add(p,&p->invalid_samples,1u);return 0;}

void thy_profile_init(thy_profile *p)
{
    if(!p)return;
    uint8_t *bytes=(uint8_t *)p;
    for(unsigned n=0;n<sizeof *p;++n)bytes[n]=0u;
}
int thy_profile_start(thy_profile *p,uint32_t seconds,uint32_t now,uint32_t hz)
{
    if(!p || p->active || !seconds || seconds>THY_PROFILE_MAX_SECONDS || !hz || p->id==UINT32_MAX)return 0;
    uint32_t id=p->id+1u;
    thy_profile_init(p);
    p->id=id;p->start_ms=now;p->duration_ms=seconds*1000u;p->clock_hz=hz;
    p->active=1u;p->status=THY_PROFILE_RUNNING;
    return 1;
}
int thy_profile_active(const thy_profile *p)
{return p && p->active;}
int thy_profile_tick(thy_profile *p,uint32_t now)
{
    if(!thy_profile_active(p))return 0;
    uint32_t elapsed=now-p->start_ms;
    if(elapsed>=UINT32_C(0x80000000) || elapsed<p->elapsed_ms){
        (void)reject(p);p->active=0u;p->status=THY_PROFILE_CLOCK_ERROR;return 0;
    }
    if(elapsed>=p->duration_ms){
        p->elapsed_ms=p->duration_ms;p->active=0u;p->status=THY_PROFILE_TIMED_OUT;return 0;
    }
    p->elapsed_ms=elapsed;return 1;
}
void thy_profile_stop(thy_profile *p,uint32_t now)
{
    if(thy_profile_tick(p,now)){p->active=0u;p->status=THY_PROFILE_STOPPED;}
}
int thy_profile_record(thy_profile *p,unsigned metric,uint32_t value)
{
    if(!thy_profile_active(p))return 0;
    if(metric>=THY_PROFILE_METRIC_COUNT)return reject(p);
    thy_profile_stat *stat=&p->stats[metric];
    unsigned bin=0u;
    if(value){
        bin=1u;
        for(uint32_t shifted=value>>1u;shifted && bin<THY_PROFILE_HISTOGRAM_BINS-1u;shifted>>=1u)++bin;
    }
    if(stat->saturated || stat->count==UINT32_MAX || stat->bins[bin]==UINT32_MAX ||
       UINT64_MAX-stat->sum<value){
        stat->saturated=1u;p->saturated=1u;add(p,&p->dropped_samples,1u);return 0;
    }
    if(!stat->count || value<stat->min)stat->min=value;
    if(!stat->count || value>stat->max)stat->max=value;
    ++stat->count;++stat->bins[bin];stat->sum+=value;
    return 1;
}
int thy_profile_record_cycles(thy_profile *p,unsigned metric,uint32_t cycles,uint32_t ms)
{
    if(!thy_profile_active(p))return 0;
    uint32_t hz=p->clock_hz;
    if(metric>=THY_PROFILE_METRIC_COUNT || metric==THY_PROFILE_USB_WAIT_MS ||
       metric==THY_PROFILE_DIN_WAIT_MS || !hz || ms>5000u ||
       (uint64_t)cycles>(uint64_t)hz*5u ||
       (uint64_t)hz*((uint64_t)ms+1u)>=(UINT64_C(1)<<32u)*1000u)return reject(p);
    uint64_t us;
    if(hz%1000000u==0u){
        /* Exact for whole-MHz clocks. Quotient/remainder avoids both the
         * software 64-bit divide and overflow in cycles + divisor - 1. */
        uint32_t divisor=hz/1000000u;
        us=cycles/divisor+(cycles%divisor!=0u);
    }else us=((uint64_t)cycles*1000000u+hz-1u)/hz;
    if(us>UINT32_MAX)return reject(p);
    return thy_profile_record(p,metric,(uint32_t)us);
}
void thy_profile_peak(thy_profile *p,unsigned which,uint32_t value)
{
    if(!thy_profile_active(p))return;
    if(which>=THY_PROFILE_PEAK_COUNT){(void)reject(p);return;}
    if(value>p->peaks[which])p->peaks[which]=value;
}
void thy_profile_queue_enqueue(thy_profile *p,unsigned which,unsigned slot,uint32_t now,uint32_t depth)
{
    if(!thy_profile_tick(p,now))return;
    if(which>=THY_PROFILE_QUEUE_COUNT || slot>=THY_QUEUE_CAPACITY || depth>THY_QUEUE_CAPACITY){
        (void)reject(p);return;
    }
    if(p->queue_stamps[which][slot])add(p,&p->discarded[which],1u);
    p->queue_stamps[which][slot]=(uint16_t)(p->elapsed_ms+1u);
    thy_profile_peak(p,THY_PROFILE_PEAK_USB+which,depth);
}
int thy_profile_queue_accept(thy_profile *p,unsigned which,unsigned slot,uint32_t now)
{
    if(!thy_profile_tick(p,now))return 0;
    if(which>=THY_PROFILE_QUEUE_COUNT || slot>=THY_QUEUE_CAPACITY)return reject(p);
    uint16_t stamp=p->queue_stamps[which][slot];
    p->queue_stamps[which][slot]=0u;
    if(!stamp){add(p,&p->excluded[which],1u);return 0;}
    if((uint32_t)stamp-1u>p->elapsed_ms)return reject(p);
    return thy_profile_record(p,THY_PROFILE_USB_WAIT_MS+which,p->elapsed_ms-((uint32_t)stamp-1u));
}
void thy_profile_queue_discard(thy_profile *p,unsigned which,uint32_t count)
{
    if(!thy_profile_active(p))return;
    if(which>=THY_PROFILE_QUEUE_COUNT){(void)reject(p);return;}
    for(unsigned slot=0;slot<THY_QUEUE_CAPACITY;++slot)p->queue_stamps[which][slot]=0u;
    add(p,&p->discarded[which],count);
}
uint32_t thy_profile_percentile_upper(const thy_profile_stat *stat,unsigned percent)
{
    if(!stat || !percent || percent>100u || stat->saturated)return UINT32_MAX;
    if(!stat->count)return 0u;
    uint64_t rank=((uint64_t)stat->count*percent+99u)/100u,seen=0u;
    for(unsigned bin=0;bin<THY_PROFILE_HISTOGRAM_BINS;++bin){
        seen+=stat->bins[bin];
        if(seen>=rank)return bin==THY_PROFILE_HISTOGRAM_BINS-1u?UINT32_MAX:
            bin?(UINT32_C(1)<<bin)-1u:0u;
    }
    return UINT32_MAX;
}
