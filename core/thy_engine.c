/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_engine.h"

/* The menu preserves its approved option order; pad planning has stable IDs. */
static const uint8_t layout_from_menu[] = {0u,4u,7u,1u,8u,5u,2u,3u,6u};
static const uint8_t menu_from_layout[] = {0u,3u,6u,7u,1u,5u,8u,2u,4u};
static const uint8_t route_masks[] = {5u,4u,1u,2u,3u,6u,7u};
static const uint32_t arp_rates[] = {3840u,1920u,1280u,960u,640u,480u,320u,240u,120u};
static const uint8_t meter_units[] = {1u,2u,4u,8u,16u,32u};

static int read_arp(const thy_engine *engine, uint16_t row, uint8_t instance,
    int32_t *value)
{
    if (instance >= 2u) return THY_STATE_INVALID;
    const thy_arp_config *config = &engine->arps[instance].config;
    if (row >= THY_ROW_ARPS_STEP1 && row <= THY_ROW_ARPS_STEP8) {
        *value = !!(config->step_mask & (1u << (row - THY_ROW_ARPS_STEP1)));
        return THY_STATE_OK;
    }
    if (row >= THY_ROW_ARPS_OFFSET1 && row <= THY_ROW_ARPS_OFFSET8) {
        *value = config->offsets[row - THY_ROW_ARPS_OFFSET1]; return THY_STATE_OK;
    }
    if (row >= THY_ROW_ARPS_SOURCEZONE1 && row <= THY_ROW_ARPS_SOURCEZONE6) {
        *value = !!(config->source_zones & (1u << (row - THY_ROW_ARPS_SOURCEZONE1)));
        return THY_STATE_OK;
    }
    switch (row) {
    case THY_ROW_ARPS_ENABLED: *value = config->enabled; break;
    case THY_ROW_ARPS_TYPE: *value = config->type; break;
    case THY_ROW_ARPS_GROUPING: *value = config->grouping; break;
    case THY_ROW_ARPS_SOURCEZONES: *value = config->source_zones; break;
    case THY_ROW_ARPS_LAYER: *value = config->layer; break;
    case THY_ROW_ARPS_RATETICKS: *value = (int32_t)config->rate_ticks; break;
    case THY_ROW_ARPS_NOTEVALUE:
        *value = (int32_t)(sizeof arp_rates / sizeof arp_rates[0]);
        for (unsigned n = 0u; n < sizeof arp_rates / sizeof arp_rates[0]; ++n)
            if (arp_rates[n] == config->rate_ticks) *value = (int32_t)n;
        break;
    case THY_ROW_ARPS_SYNC: *value = config->start; break;
    case THY_ROW_ARPS_RESTART: *value = config->restart_on_change; break;
    case THY_ROW_ARPS_PATTERN: *value = config->pattern; break;
    case THY_ROW_ARPS_GATE: *value = config->gate_percent; break;
    case THY_ROW_ARPS_SWING: *value = config->swing_percent; break;
    case THY_ROW_ARPS_CHANNEL: *value = config->channel + 1; break;
    case THY_ROW_ARPS_DESTINATIONS:
        for (unsigned n = 0u; n < sizeof route_masks; ++n)
            if (route_masks[n] == config->destinations) { *value = (int32_t)n; return THY_STATE_OK; }
        return THY_STATE_INVALID;
    default: return THY_STATE_UNAVAILABLE;
    }
    return THY_STATE_OK;
}

static int write_arp(thy_engine *engine, uint16_t row, uint8_t instance,
    int32_t value)
{
    if (instance >= 2u) return THY_STATE_INVALID;
    if (row == THY_ROW_ARPS_CLEAR)
        return thy_engine_arp_clear(engine, instance) == THY_OK ? THY_STATE_OK : THY_STATE_ADAPTER_ERROR;
    thy_arp_config config = engine->arps[instance].config;
    if (row >= THY_ROW_ARPS_STEP1 && row <= THY_ROW_ARPS_STEP8) {
        uint8_t bit = (uint8_t)(1u << (row - THY_ROW_ARPS_STEP1));
        config.step_mask = value ? (uint8_t)(config.step_mask | bit) : (uint8_t)(config.step_mask & (uint8_t)~bit);
    } else if (row >= THY_ROW_ARPS_OFFSET1 && row <= THY_ROW_ARPS_OFFSET8)
        config.offsets[row - THY_ROW_ARPS_OFFSET1] = (int8_t)value;
    else if (row >= THY_ROW_ARPS_SOURCEZONE1 && row <= THY_ROW_ARPS_SOURCEZONE6) {
        uint8_t bit = (uint8_t)(1u << (row - THY_ROW_ARPS_SOURCEZONE1));
        config.source_zones = value ? (uint8_t)(config.source_zones | bit) : (uint8_t)(config.source_zones & (uint8_t)~bit);
    } else switch (row) {
    case THY_ROW_ARPS_ENABLED: config.enabled = (uint8_t)value; break;
    case THY_ROW_ARPS_TYPE: config.type = (uint8_t)value; break;
    case THY_ROW_ARPS_GROUPING: config.grouping = (uint8_t)value; break;
    case THY_ROW_ARPS_SOURCEZONES: config.source_zones = (uint8_t)value; break;
    case THY_ROW_ARPS_LAYER: config.layer = (uint8_t)value; break;
    case THY_ROW_ARPS_RATETICKS: config.rate_ticks = (uint32_t)value; break;
    case THY_ROW_ARPS_NOTEVALUE:
        if (value >= 0 && value < (int32_t)(sizeof arp_rates / sizeof arp_rates[0])) config.rate_ticks = arp_rates[value];
        else if (value != (int32_t)(sizeof arp_rates / sizeof arp_rates[0])) return THY_STATE_INVALID;
        break;
    case THY_ROW_ARPS_SYNC: config.start = (uint8_t)value; break;
    case THY_ROW_ARPS_RESTART: config.restart_on_change = (uint8_t)value; break;
    case THY_ROW_ARPS_PATTERN: config.pattern = (uint8_t)value; break;
    case THY_ROW_ARPS_GATE: config.gate_percent = (uint8_t)value; break;
    case THY_ROW_ARPS_SWING: config.swing_percent = (uint8_t)value; break;
    case THY_ROW_ARPS_CHANNEL: config.channel = (uint8_t)(value - 1); break;
    case THY_ROW_ARPS_DESTINATIONS:
        if (value < 0 || value >= (int32_t)sizeof route_masks) return THY_STATE_INVALID;
        config.destinations = route_masks[value]; break;
    default: return THY_STATE_UNAVAILABLE;
    }
    return thy_engine_arp_configure(engine, instance, &config) == THY_OK ? THY_STATE_OK : THY_STATE_ADAPTER_ERROR;
}

static int read_view(const thy_instrument *i, uint16_t row, uint8_t instance, int32_t *value)
{
    if (instance >= THY_VIEW_COUNT) return THY_STATE_INVALID;
    const thy_view_config *config = &i->display_config[instance];
    if (row >= THY_ROW_VIEWS_DETAIL1 && row <= THY_ROW_VIEWS_DETAIL6)
        *value = config->detail_rows[row - THY_ROW_VIEWS_DETAIL1];
    else switch (row) {
    case THY_ROW_VIEWS_SLOT1: *value = config->slots[0]; break;
    case THY_ROW_VIEWS_SLOT2: *value = config->slots[1]; break;
    case THY_ROW_VIEWS_HEADER: *value = config->header; break;
    case THY_ROW_VIEWS_MOTION: *value = config->motion; break;
    default: return THY_STATE_UNAVAILABLE;
    }
    return THY_STATE_OK;
}

static int write_view(thy_instrument *i, uint16_t row, uint8_t instance, int32_t value)
{
    if (instance >= THY_VIEW_COUNT || value < 0) return THY_STATE_INVALID;
    thy_view_config config = i->display_config[instance];
    if (row >= THY_ROW_VIEWS_DETAIL1 && row <= THY_ROW_VIEWS_DETAIL6)
        config.detail_rows[row - THY_ROW_VIEWS_DETAIL1] = (uint8_t)value;
    else switch (row) {
    case THY_ROW_VIEWS_SLOT1: config.slots[0] = (uint8_t)value; break;
    case THY_ROW_VIEWS_SLOT2: config.slots[1] = (uint8_t)value; break;
    case THY_ROW_VIEWS_HEADER: config.header = (uint8_t)value; break;
    case THY_ROW_VIEWS_MOTION: config.motion = (uint8_t)value; break;
    default: return THY_STATE_UNAVAILABLE;
    }
    if (value > 255 || !thy_view_config_valid(&config)) return THY_STATE_INVALID;
    i->display_config[instance] = config; ++i->revision;
    return THY_STATE_OK;
}

int thy_engine_workspace(thy_engine *engine, uint8_t workspace)
{
    if (!engine || !engine->instrument || !thy_play_engine_available(workspace)) return THY_STATE_UNAVAILABLE;
    if (workspace == THY_PLAY_ENGINE_SEQUENCER)
        return thy_engine_sequencer_open(engine, THY_SEQ_VIEW_TRACKS);
    if (engine->sequencer_ui.open) return thy_engine_sequencer_close(engine);
    thy_instrument *i = engine->instrument;
    if (i->engine_kind == workspace) {
        engine->session.playing_mode = workspace;
        return THY_STATE_OK;
    }
    i->engine_kind = workspace;
    engine->session.playing_mode = workspace;
    ++i->revision;
    return THY_STATE_OK;
}

int thy_engine_sensitivity_electrical(const thy_engine *e,uint8_t instance)
{
    if(!e || !e->instrument || instance>97u)return -2;
    if(instance==96u)return -1;
    if(instance==97u)return -3;
    for(unsigned pad=0;pad<96u;++pad)
        if(e->instrument->mapping[pad]==instance)return (int)pad;
    return -2;
}

