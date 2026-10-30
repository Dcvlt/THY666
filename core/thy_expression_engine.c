/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_engine.h"

static int valid(const thy_engine *e)
{ return e && e->instrument && e->instrument->core; }

void thy_engine_bind_expression(thy_engine *e, thy_mpe *workspace)
{
    if (!valid(e)) return;
    e->expression = workspace;
    if (workspace) {
        thy_mpe_init(workspace, e->instrument->core);
        (void)thy_mpe_configure(workspace, &e->expression_config);
    }
    e->expression_service_ms = e->instrument->core->now_ms;
}

int thy_engine_expression_can_configure(const thy_engine *e, const thy_mpe_config *c)
{
    if (!valid(e) || !thy_mpe_config_valid(c)) return THY_INVALID_ARGUMENT;
    const thy_core *core = e->instrument->core;
    if (c->enabled && !e->expression) return THY_ENGINE_SOURCE_NOT_READY;
    if (c->enabled && !thy_engine_channel_quiet(e)) return THY_SEQ_BUSY;
    if (thy_core_sounding(core) || thy_core_pending(core) || thy_core_generated_count(core) || core->sustain_count)
        return THY_SEQ_BUSY;
    if (e->expression && thy_mpe_active(e->expression)) return THY_SEQ_BUSY;
    for (unsigned p=0; p<THY_PAD_COUNT; ++p)
        if (thy_core_pad_held(core,(uint8_t)p) || e->instrument->sensors[p].held ||
            (e->expression && thy_mpe_consumed(e->expression,(uint8_t)p)))
            return THY_SEQ_BUSY;
    if (c->enabled && (e->arps[0].config.enabled || e->arps[1].config.enabled ||
        e->rhythm_transport.state == THY_TRANSPORT_PLAYING ||
        e->sequencer.capture.state != THY_SEQ_CAPTURE_OFF ||
        e->sequencer_ui.open || e->calibration_open)) return THY_SEQ_BUSY;
    return THY_OK;
}

int thy_engine_expression_configure(thy_engine *e, const thy_mpe_config *c)
{
    int r=thy_engine_expression_can_configure(e,c);
    if (r!=THY_OK) return r;
    if (e->expression) {
        r=thy_mpe_configure(e->expression,c);
        if (r!=THY_OK) return r;
    }
    e->expression_config=*c;
    ++e->instrument->revision;
    return THY_OK;
}

int thy_engine_expression_send_setup(thy_engine *e)
{
    if (!valid(e) || !e->expression || !e->expression_config.enabled)
        return THY_ENGINE_SOURCE_NOT_READY;
    int r=thy_engine_expression_can_configure(e,&e->expression_config);
    if (r!=THY_OK) return r;
    uint8_t destinations=0;
    for (unsigned p=0;p<THY_PAD_COUNT;++p) {
        thy_instrument_route route;
        uint8_t target=thy_instrument_route_target(e->instrument,(uint8_t)p);
        if (thy_instrument_get_pad_route(e->instrument,target,&route)!=THY_OK)
            return THY_INVALID_ARGUMENT;
        destinations |= route.destinations;
    }
    return thy_mpe_send_configuration(e->expression,destinations);
}

int thy_engine_expression_pressure(thy_engine *e, uint8_t pad, uint8_t value)
{
    return thy_engine_expression_pressure_axes(e,pad,value,value);
}

int thy_engine_expression_pressure_axes(thy_engine *e,uint8_t pad,uint8_t pressure,uint8_t aftertouch)
{
    if (!valid(e) || pad>=THY_PAD_COUNT || pressure>127 || aftertouch>127) return THY_INVALID_ARGUMENT;
    if (e->expression_config.enabled && e->expression) {
        int result=thy_mpe_pressure(e->expression,pad,pressure);
        thy_expression_profile profile;uint8_t custom;
        if(result==THY_OK && thy_instrument_get_expression_profile(e->instrument,pad,&profile,&custom)==THY_OK && profile.enabled)
            result=thy_mpe_timbre(e->expression,pad,aftertouch);
        return result;
    }
    if(e->channel_expression) {
        int r=thy_channel_expression_pressure_axes(e->channel_expression,pad,aftertouch,pressure);
        if(r!=THY_OK)return r;
        return (e->channel_expression->flags[pad]&3u)==THY_CHANNEL_PRESSURE_POLY?
            thy_core_pressure(e->instrument->core,pad,aftertouch):THY_OK;
    }
    return e->instrument->held_pressure_enabled[pad]?
        thy_core_pressure(e->instrument->core,pad,aftertouch):THY_OK;
}

