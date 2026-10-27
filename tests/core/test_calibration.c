/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_calibration.h"

static thy_calibration w,frozen;
static unsigned assertions;
#define CHECK(x) do{++assertions;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static int tap(uint8_t pad,uint16_t baseline,uint16_t peak,uint16_t rate,uint16_t quiet)
{
    int result=thy_calibration_contact_begin(&w,pad,baseline,quiet);
    if(result!=THY_CALIBRATION_OK)return result;
    (void)thy_calibration_observe(&w,pad,(uint16_t)(baseline+(peak-baseline)/2u),(uint16_t)(rate/2u));
    (void)thy_calibration_observe(&w,pad,peak,rate);
    (void)thy_calibration_observe(&w,pad,(uint16_t)(peak-1u),(uint16_t)(rate/2u));
    return thy_calibration_contact_end(&w,pad);
}
static int strike_levels(uint8_t pad)
{
    /* Pressure peaks intentionally run backwards: strike speed is a separate
     * gesture from the later continuous pressure calibration. */
    const uint16_t peaks[3]={2100u,1650u,1400u},rates[3]={8u,32u,96u};
    for(unsigned stage=0;stage<3u;++stage)for(unsigned hit=0;hit<3u;++hit)
        CHECK(tap(pad,1300u,(uint16_t)(peaks[stage]+hit), (uint16_t)(rates[stage]+hit),2u)==THY_CALIBRATION_OK);
    CHECK(w.stage==THY_CALIBRATION_PRESSURE && !w.hits && !thy_calibration_completed(&w,pad));
    return 1;
}
static void pressure_samples(uint8_t pad,uint16_t soft,uint16_t firm,uint32_t started,unsigned duration,int spike)
{
    for(unsigned elapsed=0;elapsed<=duration;elapsed+=30u){
        unsigned raw=elapsed<1000u?soft:elapsed<1450u?
            soft+(firm-soft)*(elapsed-1000u)/450u:firm;
        if(spike && elapsed==1530u)raw=4095u;
        (void)thy_calibration_observe_timed(&w,pad,(uint16_t)raw,0u,started+elapsed);
    }
    (void)thy_calibration_observe_timed(&w,pad,1300u,0u,started+duration+30u);
}
static int pressure_hold(uint8_t pad,uint16_t soft,uint16_t firm,uint32_t started,unsigned duration,int spike)
{
    int result=thy_calibration_contact_begin(&w,pad,1300u,2u);
    if(result!=THY_CALIBRATION_OK)return result;
    pressure_samples(pad,soft,firm,started,duration,spike);
    return thy_calibration_contact_end(&w,pad);
}
static int finish_pad(uint8_t pad)
{
    CHECK(strike_levels(pad));
    CHECK(pressure_hold(pad,1400u,2100u,w.hold_last_ms+30u,1800u,0)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_completed(&w,pad));return 1;
}
static int test_workspace_and_validation(void)
{
    CHECK(sizeof(thy_calibration)<=2304u && sizeof(thy_calibration_candidate)==20u);
    thy_calibration_init(&w);CHECK(w.state==THY_CALIBRATION_OFF && !w.completed_count);
    CHECK(thy_calibration_begin(NULL)==THY_CALIBRATION_INVALID);
    CHECK(thy_calibration_begin(&w)==THY_CALIBRATION_OK);
    CHECK(w.preview_percent==80u && !w.review_ready && !w.review_outlier_count);
    CHECK(w.state==THY_CALIBRATION_COLLECT && !w.target && !w.hits);
    CHECK(thy_calibration_preview(&w,1)==THY_CALIBRATION_NOT_READY);
    CHECK(thy_calibration_request_apply(&w)==THY_CALIBRATION_NOT_READY);
    CHECK(thy_calibration_contact_begin(&w,96u,1300u,0u)==THY_CALIBRATION_INVALID);
    CHECK(thy_calibration_contact_begin(&w,1u,1300u,0u)==THY_CALIBRATION_RETRY);
    CHECK(w.reason==THY_CALIBRATION_WRONG_PAD && !w.hits && !w.contact_active);
    CHECK(thy_calibration_contact_begin(&w,0u,1300u,33u)==THY_CALIBRATION_RETRY);
    CHECK(w.reason==THY_CALIBRATION_NOISY_BASELINE);
    CHECK(thy_calibration_observe(&w,0u,4096u,1u)==THY_CALIBRATION_INVALID);
    CHECK(thy_calibration_observe(&w,0u,1400u,1u)==THY_CALIBRATION_IGNORED);
    CHECK(thy_calibration_contact_begin(&w,0u,1300u,2u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_observe(&w,0u,1400u,8u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_contact_end(&w,0u)==THY_CALIBRATION_RETRY);
    CHECK(w.reason==THY_CALIBRATION_TOO_SHORT && !w.hits);
    CHECK(tap(0u,1300u,1310u,3u,2u)==THY_CALIBRATION_RETRY && w.reason==THY_CALIBRATION_WEAK_PRESS);
    CHECK(tap(0u,1300u,1400u,0u,2u)==THY_CALIBRATION_RETRY && w.reason==THY_CALIBRATION_NO_RISE);
    CHECK(thy_calibration_select(&w,96u)==THY_CALIBRATION_INVALID);
    CHECK(thy_calibration_pad_at(0u)==0u && thy_calibration_pad_at(1u)==4u);
    CHECK(thy_calibration_pad_at(11u)==44u && thy_calibration_pad_at(12u)==1u);
    CHECK(thy_calibration_pad_at(47u)==47u && thy_calibration_pad_at(48u)==48u);
    CHECK(thy_calibration_pad_at(49u)==52u && thy_calibration_pad_at(95u)==95u);
    CHECK(thy_calibration_pad_at(96u)==96u && thy_calibration_pad_at(1000u)==96u);
    CHECK(thy_calibration_contact_begin(&w,0u,1300u,2u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_observe(&w,0u,1400u,8u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_observe(&w,0u,1300u,0u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_contact_end(&w,0u)==THY_CALIBRATION_OK && w.hits==1u);
    return 1;
}
static int test_four_phases_make_one_bounded_candidate(void)
{
    CHECK(thy_calibration_begin(&w)==THY_CALIBRATION_OK);
    CHECK(finish_pad(0u));
    CHECK(w.completed_count==1u && thy_calibration_completed(&w,0u) && w.target==4u);
    CHECK(w.state==THY_CALIBRATION_COLLECT && !w.hits && !w.stage);
    thy_calibration_candidate c;
    frozen=w;
    CHECK(thy_calibration_candidate_get(&w,0u,&c)==THY_CALIBRATION_OK);
    CHECK(!memcmp(&w,&frozen,sizeof w));
    CHECK(c.trigger>1300u && c.trigger<1400u && c.release>=1300u && c.release<c.trigger);
    CHECK(c.pressure_floor==1400u && c.pressure_full==2100u && c.pressure_curve==0u);
    CHECK(c.anchors[0]==2101u && c.anchors[1]==1651u && c.anchors[2]==1401u);
    CHECK(c.soft_rate==9u && c.hard_rate==97u && c.pressure_curve<=2u && c.velocity_curve<=2u);
    CHECK(thy_calibration_candidate_get(&w,4u,&c)==THY_CALIBRATION_NOT_READY);
    for(unsigned n=0;n<3u;++n)CHECK(tap(4u,1300u,(uint16_t)(1400u+n),(uint16_t)(8u+n),2u)==THY_CALIBRATION_OK);
    CHECK(tap(4u,1300u,1650u,32u,2u)==THY_CALIBRATION_OK);
    CHECK(w.target==4u && w.stage==THY_CALIBRATION_MEDIUM && w.hits==1u);
    frozen=w;
    CHECK(thy_calibration_select(&w,0u)==THY_CALIBRATION_OK);
    CHECK(w.selected==0u && w.target==4u && w.stage==THY_CALIBRATION_MEDIUM && w.hits==1u);
    CHECK(!memcmp(w.taps,frozen.taps,sizeof w.taps));
    frozen=w;
    CHECK(thy_calibration_preview(&w,0)==THY_CALIBRATION_NOT_READY && w.state==THY_CALIBRATION_COLLECT);
    CHECK(thy_calibration_preview(&w,1)==THY_CALIBRATION_NOT_READY && !memcmp(&w,&frozen,sizeof w));
    CHECK(thy_calibration_observe(&w,0u,1700u,45u)==THY_CALIBRATION_IGNORED);
    CHECK(w.completed_count==1u && !w.review_ready);
    CHECK(thy_calibration_request_apply(&w)==THY_CALIBRATION_NOT_READY);
    CHECK(thy_calibration_resume(&w)==THY_CALIBRATION_INVALID && w.state==THY_CALIBRATION_COLLECT && w.target==4u);
    CHECK(w.stage==THY_CALIBRATION_MEDIUM && w.hits==1u && !memcmp(w.taps,frozen.taps,sizeof w.taps));
    return 1;
}
static int test_robust_strikes_ignore_one_outlier_and_repeat_inconsistent_group(void)
{
    CHECK(thy_calibration_begin(&w)==THY_CALIBRATION_OK);
    CHECK(tap(0u,1300u,1400u,8u,2u)==THY_CALIBRATION_OK);
    CHECK(tap(0u,1300u,1402u,9u,2u)==THY_CALIBRATION_OK);
    CHECK(tap(0u,1300u,1800u,4000u,2u)==THY_CALIBRATION_OK);
    CHECK(w.stage==THY_CALIBRATION_MEDIUM && !w.hits);
    CHECK(w.variance>1000u && !w.completed_count);
    CHECK(tap(0u,1300u,1400u,1u,2u)==THY_CALIBRATION_OK);
    CHECK(tap(0u,1300u,1400u,100u,2u)==THY_CALIBRATION_OK);
    CHECK(tap(0u,1300u,1400u,4000u,2u)==THY_CALIBRATION_RETRY);
    CHECK(w.reason==THY_CALIBRATION_OUTLIER && !w.hits && w.stage==THY_CALIBRATION_MEDIUM);
    for(unsigned n=0;n<3u;++n)CHECK(tap(0u,1300u,1400u,(uint16_t)(16u+n),2u)==THY_CALIBRATION_OK);
    CHECK(w.stage==THY_CALIBRATION_FIRM && !w.hits);
    return 1;
}
static int test_overlapping_ranges_allow_ordered_medians(void)
{
    CHECK(thy_calibration_begin(&w)==THY_CALIBRATION_OK);
    for(unsigned n=0;n<3u;++n)CHECK(tap(0u,1300u,(uint16_t)(1400u+n),(uint16_t)(8u+n),2u)==THY_CALIBRATION_OK);
    for(unsigned n=0;n<2u;++n)CHECK(tap(0u,1300u,(uint16_t)(1650u+n),9u,2u)==THY_CALIBRATION_OK);
    CHECK(tap(0u,1300u,1652u,9u,2u)==THY_CALIBRATION_RETRY);
    CHECK(w.reason==THY_CALIBRATION_RATE_ORDER && !w.hits && !w.completed_count);
    const uint16_t overlapping_rates[]={8u,12u,16u};
    for(unsigned n=0;n<3u;++n)CHECK(tap(0u,1300u,1350u,overlapping_rates[n],2u)==THY_CALIBRATION_OK);
    CHECK(w.stage==THY_CALIBRATION_FIRM);
    CHECK(thy_calibration_retry_pad(&w)==THY_CALIBRATION_OK);
    CHECK(w.target==0u && !w.stage && !w.hits && !w.completed_count);
    return 1;
}
static int test_pressure_hold_is_separate_timed_and_robust(void)
{
    CHECK(thy_calibration_begin(&w)==THY_CALIBRATION_OK);
    CHECK(strike_levels(0u));
    thy_calibration_tap strikes[9];memcpy(strikes,w.taps,sizeof strikes);
    CHECK(pressure_hold(0u,1400u,2100u,10000u,300u,0)==THY_CALIBRATION_RETRY);
    CHECK(w.reason==THY_CALIBRATION_HOLD_TOO_SHORT && w.stage==THY_CALIBRATION_PRESSURE && !w.completed_count);
    CHECK(!memcmp(strikes,w.taps,sizeof strikes));
    CHECK(pressure_hold(0u,1400u,1400u,11000u,1800u,0)==THY_CALIBRATION_RETRY);
    CHECK(w.reason==THY_CALIBRATION_HOLD_RANGE && w.stage==THY_CALIBRATION_PRESSURE && !w.completed_count);
    CHECK(!memcmp(strikes,w.taps,sizeof strikes));
    CHECK(pressure_hold(0u,1400u,2100u,UINT32_MAX-400u,1800u,1)==THY_CALIBRATION_OK);
    CHECK(w.completed_count==1u && w.candidates[0].pressure_floor==1400u);
    CHECK(w.candidates[0].pressure_full==2100u && w.candidates[0].hard_rate==97u);
    CHECK(w.candidates[0].soft_rate==9u && w.target==4u);
    CHECK(thy_calibration_select(&w,0u)==THY_CALIBRATION_OK && w.completed_count==1u);
    CHECK(thy_calibration_retry_pad(&w)==THY_CALIBRATION_OK && !w.completed_count && w.target==0u && !w.stage);
    return 1;
}
static int test_full_wizard_explicit_preview_apply_cancel(void)
{
    CHECK(thy_calibration_begin(&w)==THY_CALIBRATION_OK);
    for(unsigned n=0;n<96u;++n){uint8_t p=thy_calibration_pad_at(n);CHECK(w.target==p);CHECK(finish_pad(p));}
    CHECK(w.state==THY_CALIBRATION_READY && w.completed_count==96u);
    CHECK(w.review_ready && w.review_span_median==700u && w.review_strike_median==97u && !w.review_outlier_count);
    for(unsigned byte=0;byte<12u;++byte)CHECK(w.completed[byte]==255u);
    CHECK(thy_calibration_select(&w,48u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_preview(&w,1)==THY_CALIBRATION_OK);
    CHECK(w.state==THY_CALIBRATION_PREVIEW_CANDIDATE);
    CHECK(thy_calibration_observe_timed(&w,48u,1700u,45u,w.hold_last_ms+30u)==THY_CALIBRATION_IGNORED);
    CHECK(w.live_raw==1700u && w.live_rate==45u);
    CHECK(thy_calibration_preview(&w,0)==THY_CALIBRATION_OK && w.state==THY_CALIBRATION_PREVIEW_ORIGINAL);
    CHECK(thy_calibration_request_apply(&w)==THY_CALIBRATION_OK && w.state==THY_CALIBRATION_APPLY_PENDING);
    CHECK(thy_calibration_select(&w,1u)==THY_CALIBRATION_INVALID);
    CHECK(thy_calibration_apply_result(&w,0)==THY_CALIBRATION_OK && w.state==THY_CALIBRATION_READY);
    CHECK(w.completed_count==96u && w.reason==THY_CALIBRATION_APPLY_FAILED);
    CHECK(thy_calibration_request_apply(&w)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_apply_result(&w,1)==THY_CALIBRATION_OK && w.state==THY_CALIBRATION_APPLIED);
    CHECK(thy_calibration_request_apply(&w)==THY_CALIBRATION_NOT_READY);
    thy_calibration_cancel(&w);
    CHECK(w.state==THY_CALIBRATION_CANCELLED && !w.completed_count);
    for(unsigned byte=0;byte<12u;++byte)CHECK(!w.completed[byte]);
    CHECK(thy_calibration_begin(&w)==THY_CALIBRATION_OK);
    CHECK(finish_pad(0u));CHECK(thy_calibration_select(&w,0u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_retry_pad(&w)==THY_CALIBRATION_OK && !w.completed_count && !thy_calibration_completed(&w,0u));
    return 1;
}
static int test_cached_review_marks_relative_response_outliers(void)
{
    CHECK(thy_calibration_begin(&w)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_review_prepare(&w)==THY_CALIBRATION_NOT_READY);
    for(unsigned n=0;n<96u;++n)CHECK(finish_pad(thy_calibration_pad_at(n)));
    w.candidates[0].pressure_floor=1800u; /* 300/700 = 43% pressure span. */
    w.candidates[4].hard_rate=200u;      /* 206% firm strike response. */
    w.candidates[8].hard_rate=40u;       /* 41% firm strike response. */
    w.candidates[12].pressure_full=1820u;/* Exactly 60%, within threshold. */
    w.candidates[16].pressure_full=2625u;/* Exactly 175%, within threshold. */
    w.review_ready=0u;
    CHECK(thy_calibration_review_prepare(&w)==THY_CALIBRATION_OK);
    CHECK(w.review_outlier_count==3u && w.review_span_median==700u && w.review_strike_median==97u);
    CHECK(thy_calibration_outlier(&w,0u) && thy_calibration_outlier(&w,4u) && thy_calibration_outlier(&w,8u));
    CHECK(!thy_calibration_outlier(&w,12u) && !thy_calibration_outlier(&w,16u) && !thy_calibration_outlier(&w,96u));
    CHECK(thy_calibration_strength_percent(&w,0u)==43u && thy_calibration_strength_percent(&w,8u)==41u);
    CHECK(thy_calibration_strength_percent(&w,12u)==60u && thy_calibration_strength_percent(&w,20u)==100u);
    frozen=w;CHECK(thy_calibration_review_prepare(&w)==THY_CALIBRATION_OK && !memcmp(&w,&frozen,sizeof w));
    CHECK(thy_calibration_select(&w,0u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_retry_pad(&w)==THY_CALIBRATION_OK);
    CHECK(!w.review_ready && !w.review_outlier_count && !w.review_span_median && !w.review_strike_median);
    CHECK(!thy_calibration_outlier(&w,4u) && !thy_calibration_strength_percent(&w,4u));
    CHECK(!w.stage && w.target==0u && w.completed_count==95u);
    return 1;
}
static int test_cluster_scopes_keep_statistics_and_completion_separate(void)
{
    CHECK(thy_calibration_begin_scope(&w,0u,48u)==THY_CALIBRATION_OK);
    CHECK(w.scope_first==0u && w.scope_count==48u && w.target==0u);
    CHECK(thy_calibration_select(&w,48u)==THY_CALIBRATION_INVALID);
    CHECK(thy_calibration_contact_begin(&w,48u,1300u,2u)==THY_CALIBRATION_IGNORED);
    for(unsigned n=0;n<47u;++n){CHECK(w.target==thy_calibration_pad_at(n));CHECK(finish_pad(w.target));}
    CHECK(w.completed_count==47u && w.target==47u && !w.review_ready);
    CHECK(thy_calibration_request_apply(&w)==THY_CALIBRATION_NOT_READY);
    CHECK(thy_calibration_preview(&w,1)==THY_CALIBRATION_NOT_READY);
    CHECK(finish_pad(47u));
    CHECK(w.completed_count==48u && w.state==THY_CALIBRATION_READY && w.review_ready);
    CHECK(w.review_span_median==700u && w.review_strike_median==97u);
    CHECK(thy_calibration_preview(&w,0)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_request_apply(&w)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_apply_result(&w,1)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_begin_scope(&w,48u,48u)==THY_CALIBRATION_OK);
    CHECK(w.scope_first==48u && w.scope_count==48u && w.target==48u);
    CHECK(!thy_calibration_completed(&w,0u) && !thy_calibration_outlier(&w,0u));
    thy_calibration_candidate candidate;
    CHECK(thy_calibration_candidate_get(&w,0u,&candidate)==THY_CALIBRATION_NOT_READY);
    for(unsigned n=48u;n<96u;++n){uint8_t p=thy_calibration_pad_at(n);CHECK(w.target==p);
        const uint16_t rates[3]={20u,80u,193u};
        for(unsigned stage=0;stage<3u;++stage)for(unsigned hit=0;hit<3u;++hit)
            CHECK(tap(p,1300u,(uint16_t)(1800u+hit),(uint16_t)(rates[stage]+hit),2u)==THY_CALIBRATION_OK);
        CHECK(pressure_hold(p,1400u,2800u,w.hold_last_ms+30u,1800u,0)==THY_CALIBRATION_OK);
    }
    CHECK(w.completed_count==48u && w.review_ready && w.review_span_median==1400u && w.review_strike_median==194u);
    CHECK(thy_calibration_strength_percent(&w,48u)==100u && !w.review_outlier_count);
    CHECK(!thy_calibration_strength_percent(&w,0u));
    frozen=w;
    CHECK(thy_calibration_begin_scope(&w,0u,0u)==THY_CALIBRATION_INVALID && !memcmp(&w,&frozen,sizeof w));
    CHECK(thy_calibration_begin_scope(&w,96u,48u)==THY_CALIBRATION_INVALID && !memcmp(&w,&frozen,sizeof w));
    CHECK(thy_calibration_begin_scope(&w,60u,48u)==THY_CALIBRATION_INVALID && !memcmp(&w,&frozen,sizeof w));
    return 1;
}
static int test_neighbor_leak_raises_safe_guards_or_flags_without_killing_soft_response(void)
{
    CHECK(thy_calibration_begin_scope(&w,0u,48u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_observe_leak(&w,4u,1350u)==THY_CALIBRATION_IGNORED);
    CHECK(strike_levels(0u));
    CHECK(thy_calibration_contact_begin(&w,0u,1300u,2u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_observe_leak(&w,0u,4095u)==THY_CALIBRATION_IGNORED);
    CHECK(thy_calibration_observe_leak(&w,48u,4095u)==THY_CALIBRATION_IGNORED);
    CHECK(thy_calibration_observe_leak(&w,4u,1350u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_observe_leak(&w,4u,1340u)==THY_CALIBRATION_OK && w.leak_floor[4]==1350u);
    CHECK(thy_calibration_observe_leak(&w,8u,1450u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_observe_leak(&w,12u,4095u)==THY_CALIBRATION_OK);
    CHECK(thy_calibration_observe_leak(&w,96u,100u)==THY_CALIBRATION_INVALID);
    CHECK(thy_calibration_observe_leak(&w,4u,4096u)==THY_CALIBRATION_INVALID);
    pressure_samples(0u,1400u,2100u,10000u,1800u,0);
    CHECK(thy_calibration_contact_end(&w,0u)==THY_CALIBRATION_OK);
    for(unsigned n=1u;n<48u;++n)CHECK(finish_pad(thy_calibration_pad_at(n)));
    CHECK(w.review_ready && w.review_leak_count==2u && w.review_outlier_count==2u);
    CHECK(w.candidates[4].release==1358u && w.candidates[4].trigger==1371u);
    CHECK(w.candidates[4].trigger-w.candidates[4].release==13u);
    CHECK(w.candidates[4].trigger<w.candidates[4].pressure_floor && !thy_calibration_leak_outlier(&w,4u));
    CHECK(thy_calibration_leak_outlier(&w,8u) && thy_calibration_leak_outlier(&w,12u));
    CHECK(thy_calibration_outlier(&w,8u) && thy_calibration_outlier(&w,12u));
    CHECK(w.candidates[8].trigger==1325u && w.candidates[8].release==1312u);
    CHECK(w.candidates[12].trigger==1325u && w.candidates[12].release==1312u);
    CHECK(w.leak_floor[0]==0u && w.leak_floor[48]==0u && !thy_calibration_leak_outlier(&w,48u));
    frozen=w;CHECK(thy_calibration_review_prepare(&w)==THY_CALIBRATION_OK && !memcmp(&w,&frozen,sizeof w));
    CHECK(thy_calibration_select(&w,8u)==THY_CALIBRATION_OK && thy_calibration_retry_pad(&w)==THY_CALIBRATION_OK);
    CHECK(!w.review_ready && !w.review_leak_count && !thy_calibration_leak_outlier(&w,8u));
    CHECK(w.leak_floor[4]==1350u && w.leak_floor[8]==1450u);
    return 1;
}
int main(void)
{
    if(!test_workspace_and_validation() || !test_four_phases_make_one_bounded_candidate() ||
       !test_robust_strikes_ignore_one_outlier_and_repeat_inconsistent_group() || !test_overlapping_ranges_allow_ordered_medians() ||
       !test_pressure_hold_is_separate_timed_and_robust() ||
       !test_full_wizard_explicit_preview_apply_cancel() ||
       !test_cached_review_marks_relative_response_outliers() ||
       !test_cluster_scopes_keep_statistics_and_completion_separate() ||
       !test_neighbor_leak_raises_safe_guards_or_flags_without_killing_soft_response())return 1;
    printf("9 calibration workspace scenarios passed (%u assertions); workspace %zu bytes.\n",assertions,sizeof w);
    return 0;
}