int thy_engine_sensitivity_raw_bounds(const thy_engine *e,uint8_t instance,int32_t *low,int32_t *high)
{
    int target=thy_engine_sensitivity_electrical(e,instance);
    if(target== -2 || !low || !high)return THY_STATE_INVALID;
    const thy_instrument *i=e->instrument;
    unsigned side=target== -3?1u:target<0?0u:instance/48u;
    thy_instrument_release_policy policy;uint8_t overridden;
    if((target<0?thy_instrument_get_cluster_release_policy(i,(uint8_t)side,&policy):
        thy_instrument_get_release_policy(i,target,&policy,&overridden))!=THY_OK)return THY_STATE_ADAPTER_ERROR;
    int32_t lower=policy.linked?1:(target<0?i->cluster_response[side].release_raw:i->sensors[target].response.release_raw)+1;
    int32_t upper=4095;
    unsigned first=target<0?0u:(unsigned)target,last=target<0?96u:(unsigned)target+1u;
    for(unsigned p=first;p<last;++p){
        if(target<0 && i->mapping[p]/48u!=side)continue;
        const thy_instrument_sensor *s=&i->sensors[p];
        if(thy_instrument_get_release_policy(i,(int)p,&policy,&overridden)!=THY_OK)return THY_STATE_ADAPTER_ERROR;
        int32_t safe=policy.linked?1:(int32_t)s->response.release_raw+1;
        int32_t floor=i->sensitivity_guard[p];
        if(s->warmup_count>=THY_INSTRUMENT_WARMUP_SAMPLES){
            int32_t measured=(int32_t)s->calibration.baseline+2*(int32_t)(s->warmup_max-s->warmup_min)+4;
            if(measured>floor)floor=measured;
        }
        if(policy.linked)safe=floor+policy.gap;
        else if(floor+1>safe)safe=floor+1;
        if(target<0 && s->response_overridden){
            uint32_t global=i->cluster_response[side].attack_raw,pad=s->response.attack_raw;
            if(!global || !pad)return THY_STATE_ADAPTER_ERROR;
            safe=(int32_t)(((uint32_t)safe*global+pad-1u)/pad);
            int32_t limit=(int32_t)(4095u*global/pad);if(limit<upper)upper=limit;
        }
        if(safe>lower)lower=safe;
    }
    if(lower<1)lower=1;if(upper>4095)upper=4095;
    if(lower>upper)return THY_STATE_INVALID;
    *low=lower;*high=upper;return THY_STATE_OK;
}

static int sensitivity_percent(const thy_engine *e,uint8_t instance,uint16_t attack,int32_t *value)
{
    int32_t low,high;int result=thy_engine_sensitivity_raw_bounds(e,instance,&low,&high);
    if(result!=THY_STATE_OK)return result;
    int32_t raw=attack;if(raw<low)raw=low;if(raw>high)raw=high;
    *value=high==low?100:((high-raw)*100+(high-low)/2)/(high-low);
    return THY_STATE_OK;
}

/* Performance scopes never borrow the calibration target. Mixed describes
 * actual effective modes, including retained per-pad overrides. */
static int read_velocity_mode(const thy_engine *e,uint8_t scope,int32_t *value)
{
    if(scope>2u)return THY_STATE_INVALID;
    const thy_instrument *i=e->instrument;
    unsigned mask=scope?1u<<(scope-1u):3u;
    *value=-1;
    for(unsigned p=0;p<THY_PAD_COUNT;++p){
        if(i->mapping[p]>=THY_PAD_COUNT)return THY_STATE_INVALID;
        if(mask&(1u<<(i->mapping[p]/48u))){
            thy_instrument_velocity velocity;uint8_t overridden;
            if(thy_instrument_get_velocity(i,(int)p,&velocity,&overridden)!=THY_OK)return THY_STATE_INVALID;
            if(*value<0)*value=velocity.mode;
            else if(velocity.mode!=*value)*value=2;
        }
    }
    return THY_STATE_OK;
}

static int write_velocity_mode(thy_engine *e,uint8_t scope,int32_t value)
{
    if(scope>2u || value<0 || value>1)return THY_STATE_INVALID;
    thy_instrument *i=e->instrument;
    unsigned mask=scope?1u<<(scope-1u):3u;
    thy_instrument_velocity defaults[2];
    /* Preflight the entire union before changing either half. No detector,
     * curve, rate, range or override ownership is changed by this operation. */
    for(unsigned side=0;side<2u;++side)if(mask&(1u<<side)){
        thy_instrument_response response;thy_instrument_release_policy policy;
        if(thy_instrument_get_cluster_velocity(i,(uint8_t)side,&defaults[side])!=THY_OK ||
           thy_instrument_get_cluster_response(i,(uint8_t)side,&response)!=THY_OK ||
           thy_instrument_get_cluster_release_policy(i,(uint8_t)side,&policy)!=THY_OK)return THY_STATE_INVALID;
        defaults[side].mode=(uint8_t)value;
        if(thy_instrument_check_cluster_sensitivity_policy(i,(uint8_t)side,&response,&defaults[side],&policy)!=THY_OK)
            return THY_STATE_BLOCKED;
    }
    for(unsigned p=0;p<THY_PAD_COUNT;++p){
        if(i->mapping[p]>=THY_PAD_COUNT)return THY_STATE_INVALID;
        if(!(mask&(1u<<(i->mapping[p]/48u))))continue;
        thy_instrument_response response;thy_instrument_velocity velocity;
        thy_instrument_release_policy policy;uint8_t custom;
        if(thy_instrument_get_pad_response(i,(uint8_t)p,&response,&custom)!=THY_OK ||
           thy_instrument_get_velocity(i,(int)p,&velocity,&custom)!=THY_OK ||
           thy_instrument_get_release_policy(i,(int)p,&policy,&custom)!=THY_OK)return THY_STATE_INVALID;
        velocity.mode=(uint8_t)value;
        if(thy_instrument_check_sensitivity_policy(i,(int)p,&response,&velocity,&policy)!=THY_OK)return THY_STATE_BLOCKED;
    }
    /* Serialized foreground access plus the full preflight guarantees these
     * velocity-only commits. Clearing strike state cannot invalidate a later
     * commit; captured musical owners retain their original attack velocity. */
    for(unsigned p=0;p<THY_PAD_COUNT;++p)if((mask&(1u<<(i->mapping[p]/48u))) && i->velocity_overridden[p]){
        thy_instrument_velocity velocity=i->pad_velocity[p];velocity.mode=(uint8_t)value;
        (void)thy_instrument_set_pad_velocity(i,(uint8_t)p,&velocity);
    }
    for(unsigned side=0;side<2u;++side)if(mask&(1u<<side))
        (void)thy_instrument_set_cluster_velocity(i,(uint8_t)side,&defaults[side]);
    return THY_STATE_OK;
}

static int read_pad_setting(const thy_engine *e,uint16_t row,uint8_t instance,int32_t *value)
{
    int electrical=thy_engine_sensitivity_electrical(e,instance);
    if(electrical== -2)return THY_STATE_INVALID;
    if(row==THY_ROW_SENSITIVITY_PROFILEENABLED || row==THY_ROW_SENSITIVITY_PROFILESPLIT ||
       row==THY_ROW_SENSITIVITY_PRESSURECURVE || row==THY_ROW_SENSITIVITY_AFTERTOUCHCURVE) {
        thy_expression_profile profile;
        if(thy_engine_profile_get(e,instance,&profile)!=THY_OK)return THY_STATE_INVALID;
        *value=row==THY_ROW_SENSITIVITY_PROFILEENABLED?profile.enabled:
            row==THY_ROW_SENSITIVITY_PROFILESPLIT?profile.split:
            row==THY_ROW_SENSITIVITY_PRESSURECURVE?profile.pressure_curve:profile.aftertouch_curve;
        return THY_STATE_OK;
    }
    unsigned side=electrical== -3?1u:0u;
    thy_instrument_response response;
    thy_instrument_velocity velocity;
    uint8_t overridden;
    if((electrical<0?thy_instrument_get_cluster_response(e->instrument,(uint8_t)side,&response):
        thy_instrument_get_pad_response(e->instrument,(uint8_t)electrical,&response,&overridden))!=THY_OK ||
       (electrical<0?thy_instrument_get_cluster_velocity(e->instrument,(uint8_t)side,&velocity):
        thy_instrument_get_velocity(e->instrument,electrical,&velocity,&overridden))!=THY_OK)return THY_STATE_INVALID;
    thy_instrument_release_policy release_policy;
    if((electrical<0?thy_instrument_get_cluster_release_policy(e->instrument,(uint8_t)side,&release_policy):
        thy_instrument_get_release_policy(e->instrument,electrical,&release_policy,&overridden))!=THY_OK)
        return THY_STATE_INVALID;
    switch(row) {
    case THY_ROW_SENSITIVITY_AMOUNT:return sensitivity_percent(e,instance,response.attack_raw,value);
    case THY_ROW_SENSITIVITY_LINKEDRELEASE:*value=release_policy.linked;break;
    case THY_ROW_SENSITIVITY_RELEASEGAP:*value=release_policy.gap;break;
    case THY_ROW_SENSITIVITY_ATTACK:*value=response.attack_raw;break;
    case THY_ROW_SENSITIVITY_RELEASE:*value=response.release_raw;break;
    case THY_ROW_SENSITIVITY_FLOOR:*value=response.pressure_floor;break;
    case THY_ROW_SENSITIVITY_CEILING:*value=response.pressure_full_scale;break;
    case THY_ROW_SENSITIVITY_CURVE:*value=response.pressure_curve;break;
    case THY_ROW_SENSITIVITY_VELOCITYCURVE:*value=velocity.curve;break;
    case THY_ROW_SENSITIVITY_SOFTSPEED:*value=velocity.soft_rate;break;
    case THY_ROW_SENSITIVITY_HARDSPEED:*value=velocity.hard_rate;break;
    case THY_ROW_SENSITIVITY_MINVELOCITY:*value=velocity.minimum;break;
    case THY_ROW_SENSITIVITY_MAXVELOCITY:*value=velocity.maximum;break;
    default:return THY_STATE_UNAVAILABLE;
    }
    return THY_STATE_OK;
}