int thy_engine_expression_bend(thy_engine *e, uint8_t pad, uint16_t value)
{
    if (!valid(e) || pad>=THY_PAD_COUNT || value>16383) return THY_INVALID_ARGUMENT;
    if (!e->expression_config.enabled || !e->expression) return THY_ENGINE_SOURCE_NOT_READY;
    return thy_mpe_bend(e->expression,pad,value);
}

void thy_engine_expression_tick(thy_engine *e, uint32_t now)
{
    if (!valid(e) || now-e->expression_service_ms<8u) return;
    e->expression_service_ms=now;
    /* One bounded service, never replay a backlog after a long host stall. */
    if(e->expression)(void)thy_mpe_service(e->expression);
    if(e->channel_expression)(void)thy_channel_expression_service(e->channel_expression);
}

static int generated_expression_guard(void *context,uint8_t channel,uint8_t destinations)
{
    thy_channel_expression *m=context;
    int r=thy_channel_expression_prepare_attack(m,channel,destinations,0u);
    if(r==THY_OK)thy_channel_expression_commit_prepared(m,channel,destinations,0u);
    return r;
}
void thy_engine_bind_channel_expression(thy_engine *e,thy_channel_expression *m)
{
    if(!valid(e))return;
    e->channel_expression=m;
    if(m)thy_channel_expression_init(m,e->instrument->core);
    thy_core_set_generated_guard(e->instrument->core,m?generated_expression_guard:NULL,m);
}
int thy_engine_channel_quiet(const thy_engine *e)
{
    if(!valid(e))return 0;
    const thy_channel_expression *m=e->channel_expression;
    if(!m)return 1;
    for(unsigned p=0;p<96u;++p)if(m->flags[p]&&e->instrument->core->owners[p].held)return 0;
    for(unsigned r=0;r<48u;++r)if(m->sent_pressure[r] || m->sent_bend[r]!=8192u)return 0;
    return 1;
}
uint8_t thy_engine_channel_aftertouch(const thy_engine *e,uint8_t target)
{
    if(!valid(e)||target>=9u)return UINT8_MAX;
    if(e->channel_routes[target].aftertouch)return (uint8_t)(e->channel_routes[target].aftertouch-1u);
    thy_instrument_route route;
    return thy_instrument_get_pad_route(e->instrument,target,&route)==THY_OK?
        (uint8_t)(route.pressure_enabled?THY_CHANNEL_PRESSURE_POLY:THY_CHANNEL_PRESSURE_OFF):UINT8_MAX;
}
int thy_engine_channel_configure(thy_engine *e,uint8_t target,const thy_engine_channel_config *c)
{
    if(!valid(e)||!c||target>=9u||c->aftertouch>3u||c->bend>2u||c->range<1u||c->range>24u)return THY_INVALID_ARGUMENT;
    e->channel_routes[target]=*c;++e->instrument->revision;return THY_OK;
}
int thy_engine_channel_send_setup(thy_engine *e)
{
    if(!valid(e)||e->route_target>=9u)return THY_INVALID_ARGUMENT;
    thy_core *core=e->instrument->core;
    if(e->expression_config.enabled||!thy_engine_channel_quiet(e)||core->sounding_count||core->pending_keys||core->sustain_count)return THY_SEQ_BUSY;
    for(unsigned p=0;p<96u;++p)if(core->owners[p].held||e->instrument->sensors[p].held)return THY_SEQ_BUSY;
    thy_instrument_route route;
    if(thy_instrument_get_pad_route(e->instrument,e->route_target,&route)!=THY_OK)return THY_INVALID_ARGUMENT;
    const uint8_t cc[]={101,100,6,38,101,100},values[]={0,0,e->channel_routes[e->route_target].range,0,127,127};
    thy_midi_event events[18];unsigned count=0;
    for(unsigned d=0;d<3;++d)if(route.destinations&(1u<<d))for(unsigned n=0;n<6;++n)
        events[count++]=(thy_midi_event){(uint8_t)d,(uint8_t)(0xb0u|route.channel),cc[n],values[n]};
    return thy_core_emit_batch(core,events,count);
}
int thy_engine_profile_get(const thy_engine *e,uint8_t instance,thy_expression_profile *p)
{
    if(!valid(e)||!p)return THY_INVALID_ARGUMENT;
    int target=thy_engine_sensitivity_electrical(e,instance);
    if(target==-1||target==-3){*p=e->instrument->expression_profiles[target==-3];return THY_OK;}
    uint8_t custom;return target>=0?thy_instrument_get_expression_profile(e->instrument,(uint8_t)target,p,&custom):THY_INVALID_ARGUMENT;
}
int thy_engine_profile_configure(thy_engine *e,uint8_t instance,const thy_expression_profile *p)
{
    if(!valid(e))return THY_INVALID_ARGUMENT;
    int target=thy_engine_sensitivity_electrical(e,instance);
    return target==-1||target==-3?thy_instrument_set_cluster_expression_profile(e->instrument,(uint8_t)(target==-3),p):
        target>=0?thy_instrument_set_pad_expression_profile(e->instrument,(uint8_t)target,p):THY_INVALID_ARGUMENT;
}

