/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_calibration.h"

_Static_assert(sizeof(thy_calibration_candidate)==20u,"Calibration candidate layout changed");
_Static_assert(sizeof(thy_calibration)<=2304u,"Calibration workspace exceeds 2304 bytes");
static void zero(void *p,size_t count){uint8_t *b=p;while(count--)*b++=0u;}
static int reject(thy_calibration *w,uint8_t reason)
{w->contact_active=0u;w->reason=reason;++w->revision;return THY_CALIBRATION_RETRY;}
static unsigned maximum(unsigned a,unsigned b){return a>b?a:b;}
static uint16_t median3(uint16_t a,uint16_t b,uint16_t c)
{if(a>b){uint16_t t=a;a=b;b=t;}if(b>c){uint16_t t=b;b=c;c=t;}return a>b?a:b;}
static uint16_t median(const uint16_t *values,unsigned count)
{
    uint16_t copy[9];
    for(unsigned n=0;n<count;++n){unsigned at=n;copy[n]=values[n];
        while(at && copy[at]<copy[at-1u]){uint16_t t=copy[at];copy[at]=copy[at-1u];copy[--at]=t;}}
    return copy[count/2u];
}
static uint16_t signal(const thy_calibration_tap *t){return t->peak>t->baseline?(uint16_t)(t->peak-t->baseline):0u;}
static unsigned difference(unsigned a,unsigned b){return a>b?a-b:b-a;}
static int in_scope(const thy_calibration *w,uint8_t p)
{return w && p>=w->scope_first && (unsigned)p<(unsigned)w->scope_first+w->scope_count;}
static int scope_complete(const thy_calibration *w)
{return w && w->scope_count && w->completed_count==w->scope_count;}
static uint8_t curve_for_midpoint(unsigned low,unsigned middle,unsigned high)
{
    unsigned x=middle<=low?0u:middle>=high?127u:(middle-low)*127u/(high-low);
    unsigned values[3]={x,127u-(127u-x)*(127u-x)/127u,x*x/127u};
    unsigned best=0u;for(unsigned n=1u;n<3u;++n)if(difference(values[n],64u)<difference(values[best],64u))best=n;
    return (uint8_t)best;
}
void thy_calibration_init(thy_calibration *w){if(w)zero(w,sizeof *w);}
uint8_t thy_calibration_pad_at(unsigned index)
{return index<96u?(uint8_t)((index/48u)*48u+(index%12u)*4u+(index%48u)/12u):96u;}
int thy_calibration_begin(thy_calibration *w)
{return thy_calibration_begin_scope(w,0u,96u);}
int thy_calibration_begin_scope(thy_calibration *w,uint8_t first,uint8_t count)
{
    if(!w || !count || first>=96u || (unsigned)first+count>96u)return THY_CALIBRATION_INVALID;
    zero(w,sizeof *w);w->scope_first=first;w->scope_count=count;
    for(unsigned n=0;n<96u;++n)if(in_scope(w,thy_calibration_pad_at(n))){w->target=thy_calibration_pad_at(n);break;}
    w->selected=w->target;w->state=THY_CALIBRATION_COLLECT;w->preview_percent=80u;w->revision=1u;return THY_CALIBRATION_OK;
}
void thy_calibration_cancel(thy_calibration *w)
{if(w){uint32_t revision=w->revision+1u;zero(w,sizeof *w);w->state=THY_CALIBRATION_CANCELLED;w->revision=revision;}}
int thy_calibration_completed(const thy_calibration *w,uint8_t p)
{return in_scope(w,p) && p<96u && !!(w->completed[p/8u]&(1u<<(p&7u)));}
int thy_calibration_candidate_get(const thy_calibration *w,uint8_t p,thy_calibration_candidate *out)
{if(!out || !thy_calibration_completed(w,p))return THY_CALIBRATION_NOT_READY;*out=w->candidates[p];return THY_CALIBRATION_OK;}
static void clear_review(thy_calibration *w)
{
    w->review_span_median=w->review_strike_median=0u;
    w->review_outlier_count=w->review_leak_count=w->review_ready=0u;
    zero(w->review_outliers,sizeof w->review_outliers);zero(w->review_leak_outliers,sizeof w->review_leak_outliers);
}
static unsigned relative(unsigned value,unsigned reference)
{return reference?(value*100u+reference/2u)/reference:0u;}
unsigned thy_calibration_strength_percent(const thy_calibration *w,uint8_t p)
{
    if(!w || !w->review_ready || !thy_calibration_completed(w,p))return 0u;
    const thy_calibration_candidate *c=&w->candidates[p];
    unsigned span=relative(c->pressure_full-c->pressure_floor,w->review_span_median);
    unsigned strike=relative(c->hard_rate,w->review_strike_median);
    unsigned strength=span<strike?span:strike;return strength>999u?999u:strength;
}
int thy_calibration_outlier(const thy_calibration *w,uint8_t p)
{return in_scope(w,p) && w->review_ready && p<96u && !!(w->review_outliers[p/8u]&(1u<<(p&7u)));}
int thy_calibration_leak_outlier(const thy_calibration *w,uint8_t p)
{return in_scope(w,p) && w->review_ready && p<96u && !!(w->review_leak_outliers[p/8u]&(1u<<(p&7u)));}
static uint16_t board_median(uint16_t values[96],unsigned count)
{
    for(unsigned n=1u;n<count;++n){unsigned at=n;
        while(at && values[at]<values[at-1u]){uint16_t t=values[at];values[at]=values[at-1u];values[--at]=t;}}
    return count&1u?values[count/2u]:(uint16_t)(((unsigned)values[count/2u-1u]+values[count/2u]+1u)/2u);
}
int thy_calibration_review_prepare(thy_calibration *w)
{
    if(!scope_complete(w) || w->contact_active)return THY_CALIBRATION_NOT_READY;
    if(w->review_ready)return THY_CALIBRATION_OK;
    uint16_t values[96];
    for(unsigned n=0;n<w->scope_count;++n){unsigned p=w->scope_first+n;const thy_calibration_candidate *c=&w->candidates[p];
        if(!thy_calibration_completed(w,(uint8_t)p) || c->pressure_full<=c->pressure_floor || !c->hard_rate ||
            c->release>=c->trigger || c->trigger>4095u || c->pressure_full>4095u)
            return THY_CALIBRATION_INVALID;
        values[n]=(uint16_t)(c->pressure_full-c->pressure_floor);
    }
    w->review_span_median=board_median(values,w->scope_count);
    for(unsigned n=0;n<w->scope_count;++n)values[n]=w->candidates[w->scope_first+n].hard_rate;
    w->review_strike_median=board_median(values,w->scope_count);
    zero(w->review_outliers,sizeof w->review_outliers);w->review_outlier_count=0u;
    zero(w->review_leak_outliers,sizeof w->review_leak_outliers);w->review_leak_count=0u;
    for(unsigned n=0;n<w->scope_count;++n){unsigned p=w->scope_first+n;thy_calibration_candidate *c=&w->candidates[p];
        unsigned guarded_release=(unsigned)w->leak_floor[p]+THY_CALIBRATION_LEAK_MARGIN;
        if(guarded_release>c->release){
            unsigned guarded_trigger=guarded_release+c->trigger-c->release;
            unsigned soft_limit=c->anchors[0]<c->pressure_floor?c->anchors[0]:c->pressure_floor;
            if(c->pressure_full<soft_limit)soft_limit=c->pressure_full;
            if(guarded_trigger<soft_limit && guarded_trigger<=4095u){
                c->trigger=(uint16_t)guarded_trigger;c->release=(uint16_t)guarded_release;
            }else{
                w->review_leak_outliers[p/8u]|=(uint8_t)(1u<<(p&7u));++w->review_leak_count;
            }
        }
        unsigned span=relative(c->pressure_full-c->pressure_floor,w->review_span_median);
        unsigned strike=relative(c->hard_rate,w->review_strike_median);
        if(span<60u || span>175u || strike<60u || strike>175u ||
            (w->review_leak_outliers[p/8u]&(1u<<(p&7u)))){
            w->review_outliers[p/8u]|=(uint8_t)(1u<<(p&7u));++w->review_outlier_count;
        }
    }
    w->review_ready=1u;return THY_CALIBRATION_OK;
}
int thy_calibration_select(thy_calibration *w,uint8_t p)
{
    if(!w || p>=96u || !in_scope(w,p) || w->contact_active || w->state==THY_CALIBRATION_APPLY_PENDING ||
        w->state==THY_CALIBRATION_OFF || w->state>=THY_CALIBRATION_APPLIED)return THY_CALIBRATION_INVALID;
    w->selected=p;
    if(w->state==THY_CALIBRATION_COLLECT && p!=w->target && !thy_calibration_completed(w,p)){
        w->target=p;w->stage=w->hits=0u;zero(w->taps,sizeof w->taps);
    }
    w->reason=0u;++w->revision;return THY_CALIBRATION_OK;
}
int thy_calibration_retry_pad(thy_calibration *w)
{
    if(!w || w->contact_active || w->state==THY_CALIBRATION_OFF ||
        w->state==THY_CALIBRATION_APPLY_PENDING || w->state>=THY_CALIBRATION_APPLIED)return THY_CALIBRATION_INVALID;
    uint8_t p=w->selected;if(!in_scope(w,p))return THY_CALIBRATION_INVALID;
    if(thy_calibration_completed(w,p)){w->completed[p/8u]&=(uint8_t)~(1u<<(p&7u));--w->completed_count;}
    clear_review(w);
    w->target=p;w->stage=w->hits=w->reason=0u;w->state=THY_CALIBRATION_COLLECT;zero(w->taps,sizeof w->taps);
    ++w->revision;return THY_CALIBRATION_OK;
}
int thy_calibration_contact_begin(thy_calibration *w,uint8_t p,uint16_t baseline,uint16_t span)
{
    if(!w || p>=96u || baseline>4095u || span>4095u)return THY_CALIBRATION_INVALID;
    if(!in_scope(w,p))return THY_CALIBRATION_IGNORED;
    if(w->state!=THY_CALIBRATION_COLLECT || w->contact_active)return THY_CALIBRATION_NOT_READY;
    if(p!=w->target)return reject(w,THY_CALIBRATION_WRONG_PAD);
    if(thy_calibration_completed(w,p))return THY_CALIBRATION_NOT_READY;
    if(span>THY_CALIBRATION_QUIET_SPAN_MAX)return reject(w,THY_CALIBRATION_NOISY_BASELINE);
    w->selected=p;w->baseline=baseline;w->quiet_span=span;w->peak=baseline;w->rise_rate=w->samples=0u;
    if(w->stage==THY_CALIBRATION_PRESSURE){
        w->hold_soft=w->hold_firm=0u;w->hold_soft_samples=w->hold_peak_samples=0u;
        w->hold_started_ms=0u;zero(w->hold_initial,sizeof w->hold_initial);zero(w->hold_peaks,sizeof w->hold_peaks);
    }
    w->contact_active=1u;w->reason=0u;++w->revision;return THY_CALIBRATION_OK;
}
int thy_calibration_observe(thy_calibration *w,uint8_t p,uint16_t raw,uint16_t rate)
{return thy_calibration_observe_timed(w,p,raw,rate,w?w->hold_last_ms+20u:0u);}
int thy_calibration_observe_leak(thy_calibration *w,uint8_t p,uint16_t raw)
{
    if(!w || p>=96u || raw>4095u)return THY_CALIBRATION_INVALID;
    if(!in_scope(w,p) || p==w->target || w->state!=THY_CALIBRATION_COLLECT ||
        w->stage!=THY_CALIBRATION_PRESSURE || !w->contact_active)return THY_CALIBRATION_IGNORED;
    if(raw>w->leak_floor[p])w->leak_floor[p]=raw;return THY_CALIBRATION_OK;
}
int thy_calibration_observe_timed(thy_calibration *w,uint8_t p,uint16_t raw,uint16_t rate,uint32_t sample_ms)
{
    if(!w || p>=96u || raw>4095u || rate>4095u)return THY_CALIBRATION_INVALID;
    unsigned viewed=w->state==THY_CALIBRATION_COLLECT?w->target:w->selected;
    if(p==viewed && w->state>=THY_CALIBRATION_COLLECT && w->state<=THY_CALIBRATION_PREVIEW_CANDIDATE){
        w->live_raw=raw;w->live_rate=rate;w->hold_last_ms=sample_ms;
    }
    if(!w->contact_active || w->state!=THY_CALIBRATION_COLLECT || p!=w->target)return THY_CALIBRATION_IGNORED;
    if(raw>w->peak)w->peak=raw;if(rate>w->rise_rate)w->rise_rate=rate;
    if(w->stage==THY_CALIBRATION_PRESSURE){
        if(!w->samples)w->hold_started_ms=sample_ms;
        uint32_t elapsed=sample_ms-w->hold_started_ms;
        if(elapsed>=100u && elapsed<=450u && w->hold_soft_samples<9u &&
            raw>w->baseline+THY_CALIBRATION_SIGNAL_MIN){
            w->hold_initial[w->hold_soft_samples++]=raw;
            w->hold_soft=median(w->hold_initial,w->hold_soft_samples);
        }
        for(unsigned at=0;at<3u;++at)if(raw>=w->hold_peaks[at]){
            for(unsigned n=2u;n>at;--n)w->hold_peaks[n]=w->hold_peaks[n-1u];
            w->hold_peaks[at]=raw;break;
        }
        if(w->hold_peak_samples<3u)++w->hold_peak_samples;
        if(w->hold_peak_samples==3u)w->hold_firm=w->hold_peaks[1];
    }
    if(w->samples<UINT16_MAX)++w->samples;
    return THY_CALIBRATION_OK;
}
static int accept_stage(thy_calibration *w)
{
    thy_calibration_tap *t=w->taps+3u*w->stage;
    uint16_t rates[3]={t[0].rate,t[1].rate,t[2].rate};
    unsigned rate=median3(rates[0],rates[1],rates[2]);
    unsigned sum=rates[0]+rates[1]+rates[2],squares=0u;
    for(unsigned n=0;n<3u;++n)squares+=(unsigned)rates[n]*rates[n];
    w->variance=(3u*squares-sum*sum)/9u;
    /* One unusual strike cannot poison the next retry. Accept when any pair
     * supports the median; otherwise repeat this whole three-strike group. */
    int consistent=0;
    for(unsigned a=0;a<3u;++a)for(unsigned b=a+1u;b<3u;++b)
        if(difference(rates[a],rates[b])<=maximum(8u,rate/2u))consistent=1;
    if(!consistent){
        zero(t,3u*sizeof *t);w->hits=0u;return reject(w,THY_CALIBRATION_OUTLIER);
    }
    if(w->stage){
        const thy_calibration_tap *previous=t-3u;
        unsigned previous_rate=median3(previous[0].rate,previous[1].rate,previous[2].rate);
        if(rate<=previous_rate){
            zero(t,3u*sizeof *t);w->hits=0u;
            return reject(w,THY_CALIBRATION_RATE_ORDER);
        }
    }
    return THY_CALIBRATION_OK;
}
static int complete_pad(thy_calibration *w)
{
    uint16_t med[3],rates[3],baselines[3];unsigned noise=w->quiet_span;
    for(unsigned stage=0;stage<3u;++stage){const thy_calibration_tap *t=w->taps+3u*stage;
        med[stage]=median3(signal(t),signal(t+1u),signal(t+2u));
        rates[stage]=median3(t[0].rate,t[1].rate,t[2].rate);
        baselines[stage]=median3(t[0].baseline,t[1].baseline,t[2].baseline);
        for(unsigned n=0;n<3u;++n)noise=maximum(noise,t[n].quiet_span);
    }
    unsigned baseline=w->baseline;
    if((uint32_t)(w->hold_last_ms-w->hold_started_ms)<THY_CALIBRATION_HOLD_MIN_MS ||
        w->hold_soft_samples<3u || w->hold_peak_samples<3u)
        return reject(w,THY_CALIBRATION_HOLD_TOO_SHORT);
    if(w->hold_soft<=baseline+maximum(noise*2u+4u,THY_CALIBRATION_SIGNAL_MIN) ||
        w->hold_firm<=w->hold_soft+maximum(16u,noise*4u+4u))
        return reject(w,THY_CALIBRATION_HOLD_RANGE);
    unsigned soft_raw=baselines[0]+med[0];
    if(soft_raw>w->hold_soft)soft_raw=w->hold_soft;
    if(soft_raw<=baseline)return reject(w,THY_CALIBRATION_NOISY_BASELINE);
    unsigned soft_delta=soft_raw-baseline;
    unsigned attack_delta=maximum(noise*2u+4u,soft_delta/4u);
    if(attack_delta>=soft_delta)return reject(w,THY_CALIBRATION_NOISY_BASELINE);
    unsigned release_delta=maximum(noise+2u,attack_delta/2u);
    if(release_delta>=attack_delta)release_delta=attack_delta-1u;
    thy_calibration_candidate c={0};
    c.trigger=(uint16_t)(baseline+attack_delta);c.release=(uint16_t)(baseline+release_delta);
    c.pressure_floor=w->hold_soft;
    c.pressure_full=w->hold_firm;c.soft_rate=rates[0];c.hard_rate=rates[2];
    for(unsigned n=0;n<3u;++n){unsigned anchor=baselines[n]+med[n];c.anchors[n]=(uint16_t)(anchor>4095u?4095u:anchor);}
    c.pressure_curve=0u; /* No known physical-force midpoint in a free ramp. */
    c.velocity_curve=curve_for_midpoint(rates[0],rates[1],rates[2]);
    w->candidates[w->target]=c;w->completed[w->target/8u]|=(uint8_t)(1u<<(w->target&7u));
    ++w->completed_count;w->selected=w->target;
    if(scope_complete(w)){w->state=THY_CALIBRATION_READY;(void)thy_calibration_review_prepare(w);}
    else {unsigned index=0u;while(thy_calibration_pad_at(index)!=w->target && index<96u)++index;
      for(unsigned offset=1u;offset<=96u;++offset){uint8_t next=thy_calibration_pad_at((index+offset)%96u);
        if(in_scope(w,next) && !thy_calibration_completed(w,next)){w->target=next;break;}}
    }
    if(w->state==THY_CALIBRATION_COLLECT)w->selected=w->target;
    w->stage=w->hits=w->reason=0u;zero(w->taps,sizeof w->taps);++w->revision;return THY_CALIBRATION_OK;
}
int thy_calibration_contact_end(thy_calibration *w,uint8_t p)
{
    if(!w || p>=96u)return THY_CALIBRATION_INVALID;
    if(!w->contact_active || w->state!=THY_CALIBRATION_COLLECT || p!=w->target)return THY_CALIBRATION_IGNORED;
    w->contact_active=0u;
    if(w->stage==THY_CALIBRATION_PRESSURE)return complete_pad(w);
    if(w->samples<2u)return reject(w,THY_CALIBRATION_TOO_SHORT);
    if(w->peak<w->baseline+THY_CALIBRATION_SIGNAL_MIN)return reject(w,THY_CALIBRATION_WEAK_PRESS);
    if(!w->rise_rate)return reject(w,THY_CALIBRATION_NO_RISE);
    w->taps[3u*w->stage+w->hits]=(thy_calibration_tap){w->baseline,w->peak,w->rise_rate,w->quiet_span};
    ++w->hits;++w->revision;
    if(w->hits<3u)return THY_CALIBRATION_OK;
    int result=accept_stage(w);if(result!=THY_CALIBRATION_OK)return result;
    ++w->stage;w->hits=0u;w->reason=0u;return THY_CALIBRATION_OK;
}
int thy_calibration_preview(thy_calibration *w,int candidate)
{
    if(!w || (candidate!=0 && candidate!=1))return THY_CALIBRATION_INVALID;
    if(!scope_complete(w) || w->contact_active || w->state==THY_CALIBRATION_APPLY_PENDING ||
        w->state==THY_CALIBRATION_OFF || w->state>=THY_CALIBRATION_APPLIED)return THY_CALIBRATION_NOT_READY;
    if(!thy_calibration_completed(w,w->selected))for(unsigned p=w->scope_first;p<(unsigned)w->scope_first+w->scope_count;++p)
        if(thy_calibration_completed(w,(uint8_t)p)){w->selected=(uint8_t)p;break;}
    w->state=candidate?THY_CALIBRATION_PREVIEW_CANDIDATE:THY_CALIBRATION_PREVIEW_ORIGINAL;
    w->reason=0u;++w->revision;return THY_CALIBRATION_OK;
}
int thy_calibration_resume(thy_calibration *w)
{
    if(!w || (w->state!=THY_CALIBRATION_PREVIEW_ORIGINAL && w->state!=THY_CALIBRATION_PREVIEW_CANDIDATE))return THY_CALIBRATION_INVALID;
    w->state=scope_complete(w)?THY_CALIBRATION_READY:THY_CALIBRATION_COLLECT;
    if(w->state==THY_CALIBRATION_COLLECT)w->selected=w->target;
    w->reason=0u;++w->revision;return THY_CALIBRATION_OK;
}
int thy_calibration_request_apply(thy_calibration *w)
{
    if(!scope_complete(w) || w->contact_active ||
        (w->state!=THY_CALIBRATION_READY && w->state!=THY_CALIBRATION_PREVIEW_ORIGINAL &&
         w->state!=THY_CALIBRATION_PREVIEW_CANDIDATE))return THY_CALIBRATION_NOT_READY;
    w->state=THY_CALIBRATION_APPLY_PENDING;w->reason=0u;++w->revision;return THY_CALIBRATION_OK;
}
int thy_calibration_apply_result(thy_calibration *w,int success)
{
    if(!w || w->state!=THY_CALIBRATION_APPLY_PENDING || (success!=0 && success!=1))return THY_CALIBRATION_INVALID;
    w->state=success?THY_CALIBRATION_APPLIED:THY_CALIBRATION_READY;
    w->reason=success?0u:THY_CALIBRATION_APPLY_FAILED;++w->revision;return THY_CALIBRATION_OK;
}
const char *thy_calibration_reason_text(uint8_t reason)
{
    static const char *const text[]={"", "USE LIT PAD", "NOISY REST / RETRY", "PRESS TOO WEAK / RETRY",
        "HOLD BRIEFLY / RETRY", "STRIKE THE PAD / RETRY", "REPEAT 3 EVEN STRIKES", "LEVELS OVERLAP / RETRY",
        "STRIKE FASTER / RETRY", "RELEASE PADS TO APPLY", "HOLD SOFT THEN FIRM", "MORE PRESSURE RANGE"};
    return reason<sizeof text/sizeof text[0]?text[reason]:"RETRY";
}