static int write_pad_setting(thy_engine *e,uint16_t row,uint8_t instance,int32_t value)
{
    int electrical=thy_engine_sensitivity_electrical(e,instance);
    if(electrical== -2)return THY_STATE_INVALID;
    if(row==THY_ROW_SENSITIVITY_PROFILEENABLED || row==THY_ROW_SENSITIVITY_PROFILESPLIT ||
       row==THY_ROW_SENSITIVITY_PRESSURECURVE || row==THY_ROW_SENSITIVITY_AFTERTOUCHCURVE) {
        thy_expression_profile profile;
        if(thy_engine_profile_get(e,instance,&profile)!=THY_OK)return THY_STATE_INVALID;
        if(row==THY_ROW_SENSITIVITY_PROFILEENABLED)profile.enabled=(uint8_t)value;
        else if(row==THY_ROW_SENSITIVITY_PROFILESPLIT)profile.split=(uint8_t)value;
        else if(row==THY_ROW_SENSITIVITY_PRESSURECURVE)profile.pressure_curve=(uint8_t)value;
        else profile.aftertouch_curve=(uint8_t)value;
        int result=thy_engine_profile_configure(e,instance,&profile);
        return result==THY_OK?THY_STATE_OK:result==THY_INVALID_ARGUMENT?THY_STATE_INVALID:THY_STATE_BLOCKED;
    }
    unsigned side=electrical== -3?1u:0u;
    if((row==THY_ROW_SENSITIVITY_INHERIT && electrical<0) ||
       (row==THY_ROW_SENSITIVITY_RESET && electrical>=0))return THY_STATE_UNAVAILABLE;
    if(row==THY_ROW_SENSITIVITY_RESET){
        /* Defaults resets the cluster family and retains individual overrides.
         * An inherited profile may still affect an overridden detector, so its
         * quiet preflight is independent of the detector reset's own scope. */
        const thy_expression_profile *profile=&e->instrument->expression_profiles[side];
        int changed=profile->enabled || profile->split!=70u || profile->pressure_curve || profile->aftertouch_curve;
        for(unsigned n=0;n<THY_PAD_COUNT;++n)if(changed && e->instrument->mapping[n]/48u==side &&
            !e->instrument->expression_profile_overridden[n]){
            const thy_instrument_sensor *sensor=&e->instrument->sensors[n];
            if(sensor->held || sensor->blocked || sensor->attack_count || sensor->strike_ready ||
                thy_core_pad_held(e->instrument->core,(uint8_t)n) ||
                (sensor->detector_profile==THY_DETECTOR_RELATIVE?
                 sensor->delta>sensor->calibration.release_delta:sensor->raw>sensor->response.release_raw))return THY_STATE_BLOCKED;
        }
        thy_instrument_response r={1377u,1354u,1353u,4095u,0u};
        thy_instrument_velocity v;thy_instrument_velocity_defaults(&v);
        thy_instrument_release_policy p;thy_instrument_release_defaults(&p);
        if(thy_instrument_set_cluster_sensitivity_policy(e->instrument,(uint8_t)side,&r,&v,&p)!=THY_OK)
            return THY_STATE_BLOCKED;
        e->instrument->expression_profiles[side]=(thy_expression_profile){0u,70u,0u,0u};
        ++e->instrument->revision;return THY_STATE_OK;
    }
    if(row==THY_ROW_SENSITIVITY_INHERIT){
        unsigned cluster=e->instrument->mapping[electrical]/48u;
        const thy_instrument_response *r=&e->instrument->cluster_response[cluster];
        if(e->instrument->expression_profiles[cluster].enabled &&
            r->pressure_full_scale-r->pressure_floor<2u)return THY_STATE_BLOCKED;
        if(thy_instrument_reset_sensitivity(e->instrument,electrical)!=THY_OK)return THY_STATE_BLOCKED;
        e->instrument->expression_profile_overridden[electrical]=0u;
        ++e->instrument->revision;return THY_STATE_OK;
    }
    if(row==THY_ROW_SENSITIVITY_LINKEDRELEASE || row==THY_ROW_SENSITIVITY_RELEASEGAP) {
        thy_instrument_release_policy release_policy;uint8_t overridden;
        if((electrical<0?thy_instrument_get_cluster_release_policy(e->instrument,(uint8_t)side,&release_policy):
            thy_instrument_get_release_policy(e->instrument,electrical,&release_policy,&overridden))!=THY_OK)
            return THY_STATE_INVALID;
        if(row==THY_ROW_SENSITIVITY_LINKEDRELEASE)release_policy.linked=(uint8_t)value;
        else release_policy.gap=(uint16_t)value;
        return (electrical<0?thy_instrument_set_cluster_release_policy(e->instrument,(uint8_t)side,&release_policy):
            thy_instrument_set_release_policy(e->instrument,electrical,&release_policy))==THY_OK?
            THY_STATE_OK:THY_STATE_BLOCKED;
    }
    thy_instrument_response response;
    thy_instrument_velocity velocity;
    uint8_t overridden;
    if((electrical<0?thy_instrument_get_cluster_response(e->instrument,(uint8_t)side,&response):
        thy_instrument_get_pad_response(e->instrument,(uint8_t)electrical,&response,&overridden))!=THY_OK ||
       (electrical<0?thy_instrument_get_cluster_velocity(e->instrument,(uint8_t)side,&velocity):
        thy_instrument_get_velocity(e->instrument,electrical,&velocity,&overridden))!=THY_OK)return THY_STATE_INVALID;
    int pressure=1;
    switch(row) {
    case THY_ROW_SENSITIVITY_AMOUNT: {
        int32_t current,low,high;
        if(value<0 || value>100 || sensitivity_percent(e,instance,response.attack_raw,&current)!=THY_STATE_OK ||
           thy_engine_sensitivity_raw_bounds(e,instance,&low,&high)!=THY_STATE_OK)return THY_STATE_INVALID;
        /* A whole-percent readback is intentionally coarse. Applying its
         * unchanged display value must preserve a precise raw calibration. */
        if(value==current && response.attack_raw>=low && response.attack_raw<=high)return THY_STATE_OK;
        uint16_t attack=(uint16_t)(high-((uint32_t)value*(uint32_t)(high-low)+50u)/100u);
        int result=electrical<0?thy_instrument_scale_cluster_sensitivity(e->instrument,(uint8_t)side,attack):
            thy_instrument_set_trigger(e->instrument,electrical,attack);
        return result==THY_OK?
            THY_STATE_OK:THY_STATE_BLOCKED;
    }
    case THY_ROW_SENSITIVITY_ATTACK:
        if(electrical<0){
            thy_instrument_release_policy p;
            if(thy_instrument_get_cluster_release_policy(e->instrument,(uint8_t)side,&p)!=THY_OK)return THY_STATE_INVALID;
            response.attack_raw=(uint16_t)value;
            if(p.linked)response.release_raw=value>p.gap?(uint16_t)(value-p.gap):0u;
            return thy_instrument_set_cluster_sensitivity_policy(e->instrument,(uint8_t)side,&response,&velocity,&p)==THY_OK?
                THY_STATE_OK:THY_STATE_BLOCKED;
        }
        return thy_instrument_set_trigger(e->instrument,electrical,(uint16_t)value)==THY_OK?
            THY_STATE_OK:THY_STATE_BLOCKED;
    case THY_ROW_SENSITIVITY_RELEASE:response.release_raw=(uint16_t)value;break;
    case THY_ROW_SENSITIVITY_FLOOR:response.pressure_floor=(uint16_t)value;break;
    case THY_ROW_SENSITIVITY_CEILING:response.pressure_full_scale=(uint16_t)value;break;
    case THY_ROW_SENSITIVITY_CURVE:response.pressure_curve=(uint8_t)value;break;
    case THY_ROW_SENSITIVITY_VELOCITYCURVE:velocity.curve=(uint8_t)value;pressure=0;break;
    case THY_ROW_SENSITIVITY_SOFTSPEED:velocity.soft_rate=(uint16_t)value;pressure=0;break;
    case THY_ROW_SENSITIVITY_HARDSPEED:velocity.hard_rate=(uint16_t)value;pressure=0;break;
    case THY_ROW_SENSITIVITY_MINVELOCITY:velocity.minimum=(uint8_t)value;pressure=0;break;
    case THY_ROW_SENSITIVITY_MAXVELOCITY:velocity.maximum=(uint8_t)value;pressure=0;break;
    default:return THY_STATE_UNAVAILABLE;
    }
    int result=pressure?(electrical<0?thy_instrument_set_cluster_response(e->instrument,(uint8_t)side,&response):
        thy_instrument_set_pad_response(e->instrument,(uint8_t)electrical,&response)):
        (electrical<0?thy_instrument_set_cluster_velocity(e->instrument,(uint8_t)side,&velocity):
        thy_instrument_set_pad_velocity(e->instrument,(uint8_t)electrical,&velocity));
    return result==THY_OK?THY_STATE_OK:THY_STATE_BLOCKED;
}

int thy_engine_sensitivity_pad(thy_engine *e,uint8_t electrical,int pressed,int *result)
{
    if(!e || !e->instrument || !result || electrical>=96u)return 0;
    uint8_t bit=(uint8_t)(1u<<(electrical&7u));
    uint8_t *mask=&e->sensitivity_muted[electrical/8u];
    if(*mask&bit) {
        if(!pressed) {
            *mask&=(uint8_t)~bit;
            unsigned held=0;for(unsigned n=0;n<12u;++n)held|=e->sensitivity_muted[n];
            if(!held){e->sensitivity_pick=e->sensitivity_picked=0u;e->state.notice=THY_NOTICE_APPLIED;}
        }
        *result=THY_OK;return 1;
    }
    if(!e->sensitivity_pick)return 0;
    if(e->state.level!=THY_STATE_ROWS || e->state.editing ||
       e->state.group_index!=thy_menu_rows[THY_ROW_SENSITIVITY_TARGET].group) {
        e->sensitivity_pick=e->sensitivity_picked=0u;return 0;
    }
    if(!pressed)return 0; /* Existing musical owners still receive release. */
    *mask|=bit;
    if(!e->sensitivity_picked) {
        e->sensitivity_target=(uint8_t)(e->instrument->mapping[electrical]+1u);
        e->sensitivity_picked=1u;++e->state.revision;
    }
    *result=THY_OK;return 1;
}