uint8_t thy_engine_performance_route(const thy_engine *e,uint8_t pad)
{
    if(!valid(e)||pad>=THY_PAD_COUNT||e->instrument->mapping[pad]>=THY_PAD_COUNT)return UINT8_MAX;
    unsigned logical=thy_instrument_role_pad(e->instrument,e->instrument->mapping[pad]);
    int input=thy_sequencer_ui_input(&e->sequencer_ui,(uint8_t)logical);
    if(!input&&(e->state.level==THY_STATE_PLAY||e->sequencer_ui.options_open)){
        uint8_t track,pitch;uint32_t tick;
        int role=thy_sequencer_ui_pad_role(&e->sequencer_ui,(uint8_t)logical,&track,&tick,&pitch);
        input=role==THY_SEQ_PAD_INPUT_NOTES?1:role==THY_SEQ_PAD_INPUT_CHORDS?2:0;
    }
    unsigned source=input?logical%48u+(input==1?48u:0u):logical;
    return thy_instrument_route_target(e->instrument,(uint8_t)source);
}

thy_engine_channel_config thy_engine_performance_response(const thy_engine *e,uint8_t pad,uint8_t target)
{
    thy_engine_channel_config c={0};
    if(!valid(e)||pad>=THY_PAD_COUNT||target>=THY_INSTRUMENT_ROUTE_TARGETS||
       e->instrument->mapping[pad]>=THY_PAD_COUNT)return c;
    const thy_engine_performance_config *p=&e->performance[e->instrument->mapping[pad]/48u];
    c=e->channel_routes[target];
    c.aftertouch=p->aftertouch?(uint8_t)(p->aftertouch-1u):thy_engine_channel_aftertouch(e,target);
    if(p->bend)c.bend=(uint8_t)(p->bend-1u);
    if(p->range)c.range=p->range;
    return c;
}

/* Inspect effective values, including per-pad overrides and inherited routes.
 * The scope is always physical: swapping musical roles cannot swap calibration. */
