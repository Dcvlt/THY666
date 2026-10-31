/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_profile.h"

static thy_profile profile,before;
static unsigned assertions;
#define CHECK(x) do{++assertions;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static void start_at(uint32_t now)
{thy_profile_init(&profile);(void)thy_profile_start(&profile,60u,now,168000000u);}

static int zero_and_inactive_are_inert(void)
{
    memset(&profile,0xa5,sizeof profile);thy_profile_init(&profile);
    const uint8_t *bytes=(const uint8_t *)&profile;
    for(unsigned n=0;n<sizeof profile;++n)CHECK(bytes[n]==0u);
    CHECK(profile.status==THY_PROFILE_IDLE && !thy_profile_active(&profile));
    before=profile;
    CHECK(!thy_profile_record(&profile,THY_PROFILE_LOOP_US,3u));
    CHECK(!thy_profile_record_cycles(&profile,999u,UINT32_MAX,UINT32_MAX));
    thy_profile_peak(&profile,999u,UINT32_MAX);
    thy_profile_queue_enqueue(&profile,999u,9999u,UINT32_MAX,UINT32_MAX);
    CHECK(!thy_profile_queue_accept(&profile,999u,9999u,UINT32_MAX));
    thy_profile_queue_discard(&profile,999u,UINT32_MAX);
    CHECK(!thy_profile_tick(&profile,UINT32_MAX));thy_profile_stop(&profile,UINT32_MAX);
    CHECK(!memcmp(&profile,&before,sizeof profile));
    CHECK(!thy_profile_start(&profile,0u,0u,1u));
    CHECK(!thy_profile_start(&profile,61u,0u,1u));
    CHECK(!thy_profile_start(&profile,1u,0u,0u));
    CHECK(!memcmp(&profile,&before,sizeof profile));
    thy_profile_init(NULL);thy_profile_stop(NULL,0u);thy_profile_peak(NULL,0u,0u);
    thy_profile_queue_enqueue(NULL,0u,0u,0u,0u);thy_profile_queue_discard(NULL,0u,0u);
    CHECK(!thy_profile_start(NULL,1u,0u,1u) && !thy_profile_active(NULL));
    CHECK(!thy_profile_tick(NULL,0u) && !thy_profile_record(NULL,0u,0u));
    CHECK(!thy_profile_record_cycles(NULL,0u,0u,0u) && !thy_profile_queue_accept(NULL,0u,0u,0u));
    CHECK(thy_profile_percentile_upper(NULL,50u)==UINT32_MAX);
    CHECK(thy_profile_percentile_upper(&profile.stats[0],50u)==0u);
    return 1;
}
static int lifecycle_deadline_and_session_identity(void)
{
    start_at(UINT32_MAX-100u);uint32_t start=profile.start_ms;
    CHECK(profile.id==1u && profile.duration_ms==60000u && profile.clock_hz==168000000u);
    CHECK(thy_profile_active(&profile) && profile.status==THY_PROFILE_RUNNING);
    before=profile;CHECK(!thy_profile_start(&profile,1u,0u,1000u));
    CHECK(!memcmp(&profile,&before,sizeof profile));
    CHECK(thy_profile_tick(&profile,start+200u) && profile.elapsed_ms==200u);
    thy_profile_queue_enqueue(&profile,THY_PROFILE_QUEUE_USB,1023u,start+59999u,1024u);
    CHECK(profile.queue_stamps[0][1023]==60000u && profile.peaks[THY_PROFILE_PEAK_USB]==1024u);
    CHECK(!thy_profile_queue_accept(&profile,0u,1023u,start+60000u));
    CHECK(!profile.active && profile.status==THY_PROFILE_TIMED_OUT && profile.elapsed_ms==60000u);
    CHECK(profile.queue_stamps[0][1023]==60000u && !profile.stats[THY_PROFILE_USB_WAIT_MS].count);
    before=profile;thy_profile_stop(&profile,0u);thy_profile_tick(&profile,UINT32_MAX);
    thy_profile_queue_discard(&profile,0u,10u);thy_profile_record(&profile,0u,10u);
    CHECK(!memcmp(&profile,&before,sizeof profile));
    CHECK(thy_profile_start(&profile,1u,1000u,1000000u));
    CHECK(profile.id==2u && !profile.queue_stamps[0][1023] && !profile.peaks[1]);
    CHECK(thy_profile_record(&profile,0u,17u));thy_profile_stop(&profile,1050u);
    CHECK(profile.status==THY_PROFILE_STOPPED && profile.elapsed_ms==50u && profile.stats[0].count==1u);
    before=profile;thy_profile_stop(&profile,2000u);CHECK(!memcmp(&profile,&before,sizeof profile));
    profile.id=UINT32_MAX;before=profile;
    CHECK(!thy_profile_start(&profile,1u,2000u,1u) && !memcmp(&profile,&before,sizeof profile));
    profile.id=UINT32_MAX-1u;CHECK(thy_profile_start(&profile,1u,2000u,1u));
    CHECK(profile.id==UINT32_MAX);thy_profile_stop(&profile,2000u);before=profile;
    CHECK(!thy_profile_start(&profile,1u,2000u,1u) && !memcmp(&profile,&before,sizeof profile));
    return 1;
}
static int backward_and_ambiguous_clocks_stop_once(void)
{
    start_at(UINT32_MAX-50u);
    CHECK(thy_profile_tick(&profile,25u) && profile.elapsed_ms==76u);
    CHECK(!thy_profile_tick(&profile,20u));
    CHECK(profile.status==THY_PROFILE_CLOCK_ERROR && profile.invalid_samples==1u && profile.elapsed_ms==76u);
    before=profile;thy_profile_tick(&profile,26u);CHECK(!memcmp(&profile,&before,sizeof profile));
    start_at(0u);CHECK(!thy_profile_tick(&profile,UINT32_C(0x80000000)));
    CHECK(profile.status==THY_PROFILE_CLOCK_ERROR && !profile.elapsed_ms);
    start_at(100u);CHECK(!thy_profile_tick(&profile,99u) && profile.status==THY_PROFILE_CLOCK_ERROR);
    start_at(0u);CHECK(!thy_profile_tick(&profile,UINT32_C(0x7fffffff)));
    CHECK(profile.status==THY_PROFILE_TIMED_OUT && profile.elapsed_ms==60000u);
    start_at(0u);CHECK(thy_profile_tick(&profile,0u) && thy_profile_tick(&profile,0u));
    CHECK(thy_profile_tick(&profile,59999u));
    CHECK(!thy_profile_tick(&profile,60000u) && profile.status==THY_PROFILE_TIMED_OUT);
    return 1;
}
static int histogram_boundaries_and_percentile_upper_bounds(void)
{
    start_at(0u);uint64_t sum=1u;
    CHECK(thy_profile_record(&profile,0u,0u) && thy_profile_record(&profile,0u,1u));
    for(unsigned bin=2u;bin<19u;++bin){
        uint32_t lo=UINT32_C(1)<<(bin-1u),hi=(UINT32_C(1)<<bin)-1u;
        CHECK(thy_profile_record(&profile,0u,lo) && thy_profile_record(&profile,0u,hi));
        sum+=(uint64_t)lo+hi;
        CHECK(profile.stats[0].bins[bin]==2u);
    }
    CHECK(thy_profile_record(&profile,0u,262144u) && thy_profile_record(&profile,0u,UINT32_MAX));
    sum+=UINT64_C(262144)+UINT32_MAX;
    CHECK(profile.stats[0].sum==sum && profile.stats[0].count==38u);
    CHECK(profile.stats[0].min==0u && profile.stats[0].max==UINT32_MAX);
    CHECK(profile.stats[0].bins[0]==1u && profile.stats[0].bins[1]==1u && profile.stats[0].bins[19]==2u);
    for(unsigned n=1u;n<THY_PROFILE_METRIC_COUNT;++n)CHECK(!profile.stats[n].count);
    start_at(0u);const uint32_t samples[]={0u,1u,2u,3u,4u,7u,8u,262144u};
    for(unsigned n=0;n<sizeof samples/sizeof samples[0];++n)CHECK(thy_profile_record(&profile,0u,samples[n]));
    CHECK(thy_profile_percentile_upper(&profile.stats[0],1u)==0u);
    CHECK(thy_profile_percentile_upper(&profile.stats[0],25u)==1u);
    CHECK(thy_profile_percentile_upper(&profile.stats[0],50u)==3u);
    CHECK(thy_profile_percentile_upper(&profile.stats[0],75u)==7u);
    CHECK(thy_profile_percentile_upper(&profile.stats[0],87u)==15u);
    CHECK(thy_profile_percentile_upper(&profile.stats[0],88u)==UINT32_MAX);
    CHECK(thy_profile_percentile_upper(&profile.stats[0],99u)==UINT32_MAX);
    CHECK(thy_profile_percentile_upper(&profile.stats[0],100u)==UINT32_MAX);
    CHECK(thy_profile_percentile_upper(&profile.stats[0],0u)==UINT32_MAX);
    CHECK(thy_profile_percentile_upper(&profile.stats[0],101u)==UINT32_MAX);
    thy_profile_stat corrupt={.count=UINT32_MAX};
    CHECK(thy_profile_percentile_upper(&corrupt,100u)==UINT32_MAX);
    return 1;
}
static int saturated_accounting_never_wraps(void)
{
    start_at(0u);profile.stats[0].count=UINT32_MAX;
    CHECK(!thy_profile_record(&profile,0u,5u));
    CHECK(profile.stats[0].count==UINT32_MAX && !profile.stats[0].sum && profile.stats[0].saturated);
    CHECK(profile.dropped_samples==1u && profile.saturated);
    CHECK(!thy_profile_record(&profile,0u,0u) && profile.dropped_samples==2u);
    CHECK(thy_profile_percentile_upper(&profile.stats[0],50u)==UINT32_MAX);
    start_at(0u);profile.stats[0].bins[2]=UINT32_MAX;
    CHECK(!thy_profile_record(&profile,0u,3u) && !profile.stats[0].count);
    CHECK(profile.stats[0].bins[2]==UINT32_MAX && profile.stats[0].saturated);
    start_at(0u);profile.stats[0].sum=UINT64_MAX-1u;
    CHECK(!thy_profile_record(&profile,0u,3u) && profile.stats[0].sum==UINT64_MAX-1u);
    CHECK(profile.stats[0].saturated && !profile.stats[0].count);
    profile.dropped_samples=UINT32_MAX;CHECK(!thy_profile_record(&profile,0u,4u));
    CHECK(profile.dropped_samples==UINT32_MAX);
    profile.invalid_samples=UINT32_MAX;CHECK(!thy_profile_record(&profile,THY_PROFILE_METRIC_COUNT,0u));
    CHECK(profile.invalid_samples==UINT32_MAX);
    profile.excluded[0]=UINT32_MAX;CHECK(!thy_profile_queue_accept(&profile,0u,0u,0u));
    CHECK(profile.excluded[0]==UINT32_MAX);
    profile.discarded[0]=UINT32_MAX-2u;thy_profile_queue_discard(&profile,0u,10u);
    CHECK(profile.discarded[0]==UINT32_MAX && profile.saturated);
    return 1;
}
static int cycles_units_limits_and_peaks(void)
{
    start_at(0u);
    const uint32_t cycles[]={0u,1u,168u,169u,168000u,840000000u};
    const uint32_t expected[]={0u,1u,1u,2u,1000u,5000000u};
    for(unsigned n=0;n<sizeof cycles/sizeof cycles[0];++n){
        CHECK(thy_profile_record_cycles(&profile,n,cycles[n],n==5u?5000u:0u));
        CHECK(profile.stats[n].count==1u && profile.stats[n].sum==expected[n]);
    }
    CHECK(thy_profile_record_cycles(&profile,THY_PROFILE_SETTINGS_PROGRAM_US,168u,0u));
    CHECK(thy_profile_record_cycles(&profile,THY_PROFILE_SETTINGS_ERASE_US,336u,0u));
    CHECK(!thy_profile_record_cycles(&profile,0u,168u,5001u));
    CHECK(!thy_profile_record_cycles(&profile,0u,840000001u,0u));
    CHECK(!thy_profile_record_cycles(&profile,THY_PROFILE_USB_WAIT_MS,168u,0u));
    CHECK(!thy_profile_record_cycles(&profile,THY_PROFILE_DIN_WAIT_MS,168u,0u));
    CHECK(!thy_profile_record_cycles(&profile,THY_PROFILE_METRIC_COUNT,168u,0u));
    CHECK(profile.invalid_samples==5u);
    profile.clock_hz=0u;CHECK(!thy_profile_record_cycles(&profile,0u,0u,0u));
    profile.clock_hz=UINT32_MAX;CHECK(!thy_profile_record_cycles(&profile,0u,0u,1000u));
    CHECK(thy_profile_record_cycles(&profile,0u,0u,999u));
    profile.clock_hz=1u;CHECK(thy_profile_record_cycles(&profile,0u,5u,5000u));
    CHECK(!thy_profile_record_cycles(&profile,0u,6u,0u));
    for(unsigned which=0;which<THY_PROFILE_PEAK_COUNT;++which){
        thy_profile_peak(&profile,which,1u);thy_profile_peak(&profile,which,0u);
        CHECK(profile.peaks[which]==1u);
        thy_profile_peak(&profile,which,UINT32_MAX);CHECK(profile.peaks[which]==UINT32_MAX);
    }
    uint32_t rejected=profile.invalid_samples;thy_profile_peak(&profile,THY_PROFILE_PEAK_COUNT,3u);
    CHECK(profile.invalid_samples==rejected+1u);
    return 1;
}
static int queue_lifetimes_reuse_discard_and_unmeasured_entries(void)
{
    start_at(UINT32_MAX-200u);uint32_t start=profile.start_ms;
    thy_profile_queue_enqueue(&profile,0u,1023u,start,1u);
    CHECK(profile.queue_stamps[0][1023]==1u);
    CHECK(thy_profile_queue_accept(&profile,0u,1023u,start+100u));
    CHECK(!profile.queue_stamps[0][1023] && profile.stats[THY_PROFILE_USB_WAIT_MS].sum==100u);
    CHECK(!thy_profile_queue_accept(&profile,1u,0u,start+100u));
    CHECK(profile.excluded[1]==1u && !profile.stats[THY_PROFILE_DIN_WAIT_MS].count);
    thy_profile_queue_enqueue(&profile,0u,1023u,start+100u,2u);
    thy_profile_queue_enqueue(&profile,0u,1023u,start+110u,3u);
    CHECK(profile.discarded[0]==1u && profile.queue_stamps[0][1023]==111u);
    CHECK(thy_profile_queue_accept(&profile,0u,1023u,start+150u));
    CHECK(profile.stats[THY_PROFILE_USB_WAIT_MS].count==2u && profile.stats[THY_PROFILE_USB_WAIT_MS].sum==140u);
    thy_profile_queue_enqueue(&profile,0u,0u,start+160u,4u);
    thy_profile_queue_enqueue(&profile,1u,0u,start+160u,2u);
    thy_profile_queue_discard(&profile,0u,7u);
    CHECK(profile.discarded[0]==8u && !profile.queue_stamps[0][0] && profile.queue_stamps[1][0]==161u);
    CHECK(!thy_profile_queue_accept(&profile,0u,0u,start+170u) && profile.excluded[0]==1u);
    CHECK(thy_profile_queue_accept(&profile,1u,0u,start+180u));
    CHECK(profile.stats[THY_PROFILE_DIN_WAIT_MS].sum==20u);
    thy_profile_queue_enqueue(&profile,1u,1023u,start+180u,7u);
    CHECK(thy_profile_tick(&profile,start+200u) && thy_profile_tick(&profile,start+250u));
    CHECK(profile.queue_stamps[1][1023]==181u); /* Busy sink never called accept. */
    CHECK(thy_profile_queue_accept(&profile,1u,1023u,start+300u));
    CHECK(profile.stats[THY_PROFILE_DIN_WAIT_MS].sum==140u && profile.peaks[THY_PROFILE_PEAK_DIN]==7u);
    for(unsigned slot=0;slot<THY_QUEUE_CAPACITY;++slot)
        thy_profile_queue_enqueue(&profile,0u,slot,start+300u,slot+1u);
    for(unsigned slot=0;slot<THY_QUEUE_CAPACITY;++slot)
        CHECK(thy_profile_queue_accept(&profile,0u,slot,start+301u));
    CHECK(profile.peaks[THY_PROFILE_PEAK_USB]==THY_QUEUE_CAPACITY);
    CHECK(profile.stats[THY_PROFILE_USB_WAIT_MS].count==THY_QUEUE_CAPACITY+2u);
    thy_profile_queue_enqueue(&profile,0u,0u,start+60000u,1u);
    CHECK(!profile.active && !profile.queue_stamps[0][0]);
    CHECK(thy_profile_start(&profile,1u,1000u,168000000u));
    CHECK(!thy_profile_queue_accept(&profile,0u,0u,1001u));
    CHECK(profile.excluded[0]==1u && !profile.stats[THY_PROFILE_USB_WAIT_MS].count);
    return 1;
}
static int cycle_conversion_matches_full_width_reference(void)
{
    const uint32_t frequencies[]={1u,1000u,999999u,1000000u,2000000u,168000000u,
        167999999u,169000001u,199999999u,200000000u,2147000000u,4000000000u,4294000000u,UINT32_MAX};
    const uint32_t milliseconds[]={0u,1u,9u,999u,1000u,4999u,5000u,5001u,UINT32_MAX};
    const uint32_t boundaries[]={0u,1u,167u,168u,169u,999999u,1000000u,
        167999999u,168000000u,168000001u,UINT32_MAX-1u,UINT32_MAX};
    uint32_t random=UINT32_C(0x19a6f83d);
    for(unsigned f=0;f<sizeof frequencies/sizeof frequencies[0];++f){
        uint32_t hz=frequencies[f];thy_profile_init(&profile);
        CHECK(thy_profile_start(&profile,60u,0u,hz));
        for(unsigned sample=0;sample<1036u;++sample){
            random=random*UINT32_C(1664525)+UINT32_C(1013904223);
            uint32_t cycles=sample<12u?boundaries[sample]:random;
            for(unsigned t=0;t<sizeof milliseconds/sizeof milliseconds[0];++t){
                uint32_t ms=milliseconds[t];uint32_t count=profile.stats[0].count;
                uint32_t invalid=profile.invalid_samples;uint64_t sum=profile.stats[0].sum;
                int admissible=ms<=5000u && (uint64_t)cycles<=(uint64_t)hz*5u &&
                    (uint64_t)hz*((uint64_t)ms+1u)<(UINT64_C(1)<<32u)*1000u;
                int result=thy_profile_record_cycles(&profile,THY_PROFILE_LOOP_US,cycles,ms);
                CHECK(result==admissible);
                if(admissible){
                    uint64_t reference=((uint64_t)cycles*1000000u+hz-1u)/hz;
                    CHECK(profile.stats[0].count==count+1u && profile.stats[0].sum==sum+reference);
                    CHECK(profile.invalid_samples==invalid);
                }else{
                    CHECK(profile.stats[0].count==count && profile.stats[0].sum==sum);
                    CHECK(profile.invalid_samples==invalid+1u);
                }
            }
        }
    }
    return 1;
}
static int invalid_queue_arguments_and_future_stamps_are_bounded(void)
{
    static struct{uint64_t head;thy_profile p;uint64_t tail;} guarded;
    guarded.head=UINT64_C(0xabcdef0123456789);guarded.tail=UINT64_C(0x0123456789abcdef);
    thy_profile_init(&guarded.p);CHECK(thy_profile_start(&guarded.p,60u,0u,168000000u));
    thy_profile_queue_enqueue(&guarded.p,2u,0u,1u,1u);
    thy_profile_queue_enqueue(&guarded.p,0u,THY_QUEUE_CAPACITY,1u,1u);
    thy_profile_queue_enqueue(&guarded.p,0u,0u,1u,THY_QUEUE_CAPACITY+1u);
    CHECK(!thy_profile_queue_accept(&guarded.p,2u,0u,1u));
    CHECK(!thy_profile_queue_accept(&guarded.p,0u,THY_QUEUE_CAPACITY,1u));
    thy_profile_queue_discard(&guarded.p,2u,UINT32_MAX);
    CHECK(guarded.p.invalid_samples==6u);
    guarded.p.queue_stamps[0][0]=65000u;
    CHECK(!thy_profile_queue_accept(&guarded.p,0u,0u,2u));
    CHECK(guarded.p.invalid_samples==7u && !guarded.p.queue_stamps[0][0]);
    CHECK(!guarded.p.stats[THY_PROFILE_USB_WAIT_MS].count && !guarded.p.excluded[0]);
    CHECK(guarded.head==UINT64_C(0xabcdef0123456789) && guarded.tail==UINT64_C(0x0123456789abcdef));
    return 1;
}
int main(void)
{
    int(*tests[])(void)={zero_and_inactive_are_inert,lifecycle_deadline_and_session_identity,
        backward_and_ambiguous_clocks_stop_once,histogram_boundaries_and_percentile_upper_bounds,
        saturated_accounting_never_wraps,cycles_units_limits_and_peaks,
        queue_lifetimes_reuse_discard_and_unmeasured_entries,invalid_queue_arguments_and_future_stamps_are_bounded,
        cycle_conversion_matches_full_width_reference};
    for(unsigned n=0;n<sizeof tests/sizeof tests[0];++n)if(!tests[n]())return 1;
    printf("Profiling: %u scenarios, %u assertions; state=%zu bytes, statistic=%zu bytes\n",
        (unsigned)(sizeof tests/sizeof tests[0]),assertions,sizeof profile,sizeof(thy_profile_stat));
    return 0;
}