static uint16_t canonical_route_row(uint16_t row)
{
    switch(row) {
    case THY_ROW_PERFORMANCE_ROUTETARGET:return THY_ROW_MIDI_ROUTETARGET;
    case THY_ROW_PERFORMANCE_PRESSUREMODE:return THY_ROW_MIDI_PRESSURE;
    case THY_ROW_PERFORMANCE_PRESSUREBEND:return THY_ROW_MIDI_PRESSUREBEND;
    case THY_ROW_PERFORMANCE_BENDRANGE:return THY_ROW_MIDI_BENDRANGE;
    default:return row;
    }
}

static int read_setting(void *context, uint16_t row, uint8_t instance, int32_t *value)
{
    row=canonical_route_row(row);
    thy_engine *engine = context;
    const thy_instrument *i = engine->instrument;
    if (i == NULL || value == NULL) return THY_STATE_UNAVAILABLE;
    if(row==THY_ROW_SENSITIVITY_VELOCITYMODE)return read_velocity_mode(engine,instance,value);
    if(thy_menu_rows[row].instance_kind==THY_MENU_PAD)return read_pad_setting(engine,row,instance,value);
    if (thy_menu_rows[row].group == thy_menu_rows[THY_ROW_SEQUENCER_OPEN].group)
        return thy_engine_sequencer_read(engine, row, instance, value);
    if (thy_menu_rows[row].instance_kind == THY_MENU_VIEW) return read_view(i, row, instance, value);
    if (thy_menu_rows[row].instance_kind == THY_MENU_SHORTCUT) {
        *value = thy_instrument_scale_shortcut(i, instance); return THY_STATE_OK;
    }
    if (thy_menu_rows[row].instance_kind == THY_MENU_ARP)
        return read_arp(engine, row, instance, value);
    if (thy_menu_rows[row].instance_kind == THY_MENU_ROUTE) {
        thy_instrument_route route;
        if (thy_instrument_get_pad_route(i, instance, &route) != THY_OK)
            return THY_STATE_INVALID;
        if (row == THY_ROW_MIDI_CHANNEL) *value = route.channel + 1;
        else if (row == THY_ROW_MIDI_VELOCITY) *value = route.velocity;
        else if (row == THY_ROW_MIDI_PRESSURE) *value = thy_engine_channel_aftertouch(engine,instance);
        else if (row == THY_ROW_MIDI_PRESSUREBEND) *value = engine->channel_routes[instance].bend;
        else if (row == THY_ROW_MIDI_BENDRANGE) *value = engine->channel_routes[instance].range;
        else if (row == THY_ROW_MIDI_DESTINATIONS) {
            for (unsigned n = 0; n < sizeof route_masks; ++n)
                if (route_masks[n] == route.destinations) { *value = (int32_t)n; return THY_STATE_OK; }
            return THY_STATE_INVALID;
        } else return THY_STATE_UNAVAILABLE;
        return THY_STATE_OK;
    }
    switch (row) {
    case THY_ROW_PERFORMANCE_MPE:
    case THY_ROW_EXPRESSION_ENABLED:*value=engine->expression_config.enabled;break;
    case THY_ROW_PERFORMANCE_VELOCITYTARGET:*value=engine->velocity_target;break;
    case THY_ROW_EXPRESSION_ZONE:*value=engine->expression_config.upper_zone;break;
    case THY_ROW_EXPRESSION_MEMBERS:*value=engine->expression_config.members;break;
    case THY_ROW_EXPRESSION_BENDRANGE:*value=engine->expression_config.bend_range;break;
    case THY_ROW_EXPRESSION_MANAGERRANGE:*value=engine->expression_config.manager_bend_range;break;
    case THY_ROW_EXPRESSION_GLIDE:*value=engine->expression_config.glide;break;
    case THY_ROW_EXPRESSION_PADWINDOW:*value=engine->expression_config.pad_window;break;
    case THY_ROW_EXPRESSION_INTERVAL:*value=engine->expression_config.semitone_window;break;
    case THY_ROW_SENSITIVITY_TARGET:*value=engine->sensitivity_target;break;
    case THY_ROW_ARPS_TARGET: *value = engine->arp_target; break;
    case THY_ROW_MIDI_REMOTERECORD: *value=engine->remote_record; break;
    case THY_ROW_MIDI_REMOTEPOSITION: *value=engine->remote_position; break;
    case THY_ROW_MIDI_REMOTECHANNEL: *value=engine->remote_channel; break;
    case THY_ROW_ARPS_QUICKKEYS: *value = engine->arp_quick_keys; break;
    case THY_ROW_MIDI_CLOCK: *value = engine->musical_clock.source; break;
    case THY_ROW_MIDI_TEMPO: *value = (int32_t)(engine->musical_clock.tempo_milli_bpm / 1000u); break;
    case THY_ROW_MIDI_METERNUMERATOR: *value = engine->musical_clock.numerator; break;
    case THY_ROW_MIDI_METERDENOMINATOR:
        for (unsigned n = 0u; n < sizeof meter_units; ++n)
            if (meter_units[n] == engine->musical_clock.denominator) { *value = (int32_t)n; return THY_STATE_OK; }
        return THY_STATE_INVALID;
    case THY_ROW_ENGINES_SHORTCUT: *value = engine->shortcut_target; break;
    case THY_ROW_ENGINES_SCALE:
    case THY_ROW_PERFORMANCE_SCALE: *value = engine->scale; break;
    case THY_ROW_ENGINES_ROOT:
    case THY_ROW_PERFORMANCE_ROOT: *value = i->performance.root; break;
    case THY_ROW_PERFORMANCE_MELODYLAYOUT: *value = menu_from_layout[i->performance.melody_layout < THY_LAYOUT_COUNT ? i->performance.melody_layout : 0u]; break;
    case THY_ROW_PERFORMANCE_GUITARWINDOW: *value = i->performance.guitar_window; break;
    case THY_ROW_PERFORMANCE_PADOVERLAYS: *value = i->context_controls; break;
    case THY_ROW_PERFORMANCE_CONTEXTLOCATION: *value = i->context_location; break;
    case THY_ROW_PERFORMANCE_CONTEXTCHORDOCTAVES: *value = !!(i->context_mask & 3u); break;
    case THY_ROW_PERFORMANCE_CONTEXTMELODYOCTAVES: *value = !!(i->context_mask & 48u); break;
    case THY_ROW_PERFORMANCE_CONTEXTBASS: *value = !!(i->context_mask & 8u); break;
    case THY_ROW_PERFORMANCE_CONTEXTINVERSION: *value = !!(i->context_mask & 4u); break;
    case THY_ROW_PERFORMANCE_BASSOCTAVE: *value = i->bass_octave; break;
    case THY_ROW_PERFORMANCE_TRANSPOSE: *value = i->transpose[0]; break;
    case THY_ROW_PERFORMANCE_CHORDOCTAVE: *value = i->performance.chord_octave; break;
    case THY_ROW_PERFORMANCE_MELODYOCTAVE: *value = i->performance.melody_octave; break;
    case THY_ROW_PERFORMANCE_VELOCITY: *value = i->fixed_velocity; break;
    case THY_ROW_PERFORMANCE_AFTERTOUCH: *value = i->pressure_enabled; break;
    case THY_ROW_PERFORMANCE_KEEPTRANSPOSE: *value=i->keep_transpose;break;
    case THY_ROW_PERFORMANCE_LIVEUPDATE: *value=i->live_update;break;
    case THY_ROW_PERFORMANCE_LIVEUPDATELOCKSCALE: *value=i->live_lock_scale;break;
    case THY_ROW_PERFORMANCE_LIVEUPDATEMUTECHORD: *value=i->live_mute_chord;break;
    case THY_ROW_MIDI_ROUTINGPROFILE: *value = i->routing_profile; break;
    case THY_ROW_MIDI_ROUTETARGET: *value = engine->route_target; break;
    case THY_ROW_CHORDS_INVERSION: *value = i->performance.inversion; break;
    case THY_ROW_CHORDS_LAYOUT:*value=i->chord_layout;break;
    case THY_ROW_CHORDS_QUALITY:*value=i->chord_quality;break;
    case THY_ROW_CHORDS_USERSCALE:*value=i->user_scale;break;
    case THY_ROW_CHORDS_SIXBANK:*value=i->six_bank;break;
    case THY_ROW_CHORDS_VOICINGMODE:*value=i->voicing_mode;break;
    case THY_ROW_CHORDS_DROP2:case THY_ROW_CHORDS_DROP3:case THY_ROW_CHORDS_DROP4:
        *value=!!(i->drop_mask&(1u<<(row-THY_ROW_CHORDS_DROP2)));break;
    case THY_ROW_CHORDS_SPREAD: *value = i->performance.spread; break;
    case THY_ROW_CHORDS_OCTAVESTACKS: *value = i->performance.upper_octaves; break;
    case THY_ROW_CHORDS_STRUM: *value = i->performance.strum; break;
    case THY_ROW_CHORDS_STRUMDELAY: *value = i->performance.strum_ms; break;
    case THY_ROW_CHORDS_STRUMDIRECTION: *value = i->performance.strum_order; break;
    case THY_ROW_SENSITIVITY_CENTERRESPONSE: *value = i->center_poll_ms; break;
    case THY_ROW_LIGHTS_CHORDCOLOR: *value = i->light_chord_mode; break;
    case THY_ROW_LIGHTS_NOTECOLORS: *value = i->light_palette; break;
    case THY_ROW_LIGHTS_KEYCONTRAST: *value = i->light_key_contrast; break;
    case THY_ROW_LIGHTS_KEYDIM: *value = i->light_key_contrast_percent; break;
    case THY_ROW_LIGHTS_HELDSTYLE: *value = i->light_held_style; break;
    case THY_ROW_LIGHTS_HELDFEEDBACK: *value = i->light_held_feedback; break;
    case THY_ROW_LIGHTS_VIEW: *value = i->display_view; break;
    case THY_ROW_CONTROLS_LEFT: *value = i->play_nav[0]; break;
    case THY_ROW_CONTROLS_UP: *value = i->play_nav[1]; break;
    case THY_ROW_CONTROLS_DOWN: *value = i->play_nav[2]; break;
    case THY_ROW_CONTROLS_RIGHT: *value = i->play_nav[3]; break;
    case THY_ROW_CONTROLS_SLOTSCOPE: *value = i->slot_scope; break;
    case THY_ROW_CONTROLS_LOWERMODE: *value = i->lower_mode; break;
    case THY_ROW_LIGHTS_CENTER: *value = i->center_brightness; break;
    case THY_ROW_LIGHTS_ANIMATIONS: *value = i->animations_enabled; break;
    case THY_ROW_LIGHTS_ENABLED: *value = i->lights_enabled; break;
    case THY_ROW_LIGHTS_IDLE: *value = i->light_idle; break;
    case THY_ROW_LIGHTS_HELD: *value = i->light_held; break;
    case THY_ROW_LIGHTS_FADE: *value = i->light_fade_ms; break;
    default: return THY_STATE_UNAVAILABLE;
    }
    return THY_STATE_OK;
}