int thy_engine_performance_get(const thy_engine *e,uint16_t row,uint8_t scope,int32_t *value)
{
    if(!valid(e)||!value||scope>2u)return THY_STATE_INVALID;
    unsigned mask=scope?1u<<(scope-1u):3u;
    int have=0;
    for(unsigned pad=0;pad<THY_PAD_COUNT;++pad){
        const thy_instrument *i=e->instrument;
        if(i->mapping[pad]>=THY_PAD_COUNT)return THY_STATE_INVALID;
        unsigned side=i->mapping[pad]/48u;
        if(!(mask&(1u<<side)))continue;
        int32_t current;
        if(row==THY_ROW_SENSITIVITY_VELOCITYMODE){
            thy_instrument_velocity v;uint8_t custom;
            if(thy_instrument_get_velocity(i,(int)pad,&v,&custom)!=THY_OK)return THY_STATE_INVALID;
            current=v.mode;
        }else if(row==THY_ROW_PERFORMANCE_PROFILEENABLED){
            thy_expression_profile p;uint8_t custom;
            if(thy_instrument_get_expression_profile(i,(uint8_t)pad,&p,&custom)!=THY_OK)return THY_STATE_INVALID;
            current=p.enabled;
        }else if(row==THY_ROW_PERFORMANCE_MPEOUTPUT)current=e->expression_config.pressure_output[side];
        else {
            uint8_t target=thy_engine_performance_route(e,(uint8_t)pad);
            thy_engine_channel_config response=thy_engine_performance_response(e,(uint8_t)pad,target);
            if(row==THY_ROW_PERFORMANCE_VELOCITY){
                thy_instrument_route route;
                if(thy_instrument_get_pad_route(i,target,&route)!=THY_OK)return THY_STATE_INVALID;
                current=e->performance[side].velocity?e->performance[side].velocity:route.velocity;
            }else if(row==THY_ROW_PERFORMANCE_PRESSUREMODE)current=response.aftertouch;
            else if(row==THY_ROW_PERFORMANCE_PRESSUREBEND)current=response.bend;
            else if(row==THY_ROW_PERFORMANCE_BENDRANGE)current=response.range;
            else return THY_STATE_UNAVAILABLE;
        }
        if(!have){*value=current;have=1;}
        else if(*value!=current){*value=row==THY_ROW_SENSITIVITY_VELOCITYMODE?2:-1;return THY_STATE_OK;}
    }
    return have?THY_STATE_OK:THY_STATE_INVALID;
}

static int performance_bend_setup(thy_engine *e,unsigned mask)
{
    thy_core *core=e->instrument->core;
    if(e->expression_config.enabled||!thy_engine_channel_quiet(e)||core->sounding_count||
       core->pending_keys||core->sustain_count)return THY_STATE_BLOCKED;
    uint8_t ranges[THY_DEST_COUNT][16]={{0}};
    for(unsigned pad=0;pad<THY_PAD_COUNT;++pad){
        const thy_instrument *i=e->instrument;
        if(core->owners[pad].held||i->sensors[pad].held)return THY_STATE_BLOCKED;
        if(i->mapping[pad]>=THY_PAD_COUNT)return THY_STATE_INVALID;
        if(!(mask&(1u<<(i->mapping[pad]/48u))))continue;
        uint8_t target=thy_engine_performance_route(e,(uint8_t)pad);
        thy_instrument_route route;
        if(thy_instrument_get_pad_route(i,target,&route)!=THY_OK)return THY_STATE_INVALID;
        uint8_t range=thy_engine_performance_response(e,(uint8_t)pad,target).range;
        for(unsigned d=0;d<THY_DEST_COUNT;++d)if(route.destinations&(1u<<d)){
            if(ranges[d][route.channel]&&ranges[d][route.channel]!=range)return THY_STATE_INVALID;
            ranges[d][route.channel]=range;
        }
    }
    /* At most nine routes, each with three destinations. The whole setup is
     * enqueued atomically, with no receiver changes on a conflicting range. */
    thy_midi_event events[THY_INSTRUMENT_ROUTE_TARGETS*THY_DEST_COUNT*6u];unsigned count=0;
    static const uint8_t cc[]={101,100,6,38,101,100};
    for(unsigned d=0;d<THY_DEST_COUNT;++d)for(unsigned ch=0;ch<16u;++ch)if(ranges[d][ch]){
        if(count+6u>sizeof(events)/sizeof(events[0]))return THY_STATE_INVALID;
        const uint8_t values[]={0,0,ranges[d][ch],0,127,127};
        for(unsigned n=0;n<6u;++n)events[count++]=(thy_midi_event){(uint8_t)d,(uint8_t)(0xb0u|ch),cc[n],values[n]};
    }
    return thy_core_emit_batch(core,events,count)==THY_OK?THY_STATE_OK:THY_STATE_BLOCKED;
}

