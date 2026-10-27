/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_engine.h"
#include "thy_calibration.h"

static int review(const thy_calibration *w)
{
    return w && w->scope_count && w->completed_count==w->scope_count &&
        w->state>=THY_CALIBRATION_READY && w->state<=THY_CALIBRATION_PREVIEW_CANDIDATE;
}
static int in_scope(const thy_calibration *w,unsigned logical)
{return logical>=w->scope_first && logical<(unsigned)w->scope_first+w->scope_count;}
static int quiet(const thy_engine *e)
{
    if(!e || !e->instrument || !e->calibration)return 0;
    const thy_instrument *i=e->instrument;
    if(i->core->sounding_count || i->core->pending_keys || i->core->generated_count ||
       i->core->queue_count || i->core->sustain_count || e->rhythm_transport.state==THY_TRANSPORT_PLAYING)return 0;
    for(unsigned p=0;p<96u;++p)
        if(i->sensors[p].held || i->sensors[p].blocked || i->sensors[p].attack_count ||
           i->core->owners[p].held)return 0;
    return !e->calibration->contact_active;
}
static void transition(thy_engine *e,int8_t direction)
{
    e->state.transition_direction=direction;
    if(++e->state.transition_revision==0u)e->state.transition_revision=1u;
}
static void configuration(const thy_calibration_candidate *c,thy_instrument_response *r,
    thy_instrument_velocity *v,thy_instrument_release_policy *p)
{
    *r=(thy_instrument_response){c->trigger,c->release,c->pressure_floor,c->pressure_full,c->pressure_curve};
    *v=(thy_instrument_velocity){c->soft_rate,c->hard_rate,THY_VELOCITY_STRIKE,c->velocity_curve,1u,127u};
    *p=(thy_instrument_release_policy){(uint16_t)(c->trigger-c->release),1u};
}
static int candidate_config(const thy_engine *e,unsigned electrical,thy_instrument_response *r,
    thy_instrument_velocity *v,thy_instrument_release_policy *policy)
{
    const thy_calibration *w=e->calibration;
    unsigned logical=e->instrument->mapping[electrical];uint8_t overridden;
    if(!in_scope(w,logical))return 0;
    if(thy_calibration_leak_outlier(w,(uint8_t)logical)){
        if(thy_instrument_get_pad_response(e->instrument,(uint8_t)electrical,r,&overridden)!=THY_OK ||
           thy_instrument_get_velocity(e->instrument,(int)electrical,v,&overridden)!=THY_OK ||
           thy_instrument_get_release_policy(e->instrument,(int)electrical,policy,&overridden)!=THY_OK)return -1;
        return 1; /* A response that cannot be separated stays exactly as saved. */
    }
    thy_calibration_candidate c;
    if(thy_calibration_candidate_get(w,(uint8_t)logical,&c)!=THY_CALIBRATION_OK)return -1;
    configuration(&c,r,v,policy);
    unsigned reference=e->calibration_review_reference;
    if(reference && e->calibration_review_attack){
        unsigned attack=((uint32_t)c.trigger*e->calibration_review_attack+reference/2u)/reference;
        if(!attack || attack>4095u)return -1;
        r->attack_raw=(uint16_t)attack;
        r->release_raw=attack>policy->gap?(uint16_t)(attack-policy->gap):0u;
    }
    return 1;
}
static int sampling(void *context,uint8_t electrical,thy_instrument_response *r,thy_instrument_velocity *v)
{
    const thy_engine *e=context;
    if(!e || !e->calibration_open || !review(e->calibration) ||
       e->calibration->state!=THY_CALIBRATION_PREVIEW_CANDIDATE)return 0;
    if(thy_calibration_leak_outlier(e->calibration,e->instrument->mapping[electrical]))return 0;
    thy_instrument_release_policy policy;
    return candidate_config(e,electrical,r,v,&policy)>0?1:0;
}
static int review_bounds(const thy_engine *e,int32_t *low,int32_t *high)
{
    const thy_calibration *w=e->calibration;
    if(!review(w) || !e->calibration_review_reference)return THY_STATE_UNAVAILABLE;
    unsigned side=w->scope_first/48u;
    const thy_instrument_release_policy *global=&e->instrument->cluster_release_policy[side];
    int32_t lower=global->linked?1:e->instrument->cluster_response[side].release_raw+1,upper=4095;
    uint32_t reference=e->calibration_review_reference;
    for(unsigned p=0;p<96u;++p){
        unsigned logical=e->instrument->mapping[p];
        if(!in_scope(w,logical) || thy_calibration_leak_outlier(w,(uint8_t)logical))continue;
        const thy_calibration_candidate *c=&w->candidates[logical];
        const thy_instrument_sensor *s=&e->instrument->sensors[p];
        if(!c->trigger || c->trigger<=c->release)return THY_STATE_INVALID;
        unsigned gap=c->trigger-c->release;
        unsigned floor=s->calibration.baseline+2u*(s->warmup_max-s->warmup_min)+4u;
        if(w->leak_floor[logical] && w->leak_floor[logical]+THY_CALIBRATION_LEAK_MARGIN>floor)floor=w->leak_floor[logical]+THY_CALIBRATION_LEAK_MARGIN;
        unsigned safe=floor+gap;
        int32_t bound=(int32_t)((safe*reference+c->trigger-1u)/c->trigger);
        if(bound>lower)lower=bound;
        bound=(int32_t)(4095u*reference/c->trigger);if(bound<upper)upper=bound;
    }
    if(lower<1)lower=1;if(upper>4095)upper=4095;
    if(lower>upper)return THY_STATE_INVALID;
    *low=lower;*high=upper;return THY_STATE_OK;
}
static void prepare_review(thy_engine *e)
{
    thy_calibration *w=e->calibration;
    if(!review(w) || !e->calibration_review_reference)return;
    (void)thy_calibration_review_prepare(w);
    int32_t low,high;
    if(review_bounds(e,&low,&high)!=THY_STATE_OK)return;
    unsigned raw=e->calibration_review_attack;
    if(raw<(unsigned)low)raw=(unsigned)low;if(raw>(unsigned)high)raw=(unsigned)high;
    e->calibration_review_attack=(uint16_t)raw;
    w->preview_percent=(uint8_t)(high==low?100:((high-(int32_t)raw)*100+(high-low)/2)/(high-low));
    e->calibration_review_draft=w->preview_percent;
}
void thy_engine_bind_calibration(thy_engine *e,thy_calibration *workspace)
{
    if(!e)return;
    e->calibration=workspace;e->calibration_open=e->calibration_pick=e->calibration_rejected=0u;
    e->calibration_review_cursor=e->calibration_review_editing=0u;
    e->calibration_review_reference=e->calibration_review_attack=0u;
    if(workspace)thy_calibration_init(workspace);
    if(e->instrument)thy_instrument_set_sampling_provider(e->instrument,sampling,e);
    for(unsigned p=0;p<96u;++p)e->calibration_sample_ms[p]=0u;
    for(unsigned b=0;b<12u;++b)e->calibration_contacts[b]=0u;
}
int thy_engine_calibration_open(thy_engine *e)
{
    if(e&&e->instrument&&e->instrument->guidance.open)return THY_STATE_BLOCKED;
    if(!e || !e->calibration)return THY_STATE_UNAVAILABLE;
    if(!quiet(e)){e->state.notice=THY_NOTICE_RELEASE;return THY_STATE_BLOCKED;}
    unsigned side=e->sensitivity_target==97u?1u:
        e->sensitivity_target?((unsigned)e->sensitivity_target-1u)/48u:0u;
    thy_calibration *w=e->calibration;
    if(w->state!=THY_CALIBRATION_OFF && w->state<THY_CALIBRATION_APPLIED &&
       w->scope_count && w->scope_first!=side*48u){e->state.notice=THY_NOTICE_ADAPTER;return THY_STATE_BLOCKED;}
    if(e->sequencer_ui.open)(void)thy_engine_sequencer_close(e);
    thy_engine_repeat_cancel(e);
    if(w->state==THY_CALIBRATION_OFF || w->state>=THY_CALIBRATION_APPLIED){
        (void)thy_calibration_begin_scope(w,(uint8_t)(side*48u),48u);
        e->calibration_review_reference=e->instrument->cluster_response[side].attack_raw;
        e->calibration_review_attack=e->calibration_review_reference;
        e->calibration_review_cursor=e->calibration_review_editing=0u;
    }
    e->calibration_open=1u;e->calibration_pick=e->calibration_rejected=0u;e->arp_quick_open=0u;e->sensitivity_pick=0u;
    for(unsigned p=0;p<96u;++p)e->calibration_sample_ms[p]=e->instrument->sensors[p].last_sample_ms;
    thy_state_play(&e->state);transition(e,-1);return THY_STATE_OK;
}
void thy_engine_calibration_close(thy_engine *e)
{
    if(!e || !e->calibration_open)return;
    if(review(e->calibration) && !quiet(e)){e->state.notice=THY_NOTICE_RELEASE;return;}
    e->calibration_open=e->calibration_pick=e->calibration_review_editing=0u;
    if(e->calibration->contact_active){
        e->calibration->contact_active=0u;e->calibration->reason=THY_CALIBRATION_TOO_SHORT;
        ++e->calibration->revision;
    }
    for(unsigned p=0;p<96u;++p)
        if(!e->instrument->sensors[p].held &&
           e->instrument->sensors[p].raw<=e->instrument->sensors[p].response.release_raw)
            e->calibration_contacts[p/8u]&=(uint8_t)~(1u<<(p&7u));
    thy_engine_repeat_cancel(e);thy_state_play(&e->state);transition(e,1);
}
int thy_engine_calibration_set_sensitivity(thy_engine *e,int percent)
{
    if(!e || !e->calibration_open || !review(e->calibration))return THY_STATE_UNAVAILABLE;
    if(percent<0 || percent>100)return THY_STATE_INVALID;
    if(!quiet(e)){e->state.notice=THY_NOTICE_RELEASE;return THY_STATE_BLOCKED;}
    int32_t low,high;if(review_bounds(e,&low,&high)!=THY_STATE_OK)return THY_STATE_INVALID;
    if(percent!=e->calibration->preview_percent)
        e->calibration_review_attack=(uint16_t)(high-((uint32_t)percent*(uint32_t)(high-low)+50u)/100u);
    e->calibration->preview_percent=(uint8_t)percent;
    e->calibration_review_draft=(uint8_t)percent;++e->calibration->revision;
    return THY_STATE_OK;
}
int thy_engine_calibration_select(thy_engine *e,uint8_t logical)
{
    if(!e || !e->calibration_open || !in_scope(e->calibration,logical))return THY_STATE_INVALID;
    if(!quiet(e))return THY_STATE_BLOCKED;
    thy_calibration *w=e->calibration;
    if(review(w))return thy_calibration_select(w,logical)==THY_CALIBRATION_OK?THY_STATE_OK:THY_STATE_BLOCKED;
    if(thy_calibration_select(w,logical)!=THY_CALIBRATION_OK)return THY_STATE_BLOCKED;
    if(thy_calibration_completed(w,logical))(void)thy_calibration_retry_pad(w);
    e->calibration_pick=0u;
    return THY_STATE_OK;
}
static uint16_t median(uint16_t *values,unsigned count)
{
    for(unsigned n=1;n<count;++n){uint16_t value=values[n];unsigned k=n;
        while(k && values[k-1u]>value){values[k]=values[k-1u];--k;}values[k]=value;}
    return values[count/2u];
}
int thy_engine_calibration_apply(thy_engine *e)
{
    if(!e || !e->calibration || !e->calibration_open)return THY_STATE_UNAVAILABLE;
    thy_calibration *w=e->calibration;
    if(!quiet(e))return THY_STATE_BLOCKED;
    if(!review(w))return THY_STATE_UNAVAILABLE;
    if(w->scope_count!=48u)return THY_STATE_UNAVAILABLE;
    prepare_review(e);
    unsigned side=w->scope_first/48u;
    thy_instrument_response base=e->instrument->cluster_response[side];
    thy_instrument_velocity velocity=e->instrument->cluster_velocity[side];
    thy_instrument_release_policy base_policy=e->instrument->cluster_release_policy[side];
    uint16_t values[4][48];unsigned learned=0u;
    for(unsigned p=w->scope_first;p<(unsigned)w->scope_first+w->scope_count;++p){
        if(thy_calibration_leak_outlier(w,(uint8_t)p))continue;
        const thy_calibration_candidate *c=&w->candidates[p];
        values[0][learned]=c->pressure_floor;values[1][learned]=c->pressure_full;
        values[2][learned]=c->soft_rate;values[3][learned]=c->hard_rate;++learned;
    }
    if(!learned)return THY_STATE_UNAVAILABLE;
    base.pressure_floor=median(values[0],learned);base.pressure_full_scale=median(values[1],learned);base.pressure_curve=0u;
    velocity=(thy_instrument_velocity){median(values[2],learned),median(values[3],learned),THY_VELOCITY_STRIKE,0u,1u,127u};
    base.attack_raw=e->calibration_review_attack;
    if(base_policy.linked)base.release_raw=base.attack_raw>base_policy.gap?(uint16_t)(base.attack_raw-base_policy.gap):0u;
    if(thy_instrument_check_cluster_sensitivity_policy(e->instrument,(uint8_t)side,&base,&velocity,&base_policy)!=THY_OK)
        return THY_STATE_BLOCKED;
    for(unsigned p=0;p<96u;++p){
        if(!in_scope(w,e->instrument->mapping[p]))continue;
        thy_instrument_response r;thy_instrument_velocity v;thy_instrument_release_policy policy;
        if(candidate_config(e,p,&r,&v,&policy)<1 ||
           thy_instrument_check_sensitivity_policy(e->instrument,(int)p,&r,&v,&policy)!=THY_OK)return THY_STATE_BLOCKED;
    }
    if(thy_calibration_request_apply(w)!=THY_CALIBRATION_OK)return THY_STATE_UNAVAILABLE;
    /* All checks precede all commits. Capture original values for an unresolved
     * coupled pad before changing the cluster's inherited defaults. */
    for(unsigned p=0;p<96u;++p){
        if(!in_scope(w,e->instrument->mapping[p]))continue;
        thy_instrument_response r;thy_instrument_velocity v;thy_instrument_release_policy policy;
        (void)candidate_config(e,p,&r,&v,&policy);
        unsigned logical=e->instrument->mapping[p];
        const thy_instrument_sensor *s=&e->instrument->sensors[p];
        /* The response commit restarts sensor conditioning. Capture the old
         * measured floor before that reset, so it survives in saved settings. */
        unsigned guard=s->calibration.baseline+2u*(s->warmup_max-s->warmup_min)+4u;
        if(w->leak_floor[logical] && w->leak_floor[logical]+THY_CALIBRATION_LEAK_MARGIN>guard)guard=w->leak_floor[logical]+THY_CALIBRATION_LEAK_MARGIN;
        (void)thy_instrument_set_sensitivity_policy(e->instrument,(int)p,&r,&v,&policy);
        if(!thy_calibration_leak_outlier(w,(uint8_t)logical)){
            (void)thy_instrument_set_pad_guard(e->instrument,(uint8_t)p,(uint16_t)guard);
        }
    }
    (void)thy_instrument_set_cluster_sensitivity_policy(e->instrument,(uint8_t)side,&base,&velocity,&base_policy);
    (void)thy_calibration_apply_result(w,1);e->state.notice=THY_NOTICE_APPLIED;
    thy_engine_calibration_close(e);
    return THY_STATE_OK;
}
static int preview_mode(thy_engine *e,int after)
{
    if(!review(e->calibration))return THY_STATE_UNAVAILABLE;
    if(!quiet(e)){e->state.notice=THY_NOTICE_RELEASE;return THY_STATE_BLOCKED;}
    prepare_review(e);
    return thy_calibration_preview(e->calibration,after)==THY_CALIBRATION_OK?THY_STATE_OK:THY_STATE_BLOCKED;
}
int thy_engine_calibration_navigate(thy_engine *e,uint8_t action)
{
    if(!e || !e->calibration || !e->calibration_open)return THY_STATE_UNAVAILABLE;
    thy_calibration *w=e->calibration;
    if(review(w)){
        if(e->calibration_review_editing){
            if(action==THY_STATE_UP || action==THY_STATE_DOWN){
                int value=e->calibration_review_draft+(action==THY_STATE_UP?1:-1);
                if(value<0)value=0;if(value>100)value=100;
                e->calibration_review_draft=(uint8_t)value;return THY_STATE_OK;
            }
            if(action==THY_STATE_LEFT){e->calibration_review_editing=0u;return THY_STATE_OK;}
            if(action==THY_STATE_RIGHT){
                int result=thy_engine_calibration_set_sensitivity(e,e->calibration_review_draft);
                if(result==THY_STATE_OK)e->calibration_review_editing=0u;return result;
            }
        }
        if(action==THY_STATE_SELECT || action==THY_STATE_LEFT){
            thy_engine_calibration_close(e);return e->calibration_open?THY_STATE_BLOCKED:THY_STATE_OK;
        }
        if(action==THY_STATE_UP || action==THY_STATE_DOWN){
            e->calibration_review_cursor=(uint8_t)((e->calibration_review_cursor+(action==THY_STATE_UP?3u:1u))%4u);
            return THY_STATE_OK;
        }
        if(action!=THY_STATE_RIGHT)return THY_STATE_INVALID;
        /* Direct host action aliases use the same guarded backend. */
        if(w->action_cursor==THY_CALIBRATION_BEFORE){w->action_cursor=THY_CALIBRATION_LEARN;return preview_mode(e,0);}
        if(w->action_cursor==THY_CALIBRATION_AFTER){w->action_cursor=THY_CALIBRATION_LEARN;return preview_mode(e,1);}
        if(w->action_cursor==THY_CALIBRATION_APPLY){w->action_cursor=THY_CALIBRATION_LEARN;return thy_engine_calibration_apply(e);}
        if(w->action_cursor==THY_CALIBRATION_CANCEL){
            if(!quiet(e))return THY_STATE_BLOCKED;thy_calibration_cancel(w);thy_engine_calibration_close(e);return THY_STATE_OK;
        }
        if(e->calibration_review_cursor==0u)return preview_mode(e,w->state!=THY_CALIBRATION_PREVIEW_CANDIDATE);
        if(e->calibration_review_cursor==1u){e->calibration_review_editing=1u;e->calibration_review_draft=w->preview_percent;return THY_STATE_OK;}
        if(e->calibration_review_cursor==2u)return thy_engine_calibration_apply(e);
        if(!quiet(e))return THY_STATE_BLOCKED;thy_calibration_cancel(w);thy_engine_calibration_close(e);return THY_STATE_OK;
    }
    if(action==THY_STATE_SELECT || action==THY_STATE_LEFT){thy_engine_calibration_close(e);return THY_STATE_OK;}
    if(action==THY_STATE_UP || action==THY_STATE_DOWN){
        w->action_cursor=w->action_cursor==THY_CALIBRATION_CANCEL?THY_CALIBRATION_LEARN:THY_CALIBRATION_CANCEL;
        ++w->revision;return THY_STATE_OK;
    }
    if(action!=THY_STATE_RIGHT)return THY_STATE_INVALID;
    if(w->action_cursor==THY_CALIBRATION_CANCEL){thy_calibration_cancel(w);thy_engine_calibration_close(e);return THY_STATE_OK;}
    if(w->action_cursor!=THY_CALIBRATION_LEARN && w->action_cursor!=THY_CALIBRATION_RETRY_PAD)return THY_STATE_UNAVAILABLE;
    if(w->contact_active)return THY_STATE_BLOCKED;
    e->calibration_pick=1u;++w->revision;return THY_STATE_OK;
}
int thy_engine_calibration_pad(thy_engine *e,uint8_t p,int pressed,int *result)
{
    if(!e || !e->calibration || !result || p>=96u)return 0;
    uint8_t bit=(uint8_t)(1u<<(p&7u));
    if(!e->calibration_open && !(e->calibration_contacts[p/8u]&bit))return 0;
    if(e->calibration_open && review(e->calibration) && !e->calibration_pick){
        if(pressed && in_scope(e->calibration,e->instrument->mapping[p]))e->calibration->selected=e->instrument->mapping[p];
        return 0;
    }
    if(e->calibration_open && pressed && e->calibration_pick==1u){
        thy_calibration *w=e->calibration;uint8_t logical=e->instrument->mapping[p];
        if(in_scope(w,logical) && !w->contact_active){
            if(thy_calibration_select(w,logical)==THY_CALIBRATION_OK){
                if(thy_calibration_completed(w,logical))(void)thy_calibration_retry_pad(w);
                e->calibration_pick=2u;e->calibration_pick_pad=p;
                e->calibration_contacts[p/8u]|=bit;
            }
        }
    }
    *result=THY_OK;return 1;
}
void thy_engine_calibration_tick(thy_engine *e)
{
    if(!e || !e->calibration || !e->instrument)return;
    unsigned contacts=0u;for(unsigned b=0;b<12u;++b)contacts|=e->calibration_contacts[b];
    if(!e->calibration_open && !contacts)return;
    thy_calibration *w=e->calibration;
    int electrical=thy_engine_sensitivity_electrical(e,w->target);
    int fresh=electrical>=0 && e->calibration_sample_ms[electrical]!=e->instrument->sensors[electrical].last_sample_ms;
    for(unsigned p=0;p<96u;++p){
        const thy_instrument_sensor *s=&e->instrument->sensors[p];
        if(e->calibration_sample_ms[p]==s->last_sample_ms)continue;
        e->calibration_sample_ms[p]=s->last_sample_ms;
        if(s->warmup_count<THY_INSTRUMENT_WARMUP_SAMPLES)continue;
        uint8_t bit=(uint8_t)(1u<<(p&7u));
        unsigned noise=s->warmup_max-s->warmup_min,baseline=s->calibration.baseline;
        unsigned press=noise*3u+8u;if(press<16u)press=16u;
        unsigned release=noise*2u+4u;if(release<8u)release=8u;
        if(e->calibration_open && s->raw>=baseline+press)e->calibration_contacts[p/8u]|=bit;
        if(s->raw<=baseline+release)e->calibration_contacts[p/8u]&=(uint8_t)~bit;
    }
    if(!e->calibration_open)return;
    if(review(w)){
        if(!e->calibration_review_reference)e->calibration_review_reference=e->instrument->cluster_response[w->scope_first/48u].attack_raw;
        if(!e->calibration_review_attack){e->calibration_review_attack=e->calibration_review_reference;prepare_review(e);}
        if(w->state==THY_CALIBRATION_READY && quiet(e)){
            prepare_review(e);(void)thy_calibration_preview(w,1);
        }
        int selected=thy_engine_sensitivity_electrical(e,w->selected);
        if(selected>=0){w->live_raw=e->instrument->sensors[selected].raw;w->live_rate=e->instrument->sensors[selected].strike_peak_rate;}
        return;
    }
    if(e->calibration_pick==2u){
        unsigned p=e->calibration_pick_pad;
        if(!(e->calibration_contacts[p/8u]&(1u<<(p&7u))) && !e->instrument->sensors[p].held)e->calibration_pick=0u;
        return;
    }
    if(e->calibration_pick || !fresh || electrical<0)return;
    const thy_instrument_sensor *s=&e->instrument->sensors[electrical];
    if(s->warmup_count<THY_INSTRUMENT_WARMUP_SAMPLES)return;
    unsigned baseline=s->calibration.baseline,noise=s->warmup_max-s->warmup_min;
    unsigned signal=s->raw>baseline?s->raw-baseline:0u;
    unsigned press=noise*3u+8u;if(press<16u)press=16u;
    unsigned release=noise*2u+4u;if(release<8u)release=8u;
    int competing=0;
    for(unsigned p=0;p<96u;++p)if(p!=(unsigned)electrical){
        const thy_instrument_sensor *other=&e->instrument->sensors[p];
        unsigned delta=other->raw>other->calibration.baseline?other->raw-other->calibration.baseline:0u;
        unsigned guard=6u*(other->warmup_max-other->warmup_min)+32u;if(guard<128u)guard=128u;
        if(delta>guard && delta>signal+signal/2u+32u){competing=1;break;}
    }
    if(competing){
        if(w->contact_active){w->contact_active=0u;++w->revision;}
        if(signal<=release)e->calibration_rejected=0u;
        else if(signal>=press)e->calibration_rejected=1u;
        w->reason=THY_CALIBRATION_WRONG_PAD;return;
    }
    if(e->calibration_rejected){if(signal<=release)e->calibration_rejected=0u;return;}
    if(!w->contact_active && signal>=press)
        (void)thy_calibration_contact_begin(w,w->target,(uint16_t)baseline,(uint16_t)noise);
    if(w->contact_active){
        (void)thy_calibration_observe_timed(w,w->target,s->raw,s->strike_peak_rate,s->last_sample_ms);
        if(w->stage==THY_CALIBRATION_PRESSURE && signal>=press){
            for(unsigned p=0;p<96u;++p)if(p!=(unsigned)electrical){
                const thy_instrument_sensor *other=&e->instrument->sensors[p];
                unsigned n=other->warmup_max-other->warmup_min;
                if(other->raw>other->calibration.baseline+3u*n+8u)
                    (void)thy_calibration_observe_leak(w,e->instrument->mapping[p],other->raw);
            }
        }
        if(signal<=release){
            (void)thy_calibration_contact_end(w,w->target);
            if(review(w)){e->calibration_review_editing=e->calibration_review_cursor=0u;w->action_cursor=THY_CALIBRATION_LEARN;prepare_review(e);}
        }
    }
}
int thy_engine_calibration_lights(const thy_engine *e,uint32_t now,uint32_t colors[96])
{
    if(!e || !e->calibration_open || !e->calibration || !colors || review(e->calibration))return 0;
    const thy_calibration *w=e->calibration;
    for(unsigned p=0;p<96u;++p){
        colors[p]=!in_scope(w,p)?0u:thy_calibration_completed(w,(uint8_t)p)?UINT32_C(0x080001):UINT32_C(0x010101);
        if(p==w->target)colors[p]=(now/300u)&1u?UINT32_C(0x040800):UINT32_C(0x010200);
        if(w->contact_active && p==w->target)colors[p]=UINT32_C(0x080808);
    }
    return 1;
}
int thy_engine_calibration_value(const thy_engine *e,uint8_t logical,uint16_t raw,
    uint16_t rate,int candidate,int velocity)
{
    if(!e || !e->instrument || !e->calibration || logical>=96u || raw>4095u ||
       rate>4095u || (candidate!=0 && candidate!=1) || (velocity!=0 && velocity!=1))return -1;
    int electrical=thy_engine_sensitivity_electrical(e,logical);
    if(electrical<0)return -1;
    thy_instrument_response r;thy_instrument_velocity v;uint8_t overridden;
    if(candidate){thy_instrument_release_policy policy;
        if(candidate_config(e,(unsigned)electrical,&r,&v,&policy)<1)return -1;
    }else if(thy_instrument_get_pad_response(e->instrument,(uint8_t)electrical,&r,&overridden)!=THY_OK ||
             thy_instrument_get_velocity(e->instrument,electrical,&v,&overridden)!=THY_OK)return -1;
    if(!velocity)return thy_instrument_response_pressure(&r,raw);
    if(v.mode==THY_VELOCITY_STRIKE)return thy_instrument_velocity_value(&v,rate);
    thy_instrument_route route;
    return thy_instrument_get_pad_route(e->instrument,thy_instrument_route_target(e->instrument,logical),&route)==THY_OK?route.velocity:-1;
}