static int write_setting(void *context, uint16_t row, uint8_t instance, int32_t value)
{
    row=canonical_route_row(row);
    thy_engine *engine = context;
    thy_instrument *i = engine->instrument;
    if (i == NULL) return THY_STATE_UNAVAILABLE;
    if(row==THY_ROW_SENSITIVITY_VELOCITYMODE)return write_velocity_mode(engine,instance,value);
    if(row==THY_ROW_PERFORMANCE_MPE || thy_menu_rows[row].group==thy_menu_rows[THY_ROW_EXPRESSION_ENABLED].group) {
        if(row==THY_ROW_EXPRESSION_SETUP) {
            int result=thy_engine_expression_send_setup(engine);
            return result==THY_OK?THY_STATE_OK:result==THY_INVALID_ARGUMENT?THY_STATE_INVALID:THY_STATE_BLOCKED;
        }
        thy_mpe_config config=engine->expression_config;
        if(value<0 || value>96)return THY_STATE_INVALID;
        switch(row) {
        case THY_ROW_PERFORMANCE_MPE:
        case THY_ROW_EXPRESSION_ENABLED:config.enabled=(uint8_t)value;break;
        case THY_ROW_EXPRESSION_ZONE:config.upper_zone=(uint8_t)value;break;
        case THY_ROW_EXPRESSION_MEMBERS:config.members=(uint8_t)value;break;
        case THY_ROW_EXPRESSION_BENDRANGE:config.bend_range=(uint8_t)value;break;
        case THY_ROW_EXPRESSION_MANAGERRANGE:config.manager_bend_range=(uint8_t)value;break;
        case THY_ROW_EXPRESSION_GLIDE:config.glide=(uint8_t)value;break;
        case THY_ROW_EXPRESSION_PADWINDOW:config.pad_window=(uint8_t)value;break;
        case THY_ROW_EXPRESSION_INTERVAL:config.semitone_window=(uint8_t)value;break;
        default:return THY_STATE_UNAVAILABLE;
        }
        int result=thy_engine_expression_configure(engine,&config);
        return result==THY_OK?THY_STATE_OK:result==THY_INVALID_ARGUMENT?THY_STATE_INVALID:THY_STATE_BLOCKED;
    }
    if(thy_menu_rows[row].instance_kind==THY_MENU_PAD)return write_pad_setting(engine,row,instance,value);
    if (thy_menu_rows[row].group == thy_menu_rows[THY_ROW_SEQUENCER_OPEN].group)
        return thy_engine_sequencer_write(engine, row, instance, value);
    if (thy_menu_rows[row].instance_kind == THY_MENU_VIEW) return write_view(i, row, instance, value);
    if (thy_menu_rows[row].instance_kind == THY_MENU_SHORTCUT)
        return value >= 0 && value <= (int32_t)THY_SCALE_SHORTCUT_LAYOUT && thy_instrument_set_scale_shortcut(i, instance, (uint8_t)value) == THY_OK ? THY_STATE_OK : THY_STATE_INVALID;
    if (thy_menu_rows[row].instance_kind == THY_MENU_ARP)
        return write_arp(engine, row, instance, value);
    if (row == THY_ROW_MIDI_CLOCK || row == THY_ROW_MIDI_TEMPO ||
        row == THY_ROW_MIDI_METERNUMERATOR || row == THY_ROW_MIDI_METERDENOMINATOR) {
        if (row == THY_ROW_MIDI_METERDENOMINATOR && (value < 0 || value >= (int32_t)sizeof meter_units)) return THY_STATE_INVALID;
        return thy_engine_clock_configure(engine,
            row == THY_ROW_MIDI_TEMPO ? (uint32_t)value * 1000u : engine->musical_clock.tempo_milli_bpm,
            row == THY_ROW_MIDI_CLOCK ? (uint8_t)value : engine->musical_clock.source,
            row == THY_ROW_MIDI_METERNUMERATOR ? (uint8_t)value : engine->musical_clock.numerator,
            row == THY_ROW_MIDI_METERDENOMINATOR ? meter_units[value] : engine->musical_clock.denominator,
            engine->rhythm_transport.launch_quantize) == THY_OK ? THY_STATE_OK : THY_STATE_ADAPTER_ERROR;
    }
    if (thy_menu_rows[row].instance_kind == THY_MENU_ROUTE) {
        thy_instrument_route route;
        if (thy_instrument_get_pad_route(i, instance, &route) != THY_OK)
            return THY_STATE_INVALID;
        int result;
        if (row == THY_ROW_MIDI_CHANNEL)
            result = thy_instrument_set_pad_route(i, instance, (uint8_t)(value - 1), route.destinations);
        else if (row == THY_ROW_MIDI_DESTINATIONS) {
            if (value < 0 || value >= (int32_t)sizeof route_masks) return THY_STATE_INVALID;
            result = thy_instrument_set_pad_route(i, instance, route.channel, route_masks[value]);
        } else if (row == THY_ROW_MIDI_VELOCITY)
            result = thy_instrument_set_route_velocity(i, instance, (uint8_t)value);
        else if (row == THY_ROW_MIDI_PRESSURE || row == THY_ROW_MIDI_PRESSUREBEND || row == THY_ROW_MIDI_BENDRANGE) {
            thy_engine_channel_config config=engine->channel_routes[instance];
            if(row==THY_ROW_MIDI_PRESSURE)config.aftertouch=(uint8_t)(value+1);
            else if(row==THY_ROW_MIDI_PRESSUREBEND)config.bend=(uint8_t)value;
            else config.range=(uint8_t)value;
            result=thy_engine_channel_configure(engine,instance,&config);
            if(result==THY_OK && row==THY_ROW_MIDI_PRESSURE)
                result=thy_instrument_set_route_pressure(i,instance,(uint8_t)(value!=0));
        } else if(row==THY_ROW_MIDI_BENDSETUP) {
            result=thy_engine_channel_send_setup(engine);
            return result==THY_OK?THY_STATE_OK:result==THY_INVALID_ARGUMENT?THY_STATE_INVALID:THY_STATE_BLOCKED;
        }
        else return THY_STATE_UNAVAILABLE;
        return result == THY_OK ? THY_STATE_OK : THY_STATE_INVALID;
    }
    switch (row) {
    case THY_ROW_PERFORMANCE_VELOCITYTARGET:
        if(value<0 || value>2)return THY_STATE_INVALID;
        engine->velocity_target=(uint8_t)value;return THY_STATE_OK;
    case THY_ROW_SENSITIVITY_TARGET:
        if(value<0 || value>97)return THY_STATE_INVALID;
        engine->sensitivity_target=(uint8_t)value;break;
    case THY_ROW_SENSITIVITY_PICK:
        engine->sensitivity_pick=1u;engine->sensitivity_picked=0u;return THY_STATE_OK;
    case THY_ROW_SENSITIVITY_CALIBRATE:return thy_engine_calibration_open(engine);
    case THY_ROW_ARPS_OPEN: return thy_engine_arp_quick_open(engine);
    case THY_ROW_ENGINES_ARP: {
        int result=thy_state_set_instance(&engine->state,THY_ROW_ARPS_ENABLED,engine->arp_target,1);
        return result==THY_STATE_OK?thy_engine_arp_quick_open(engine):result;
    }
    case THY_ROW_ENGINES_SEQUENCER: return thy_engine_sequencer_open(engine, THY_SEQ_VIEW_TRACKS);
    case THY_ROW_ARPS_TARGET: engine->arp_target = (uint8_t)value; return THY_STATE_OK;
    case THY_ROW_MIDI_REMOTERECORD:
        if(!value) thy_engine_sequencer_usb_clear(engine);
        engine->remote_record=(uint8_t)value; return THY_STATE_OK;
    case THY_ROW_MIDI_REMOTEPOSITION: engine->remote_position=(uint8_t)value; return THY_STATE_OK;
    case THY_ROW_MIDI_REMOTECHANNEL:
        thy_engine_sequencer_usb_clear(engine); engine->remote_channel=(uint8_t)value; return THY_STATE_OK;
    case THY_ROW_ARPS_QUICKKEYS: engine->arp_quick_keys = (uint8_t)value; return THY_STATE_OK;
    case THY_ROW_ENGINES_PLAY: return thy_engine_workspace(engine, THY_PLAY_ENGINE_PLAY);
    case THY_ROW_ENGINES_SHORTCUT: engine->shortcut_target = (uint8_t)value; return THY_STATE_OK;
    case THY_ROW_ENGINES_SCALE:
    case THY_ROW_PERFORMANCE_SCALE:
        if(engine->scale!=(uint16_t)value||i->user_scale)thy_instrument_key_changed(i);
        i->user_scale=0u;(void)thy_instrument_apply_scale(i);
        engine->scale = (uint16_t)value; ++i->layout_revision; break;
    case THY_ROW_ENGINES_ROOT:
    case THY_ROW_PERFORMANCE_ROOT:
        if(i->performance.root!=(uint8_t)value)thy_instrument_key_changed(i);
        i->performance.root = (uint8_t)value; ++i->layout_revision; break;
    case THY_ROW_PERFORMANCE_KEEPTRANSPOSE:i->keep_transpose=(uint8_t)value;break;
    case THY_ROW_PERFORMANCE_LIVEUPDATE:
        return thy_instrument_set_live_update(i,(uint8_t)value,i->live_lock_scale,i->live_mute_chord)==THY_OK?THY_STATE_OK:THY_STATE_INVALID;
    case THY_ROW_PERFORMANCE_LIVEUPDATELOCKSCALE:
        return thy_instrument_set_live_update(i,i->live_update,(uint8_t)value,i->live_mute_chord)==THY_OK?THY_STATE_OK:THY_STATE_INVALID;
    case THY_ROW_PERFORMANCE_LIVEUPDATEMUTECHORD:
        return thy_instrument_set_live_update(i,i->live_update,i->live_lock_scale,(uint8_t)value)==THY_OK?THY_STATE_OK:THY_STATE_INVALID;
    case THY_ROW_PERFORMANCE_MELODYLAYOUT:
        if (value < 0 || value >= THY_LAYOUT_COUNT) return THY_STATE_INVALID;
        i->performance.melody_layout = layout_from_menu[value];
        i->engine_kind = THY_PLAY_ENGINE_PLAY;
        engine->session.playing_mode = i->engine_kind; ++i->layout_revision; break;
    case THY_ROW_PERFORMANCE_GUITARWINDOW:
        i->performance.guitar_window = (uint8_t)value; ++i->layout_revision; break;
    case THY_ROW_PERFORMANCE_GUITARSHORTCUT:
        i->performance.melody_layout = THY_LAYOUT_GUITAR; i->engine_kind = THY_PLAY_ENGINE_PLAY; engine->session.playing_mode = i->engine_kind;
        ++i->layout_revision; break;
    case THY_ROW_PERFORMANCE_PADOVERLAYS:
        return thy_instrument_set_context_controls(i, (uint8_t)value) == THY_OK ? THY_STATE_OK : THY_STATE_BLOCKED;
    case THY_ROW_PERFORMANCE_CONTEXTLOCATION:
        return thy_instrument_set_context_location(i, (uint8_t)value) == THY_OK ? THY_STATE_OK : THY_STATE_BLOCKED;
    case THY_ROW_PERFORMANCE_CONTEXTCHORDOCTAVES:
    case THY_ROW_PERFORMANCE_CONTEXTMELODYOCTAVES:
    case THY_ROW_PERFORMANCE_CONTEXTBASS:
    case THY_ROW_PERFORMANCE_CONTEXTINVERSION: {
        uint8_t bits = row == THY_ROW_PERFORMANCE_CONTEXTCHORDOCTAVES ? 3u :
            row == THY_ROW_PERFORMANCE_CONTEXTMELODYOCTAVES ? 48u :
            row == THY_ROW_PERFORMANCE_CONTEXTBASS ? 8u : 4u;
        uint8_t mask = value ? (uint8_t)(i->context_mask | bits) :
            (uint8_t)(i->context_mask & (uint8_t)~bits);
        return thy_instrument_set_context_mask(i, mask) == THY_OK ? THY_STATE_OK : THY_STATE_BLOCKED;
    }
    case THY_ROW_PERFORMANCE_BASSOCTAVE: i->bass_octave = (int8_t)value; break;
    case THY_ROW_PERFORMANCE_TRANSPOSE:
        i->transpose[0] = i->transpose[1] = (int16_t)value; break;
    case THY_ROW_PERFORMANCE_CHORDOCTAVE: i->performance.chord_octave = (int8_t)value; break;
    case THY_ROW_PERFORMANCE_MELODYOCTAVE: i->performance.melody_octave = (int8_t)value; break;
    case THY_ROW_PERFORMANCE_VELOCITY:
        return thy_instrument_set_expression(i, (uint8_t)value, i->pressure_enabled) == THY_OK ? THY_STATE_OK : THY_STATE_ADAPTER_ERROR;
    case THY_ROW_PERFORMANCE_AFTERTOUCH:
        return thy_instrument_set_expression(i, i->fixed_velocity, (uint8_t)value) == THY_OK ? THY_STATE_OK : THY_STATE_ADAPTER_ERROR;
    case THY_ROW_MIDI_ROUTINGPROFILE:
        if (thy_instrument_set_routing_profile(i, (uint8_t)value) != THY_OK) return THY_STATE_INVALID;
        if (!thy_state_option_allowed(&engine->state, THY_ROW_MIDI_ROUTETARGET, engine->route_target))
            engine->route_target = value == THY_ROUTING_WHOLE ? 0u : value == THY_ROUTING_SPLIT ? 1u : 3u;
        return THY_STATE_OK;
    case THY_ROW_MIDI_ROUTETARGET:
        engine->route_target = (uint8_t)value; break;
    case THY_ROW_CHORDS_INVERSION: i->performance.inversion = (uint8_t)value; break;
    case THY_ROW_CHORDS_LAYOUT:i->chord_layout=(uint8_t)value;i->live_degree_count=0u;break;
    case THY_ROW_CHORDS_QUALITY:i->chord_quality=(uint8_t)value;i->live_degree_count=0u;break;
    case THY_ROW_CHORDS_USERSCALE:return thy_engine_user_scale(engine,(uint8_t)value)==THY_OK?THY_STATE_OK:THY_STATE_INVALID;
    case THY_ROW_CHORDS_SIXBANK:i->six_bank=(uint8_t)value;i->live_degree_count=0u;break;
    case THY_ROW_CHORDS_EDITSCALES:return thy_engine_library_open(engine,THY_LIBRARY_UI_SCALE,0u);
    case THY_ROW_CHORDS_EDITCHORDS:return thy_engine_library_open(engine,THY_LIBRARY_UI_CHORD,0u);
    case THY_ROW_CHORDS_EDITSIX:return thy_engine_library_open(engine,THY_LIBRARY_UI_SIX,0u);
    case THY_ROW_CHORDS_VOICINGMODE:i->voicing_mode=(uint8_t)value;break;
    case THY_ROW_CHORDS_DROP2:case THY_ROW_CHORDS_DROP3:case THY_ROW_CHORDS_DROP4:{
        uint8_t bit=(uint8_t)(1u<<(row-THY_ROW_CHORDS_DROP2));
        i->drop_mask=value?(uint8_t)(i->drop_mask|bit):(uint8_t)(i->drop_mask&~bit);break;}
    case THY_ROW_CHORDS_SPREAD: i->performance.spread = (uint8_t)value; break;
    case THY_ROW_CHORDS_OCTAVESTACKS: i->performance.upper_octaves = (uint8_t)value; break;
    case THY_ROW_CHORDS_STRUM: i->performance.strum = (uint8_t)value; break;
    case THY_ROW_CHORDS_STRUMDELAY: i->performance.strum_ms = (uint16_t)value; break;
    case THY_ROW_CHORDS_STRUMDIRECTION: i->performance.strum_order = (uint8_t)value; break;
    case THY_ROW_SENSITIVITY_CENTERRESPONSE: i->center_poll_ms = (uint8_t)value; break;
    case THY_ROW_LIGHTS_CHORDCOLOR: i->light_chord_mode = (uint8_t)value; break;
    case THY_ROW_LIGHTS_NOTECOLORS: i->light_palette = (uint8_t)value; break;
    case THY_ROW_LIGHTS_KEYCONTRAST: i->light_key_contrast = (uint8_t)value; break;
    case THY_ROW_LIGHTS_KEYDIM: i->light_key_contrast_percent = (uint8_t)value; break;
    case THY_ROW_LIGHTS_HELDSTYLE: i->light_held_style = (uint8_t)value; break;
    case THY_ROW_LIGHTS_HELDFEEDBACK: i->light_held_feedback = (uint8_t)value; break;
    case THY_ROW_LIGHTS_VIEW:
        if (value < 0 || !thy_view_available((unsigned)value)) return THY_STATE_INVALID;
        i->display_view = (uint8_t)value; break;
    case THY_ROW_CONTROLS_LEFT: i->play_nav[0] = (uint8_t)value; break;
    case THY_ROW_CONTROLS_UP: i->play_nav[1] = (uint8_t)value; break;
    case THY_ROW_CONTROLS_DOWN: i->play_nav[2] = (uint8_t)value; break;
    case THY_ROW_CONTROLS_RIGHT: i->play_nav[3] = (uint8_t)value; break;
    case THY_ROW_CONTROLS_SLOTSCOPE: i->slot_scope = (uint8_t)value; break;
    case THY_ROW_CONTROLS_LOWERMODE:
        return thy_instrument_set_lower_mode(i, (uint8_t)value) == THY_OK ? THY_STATE_OK : THY_STATE_INVALID;
    case THY_ROW_LIGHTS_CENTER: i->center_brightness = (uint8_t)value; break;
    case THY_ROW_SYSTEM_ABOUT: engine->state.about_open = 1u; break;
    case THY_ROW_LIGHTS_ANIMATIONS: i->animations_enabled = (uint8_t)value; break;
    case THY_ROW_LIGHTS_ENABLED: case THY_ROW_LIGHTS_IDLE:
    case THY_ROW_LIGHTS_HELD: case THY_ROW_LIGHTS_FADE:
        return thy_instrument_set_lighting(i,
            row == THY_ROW_LIGHTS_ENABLED ? (uint8_t)value : i->lights_enabled,
            row == THY_ROW_LIGHTS_IDLE ? (uint8_t)value : i->light_idle,
            row == THY_ROW_LIGHTS_HELD ? (uint8_t)value : i->light_held,
            row == THY_ROW_LIGHTS_FADE ? (uint16_t)value : i->light_fade_ms) == THY_OK ? THY_STATE_OK : THY_STATE_ADAPTER_ERROR;
    default: return THY_STATE_UNAVAILABLE;
    }
    ++i->revision;
    return THY_STATE_OK;
}