int thy_engine_performance_set(thy_engine *e,uint16_t row,uint8_t scope,int32_t value)
{
    if(!valid(e)||scope>2u)return THY_STATE_INVALID;
    unsigned mask=scope?1u<<(scope-1u):3u;
    if(row==THY_ROW_PERFORMANCE_BENDSETUP)return performance_bend_setup(e,mask);
    if(row==THY_ROW_PERFORMANCE_MPEOUTPUT){
        if(value<0||value>THY_MPE_CC1)return THY_STATE_INVALID;
        thy_mpe_config config=e->expression_config;
        for(unsigned side=0;side<2u;++side)if(mask&(1u<<side))config.pressure_output[side]=(uint8_t)value;
        int r=thy_engine_expression_configure(e,&config);
        return r==THY_OK?THY_STATE_OK:r==THY_INVALID_ARGUMENT?THY_STATE_INVALID:THY_STATE_BLOCKED;
    }
    if(row==THY_ROW_PERFORMANCE_PROFILEENABLED){
        if(value<0||value>1)return THY_STATE_INVALID;
        thy_instrument *i=e->instrument;
        /* Preflight every selected physical contact before publishing either
         * half. Preserve each pad's own split, curves and override identity. */
        for(unsigned pad=0;pad<THY_PAD_COUNT;++pad){
            if(i->mapping[pad]>=THY_PAD_COUNT)return THY_STATE_INVALID;
            if(!(mask&(1u<<(i->mapping[pad]/48u))))continue;
            thy_expression_profile p;uint8_t custom;
            if(thy_instrument_get_expression_profile(i,(uint8_t)pad,&p,&custom)!=THY_OK)return THY_STATE_INVALID;
            p.enabled=(uint8_t)value;
            if(thy_instrument_check_expression_profile(i,(uint8_t)pad,&p)!=THY_OK)return THY_STATE_BLOCKED;
        }
        for(unsigned pad=0;pad<THY_PAD_COUNT;++pad)
            if((mask&(1u<<(i->mapping[pad]/48u)))&&i->expression_profile_overridden[pad])
                i->pad_expression_profiles[pad].enabled=(uint8_t)value;
        for(unsigned side=0;side<2u;++side)if(mask&(1u<<side))i->expression_profiles[side].enabled=(uint8_t)value;
    }else {
        if(row==THY_ROW_PERFORMANCE_VELOCITY){if(value<1||value>127)return THY_STATE_INVALID;}
        else if(row==THY_ROW_PERFORMANCE_PRESSUREMODE||row==THY_ROW_PERFORMANCE_PRESSUREBEND){if(value<0||value>2)return THY_STATE_INVALID;}
        else if(row==THY_ROW_PERFORMANCE_BENDRANGE){if(value<1||value>24)return THY_STATE_INVALID;}
        else return THY_STATE_UNAVAILABLE;
        for(unsigned side=0;side<2u;++side)if(mask&(1u<<side)){
            thy_engine_performance_config *p=&e->performance[side];
            if(row==THY_ROW_PERFORMANCE_VELOCITY)p->velocity=(uint8_t)value;
            else if(row==THY_ROW_PERFORMANCE_PRESSUREMODE)p->aftertouch=(uint8_t)(value+1);
            else if(row==THY_ROW_PERFORMANCE_PRESSUREBEND)p->bend=(uint8_t)(value+1);
            else p->range=(uint8_t)value;
        }
    }
    ++e->instrument->revision;return THY_STATE_OK;
}
