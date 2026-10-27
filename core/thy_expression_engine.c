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