static const char *setting_label(void *context, uint16_t row, int32_t value)
{
    const thy_engine *e=context;
    if(e&&e->instrument){
        if(row==THY_ROW_CHORDS_USERSCALE&&value>0&&value<=8)
            return e->instrument->library.scales[value-1].used?(const char *)e->instrument->library.scales[value-1].name:"Empty scale";
        if(row==THY_ROW_CHORDS_SIXBANK&&value>=0&&value<3)
            return e->instrument->library.six[value].used?(const char *)e->instrument->library.six[value].name:"Empty set";
        if(row<THY_MENU_ROW_COUNT&&thy_menu_rows[row].dynamic==2u&&value>70&&value<=78)
            return e->instrument->library.scales[value-71].used?(const char *)e->instrument->library.scales[value-71].name:"Empty scale";
    }
    if (row < THY_MENU_ROW_COUNT && thy_menu_rows[row].dynamic == 2u && value == (int32_t)THY_SCALE_SHORTCUT_LAYOUT) return "Cycle layout";
    if (row < THY_MENU_ROW_COUNT && thy_menu_rows[row].dynamic &&
        value >= 0 && value < (int32_t)THY_MENU_DEFAULT_SCALE_COUNT)
        return thy_menu_scale_names[value];
    return NULL;
}

void thy_engine_init(thy_engine *engine, thy_instrument *instrument,
    uint16_t scale, uint16_t scale_count)
{
    if (engine == NULL) return;
    *engine = (thy_engine){0};
    thy_mpe_defaults(&engine->expression_config);
    for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n)engine->channel_routes[n].range=2u;
    engine->instrument = instrument;
    engine->session.playing_mode = instrument ? instrument->engine_kind : THY_PLAY_ENGINE_PLAY;
    engine->arp_quick_keys = 1u;
    engine->route_target = instrument && instrument->routing_profile == THY_ROUTING_WHOLE ? 0u :
        instrument && instrument->routing_profile == THY_ROUTING_ZONES ? 3u : 1u;
    engine->scale = scale < scale_count ? scale : 0u;
    const thy_state_callbacks callbacks = {read_setting, write_setting, setting_label};
    thy_state_init(&engine->state, &callbacks, engine);
    engine->state.instrument = instrument;
    engine->state.engine = engine;
    (void)thy_state_set_scale_count(&engine->state, scale_count);
    engine->session.epoch = 1u;
    engine->session.system = THY_ENGINE_CONDITIONING;
    thy_clock_init(&engine->musical_clock, 0u);
    thy_transport_init(&engine->rhythm_transport, &engine->musical_clock);
    engine->observed_clock_generation = engine->musical_clock.generation;
    for (unsigned n = 0u; n < 3u; ++n) engine->live_generation[n] = 1u;
    for (unsigned n = 0u; n < 2u; ++n) thy_arp_init(&engine->arps[n], n + 1u);
    thy_engine_sequencer_init(engine);
    thy_engine_bind_music(engine);
}

void thy_engine_repeat_cancel(thy_engine *engine)
{
    if (engine) engine->navigation_repeat.action = 0u;
}

static int repeat_eligible(const thy_engine *engine)
{
    const thy_library_ui *library=&engine->instrument->library_editor;
    if(library->open)return (library->page==THY_LIBRARY_UI_NAME && library->name_cursor<12u)||
        (library->editing==THY_LIBRARY_UI_VALUE_EDIT&&library->kind!=THY_LIBRARY_UI_SCALE)||
        (library->editing==THY_LIBRARY_UI_SLOT_EDIT&&library->kind==THY_LIBRARY_UI_CHORD);
    const thy_state *state = &engine->state;
    if (!state->editing || state->edit_row >= THY_MENU_ROW_COUNT ||
        state->info_open || state->about_open ||
        state->edit_row == THY_ROW_SENSITIVITY_TARGET) return 0;
    const thy_menu_row *row = &thy_menu_rows[state->edit_row];
    int32_t low, high;
    return row->type == THY_MENU_NUMBER &&
        thy_state_edit_bounds(state, state->edit_row, state->edit_instance, &low, &high) == THY_STATE_OK &&
        (int64_t)high - low > 16;
}

static void repeat_arm(thy_engine *engine, uint8_t action)
{
    if ((action != THY_STATE_UP && action != THY_STATE_DOWN) ||
        (engine->instrument->control_masks[0] & 31u) != (1u << action) ||
        !repeat_eligible(engine)) return;
    thy_engine_hold_repeat *repeat = &engine->navigation_repeat;
    const thy_state *state = &engine->state;
    repeat->started_ms = engine->instrument->core->clock_initialized ?
        engine->instrument->core->now_ms : engine->musical_clock.now_ms;
    repeat->next_ms = repeat->started_ms + 400u;
    repeat->state_revision = state->revision;
    repeat->transition_revision = state->transition_revision;
    repeat->value = state->edit_value;
    repeat->row = state->edit_row; repeat->instance = state->edit_instance;
    repeat->group = state->group_index; repeat->level = state->level;
    repeat->action = action;
    if(engine->instrument->library_editor.open){const thy_library_ui *library=&engine->instrument->library_editor;
        repeat->row=THY_STATE_NO_ROW;repeat->state_revision=library->revision;
        repeat->instance=library->kind;repeat->group=library->cursor;repeat->level=library->page;
        repeat->value=library->name_cursor;}
}

void thy_engine_repeat_tick(thy_engine *engine, uint32_t now_ms)
{
    if (!engine || !engine->instrument) return;
    thy_engine_hold_repeat *repeat = &engine->navigation_repeat;
    if (!repeat->action) return;
    thy_state *state = &engine->state;
    if(repeat->row==THY_STATE_NO_ROW){
        const thy_library_ui *library=&engine->instrument->library_editor;
        if((engine->instrument->control_masks[0]&31u)!=(1u<<repeat->action)||!repeat_eligible(engine)||
            !library->open||library->revision!=repeat->state_revision||library->kind!=repeat->instance||
            library->cursor!=repeat->group||library->page!=repeat->level||library->name_cursor!=repeat->value||
            state->transition_revision!=repeat->transition_revision){thy_engine_repeat_cancel(engine);return;}
        if((int32_t)(now_ms-repeat->next_ms)<0)return;
        repeat->next_ms=now_ms+((uint32_t)(now_ms-repeat->started_ms)>=1500u?30u:60u);
        if(thy_engine_library_navigate(engine,repeat->action)!=THY_STATE_OK)thy_engine_repeat_cancel(engine);
        repeat->state_revision=library->revision;return;
    }
    if ((engine->instrument->control_masks[0] & 31u) != (1u << repeat->action) ||
        !repeat_eligible(engine) || state->edit_row != repeat->row ||
        state->edit_instance != repeat->instance || state->group_index != repeat->group ||
        state->level != repeat->level || state->revision != repeat->state_revision ||
        state->transition_revision != repeat->transition_revision || state->edit_value != repeat->value) {
        thy_engine_repeat_cancel(engine); return;
    }
    if ((int32_t)(now_ms - repeat->next_ms) < 0) return;
    /* Schedule from the current frame, never replay a backlog after a stall. */
    repeat->next_ms = now_ms + ((uint32_t)(now_ms - repeat->started_ms) >= 1500u ? 30u : 60u);
    if (thy_state_adjust(state, repeat->action == THY_STATE_UP ? 1 : -1) != THY_STATE_OK)
        thy_engine_repeat_cancel(engine);
    repeat->value = state->edit_value;
}

static void navigation_event(void *context, uint8_t action)
{
    thy_engine *engine = context;
    if (thy_engine_navigate(engine, action) == THY_STATE_OK) repeat_arm(engine, action);
}

static int shortcut_index(uint8_t action)
{
    return action == THY_STATE_LEFT ? 0 : action == THY_STATE_UP ? 1 :
        action == THY_STATE_DOWN ? 2 : action == THY_STATE_RIGHT ? 3 : -1;
}

uint16_t thy_engine_arp_quick_row(uint8_t cursor)
{
    static const uint16_t rows[THY_ENGINE_ARP_QUICK_ROWS] = {
        THY_ROW_ARPS_TARGET, THY_ROW_ARPS_ENABLED, THY_ROW_ARPS_TYPE,
        THY_ROW_ARPS_GROUPING, THY_ROW_ARPS_NOTEVALUE, THY_ROW_ARPS_GATE,
        THY_ROW_ARPS_SWING, THY_ROW_ARPS_LAYER, THY_ROW_ARPS_SOURCEZONES,
        THY_ROW_ARPS_SYNC, THY_ROW_ARPS_RESTART, THY_ROW_ARPS_PATTERN,
        THY_ROW_ARPS_CHANNEL, THY_ROW_ARPS_DESTINATIONS, THY_ROW_ARPS_QUICKKEYS
    };
    return cursor < THY_ENGINE_ARP_QUICK_ROWS ? rows[cursor] : THY_STATE_NO_ROW;
}

static void quick_transition(thy_engine *engine, int8_t direction)
{
    engine->state.transition_direction = direction;
    if (++engine->state.transition_revision == 0u) engine->state.transition_revision = 1u;
}

int thy_engine_arp_quick_open(thy_engine *engine)
{
    if (!engine || !engine->instrument) return THY_STATE_INVALID;
    thy_engine_calibration_close(engine);
    if(engine->calibration_open)return THY_STATE_BLOCKED;
    if (engine->arp_quick_open && engine->state.level == THY_STATE_PLAY) return THY_STATE_OK;
    if(engine->sequencer_ui.open) (void)thy_engine_sequencer_close(engine);
    thy_state_play(&engine->state);
    engine->arp_quick_open = 1u;
    if (engine->arp_quick_cursor >= THY_ENGINE_ARP_QUICK_ROWS) engine->arp_quick_cursor = 0u;
    quick_transition(engine, -1);
    return THY_STATE_OK;
}

int thy_engine_arp_quick_close(thy_engine *engine)
{
    if (!engine || !engine->instrument) return THY_STATE_INVALID;
    if (!engine->arp_quick_open) return THY_STATE_OK;
    thy_state_play(&engine->state);
    engine->arp_quick_open = 0u;
    quick_transition(engine, 1);
    return THY_STATE_OK;
}

int thy_engine_control(thy_engine *engine, uint8_t bank, uint8_t index, int pressed)
{
    if (!engine || !engine->instrument) return 0;
    if(engine->calibration_open && bank!=0u)return 1;
    if (bank == 0u) thy_engine_repeat_cancel(engine);
    if (thy_sequencer_ui_control(&engine->sequencer_ui, bank, index, pressed)) return 1;
    if (!engine->arp_quick_open || engine->state.level != THY_STATE_PLAY ||
        !engine->arp_quick_keys || bank != 3u || index < 2u || index > 8u) {
        if (pressed && bank == 3u && index < THY_SCALE_SHORTCUT_COUNT) {
            uint8_t assignment = thy_instrument_scale_shortcut(engine->instrument, index);
            if (assignment < engine->state.scale_count)
                (void)thy_state_set(&engine->state, THY_ROW_PERFORMANCE_SCALE, assignment);
            else if(assignment>70u&&assignment<=78u)(void)thy_engine_user_scale(engine,(uint8_t)(assignment-70u));
        }
        return 0;
    }
    if (!pressed) return 1;
    unsigned key = index - 2u;
    if (key < 2u) {
        (void)thy_state_set_instance(&engine->state, THY_ROW_ARPS_ENABLED, (uint8_t)key,
            !engine->arps[key].config.enabled);
    } else if (key < 4u) {
        engine->state.notice = THY_NOTICE_HOLD_ONLY;
    } else if (key < 6u) {
        int result = thy_engine_arp_clear(engine, (uint8_t)(key - 4u));
        engine->state.notice = result == THY_OK ? THY_NOTICE_APPLIED : THY_NOTICE_ADAPTER;
    } else {
        if (engine->state.editing) (void)thy_state_cancel(&engine->state);
        (void)thy_state_set(&engine->state, THY_ROW_ARPS_TARGET, !engine->arp_target);
    }
    return 1;
}

static int control_filter(void *context, uint8_t bank, uint8_t index, int pressed)
{
    return thy_engine_control(context, bank, index, pressed);
}

int thy_engine_navigate(thy_engine *engine, uint8_t action)
{
    if (engine == NULL || engine->instrument == NULL || action > THY_STATE_RIGHT)
        return THY_STATE_INVALID;
    thy_engine_repeat_cancel(engine);
    if(engine->instrument->library_editor.open)return thy_engine_library_navigate(engine,action);
    if(engine->calibration_open)return thy_engine_calibration_navigate(engine,action);
    if(engine->sensitivity_pick) {
        engine->sensitivity_pick=engine->sensitivity_picked=0u;
        if(action==THY_STATE_LEFT){engine->state.notice=THY_NOTICE_CANCELLED;return THY_STATE_OK;}
    }
    if(engine->song_status>1u) engine->song_status=0u;
    if (engine->sequencer_ui.open && action == THY_STATE_SELECT)
        return thy_engine_sequencer_close(engine);
    if (engine->sequencer_ui.options_open) {
        if(action==THY_STATE_LEFT && !engine->state.editing && !engine->state.info_open)
            return thy_engine_sequencer_options_close(engine);
        return thy_state_navigate(&engine->state,action);
    }
    if (engine->sequencer_ui.open && engine->state.level == THY_STATE_PLAY)
        return thy_sequencer_ui_navigate(&engine->sequencer_ui, action) ? THY_STATE_OK : THY_STATE_INVALID;
    if (engine->arp_quick_open && engine->state.level != THY_STATE_PLAY)
        engine->arp_quick_open = 0u;
    if (engine->arp_quick_open) {
        if (action == THY_STATE_SELECT) return thy_engine_arp_quick_close(engine);
        if (engine->state.editing) {
            if (action == THY_STATE_LEFT) return thy_state_cancel(&engine->state);
            if (action == THY_STATE_RIGHT) return thy_state_commit(&engine->state);
            return thy_state_adjust(&engine->state, action == THY_STATE_UP ? 1 : -1);
        }
        if (action == THY_STATE_LEFT) return thy_engine_arp_quick_close(engine);
        if (action == THY_STATE_RIGHT)
            return thy_state_begin_edit(&engine->state, thy_engine_arp_quick_row(engine->arp_quick_cursor));
        int delta = action == THY_STATE_UP ? -1 : 1;
        engine->arp_quick_cursor = (uint8_t)((engine->arp_quick_cursor + delta + THY_ENGINE_ARP_QUICK_ROWS) % THY_ENGINE_ARP_QUICK_ROWS);
        engine->state.notice = THY_NOTICE_NONE;
        return THY_STATE_OK;
    }
    if (engine->state.level != THY_STATE_PLAY || engine->state.about_open ||
        action == THY_STATE_SELECT)
        return thy_state_navigate(&engine->state, action);
    int index = shortcut_index(action);
    uint8_t shortcut = index >= 0 ? engine->instrument->play_nav[index] : THY_PLAY_HIDDEN;
    static const uint16_t rows[] = {
        THY_STATE_NO_ROW, THY_ROW_LIGHTS_VIEW, THY_ROW_PERFORMANCE_VELOCITY,
        THY_ROW_MIDI_ROUTINGPROFILE, THY_ROW_LIGHTS_ENABLED,
        THY_ROW_SENSITIVITY_CENTERRESPONSE, THY_ROW_PERFORMANCE_ROOT
    };
    if (shortcut == THY_PLAY_HIDDEN) return THY_STATE_OK;
    if (shortcut == THY_PLAY_ARPS) return thy_engine_arp_quick_open(engine);
    if (shortcut == THY_PLAY_ENGINES) return thy_state_focus(&engine->state, thy_menu_rows[THY_ROW_ENGINES_PLAY].group, THY_ROW_ENGINES_PLAY);
    if (shortcut >= sizeof rows / sizeof rows[0]) return THY_STATE_INVALID;
    uint16_t row = rows[shortcut];
    return thy_state_focus(&engine->state, thy_menu_rows[row].group, row);
}

void thy_engine_center_levels(const thy_engine *engine,
    uint16_t levels[THY_INSTRUMENT_CENTER_LIGHTS])
{
    if (levels == NULL) return;
    for (unsigned n = 0u; n < THY_INSTRUMENT_CENTER_LIGHTS; ++n) levels[n] = 0u;
    if (engine == NULL || engine->instrument == NULL) return;
    const thy_instrument *i = engine->instrument;
    if (!i->center_brightness || i->center_brightness > 8u) return;
    for (unsigned n = 0u; n < 5u; ++n) {
        int index = shortcut_index((uint8_t)n);
        int available = i->library_editor.open || engine->calibration_open || engine->sequencer_ui.open || (engine->arp_quick_open || engine->state.about_open ?
            engine->arp_quick_open || n == THY_STATE_LEFT || n == THY_STATE_SELECT :
            engine->state.level != THY_STATE_PLAY || n == THY_STATE_SELECT ||
            (index >= 0 && i->play_nav[index] > THY_PLAY_HIDDEN &&
                i->play_nav[index] < THY_PLAY_SHORTCUT_COUNT));
        if (available) levels[n] = (i->control_masks[0] & (1u << n)) ? 4095u : 1536u;
    }
    /* One immutable observation for this evaluation. Generated whole-side
     * availability resolves 48 cells; do not repeat it for every indicator. */
    uint8_t lower_active=(uint8_t)(thy_instrument_lower_mask(i)&thy_instrument_lower_available(i));
    uint8_t drop_active=i->voicing_mode?(uint8_t)(i->drop_mask&thy_instrument_drop_available(i)):0u;
    for (unsigned n = 0u; n < 14u; ++n) {
        int active;
        if (n < 3u) active = i->voicing_mode?!!(drop_active&(1u<<n)):
            i->performance.inversion == n + 1u;
        else if (n == 3u) active = i->performance.strum != 0u;
        else if (n < 7u) active = i->performance.spread == n - 3u;
        else {
            uint8_t bit = (uint8_t)(1u << (n - 7u));
            active = !!(lower_active & bit);
        }
        levels[5u + n] = active ? 4095u : 256u;
        if (i->control_masks[1] & (1u << n)) levels[5u + n] = 4095u;
    }
    for (unsigned n = 0u; n < THY_INSTRUMENT_CENTER_LIGHTS; ++n)
        levels[n] = (uint16_t)(((uint32_t)levels[n] * i->center_brightness + 4u) / 8u);
}

void thy_engine_bind_controls(thy_engine *engine)
{
    if (engine != NULL && engine->instrument != NULL) {
        thy_instrument_set_navigation_handler(engine->instrument, navigation_event, engine);
        thy_instrument_set_control_filter(engine->instrument, control_filter, engine);
    }
}

int thy_engine_catalog(thy_engine *engine, uint16_t scale, uint16_t scale_count)
{
    if (engine == NULL || scale_count == 0u || scale >= scale_count)
        return THY_STATE_INVALID;
    uint16_t previous = engine->scale;
    engine->scale = scale;
    if (engine->instrument) engine->session.playing_mode = engine->instrument->engine_kind;
    if (thy_state_set_scale_count(&engine->state, scale_count) != THY_STATE_OK) {
        engine->scale = previous;
        return THY_STATE_INVALID;
    }
    if(engine->instrument && previous!=scale){thy_instrument_key_changed(engine->instrument);
        engine->instrument->user_scale=0u;(void)thy_instrument_apply_scale(engine->instrument);}
    engine->session.system = THY_ENGINE_READY;
    return THY_STATE_OK;
}
