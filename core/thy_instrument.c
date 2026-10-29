/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_instrument.h"

static const uint8_t control_counts[4] = {5u, 14u, 12u, 12u};

static int valid(const thy_instrument *instrument)
{
    return instrument != NULL && instrument->core != NULL;
}

static int error(thy_instrument *instrument, int result)
{
    if (result != THY_OK) {
        ++instrument->errors;
        instrument->last_error = result;
    }
    return result;
}

static const thy_instrument_response factory_response = {
    THY_INSTRUMENT_FACTORY_ATTACK, THY_INSTRUMENT_FACTORY_RELEASE,
    1353u, 4095u, THY_PRESSURE_LINEAR
};
static int response_editable(const thy_instrument *,unsigned,const thy_instrument_response *);
static void apply_response(thy_instrument_sensor *,const thy_instrument_response *,uint8_t);

void thy_instrument_velocity_defaults(thy_instrument_velocity *v)
{
    if(v)*v=(thy_instrument_velocity){2u,128u,THY_VELOCITY_FIXED,THY_PRESSURE_LINEAR,1u,127u};
}
int thy_instrument_velocity_valid(const thy_instrument_velocity *v)
{
    return v && v->soft_rate<v->hard_rate && v->hard_rate<=4095u &&
        v->mode<=THY_VELOCITY_STRIKE && v->curve<=THY_PRESSURE_FIRM &&
        v->minimum>=1u && v->minimum<=v->maximum && v->maximum<=127u;
}
uint8_t thy_instrument_velocity_value(const thy_instrument_velocity *v,uint16_t rate)
{
    if(!thy_instrument_velocity_valid(v))return 0u;
    uint32_t n=rate<=v->soft_rate?0u:rate>=v->hard_rate?127u:
        ((uint32_t)rate-v->soft_rate)*127u/(v->hard_rate-v->soft_rate);
    if(v->curve==THY_PRESSURE_SOFT)n=127u-(127u-n)*(127u-n)/127u;
    else if(v->curve==THY_PRESSURE_FIRM)n=n*n/127u;
    return (uint8_t)(v->minimum+((v->maximum-v->minimum)*n+63u)/127u);
}
static int velocity_editable(const thy_instrument *i,unsigned p)
{
    const thy_instrument_sensor *s=&i->sensors[p];
    return !s->held && !s->blocked && !s->attack_count && !s->strike_ready &&
        !thy_core_pad_held(i->core,(uint8_t)p) &&
        (s->detector_profile==THY_DETECTOR_RELATIVE?
         s->delta<=s->calibration.release_delta:s->raw<=s->response.release_raw);
}
static void clear_strike(thy_instrument_sensor *s)
{s->strike_peak_ms=s->last_sample_ms;s->strike_peak_rate=0u;s->strike_ready=0u;s->strike_velocity=0u;}
int thy_instrument_get_velocity(const thy_instrument *i,int p,thy_instrument_velocity *v,uint8_t *overridden)
{
    if(!valid(i) || !v || !overridden || p< -1 || p>=(int)THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    *overridden=p>=0?i->velocity_overridden[p]:0u;
    if(p>=0 && i->mapping[p]>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    *v=*overridden?i->pad_velocity[p]:p<0?i->velocity_response:i->cluster_velocity[i->mapping[p]/48u];
    return thy_instrument_velocity_valid(v)?THY_OK:THY_INVALID_ARGUMENT;
}
int thy_instrument_set_velocity(thy_instrument *i,const thy_instrument_velocity *v)
{
    if(!valid(i) || !thy_instrument_velocity_valid(v))return THY_INVALID_ARGUMENT;
    thy_instrument_velocity requested=*v;
    for(unsigned p=0;p<THY_PAD_COUNT;++p)if(!i->velocity_overridden[p] && !velocity_editable(i,p)){
        i->menu_notice=1u;return THY_INVALID_ARGUMENT;
    }
    i->velocity_response=requested;
    i->cluster_velocity[0]=i->cluster_velocity[1]=requested;
    for(unsigned p=0;p<THY_PAD_COUNT;++p)if(!i->velocity_overridden[p])clear_strike(&i->sensors[p]);
    i->menu_notice=0u;++i->revision;return THY_OK;
}
int thy_instrument_set_pad_velocity(thy_instrument *i,uint8_t p,const thy_instrument_velocity *v)
{
    if(!valid(i) || p>=THY_PAD_COUNT || !thy_instrument_velocity_valid(v))return THY_INVALID_ARGUMENT;
    if(!velocity_editable(i,p)){i->menu_notice=1u;return THY_INVALID_ARGUMENT;}
    i->pad_velocity[p]=*v;i->velocity_overridden[p]=1u;clear_strike(&i->sensors[p]);
    i->menu_notice=0u;++i->revision;return THY_OK;
}
int thy_instrument_clear_pad_velocity(thy_instrument *i,uint8_t p)
{
    if(!valid(i) || p>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    if(!velocity_editable(i,p)){i->menu_notice=1u;return THY_INVALID_ARGUMENT;}
    if(i->mapping[p]>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    i->pad_velocity[p]=i->cluster_velocity[i->mapping[p]/48u];i->velocity_overridden[p]=0u;clear_strike(&i->sensors[p]);
    i->menu_notice=0u;++i->revision;return THY_OK;
}

static int valid_response(const thy_instrument_response *response)
{
    return response != NULL && response->release_raw < response->attack_raw &&
        response->attack_raw <= 4095u && response->pressure_floor < response->pressure_full_scale &&
        response->pressure_full_scale <= 4095u &&
        response->pressure_curve <= THY_PRESSURE_FIRM;
}
static int same_response(const thy_instrument_response *a,const thy_instrument_response *b)
{return a->attack_raw==b->attack_raw && a->release_raw==b->release_raw &&
    a->pressure_floor==b->pressure_floor && a->pressure_full_scale==b->pressure_full_scale &&
    a->pressure_curve==b->pressure_curve;}
void thy_instrument_release_defaults(thy_instrument_release_policy *policy)
{if(policy)*policy=(thy_instrument_release_policy){23u,1u};}
int thy_instrument_release_policy_valid(const thy_instrument_release_policy *policy)
{return policy && policy->linked<=1u && policy->gap>=1u && policy->gap<=4095u;}
static uint16_t linked_release(uint16_t attack,uint16_t gap)
{return attack>gap?(uint16_t)(attack-gap):0u;}

uint8_t thy_instrument_response_pressure(const thy_instrument_response *response,
    uint16_t raw)
{
    uint32_t normalized;
    if (!valid_response(response) || raw <= response->pressure_floor) return 0u;
    if (raw >= response->pressure_full_scale) return 127u;
    normalized = ((uint32_t)raw - response->pressure_floor) * 127u /
        (response->pressure_full_scale - response->pressure_floor);
    if (response->pressure_curve == THY_PRESSURE_SOFT)
        normalized = 127u - (127u - normalized) * (127u - normalized) / 127u;
    else if (response->pressure_curve == THY_PRESSURE_FIRM)
        normalized = normalized * normalized / 127u;
    return (uint8_t)normalized;
}

static void sensor_levels_with_response(thy_instrument_sensor *sensor,
    const thy_instrument_response *response, int absolute)
{
    int32_t delta = ((int32_t)sensor->raw - sensor->calibration.baseline) *
                    sensor->calibration.polarity;
    sensor->signed_delta = (int16_t)delta;
    sensor->delta = delta > 0 ? (uint16_t)delta : 0u;
    if (absolute || sensor->detector_profile != THY_DETECTOR_RELATIVE) {
        sensor->attack_raw = (int16_t)response->attack_raw;
        sensor->release_raw = (int16_t)response->release_raw;
        /* Continuous pressure is independent of the recent ADC rise used for
         * optional strike velocity. */
        sensor->pressure = thy_instrument_response_pressure(response,
            sensor->raw);
    } else {
        uint32_t normalized = (uint32_t)sensor->delta * 127u /
                              sensor->calibration.full_scale;
        sensor->attack_raw = (int16_t)((int32_t)sensor->calibration.baseline +
            sensor->calibration.polarity * (int32_t)sensor->calibration.press_delta);
        sensor->release_raw = (int16_t)((int32_t)sensor->calibration.baseline +
            sensor->calibration.polarity * (int32_t)sensor->calibration.release_delta);
        sensor->pressure = (uint8_t)(normalized > 127u ? 127u : normalized);
    }
}
static void sensor_levels(thy_instrument_sensor *sensor)
{sensor_levels_with_response(sensor,&sensor->response,0);}

static void record_transition(thy_instrument *instrument, uint8_t electrical,
    uint8_t action, uint32_t now_ms)
{
    thy_instrument_sensor *sensor = &instrument->sensors[electrical];
    thy_instrument_pad_transition *entry;
    ++instrument->transition_sequence;
    if (instrument->transition_sequence == 0u) {
        /* Do not ambiguously retain entries from the previous sequence epoch. */
        instrument->transition_sequence = 1u;
        instrument->transition_count = 0u;
    }
    entry = &instrument->transitions[(instrument->transition_sequence - 1u) %
                                    THY_INSTRUMENT_TRANSITIONS];
    *entry = (thy_instrument_pad_transition){instrument->transition_sequence,
        now_ms, sensor->raw, sensor->calibration.baseline, sensor->delta,
        sensor->signed_delta, sensor->attack_raw, sensor->release_raw,
        electrical, instrument->mapping[electrical], action, sensor->blocked,
        sensor->detector_profile, sensor->pressure};
    if (instrument->transition_count < THY_INSTRUMENT_TRANSITIONS)
        ++instrument->transition_count;
    else ++instrument->transition_overwritten;
    sensor->last_transition_ms = now_ms;
}

int thy_instrument_get_transition(const thy_instrument *instrument,
    uint32_t sequence, thy_instrument_pad_transition *transition)
{
    const thy_instrument_pad_transition *entry;
    if (instrument == NULL || transition == NULL || sequence == 0u ||
        sequence > instrument->transition_sequence ||
        instrument->transition_sequence - sequence >= instrument->transition_count)
        return 0;
    entry = &instrument->transitions[(sequence - 1u) % THY_INSTRUMENT_TRANSITIONS];
    if (entry->sequence != sequence) return 0;
    *transition = *entry;
    return 1;
}

void thy_instrument_init(thy_instrument *instrument, thy_core *core)
{
    unsigned i;
    if (instrument == NULL) return;
    *instrument = (thy_instrument){0};
    instrument->core = core;
    instrument->keep_transpose = instrument->live_lock_scale = 1u;
    instrument->performance.shortcut = 2u;
    for (i = 0u; i < THY_SCALE_SHORTCUT_COUNT; ++i)
        instrument->scale_shortcuts[i] = thy_instrument_default_scale_shortcut((uint8_t)i);
    instrument->performance.strum_ms = 24u;
    instrument->lower_mode = THY_LOWER_OCTAVE_LAYERS;
    instrument->octave_layers = 8u;
    for (i = 0u; i < 48u; ++i) instrument->pad_octave_layers[i] = 8u;
    instrument->center_poll_ms = 8u;
    instrument->animations_enabled = 1u;
    instrument->light_key_contrast = 1u;
    instrument->light_key_contrast_percent = 45u;
    instrument->display_counts = 1u;
    instrument->display_pretty_counts = 1u;
    instrument->display_motion = 1u;
    instrument->engine_kind = THY_PLAY_ENGINE_PLAY;
    instrument->display_view = THY_VIEW_SIMPLE;
    thy_views_defaults(instrument->display_config);
    instrument->center_brightness = 8u;
    instrument->context_mask = 63u;
    instrument->play_nav[0] = THY_PLAY_ENGINES;
    instrument->play_nav[1] = THY_PLAY_VIEWS;
    instrument->play_nav[2] = THY_PLAY_PERFORMANCE;
    instrument->play_nav[3] = THY_PLAY_CHORDS;
    instrument->light_held_feedback = 1u;
    instrument->fixed_velocity = 96u;
    thy_instrument_velocity_defaults(&instrument->velocity_response);
    thy_instrument_release_defaults(&instrument->release_policy);
    for(i=0u;i<2u;++i)thy_expression_profile_defaults(&instrument->expression_profiles[i]);
    for(i=0u;i<THY_PAD_COUNT;++i)thy_expression_profile_defaults(&instrument->pad_expression_profiles[i]);
    instrument->response = factory_response;
    for(i=0u;i<2u;++i){
        instrument->cluster_response[i]=instrument->response;
        instrument->cluster_velocity[i]=instrument->velocity_response;
        instrument->cluster_release_policy[i]=instrument->release_policy;
    }
    instrument->lights_enabled = 1u;
    instrument->light_idle = 3u;
    instrument->light_held = 8u;
    instrument->light_fade_ms = 120u;
    instrument->input_enabled = 1u;
    instrument->destinations[0] = THY_ROUTE_USB0;
    instrument->destinations[1] = THY_ROUTE_USB0;
    instrument->channels[1] = 1u;
    instrument->routing_profile = THY_ROUTING_SPLIT;
    for (i = 0u; i < THY_INSTRUMENT_ROUTE_TARGETS; ++i) {
        instrument->route_channels[i] = i == 2u || i >= 6u ? 1u : 0u;
        instrument->route_destinations[i] = THY_ROUTE_USB0;
    }
    instrument->melody_anchor = 60;
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        instrument->mapping[i] = (uint8_t)i;
        instrument->sensors[i].calibration =
            (thy_instrument_calibration){0u, 96u, 32u, 4095u, 1};
        instrument->sensors[i].response = factory_response;
        instrument->pad_velocity[i]=instrument->velocity_response;
        instrument->pad_release_policy[i]=instrument->release_policy;
        sensor_levels(&instrument->sensors[i]);
    }
}

void thy_instrument_set_action_handler(thy_instrument *instrument,
    thy_instrument_action_fn action, void *context)
{
    if (instrument == NULL) return;
    instrument->action = action;
    instrument->action_context = context;
}

void thy_instrument_set_navigation_handler(thy_instrument *instrument,
    thy_instrument_navigation_fn navigation_handler, void *context)
{
    if (instrument == NULL) return;
    instrument->navigation = navigation_handler;
    instrument->navigation_context = context;
}

void thy_instrument_set_musical_handler(thy_instrument *instrument,
    thy_instrument_attack_fn attack, thy_instrument_release_fn release,
    thy_instrument_reset_fn reset, void *context)
{
    if (!instrument) return;
    instrument->musical_attack = attack;
    instrument->musical_release = release;
    instrument->musical_pressure = NULL;
    instrument->musical_reset = reset;
    instrument->musical_melody_plan = NULL;
    instrument->musical_voicing_plan = NULL;
    instrument->musical_chord_plan = NULL;
    instrument->musical_quiet = NULL;
    instrument->musical_context = context;
}

void thy_instrument_set_sampling_provider(thy_instrument *instrument,
    thy_instrument_sampling_provider_fn provider,void *context)
{
    if(!instrument)return;
    instrument->sampling_provider=provider;
    instrument->sampling_provider_context=provider?context:NULL;
}

void thy_instrument_set_control_filter(thy_instrument *instrument,
    thy_instrument_control_filter_fn filter, void *context)
{
    if (!instrument) return;
    instrument->control_filter = filter;
    instrument->control_filter_context = context;
}

void thy_instrument_set_pad_filter(thy_instrument *instrument,
    thy_instrument_pad_filter_fn filter, void *context)
{
    if (!instrument) return;
    instrument->pad_filter = filter;
    instrument->pad_filter_context = context;
}

int thy_instrument_set_layout(thy_instrument *instrument,
    const thy_instrument_pad pads[THY_PAD_COUNT], int16_t anchor,
    const int16_t *degrees, uint8_t degree_count)
{
    unsigned i, slot;
    if (!valid(instrument) || pads == NULL || anchor < 0 || anchor > 127 ||
        degree_count == 0u || degree_count > 12u || degrees == NULL ||
        degrees[0] != 0 || instrument->user_scale>8u ||
        (instrument->user_scale&&(!instrument->library.scales[instrument->user_scale-1u].used||
        !thy_library_scale_valid(&instrument->library.scales[instrument->user_scale-1u])))) return THY_INVALID_ARGUMENT;
    for (i = 0u; i < degree_count; ++i) {
        if (degrees[i] < 0 || degrees[i] > 11 ||
            (i != 0u && degrees[i] <= degrees[i - 1u]))
            return THY_INVALID_ARGUMENT;
    }
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        if ((pads[i].valid_mask & 0x80u) != 0u) return THY_INVALID_ARGUMENT;
        for (slot = 0u; slot < THY_INSTRUMENT_SLOTS; ++slot) {
            if ((pads[i].valid_mask & (1u << slot)) != 0u &&
                (pads[i].notes[slot] < 0 || pads[i].notes[slot] > 127))
                return THY_INVALID_ARGUMENT;
        }
    }
    instrument->layout = pads;
    instrument->live_degree_count = 0u;
    instrument->melody_anchor = anchor;
    instrument->degree_count = degree_count;
    instrument->catalog_degree_count=degree_count;
    for (i = 0u; i < degree_count; ++i)instrument->catalog_degrees[i]=instrument->degrees[i]=degrees[i];
    if(thy_instrument_apply_scale(instrument)!=THY_OK)return THY_INVALID_ARGUMENT;
    for (i = 0u; i < 48u; ++i) {
        instrument->excluded_slots[i] = 0u;
        instrument->pad_octave_layers[i] = 8u;
    }
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_apply_scale(thy_instrument *i)
{
    if(!valid(i)||i->user_scale>THY_LIBRARY_SCALES||i->catalog_degree_count>12u)return THY_INVALID_ARGUMENT;
    if(i->user_scale){
        uint8_t degrees[12],count;
        if(thy_library_scale_degrees(&i->library.scales[i->user_scale-1u],degrees,&count)!=THY_LIBRARY_OK)return THY_INVALID_ARGUMENT;
        i->degree_count=count;for(unsigned n=0;n<count;++n)i->degrees[n]=degrees[n];
    } else {
        i->degree_count=i->catalog_degree_count;
        for(unsigned n=0;n<i->catalog_degree_count;++n)i->degrees[n]=i->catalog_degrees[n];
    }
    i->live_degree_count=0u;
    /* Harmony Apply key deliberately retains the preceding chord as its
     * voice-leading reference across the adapter's catalog reload. Explicit
     * key_changed callers still clear history before changing ordinary keys. */
    if(i->engine_kind!=THY_PLAY_ENGINE_HARMONY)i->voicing_previous_mask=0u;
    return THY_OK;
}

uint8_t thy_instrument_role_pad(const thy_instrument *i,uint8_t physical)
{
    if(!valid(i)||physical>=THY_PAD_COUNT||i->cluster_swap>1u)return UINT8_MAX;
    return i->cluster_swap?(uint8_t)((physical+48u)%96u):physical;
}
uint8_t thy_instrument_physical_pad(const thy_instrument *i,uint8_t role)
{ return thy_instrument_role_pad(i,role); }
int thy_instrument_set_cluster_swap(thy_instrument *i,uint8_t enabled)
{
    if(!valid(i)||enabled>1u)return THY_INVALID_ARGUMENT;
    if(i->cluster_swap==enabled)return THY_OK;
    const thy_core *c=i->core;
    if(c->queue_count||c->sounding_count||c->generated_count||c->pending_keys||c->sustain_count||
       (i->musical_quiet&&i->musical_quiet(i->musical_context)!=1))return THY_INVALID_ARGUMENT;
    for(unsigned p=0;p<THY_PAD_COUNT;++p)
        if(!velocity_editable(i,p)||i->context_held[p])return THY_INVALID_ARGUMENT;
    i->cluster_swap=enabled;
    ++i->layout_revision;++i->revision;
    return THY_OK;
}

int thy_instrument_set_mapping(thy_instrument *instrument,
    const uint8_t electrical_to_layout[THY_PAD_COUNT])
{
    uint8_t seen[THY_PAD_COUNT] = {0};
    unsigned i;
    if (!valid(instrument) || electrical_to_layout == NULL)
        return THY_INVALID_ARGUMENT;
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        unsigned target = electrical_to_layout[i];
        if (target >= THY_PAD_COUNT || seen[target] != 0u ||
            instrument->sensors[i].held != 0u || thy_core_pad_held(instrument->core,(uint8_t)i))
            return THY_INVALID_ARGUMENT;
        seen[target] = 1u;
        if(target/48u!=instrument->mapping[i]/48u){
            if(!instrument->sensors[i].response_overridden &&
                !response_editable(instrument,i,&instrument->cluster_response[target/48u]))
                return THY_INVALID_ARGUMENT;
            if(!instrument->velocity_overridden[i] && !velocity_editable(instrument,i))
                return THY_INVALID_ARGUMENT;
        }
    }
    for (i = 0u; i < THY_PAD_COUNT; ++i){
        unsigned old_side=instrument->mapping[i]/48u,side=electrical_to_layout[i]/48u;
        instrument->mapping[i] = electrical_to_layout[i];
        if(old_side!=side){
            if(!instrument->sensors[i].response_overridden){
                if(!same_response(&instrument->sensors[i].response,&instrument->cluster_response[side]))
                    apply_response(&instrument->sensors[i],&instrument->cluster_response[side],0u);
                instrument->pad_release_policy[i]=instrument->cluster_release_policy[side];
            }
            if(!instrument->velocity_overridden[i])clear_strike(&instrument->sensors[i]);
        }
    }
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_set_route(thy_instrument *instrument, uint8_t side,
    uint8_t channel, uint8_t destinations, int16_t transpose)
{
    if (!valid(instrument) || side > 1u || channel > 15u || destinations == 0u ||
        (destinations & ~THY_ROUTE_ALL) != 0u ||
        transpose < -127 || transpose > 127) return THY_INVALID_ARGUMENT;
    instrument->channels[side] = channel;
    instrument->destinations[side] = destinations;
    instrument->transpose[side] = transpose;
    if (side == 0u) {
        instrument->route_channels[0] = channel;
        instrument->route_destinations[0] = destinations;
    }
    for (unsigned zone = 3u + 3u * side; zone < 6u + 3u * side; ++zone) {
        instrument->route_channels[zone] = channel;
        instrument->route_destinations[zone] = destinations;
    }
    ++instrument->revision;
    return THY_OK;
}

uint8_t thy_instrument_route_target(const thy_instrument *instrument, uint8_t logical)
{
    if (!valid(instrument) || logical >= THY_PAD_COUNT) return UINT8_MAX;
    if (instrument->routing_profile == THY_ROUTING_WHOLE) return 0u;
    if (instrument->routing_profile == THY_ROUTING_SPLIT)
        return logical < 48u ? 1u : 2u;
    if (instrument->routing_profile == THY_ROUTING_ZONES)
        return (uint8_t)((logical < 48u ? 3u : 6u) + logical % 48u / 16u);
    return UINT8_MAX;
}

int thy_instrument_get_pad_route(const thy_instrument *instrument, uint8_t target,
    thy_instrument_route *route)
{
    thy_instrument_route resolved;
    if (!valid(instrument) || target >= THY_INSTRUMENT_ROUTE_TARGETS || route == NULL)
        return THY_INVALID_ARGUMENT;
    resolved.channel = target == 1u || target == 2u ?
        instrument->channels[target - 1u] : instrument->route_channels[target];
    resolved.destinations = target == 1u || target == 2u ?
        instrument->destinations[target - 1u] : instrument->route_destinations[target];
    resolved.velocity = instrument->route_velocity[target] != 0u ?
        instrument->route_velocity[target] : instrument->fixed_velocity;
    resolved.pressure_enabled = instrument->route_pressure[target] != 0u ?
        (uint8_t)(instrument->route_pressure[target] == 2u) : instrument->pressure_enabled;
    if (resolved.channel > 15u || !resolved.destinations ||
        (resolved.destinations & ~THY_ROUTE_ALL) ||
        resolved.velocity == 0u || resolved.velocity > 127u ||
        resolved.pressure_enabled > 1u) return THY_INVALID_ARGUMENT;
    *route = resolved;
    return THY_OK;
}

int thy_instrument_set_pad_route(thy_instrument *instrument, uint8_t target,
    uint8_t channel, uint8_t destinations)
{
    if (!valid(instrument) || target >= THY_INSTRUMENT_ROUTE_TARGETS || channel > 15u ||
        destinations == 0u || (destinations & ~THY_ROUTE_ALL)) return THY_INVALID_ARGUMENT;
    if (target == 1u || target == 2u) {
        instrument->channels[target - 1u] = channel;
        instrument->destinations[target - 1u] = destinations;
    } else {
        instrument->route_channels[target] = channel;
        instrument->route_destinations[target] = destinations;
    }
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_set_routing_profile(thy_instrument *instrument, uint8_t profile)
{
    if (!valid(instrument) || profile > THY_ROUTING_ZONES) return THY_INVALID_ARGUMENT;
    instrument->routing_profile = profile;
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_set_route_expression(thy_instrument *instrument, uint8_t target,
    uint8_t fixed_velocity, uint8_t pressure_enabled)
{
    if (!valid(instrument) || target >= THY_INSTRUMENT_ROUTE_TARGETS ||
        !fixed_velocity || fixed_velocity > 127u || pressure_enabled > 1u)
        return THY_INVALID_ARGUMENT;
    instrument->route_velocity[target] = fixed_velocity;
    instrument->route_pressure[target] = (uint8_t)(pressure_enabled + 1u);
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_set_route_velocity(thy_instrument *instrument, uint8_t target,
    uint8_t fixed_velocity)
{
    if (!valid(instrument) || target >= THY_INSTRUMENT_ROUTE_TARGETS ||
        !fixed_velocity || fixed_velocity > 127u) return THY_INVALID_ARGUMENT;
    instrument->route_velocity[target] = fixed_velocity;
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_set_route_pressure(thy_instrument *instrument, uint8_t target,
    uint8_t pressure_enabled)
{
    if (!valid(instrument) || target >= THY_INSTRUMENT_ROUTE_TARGETS ||
        pressure_enabled > 1u) return THY_INVALID_ARGUMENT;
    instrument->route_pressure[target] = (uint8_t)(pressure_enabled + 1u);
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_calibrate(thy_instrument *instrument, uint8_t electrical,
    const thy_instrument_calibration *calibration)
{
    thy_instrument_sensor *sensor;
    if (!valid(instrument) || electrical >= THY_PAD_COUNT || calibration == NULL ||
        calibration->baseline > 4095u || calibration->release_delta == 0u ||
        calibration->release_delta >= calibration->press_delta ||
        calibration->press_delta > calibration->full_scale ||
        calibration->full_scale > 4095u ||
        (calibration->polarity != 1 && calibration->polarity != -1) ||
        instrument->sensors[electrical].held != 0u)
        return THY_INVALID_ARGUMENT;
    sensor = &instrument->sensors[electrical];
    sensor->calibration = *calibration;
    sensor->baseline_q8 = (int32_t)calibration->baseline * 256;
    sensor->warmup_count = THY_INSTRUMENT_WARMUP_SAMPLES;
    sensor->warmup_sum = 0u;
    sensor->delta = 0u;
    sensor->pressure = 0u;
    sensor->sent_pressure = 0u;
    sensor->blocked = 0u;
    sensor->attack_count = 0u;
    sensor->release_count = 0u;
    sensor->detector_profile = THY_DETECTOR_RELATIVE;
    sensor->response_overridden = 1u;
    instrument->pad_release_policy[electrical]=(thy_instrument_release_policy){
        (uint16_t)(sensor->response.attack_raw-sensor->response.release_raw),1u};
    sensor->raw = calibration->baseline;
    clear_strike(sensor);
    sensor_levels(sensor);
    return THY_OK;
}

int thy_instrument_set_detector_profile(thy_instrument *instrument, uint8_t profile)
{
    unsigned i;
    if (!valid(instrument) || profile > THY_DETECTOR_FACTORY_LINEAR)
        return THY_INVALID_ARGUMENT;
    for (i = 0u; i < THY_PAD_COUNT; ++i)
        if (instrument->sensors[i].held != 0u) return THY_INVALID_ARGUMENT;
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        thy_instrument_calibration calibration = instrument->sensors[i].calibration;
        calibration.baseline = 0u;
        if (profile == THY_DETECTOR_FACTORY_LINEAR) calibration.polarity = 1;
        instrument->sensors[i] = (thy_instrument_sensor){0};
        instrument->sensors[i].calibration = calibration;
        instrument->sensors[i].response = factory_response;
        instrument->sensors[i].detector_profile = profile;
        thy_instrument_release_defaults(&instrument->pad_release_policy[i]);
        sensor_levels(&instrument->sensors[i]);
    }
    instrument->response = factory_response;
    thy_instrument_release_defaults(&instrument->release_policy);
    for(i=0u;i<2u;++i){
        instrument->cluster_response[i]=factory_response;
        instrument->cluster_release_policy[i]=instrument->release_policy;
    }
    ++instrument->revision;
    return THY_OK;
}

static int response_editable(const thy_instrument *instrument, unsigned electrical,
    const thy_instrument_response *response)
{
    const thy_instrument_sensor *sensor = &instrument->sensors[electrical];
    const thy_expression_profile *profile=instrument->expression_profile_overridden[electrical]?
        &instrument->pad_expression_profiles[electrical]:&instrument->expression_profiles[instrument->mapping[electrical]/48u];
    if(profile->enabled && response->pressure_full_scale-response->pressure_floor<2u)return 0;
    return sensor->held == 0u && sensor->blocked == 0u &&
        sensor->attack_count == 0u && !thy_core_pad_held(instrument->core,
            (uint8_t)electrical) &&
        (sensor->detector_profile == THY_DETECTOR_RELATIVE ?
            sensor->delta <= sensor->calibration.release_delta :
            (int32_t)sensor->raw <= sensor->release_raw) &&
        sensor->raw <= response->release_raw;
}

int thy_instrument_get_pad_guard(const thy_instrument *i,uint8_t p,uint16_t *minimum_release)
{
    if(!valid(i) || p>=THY_PAD_COUNT || !minimum_release)return THY_INVALID_ARGUMENT;
    *minimum_release=i->sensitivity_guard[p];return THY_OK;
}
int thy_instrument_set_pad_guard(thy_instrument *i,uint8_t p,uint16_t minimum_release)
{
    if(!valid(i) || p>=THY_PAD_COUNT || minimum_release>4094u ||
        !response_editable(i,p,&i->sensors[p].response))return THY_INVALID_ARGUMENT;
    if(i->sensitivity_guard[p]!=minimum_release){i->sensitivity_guard[p]=minimum_release;++i->revision;}
    return THY_OK;
}

static void apply_response(thy_instrument_sensor *sensor,
    const thy_instrument_response *response, uint8_t overridden)
{
    thy_instrument_calibration calibration = sensor->calibration;
    uint16_t raw = sensor->raw;
    uint32_t sampled = sensor->last_sample_ms;
    uint32_t transitioned = sensor->last_transition_ms;
    calibration.baseline = 0u;
    calibration.polarity = 1;
    *sensor = (thy_instrument_sensor){0};
    sensor->calibration = calibration;
    sensor->response = *response;
    sensor->response_overridden = overridden;
    sensor->detector_profile = THY_DETECTOR_CUSTOM_ABSOLUTE;
    sensor->raw = raw;
    sensor->last_sample_ms = sampled;
    sensor->last_transition_ms = transitioned;
    sensor_levels(sensor);
}

int thy_instrument_check_response_policy(const thy_instrument *i,int p,
    const thy_instrument_response *response,const thy_instrument_release_policy *policy)
{
    if(!valid(i) || p< -1 || p>=(int)THY_PAD_COUNT || !valid_response(response) ||
        !thy_instrument_release_policy_valid(policy) ||
        (policy->linked && response->release_raw!=linked_release(response->attack_raw,policy->gap)))
        return THY_INVALID_ARGUMENT;
    unsigned first=p<0?0u:(unsigned)p,last=p<0?THY_PAD_COUNT:first+1u;
    for(unsigned n=first;n<last;++n)
        if((p>=0 || !i->sensors[n].response_overridden) && !response_editable(i,n,response))
            return THY_INVALID_ARGUMENT;
    return THY_OK;
}

static int commit_response(thy_instrument *instrument,int electrical,
    const thy_instrument_response *response,const thy_instrument_release_policy *policy,uint8_t overridden)
{
    if(!valid(instrument) || electrical< -1 || electrical>=(int)THY_PAD_COUNT ||
        !valid_response(response) || !thy_instrument_release_policy_valid(policy) ||
        (policy->linked && response->release_raw!=linked_release(response->attack_raw,policy->gap)))return THY_INVALID_ARGUMENT;
    const thy_instrument_response requested=*response;
    const thy_instrument_release_policy selected=*policy;
    unsigned first=electrical<0?0u:(unsigned)electrical,last=electrical<0?THY_PAD_COUNT:first+1u;
    for(unsigned i=first;i<last;++i)
        if ((electrical>=0 || instrument->sensors[i].response_overridden == 0u) &&
            !response_editable(instrument, i, &requested)) {
            instrument->menu_notice = 1u;
            return THY_INVALID_ARGUMENT;
        }
    for(unsigned i=first;i<last;++i)
        if(electrical>=0 || !instrument->sensors[i].response_overridden) {
            apply_response(&instrument->sensors[i],&requested,electrical<0?0u:overridden);
            instrument->pad_release_policy[i]=selected;
        }
    if(electrical<0){
        instrument->response=requested;instrument->release_policy=selected;
        for(unsigned side=0u;side<2u;++side){
            instrument->cluster_response[side]=requested;
            instrument->cluster_release_policy[side]=selected;
        }
    }
    instrument->menu_notice = 0u;
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_set_response_policy(thy_instrument *i,int p,
    const thy_instrument_response *response,const thy_instrument_release_policy *policy)
{return commit_response(i,p,response,policy,1u);}

int thy_instrument_get_release_policy(const thy_instrument *i,int p,
    thy_instrument_release_policy *policy,uint8_t *overridden)
{
    if(!valid(i) || !policy || !overridden || p< -1 || p>=(int)THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    *overridden=p<0?0u:i->sensors[p].response_overridden;
    if(p>=0 && i->mapping[p]>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    *policy=*overridden?i->pad_release_policy[p]:p<0?i->release_policy:i->cluster_release_policy[i->mapping[p]/48u];
    return thy_instrument_release_policy_valid(policy)?THY_OK:THY_INVALID_ARGUMENT;
}
int thy_instrument_set_release_policy(thy_instrument *i,int p,const thy_instrument_release_policy *policy)
{
    if(!valid(i) || p< -1 || p>=(int)THY_PAD_COUNT || !thy_instrument_release_policy_valid(policy))return THY_INVALID_ARGUMENT;
    thy_instrument_response response=p<0?i->response:i->sensors[p].response;
    if(policy->linked)response.release_raw=linked_release(response.attack_raw,policy->gap);
    return commit_response(i,p,&response,policy,1u);
}
int thy_instrument_set_trigger(thy_instrument *i,int p,uint16_t attack)
{
    thy_instrument_release_policy policy;uint8_t overridden;
    if(thy_instrument_get_release_policy(i,p,&policy,&overridden)!=THY_OK)return THY_INVALID_ARGUMENT;
    thy_instrument_response response=p<0?i->response:i->sensors[p].response;
    response.attack_raw=attack;
    if(policy.linked)response.release_raw=linked_release(attack,policy.gap);
    return commit_response(i,p,&response,&policy,1u);
}
static int scaled_response(const thy_instrument *i,int p,uint16_t old_attack,
    uint16_t new_attack,thy_instrument_response *r)
{
    const int overridden=p>=0 && i->sensors[p].response_overridden;
    *r=overridden?i->sensors[p].response:i->response;
    const thy_instrument_release_policy *policy=overridden?
        &i->pad_release_policy[p]:&i->release_policy;
    uint32_t attack=overridden?
        ((uint32_t)r->attack_raw*new_attack+old_attack/2u)/old_attack:new_attack;
    if(attack<1u || attack>4095u || !thy_instrument_release_policy_valid(policy))
        return THY_INVALID_ARGUMENT;
    r->attack_raw=(uint16_t)attack;
    if(policy->linked)r->release_raw=linked_release(r->attack_raw,policy->gap);
    return valid_response(r)?THY_OK:THY_INVALID_ARGUMENT;
}
int thy_instrument_scale_sensitivity(thy_instrument *i,uint16_t new_attack)
{
    if(!valid(i) || !valid_response(&i->response) || new_attack<1u || new_attack>4095u)
        return THY_INVALID_ARGUMENT;
    const uint16_t old_attack=i->response.attack_raw;
    if(new_attack==old_attack)return THY_OK;
    thy_instrument_response global;
    if(scaled_response(i,-1,old_attack,new_attack,&global)!=THY_OK)return THY_INVALID_ARGUMENT;
    /* Recompute small candidates in two bounded passes instead of retaining a
     * 96-entry configuration bank. All validation and quiet checks precede the
     * first publish; no callback or foreground yield occurs between passes. */
    for(unsigned p=0;p<THY_PAD_COUNT;++p) {
        thy_instrument_response r;
        if(scaled_response(i,(int)p,old_attack,new_attack,&r)!=THY_OK)return THY_INVALID_ARGUMENT;
        if(r.release_raw<i->sensitivity_guard[p])return THY_INVALID_ARGUMENT;
        if(!response_editable(i,p,&r)){i->menu_notice=1u;return THY_INVALID_ARGUMENT;}
    }
    for(unsigned p=0;p<THY_PAD_COUNT;++p) {
        thy_instrument_response r;
        (void)scaled_response(i,(int)p,old_attack,new_attack,&r);
        const uint8_t overridden=i->sensors[p].response_overridden;
        apply_response(&i->sensors[p],&r,overridden);
        if(!overridden)i->pad_release_policy[p]=i->release_policy;
    }
    i->response=global;
    for(unsigned side=0u;side<2u;++side){i->cluster_response[side]=global;i->cluster_release_policy[side]=i->release_policy;}
    i->menu_notice=0u;++i->revision;
    return THY_OK;
}
uint16_t thy_instrument_cluster_trigger(const thy_instrument *i,uint8_t side)
{
    return valid(i) && side<2u && valid_response(&i->cluster_response[side])?
        i->cluster_response[side].attack_raw:0u;
}
static int cluster_response_valid(const thy_instrument_response *response,
    const thy_instrument_release_policy *policy)
{return valid_response(response) && thy_instrument_release_policy_valid(policy) &&
    (!policy->linked || response->release_raw==linked_release(response->attack_raw,policy->gap));}
static int check_cluster_response(const thy_instrument *i,uint8_t side,
    const thy_instrument_response *response,const thy_instrument_release_policy *policy)
{
    if(!valid(i) || side>1u || !cluster_response_valid(response,policy))return THY_INVALID_ARGUMENT;
    for(unsigned p=0;p<THY_PAD_COUNT;++p){
        if(i->mapping[p]>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
        if(i->mapping[p]/48u==side && !i->sensors[p].response_overridden &&
            !response_editable(i,p,response))return THY_INVALID_ARGUMENT;
    }
    return THY_OK;
}
static int commit_cluster_response(thy_instrument *i,uint8_t side,
    const thy_instrument_response *response,const thy_instrument_release_policy *policy)
{
    if(!response || !policy)return THY_INVALID_ARGUMENT;
    const thy_instrument_response requested=*response;
    const thy_instrument_release_policy selected=*policy;
    if(check_cluster_response(i,side,&requested,&selected)!=THY_OK)return THY_INVALID_ARGUMENT;
    for(unsigned p=0;p<THY_PAD_COUNT;++p)if(i->mapping[p]/48u==side && !i->sensors[p].response_overridden){
        apply_response(&i->sensors[p],&requested,0u);i->pad_release_policy[p]=selected;
    }
    i->cluster_response[side]=requested;i->cluster_release_policy[side]=selected;
    i->menu_notice=0u;++i->revision;return THY_OK;
}
int thy_instrument_get_cluster_response(const thy_instrument *i,uint8_t side,thy_instrument_response *response)
{
    if(!valid(i) || side>1u || !response || !valid_response(&i->cluster_response[side]))return THY_INVALID_ARGUMENT;
    *response=i->cluster_response[side];return THY_OK;
}
int thy_instrument_set_cluster_response(thy_instrument *i,uint8_t side,const thy_instrument_response *response)
{
    if(!valid(i) || side>1u || !valid_response(response))return THY_INVALID_ARGUMENT;
    const thy_instrument_response *previous=&i->cluster_response[side];
    thy_instrument_release_policy policy=i->cluster_release_policy[side];
    if(response->attack_raw!=previous->attack_raw || response->release_raw!=previous->release_raw)
        policy=(thy_instrument_release_policy){(uint16_t)(response->attack_raw-response->release_raw),0u};
    return commit_cluster_response(i,side,response,&policy);
}
int thy_instrument_get_cluster_velocity(const thy_instrument *i,uint8_t side,thy_instrument_velocity *velocity)
{
    if(!valid(i) || side>1u || !velocity || !thy_instrument_velocity_valid(&i->cluster_velocity[side]))return THY_INVALID_ARGUMENT;
    *velocity=i->cluster_velocity[side];return THY_OK;
}
int thy_instrument_set_cluster_velocity(thy_instrument *i,uint8_t side,const thy_instrument_velocity *velocity)
{
    if(!valid(i) || side>1u || !thy_instrument_velocity_valid(velocity))return THY_INVALID_ARGUMENT;
    const thy_instrument_velocity requested=*velocity;
    for(unsigned p=0;p<THY_PAD_COUNT;++p){
        if(i->mapping[p]>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
        if(i->mapping[p]/48u==side && !i->velocity_overridden[p] && !velocity_editable(i,p))
            return THY_INVALID_ARGUMENT;
    }
    i->cluster_velocity[side]=requested;
    for(unsigned p=0;p<THY_PAD_COUNT;++p)if(i->mapping[p]/48u==side && !i->velocity_overridden[p])
        clear_strike(&i->sensors[p]);
    i->menu_notice=0u;++i->revision;return THY_OK;
}
int thy_instrument_get_cluster_release_policy(const thy_instrument *i,uint8_t side,
    thy_instrument_release_policy *policy)
{
    if(!valid(i) || side>1u || !policy || !thy_instrument_release_policy_valid(&i->cluster_release_policy[side]))
        return THY_INVALID_ARGUMENT;
    *policy=i->cluster_release_policy[side];return THY_OK;
}
int thy_instrument_set_cluster_release_policy(thy_instrument *i,uint8_t side,
    const thy_instrument_release_policy *policy)
{
    if(!valid(i) || side>1u || !thy_instrument_release_policy_valid(policy))return THY_INVALID_ARGUMENT;
    thy_instrument_response response=i->cluster_response[side];
    if(policy->linked)response.release_raw=linked_release(response.attack_raw,policy->gap);
    return commit_cluster_response(i,side,&response,policy);
}
int thy_instrument_check_cluster_sensitivity_policy(const thy_instrument *i,uint8_t side,
    const thy_instrument_response *response,const thy_instrument_velocity *velocity,
    const thy_instrument_release_policy *policy)
{
    if(check_cluster_response(i,side,response,policy)!=THY_OK || !thy_instrument_velocity_valid(velocity))
        return THY_INVALID_ARGUMENT;
    for(unsigned p=0;p<THY_PAD_COUNT;++p)if(i->mapping[p]/48u==side && !i->velocity_overridden[p] &&
        !velocity_editable(i,p))return THY_INVALID_ARGUMENT;
    return THY_OK;
}
int thy_instrument_set_cluster_sensitivity_policy(thy_instrument *i,uint8_t side,
    const thy_instrument_response *response,const thy_instrument_velocity *velocity,
    const thy_instrument_release_policy *policy)
{
    if(!response || !velocity || !policy)return THY_INVALID_ARGUMENT;
    const thy_instrument_response requested=*response;
    const thy_instrument_velocity selected_velocity=*velocity;
    const thy_instrument_release_policy selected_policy=*policy;
    if(thy_instrument_check_cluster_sensitivity_policy(i,side,&requested,&selected_velocity,&selected_policy)!=THY_OK)
        return THY_INVALID_ARGUMENT;
    int result=commit_cluster_response(i,side,&requested,&selected_policy);
    if(result!=THY_OK)return result;
    return thy_instrument_set_cluster_velocity(i,side,&selected_velocity);
}
static int cluster_scaled_response(const thy_instrument *i,unsigned p,
    uint8_t side,uint16_t reference,const thy_instrument_response *requested,thy_instrument_response *response,
    thy_instrument_release_policy *policy)
{
    const thy_instrument_sensor *s=&i->sensors[p];
    *response=s->response_overridden?s->response:*requested;
    *policy=s->response_overridden?i->pad_release_policy[p]:i->cluster_release_policy[side];
    if(s->response_overridden){
        uint32_t attack=((uint32_t)response->attack_raw*requested->attack_raw+reference/2u)/reference;
        if(attack<1u || attack>4095u)return THY_INVALID_ARGUMENT;
        response->attack_raw=(uint16_t)attack;
        if(policy->linked)response->release_raw=linked_release(response->attack_raw,policy->gap);
    }
    if(response->release_raw<i->sensitivity_guard[p])return THY_INVALID_ARGUMENT;
    return thy_instrument_check_response_policy(i,(int)p,response,policy);
}
int thy_instrument_scale_cluster_sensitivity(thy_instrument *i,uint8_t side,
    uint16_t new_reference_attack)
{
    if(!valid(i) || side>1u || new_reference_attack<1u || new_reference_attack>4095u)
        return THY_INVALID_ARGUMENT;
    uint16_t reference=thy_instrument_cluster_trigger(i,side);
    if(!reference)return THY_INVALID_ARGUMENT;
    if(reference==new_reference_attack)return THY_OK;
    thy_instrument_response requested=i->cluster_response[side];
    const thy_instrument_release_policy policy=i->cluster_release_policy[side];
    requested.attack_raw=new_reference_attack;
    if(policy.linked)requested.release_raw=linked_release(new_reference_attack,policy.gap);
    if(!cluster_response_valid(&requested,&policy))return THY_INVALID_ARGUMENT;
    /* Two bounded passes reuse one small candidate. Other targets and global
     * defaults are never modified, so the second pass recomputes identical
     * responses after the entire cluster has passed validation and quiet. */
    for(unsigned p=0;p<THY_PAD_COUNT;++p){
        if(i->mapping[p]>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
        if(i->mapping[p]/48u!=side)continue;
        thy_instrument_response response;thy_instrument_release_policy policy;
        if(cluster_scaled_response(i,p,side,reference,&requested,&response,&policy)!=THY_OK)
            return THY_INVALID_ARGUMENT;
    }
    for(unsigned p=0;p<THY_PAD_COUNT;++p)if(i->mapping[p]/48u==side){
        thy_instrument_response response;thy_instrument_release_policy policy;
        (void)cluster_scaled_response(i,p,side,reference,&requested,&response,&policy);
        const uint8_t overridden=i->sensors[p].response_overridden;
        apply_response(&i->sensors[p],&response,overridden);
        i->pad_release_policy[p]=policy;
    }
    i->cluster_response[side]=requested;
    i->menu_notice=0u;++i->revision;return THY_OK;
}
static thy_instrument_release_policy raw_response_policy(const thy_instrument *i,int p,
    const thy_instrument_response *requested)
{
    const thy_instrument_response *previous=p<0?&i->response:&i->sensors[p].response;
    thy_instrument_release_policy policy=p<0?i->release_policy:
        i->sensors[p].response_overridden?i->pad_release_policy[p]:i->cluster_release_policy[i->mapping[p]/48u];
    if(requested->attack_raw!=previous->attack_raw || requested->release_raw!=previous->release_raw)
        policy=(thy_instrument_release_policy){(uint16_t)(requested->attack_raw-requested->release_raw),0u};
    return policy;
}
int thy_instrument_set_response(thy_instrument *instrument,const thy_instrument_response *response)
{
    if(!valid(instrument) || !valid_response(response))return THY_INVALID_ARGUMENT;
    thy_instrument_release_policy policy=raw_response_policy(instrument,-1,response);
    return commit_response(instrument,-1,response,&policy,0u);
}

int thy_instrument_get_response(const thy_instrument *instrument,
    thy_instrument_response *response)
{
    if (!valid(instrument) || response == NULL) return THY_INVALID_ARGUMENT;
    *response = instrument->response;
    return THY_OK;
}

int thy_instrument_set_pad_response(thy_instrument *instrument, uint8_t electrical,
    const thy_instrument_response *response)
{
    thy_instrument_response requested;
    uint8_t overridden = response != NULL ? 1u : 0u;
    if (!valid(instrument) || electrical >= THY_PAD_COUNT || instrument->mapping[electrical]>=THY_PAD_COUNT)
        return THY_INVALID_ARGUMENT;
    unsigned side=instrument->mapping[electrical]/48u;
    requested = response != NULL ? *response : instrument->cluster_response[side];
    if (!valid_response(&requested)) return THY_INVALID_ARGUMENT;
    thy_instrument_release_policy policy=response?raw_response_policy(instrument,electrical,&requested):instrument->cluster_release_policy[side];
    int result=commit_response(instrument,electrical,&requested,&policy,overridden);
    if(result==THY_OK && !response)instrument->sensitivity_guard[electrical]=0u;
    return result;
}

int thy_instrument_get_pad_response(const thy_instrument *instrument,
    uint8_t electrical, thy_instrument_response *response, uint8_t *overridden)
{
    if (!valid(instrument) || electrical >= THY_PAD_COUNT || response == NULL ||
        overridden == NULL) return THY_INVALID_ARGUMENT;
    *response = instrument->sensors[electrical].response;
    *overridden = instrument->sensors[electrical].response_overridden;
    return THY_OK;
}

int thy_instrument_set_sensitivity(thy_instrument *i,int p,
    const thy_instrument_response *response,const thy_instrument_velocity *velocity)
{
    if(!valid(i) || p< -1 || p>=(int)THY_PAD_COUNT ||
       !valid_response(response) || !thy_instrument_velocity_valid(velocity))
        return THY_INVALID_ARGUMENT;
    const thy_instrument_response r=*response;
    const thy_instrument_velocity v=*velocity;
    unsigned first=p<0?0u:(unsigned)p,last=p<0?THY_PAD_COUNT:(unsigned)p+1u;
    for(unsigned n=first;n<last;++n) {
        if(((p>=0 || !i->sensors[n].response_overridden) && !response_editable(i,n,&r)) ||
           ((p>=0 || !i->velocity_overridden[n]) && !velocity_editable(i,n))) {
            i->menu_notice=1u;return THY_INVALID_ARGUMENT;
        }
    }
    /* Preflight covers the union of the independent inheritance sets. The
     * response update leaves every velocity target quiet, so neither commit
     * below can fail after the first family has changed. */
    if(p>=0) {
        (void)thy_instrument_set_pad_response(i,(uint8_t)p,&r);
        return thy_instrument_set_pad_velocity(i,(uint8_t)p,&v);
    }
    (void)thy_instrument_set_response(i,&r);
    return thy_instrument_set_velocity(i,&v);
}

int thy_instrument_check_sensitivity_policy(const thy_instrument *i,int p,
    const thy_instrument_response *r,const thy_instrument_velocity *v,const thy_instrument_release_policy *policy)
{
    if(!valid(i) || p< -1 || p>=(int)THY_PAD_COUNT || !valid_response(r) ||
        !thy_instrument_velocity_valid(v) || !thy_instrument_release_policy_valid(policy) ||
        (policy->linked && r->release_raw!=linked_release(r->attack_raw,policy->gap)))return THY_INVALID_ARGUMENT;
    unsigned first=p<0?0u:(unsigned)p,last=p<0?THY_PAD_COUNT:first+1u;
    for(unsigned n=first;n<last;++n)
        if(((p>=0 || !i->sensors[n].response_overridden) && !response_editable(i,n,r)) ||
            ((p>=0 || !i->velocity_overridden[n]) && !velocity_editable(i,n)))return THY_INVALID_ARGUMENT;
    return THY_OK;
}
int thy_instrument_set_sensitivity_policy(thy_instrument *i,int p,
    const thy_instrument_response *response,const thy_instrument_velocity *velocity,
    const thy_instrument_release_policy *release_policy)
{
    if(!response || !velocity || !release_policy)return THY_INVALID_ARGUMENT;
    const thy_instrument_response r=*response;
    const thy_instrument_velocity v=*velocity;
    const thy_instrument_release_policy policy=*release_policy;
    int result=thy_instrument_check_sensitivity_policy(i,p,&r,&v,&policy);
    if(result!=THY_OK)return result;
    /* The checked union guarantees both commits remain valid after the
     * response reset. Arguments are copied before any caller-owned alias can
     * change, and callers serialize this allocation-free foreground API. */
    result=commit_response(i,p,&r,&policy,1u);
    if(result!=THY_OK)return result;
    return p<0?thy_instrument_set_velocity(i,&v):thy_instrument_set_pad_velocity(i,(uint8_t)p,&v);
}

int thy_instrument_reset_sensitivity(thy_instrument *i,int p)
{
    if(!valid(i) || p< -1 || p>=(int)THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    if(p>=0 && i->mapping[p]>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    const thy_instrument_response *r=p<0?&factory_response:&i->cluster_response[i->mapping[p]/48u];
    unsigned first=p<0?0u:(unsigned)p,last=p<0?THY_PAD_COUNT:(unsigned)p+1u;
    for(unsigned n=first;n<last;++n){
        if((p>=0 || !i->sensors[n].response_overridden) && !response_editable(i,n,r)){
            i->menu_notice=1u;return THY_INVALID_ARGUMENT;
        }
        if((p>=0 || !i->velocity_overridden[n]) && !velocity_editable(i,n)){
            i->menu_notice=1u;return THY_INVALID_ARGUMENT;
        }
    }
    if(p>=0){
        (void)thy_instrument_set_pad_response(i,(uint8_t)p,NULL);
        return thy_instrument_clear_pad_velocity(i,(uint8_t)p);
    }
    thy_instrument_velocity defaults;thy_instrument_velocity_defaults(&defaults);
    thy_instrument_release_policy policy;thy_instrument_release_defaults(&policy);
    (void)commit_response(i,-1,&factory_response,&policy,0u);
    return thy_instrument_set_velocity(i,&defaults);
}

int thy_instrument_set_expression(thy_instrument *instrument,
    uint8_t fixed_velocity, uint8_t pressure_enabled)
{
    if (!valid(instrument) || fixed_velocity == 0u || fixed_velocity > 127u ||
        pressure_enabled > 1u) return THY_INVALID_ARGUMENT;
    instrument->fixed_velocity = fixed_velocity;
    instrument->pressure_enabled = pressure_enabled;
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_set_lighting(thy_instrument *instrument, uint8_t enabled,
    uint8_t idle, uint8_t held, uint16_t fade_ms)
{
    if (!valid(instrument) || enabled > 1u || idle > 8u || held > 8u || fade_ms > 500u)
        return THY_INVALID_ARGUMENT;
    instrument->lights_enabled = enabled;
    instrument->light_idle = idle;
    instrument->light_held = held;
    instrument->light_fade_ms = fade_ms;
    ++instrument->revision;
    return THY_OK;
}

typedef struct pad_plan {
    thy_shape shape;
    const int16_t *notes;
    size_t count;
    int16_t generated;
    int16_t chord_notes[THY_INSTRUMENT_SLOTS];
    uint8_t side;
} pad_plan;

int thy_instrument_note_in_key(const thy_instrument *i,int note)
{
    if(!i)return 0;
    int following=i->live_update&&i->live_degree_count&&i->performance.melody_layout!=THY_LAYOUT_PIANO;
    int16_t anchor=following?i->live_anchor:i->melody_anchor;
    const int16_t *degrees=following?i->live_degrees:i->degrees;
    unsigned count=following?i->live_degree_count:i->degree_count;
    if(count>12u)return 0;
    int relative=(note-anchor-i->transpose[1])%12;if(relative<0)relative+=12;
    for(unsigned n=0;n<count;++n)if(degrees[n]==relative)return 1;
    return 0;
}

int thy_instrument_chord_cell(const thy_instrument *i,uint8_t logical,thy_chord_cell *out)
{
    if(!valid(i)||!out||logical>=48u||!i->layout)return THY_INVALID_ARGUMENT;
    if(i->musical_chord_plan){
        thy_chord_cell cell={.degree=THY_CHORD_NO_DEGREE};
        if(i->musical_chord_plan(i->musical_context,logical,cell.notes,&cell.valid_mask)>0){
            /* Workspace plans are root-first, before transpose/voicing. Their
             * identity must also drive held color and current-chord metadata. */
            if(cell.valid_mask){
                unsigned first=0;while(first<7u&&!(cell.valid_mask&(1u<<first)))++first;
                cell.root=cell.notes[first];
                int pitch=(cell.root+i->transpose[0]-i->performance.root)%12;if(pitch<0)pitch+=12;
                for(unsigned n=0;n<i->degree_count;++n)if(i->degrees[n]%12==pitch){cell.degree=(uint8_t)n;break;}
            }
            *out=cell;return THY_OK;
        }
    }
    if(i->chord_layout==THY_CHORD_LAYOUT_USER){
        unsigned column=logical/4u,row=logical%4u,slot=column*2u+row%2u;
        const thy_library_chord *chord=&i->library.chords[slot];thy_chord_cell cell={.degree=THY_CHORD_NO_DEGREE};
        if(chord->used){
            int offset=(int)i->performance.root-(row>=2u?12:0);
            cell.root=(int16_t)(chord->root+offset);
            for(unsigned n=0;n<7u;++n){cell.notes[n]=(int16_t)(chord->notes[n]+((chord->valid_mask&(1u<<n))?offset:0));
                if((chord->valid_mask&(1u<<n))&&cell.notes[n]>=0&&cell.notes[n]<=127)cell.valid_mask|=(uint8_t)(1u<<n);}
        }
        *out=cell;return THY_OK;
    }
    if(i->chord_layout!=THY_CHORD_LAYOUT_CLASSIC){
        if(i->chord_layout==THY_CHORD_LAYOUT_SIX_USER){
            if(i->six_bank>=3u)return THY_INVALID_ARGUMENT;
            const thy_library_six *set=&i->library.six[i->six_bank];
            unsigned ref=set->chords[(logical/4u)%6u];thy_chord_template value={0};
            if(set->used&&ref<24u&&i->library.chords[ref].used){
                const thy_library_chord *chord=&i->library.chords[ref];value.valid_mask=chord->valid_mask;
                for(unsigned n=0;n<7u;++n)value.intervals[n]=(int16_t)((int)chord->notes[n]-chord->root);
            }
            return thy_chord_six_resolve(logical,i->melody_anchor,i->degrees,i->degree_count,&value,out);
        }
        return thy_chord_layout_resolve(i->chord_layout,i->chord_quality,logical,
            i->melody_anchor,i->degrees,i->degree_count,NULL,out);
    }
    thy_chord_cell cell={0};const thy_instrument_pad *pad=&i->layout[logical];
    cell.valid_mask=pad->valid_mask;cell.degree=pad->color>=1u&&pad->color<=12u?(uint8_t)(pad->color-1u):THY_CHORD_NO_DEGREE;
    int root=128;
    for(unsigned n=0;n<7u;++n){cell.notes[n]=pad->notes[n];if((pad->valid_mask&(1u<<n))&&pad->notes[n]<root)root=pad->notes[n];}
    if(cell.degree!=THY_CHORD_NO_DEGREE){const thy_instrument_pad *ref=&i->layout[48u+4u*cell.degree];
        if((ref->valid_mask&1u)&&ref->notes[0]>=0&&ref->notes[0]<=127)root=ref->notes[0];}
    cell.root=(int16_t)(root==128?0:root);*out=cell;return THY_OK;
}
int thy_instrument_chord_degree(const thy_instrument *i,uint8_t logical,uint8_t *degree)
{
    thy_chord_cell cell;if(!degree||thy_instrument_chord_cell(i,logical,&cell)!=THY_OK)return THY_INVALID_ARGUMENT;
    *degree=cell.degree;return THY_OK;
}
uint8_t thy_instrument_drop_available(const thy_instrument *i)
{
    thy_chord_cell cell;if(!valid(i)||i->selected_chord>=48u||
        thy_instrument_chord_cell(i,i->selected_chord,&cell)!=THY_OK)return 0u;
    uint8_t mask=cell.valid_mask;
    if(i->lower_mode==THY_LOWER_STORED_NOTES)mask&=(uint8_t)~(i->slot_scope==THY_SLOTS_SELECTED_PAD?
        i->excluded_slots[i->selected_chord]:i->global_excluded_slots);
    unsigned count=0u;
    for(unsigned n=0;n<7u;++n)if(mask&(1u<<n)){
        unsigned k=0u;for(;k<n;++k)if((mask&(1u<<k))&&cell.notes[k]==cell.notes[n])break;
        if(k==n)++count;
    }
    return (uint8_t)((count>1u?1u:0u)|(count>2u?2u:0u)|(count>3u?4u:0u));
}

static int plan_pad(const thy_instrument *instrument, uint8_t logical,
    pad_plan *plan)
{
    uint8_t side = logical >= 48u ? 1u : 0u;
    thy_shape *shape = &plan->shape;
    const thy_instrument_pad *pad;
    int result;
    if (!valid(instrument) || logical >= THY_PAD_COUNT || instrument->layout == NULL ||
        instrument->lower_mode > THY_LOWER_OCTAVE_LAYERS)
        return THY_INVALID_ARGUMENT;
    *plan = (pad_plan){0};
    plan->side = side;
    plan->count = THY_INSTRUMENT_SLOTS;
    pad = &instrument->layout[logical];
    plan->notes = pad->notes;
    if(!side && instrument->chord_layout!=THY_CHORD_LAYOUT_CLASSIC){
        thy_chord_cell cell;
        result=thy_instrument_chord_cell(instrument,logical,&cell);
        if(result!=THY_OK)return result;
        for(unsigned n=0;n<7u;++n)plan->chord_notes[n]=cell.notes[n];
        plan->notes=plan->chord_notes;
        shape->excluded_slots=(uint64_t)(~cell.valid_mask&127u);
    } else
    shape->excluded_slots = (uint64_t)(~pad->valid_mask & 0x7fu);
    if(!side && instrument->musical_chord_plan) {
        uint8_t mask=0u;
        int override=instrument->musical_chord_plan(instrument->musical_context,logical,plan->chord_notes,&mask);
        if(override<0)return THY_INVALID_ARGUMENT;
        if(override){
            plan->notes=plan->chord_notes;shape->excluded_slots=(uint64_t)(~mask&127u);
            if(!mask){plan->count=0u;plan->notes=NULL;return THY_OK;}
        }
    }
    shape->octave_shift = side == 0u ? instrument->performance.chord_octave :
                                   instrument->performance.melody_octave;
    if(side && instrument->musical_melody_plan) {
        int override=instrument->musical_melody_plan(instrument->musical_context,logical,&plan->generated);
        if(override<0||override>1)return THY_INVALID_ARGUMENT;
        if(override) {
            if(plan->generated<0||plan->generated>127)return THY_INVALID_ARGUMENT;
            plan->notes=&plan->generated;plan->count=1u;shape->excluded_slots=0u;
            return THY_OK;
        }
    }
    if (side == 0u) {
        if (instrument->lower_mode == THY_LOWER_STORED_NOTES) {
            shape->excluded_slots |= instrument->slot_scope == THY_SLOTS_SELECTED_PAD ?
                instrument->excluded_slots[logical] : instrument->global_excluded_slots;
        } else {
            uint8_t layers = instrument->slot_scope == THY_SLOTS_SELECTED_PAD ?
                instrument->pad_octave_layers[logical] : instrument->octave_layers;
            shape->octave_layers = layers != 0u ? layers : 8u;
        }
        if(instrument->voicing_mode==1u){
            result=thy_voicing_drop(plan->notes,(uint8_t)(~shape->excluded_slots&127u),
                instrument->drop_mask,plan->chord_notes);
            if(result!=THY_OK)return result;
            plan->notes=plan->chord_notes;
        } else if(instrument->voicing_mode==2u) {
            uint8_t mask=(uint8_t)(~shape->excluded_slots&127u);
            int override=instrument->musical_voicing_plan?
                instrument->musical_voicing_plan(instrument->musical_context,logical,
                    plan->notes,mask,plan->chord_notes):0;
            if(override<0||override>1)return THY_INVALID_ARGUMENT;
            if(override) {
                for(unsigned n=0;n<7u;++n)if((mask&(1u<<n))&&
                    (plan->chord_notes[n]<0||plan->chord_notes[n]>127))return THY_INVALID_ARGUMENT;
            }else {
                result=thy_voicing_lead(plan->notes,mask,
                    instrument->voicing_previous,instrument->voicing_previous_mask,plan->chord_notes);
                if(result!=THY_OK)return result;
            }
            plan->notes=plan->chord_notes;
        } else if(instrument->voicing_mode==3u || instrument->voicing_mode==4u) {
            uint8_t mask=(uint8_t)(~shape->excluded_slots&127u);
            result=instrument->voicing_mode==3u?
                thy_voicing_fold(plan->notes,mask,plan->chord_notes):
                thy_voicing_guitar(plan->notes,mask,plan->chord_notes);
            if(result!=THY_OK)return result;
            plan->notes=plan->chord_notes;
        } else shape->inversion = instrument->performance.inversion;
        shape->spread = instrument->performance.spread;
        shape->upper_octaves = instrument->performance.upper_octaves;
        shape->bass_octave = instrument->bass_octave;
        shape->strum_ms = instrument->performance.strum != 0u ?
                         instrument->performance.strum_ms : 0u;
        shape->strum_order = instrument->performance.strum_order;
    } else if (instrument->performance.melody_layout != 0u ||
               (instrument->live_update && instrument->live_degree_count)) {
        uint8_t position = (uint8_t)(logical - 48u);
        uint8_t mode = instrument->performance.melody_layout;
        int following=instrument->live_update && instrument->live_degree_count;
        int16_t anchor=following?instrument->live_anchor:instrument->melody_anchor;
        const int16_t *degrees=following?instrument->live_degrees:instrument->degrees;
        uint8_t degree_count=following?instrument->live_degree_count:instrument->degree_count;
        if (mode >= THY_LAYOUT_COUNT) return THY_INVALID_ARGUMENT;
        if(mode==THY_LAYOUT_PIANO){
            uint8_t active;
            result=thy_chord_piano_resolve(position,instrument->melody_anchor,&plan->generated,&active);
            if(result!=THY_OK)return result;
            if(!active){plan->count=0u;plan->notes=NULL;return THY_OK;}
        } else if (mode == THY_LAYOUT_GUITAR) {
            static const int16_t tuning[] = {40,45,50,55,59,64};
            if (instrument->performance.guitar_window > 2u) return THY_INVALID_ARGUMENT;
            plan->generated = tuning[instrument->performance.guitar_window + position % 4u] + position / 4u;
            if(following)plan->generated+=(int16_t)(anchor-instrument->melody_anchor);
            result = THY_OK;
        } else if (mode == THY_LAYOUT_CONDENSED || (following && mode==THY_LAYOUT_STORED)) {
            if (!degree_count) return THY_INVALID_ARGUMENT;
            unsigned column = position / 4u;
            plan->generated = anchor + degrees[column % degree_count] +
                12 * (int)(column / degree_count) - 12 * (int)(position % 4u);
            result = plan->generated >= 0 && plan->generated <= 127 ? THY_OK : THY_INVALID_ARGUMENT;
        } else result = thy_core_melody_pitch(mode >= THY_LAYOUT_PIANO ? THY_LAYOUT_CHROMATIC : mode,
            anchor, degrees, degree_count,
            (uint8_t)(position / 4u), (uint8_t)(position % 4u), &plan->generated);
        if(result==THY_OK && following && mode!=THY_LAYOUT_PIANO && mode!=THY_LAYOUT_CONDENSED &&
           mode!=THY_LAYOUT_STORED && mode!=THY_LAYOUT_THIRDS && mode!=THY_LAYOUT_FOURTHS) {
            unsigned offset=(unsigned)((plan->generated-anchor)%12+12)%12u,step=0u;
            for(;step<12u;++step){unsigned candidate=(offset+step)%12u,k=0u;
                for(;k<degree_count && degrees[k]!=(int16_t)candidate;++k){}
                if(k<degree_count)break;}
            plan->generated+=(int16_t)step;
            if(plan->generated<0 || plan->generated>127)result=THY_INVALID_ARGUMENT;
        }
        if (result != THY_OK) return result;
        plan->notes = &plan->generated;
        plan->count = 1u;
        shape->excluded_slots = 0u;
    }
    return THY_OK;
}

void thy_instrument_key_changed(thy_instrument *i)
{
    if(!valid(i))return;
    i->live_degree_count=0u; i->voicing_previous_mask=0u;
    if(!i->keep_transpose)i->transpose[0]=i->transpose[1]=0;
}
void thy_instrument_follow_chord(thy_instrument *i,uint8_t logical,
    const int16_t *resolved,size_t count,int16_t transpose)
{
    if(!valid(i)||!i->live_update||logical>=48u||!resolved||!count||count>THY_MAX_NOTES)return;
    int16_t root;uint16_t mask=0u;
    thy_chord_cell cell;
    if(thy_instrument_chord_cell(i,logical,&cell)==THY_OK && cell.valid_mask)root=(int16_t)(cell.root+transpose);
    else {root=resolved[0];for(size_t n=1;n<count;++n)if(resolved[n]<root)root=resolved[n];}
    unsigned pc=(unsigned)((root-transpose)%12+12)%12u;
    if(i->live_lock_scale)for(unsigned n=0;n<i->degree_count;++n)
        mask|=(uint16_t)(1u<<((i->performance.root+i->degrees[n])%12));
    else for(size_t n=0;n<count;++n)mask|=(uint16_t)(1u<<((resolved[n]-transpose)%12+12)%12);
    if(!mask)return;
    for(unsigned n=0;n<12u && !(mask&(1u<<pc));++n)pc=(pc+1u)%12u;
    int16_t anchor=(int16_t)(i->melody_anchor-i->melody_anchor%12+(int)pc);
    if(anchor>127)anchor-=12;
    i->live_anchor=anchor;i->live_degree_count=0u;
    for(unsigned offset=0;offset<12u;++offset)if(mask&(1u<<((pc+offset)%12u)))
        i->live_degrees[i->live_degree_count++]=(int16_t)offset;
    ++i->revision;
}
int thy_instrument_set_live_update(thy_instrument *i,uint8_t enabled,uint8_t lock,uint8_t mute)
{
    if(!valid(i)||enabled>1u||lock>1u||mute>1u)return THY_INVALID_ARGUMENT;
    if(i->live_update!=enabled||i->live_lock_scale!=lock)i->live_degree_count=0u;
    i->live_update=enabled;i->live_lock_scale=lock;i->live_mute_chord=mute;
    ++i->revision;return THY_OK;
}

int thy_instrument_preview_source(const thy_instrument *instrument, uint8_t logical,
    int16_t out[THY_MAX_NOTES], size_t *count)
{
    if (out == NULL || count == NULL) return THY_INVALID_ARGUMENT;
    pad_plan plan;
    int result = plan_pad(instrument, logical, &plan);
    if (result != THY_OK) return result;
    return thy_core_preview(instrument->core, plan.notes, plan.count,
        instrument->transpose[plan.side], &plan.shape, out, count);
}

int thy_instrument_preview_pad(const thy_instrument *instrument, uint8_t logical,
    int16_t out[THY_MAX_NOTES], size_t *count)
{
    if (!out || !count) return THY_INVALID_ARGUMENT;
    uint8_t role=thy_instrument_role_pad(instrument,logical);
    if(role>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    if (thy_instrument_context_pad(instrument, role)) { *count = 0u; return THY_OK; }
    return thy_instrument_preview_source(instrument, role, out, count);
}

int thy_instrument_context_pad(const thy_instrument *instrument, uint8_t logical)
{
    if(instrument && instrument->musical_chord_plan &&
       instrument->musical_chord_plan(instrument->musical_context,logical,NULL,NULL)>0)return 0;
    if (instrument == NULL || !instrument->context_controls) return 0;
    /* Explicit, labeled opt-in octave/voicing overlay. Logical IDs are
     * column-major. Ordinal0 is the upper-right corner and1 the lower-left;
     * saved upper/lower preferences keep their ordinal after this correction.
     * Moving this overlay never rewrites the chord archive. */
    static const uint8_t locations[2][6] = {
        {32u, 36u, 40u, 44u, 88u, 92u},
        {3u, 7u, 11u, 15u, 51u, 55u}
    };
    if (instrument->context_location > 1u) return 0;
    for (unsigned n = 0u; n < 6u; ++n)
        if ((instrument->context_mask & (1u << n)) &&
            locations[instrument->context_location][n] == logical) return (int)n + 1;
    return 0;
}

int thy_instrument_set_context_controls(thy_instrument *instrument, uint8_t enabled)
{
    if (!valid(instrument) || enabled > 1u) return THY_INVALID_ARGUMENT;
    for (unsigned n = 0u; n < THY_PAD_COUNT; ++n)
        if (instrument->sensors[n].held || instrument->sensors[n].blocked || instrument->context_held[n] ||
            instrument->core->owners[n].held) return THY_INVALID_ARGUMENT;
    instrument->context_controls = enabled;
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_set_context_location(thy_instrument *instrument, uint8_t location)
{
    if (!valid(instrument) || location > 1u) return THY_INVALID_ARGUMENT;
    /* Relocation while a physical contact is down could change that contact
     * from a control into a musical pad, so keep this edit atomic and quiet. */
    for (unsigned n = 0u; n < THY_PAD_COUNT; ++n)
        if (instrument->sensors[n].held || instrument->sensors[n].blocked ||
            instrument->context_held[n] || instrument->core->owners[n].held)
            return THY_INVALID_ARGUMENT;
    instrument->context_location = location;
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_set_context_mask(thy_instrument *instrument, uint8_t mask)
{
    if (!valid(instrument) || mask > 63u) return THY_INVALID_ARGUMENT;
    for (unsigned n = 0u; n < THY_PAD_COUNT; ++n)
        if (instrument->sensors[n].held || instrument->sensors[n].blocked ||
            instrument->context_held[n] || instrument->core->owners[n].held)
            return THY_INVALID_ARGUMENT;
    instrument->context_mask = mask;
    ++instrument->revision;
    return THY_OK;
}

int thy_instrument_chord_root(const thy_instrument *instrument, uint8_t logical,
    int16_t *root)
{
    int16_t notes[THY_MAX_NOTES]; size_t count = 0u;
    if (root == NULL || logical >= 48u ||
        thy_instrument_preview_source(instrument, logical, notes, &count) != THY_OK || !count)
        return THY_INVALID_ARGUMENT;
    *root=notes[0];for(size_t n=1;n<count;++n)if(notes[n]<*root)*root=notes[n];
    thy_chord_cell cell;
    if(thy_instrument_chord_cell(instrument,logical,&cell)!=THY_OK)return THY_INVALID_ARGUMENT;
    int pc = (cell.root + instrument->transpose[0]) % 12;
    if (pc < 0) pc += 12;
    if(instrument->chord_layout!=THY_CHORD_LAYOUT_CLASSIC){
        *root=(int16_t)((*root/12)*12+pc);if(*root>127)*root-=12;return THY_OK;
    }
    for (size_t n = 0u; n < count; ++n)
        if (notes[n] % 12 == pc) { *root = notes[n]; break; }
    return THY_OK;
}

static void capture_source_color(thy_instrument *instrument, uint8_t electrical,
    uint8_t logical)
{
    if (!valid(instrument) || electrical >= THY_PAD_COUNT) return;
    if (instrument->core->owners[electrical].held &&
        !instrument->core->owners[electrical].count && instrument->held_input_valid[electrical]) return;
    instrument->held_color_valid[electrical] = 0u;
    thy_chord_cell cell;
    const thy_pad_owner *owner = &instrument->core->owners[electrical];
    if (!owner->held || !owner->count) return;
    if (logical < 48u && thy_instrument_chord_cell(instrument,logical,&cell)==THY_OK) {
        int root_pc=(cell.root+instrument->transpose[0])%12;if(root_pc<0)root_pc+=12;
        int16_t root=(int16_t)(((thy_pad_owner_key(owner, (uint8_t)(0))&127u)/12u)*12u+(unsigned)root_pc);
        if(root>127)root-=12;
        /* Explicit-note host callers may use a voicing unrelated to the
         * current layout. Never attach a catalog root absent from that owner,
         * including its queued strum notes and captured transpose. */
        int matches_owner = instrument->chord_layout!=THY_CHORD_LAYOUT_CLASSIC;
        for (unsigned n = 0u; n < owner->count; ++n)
            if ((thy_pad_owner_key(owner, (uint8_t)(n)) & 127u) % 12u == (unsigned)root_pc){
                matches_owner = 1;root=(int16_t)(thy_pad_owner_key(owner, (uint8_t)(n))&127u);}
        if (!matches_owner) return;
        instrument->held_color_root[electrical] = (uint8_t)root;
        uint8_t degree=THY_CHORD_NO_DEGREE;
        (void)thy_instrument_chord_degree(instrument,logical,&degree);
        instrument->held_color_degree[electrical] = degree;
        instrument->held_color_valid[electrical] = 1u;
    }
}

void thy_instrument_capture_pad_color(thy_instrument *instrument, uint8_t electrical)
{
    if (!valid(instrument) || electrical >= THY_PAD_COUNT) return;
    capture_source_color(instrument, electrical,
        thy_instrument_role_pad(instrument,instrument->mapping[electrical]));
}

void thy_instrument_capture_silent_input(thy_instrument *i,uint8_t p,uint8_t logical,
    const int16_t *notes,size_t count)
{
    if(!valid(i)||p>=96u||logical>=96u||!notes||!count||count>THY_MAX_NOTES)return;
    i->held_input_first[p]=i->held_input_bass[p]=(uint8_t)notes[0];
    for(size_t n=1;n<count;++n)if(notes[n]<i->held_input_bass[p])i->held_input_bass[p]=(uint8_t)notes[n];
    i->held_color_valid[p]=0u;i->held_input_valid[p]=1u;
    thy_chord_cell cell;uint8_t degree;
    if(logical<48u&&thy_instrument_chord_cell(i,logical,&cell)==THY_OK){
        int root_pc=(cell.root+i->transpose[0])%12;if(root_pc<0)root_pc+=12;
        int16_t root=(int16_t)((i->held_input_bass[p]/12u)*12u+(unsigned)root_pc);if(root>127)root-=12;
        int match=i->chord_layout!=THY_CHORD_LAYOUT_CLASSIC;
        for(size_t n=0;n<count;++n)if(notes[n]%12==root_pc){match=1;root=notes[n];}
        if(match){i->held_color_root[p]=(uint8_t)root;
            if(thy_instrument_chord_degree(i,logical,&degree)==THY_OK)i->held_color_degree[p]=degree;
            i->held_color_valid[p]=1u;}
    }
}

int thy_instrument_press_pad(thy_instrument *instrument, uint8_t electrical)
{
    if (!valid(instrument) || electrical >= THY_PAD_COUNT) return THY_INVALID_ARGUMENT;
    if (instrument->core->owners[electrical].held) return THY_OK;
    int filtered = THY_OK;
    if (instrument->pad_filter && instrument->pad_filter(instrument->pad_filter_context,
        electrical, 1, &filtered)) return filtered;
    uint8_t logical = thy_instrument_role_pad(instrument,instrument->mapping[electrical]);
    if(logical>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    if (instrument->core->owners[electrical].held) return THY_OK;
    if (instrument->context_held[electrical]) return THY_OK;
    int context = thy_instrument_context_pad(instrument, logical);
    if (context) {
        instrument->context_held[electrical] = 1u;
        if (context == 1 && instrument->performance.chord_octave > -3) --instrument->performance.chord_octave;
        if (context == 2 && instrument->performance.chord_octave < 3) ++instrument->performance.chord_octave;
        if (context == 3) {
            if(instrument->voicing_mode==1u)instrument->drop_mask=instrument->drop_mask==0u?1u:
                instrument->drop_mask==1u?2u:instrument->drop_mask==2u?4u:0u;
            else if(!instrument->voicing_mode)instrument->performance.inversion = (instrument->performance.inversion + 1u) % 4u;
        }
        if (context == 4) instrument->bass_octave = instrument->bass_octave <= -3 ? 0 :
            (int8_t)(instrument->bass_octave - 1);
        if (context == 5 && instrument->performance.melody_octave > -3) --instrument->performance.melody_octave;
        if (context == 6 && instrument->performance.melody_octave < 3) ++instrument->performance.melody_octave;
        ++instrument->revision;
        return THY_OK;
    }
    return thy_instrument_press_source(instrument, electrical, logical);
}

int thy_instrument_press_source(thy_instrument *instrument, uint8_t electrical,
    uint8_t logical)
{
    if (!valid(instrument) || electrical >= THY_PAD_COUNT || logical >= THY_PAD_COUNT)
        return THY_INVALID_ARGUMENT;
    if (instrument->core->owners[electrical].held) return THY_OK;
    pad_plan plan;
    thy_instrument_route route;
    int result = plan_pad(instrument, logical, &plan);
    if (result != THY_OK) return result;
    result = thy_instrument_get_pad_route(instrument,
        thy_instrument_route_target(instrument, logical), &route);
    if (result != THY_OK) return result;
    if(instrument->sensors[electrical].strike_ready)
        route.velocity=instrument->sensors[electrical].strike_velocity;
    else instrument->sensors[electrical].strike_velocity=route.velocity;
    int mute=logical<48u && instrument->live_update && instrument->live_mute_chord;
    result = instrument->musical_attack ?
        instrument->musical_attack(instrument->musical_context, electrical,
            plan.notes, plan.count, instrument->transpose[plan.side], &route, &plan.shape) :
        mute?thy_core_press(instrument->core,electrical,NULL,0u,0,route.channel,route.destinations,route.velocity):
        thy_core_press_shaped(instrument->core, electrical, plan.notes, plan.count,
            instrument->transpose[plan.side], route.channel,
            route.destinations, route.velocity, &plan.shape);
    if (result == THY_OK) {
        if(instrument->core->owners[electrical].held){
            instrument->selected_pad=logical;
            if(logical<48u) {
                instrument->selected_chord=logical;
                uint8_t mask=(uint8_t)(~plan.shape.excluded_slots&127u);
                if(instrument->voicing_mode==2u && mask) {
                    int16_t resolved[THY_MAX_NOTES];size_t count=0u;
                    if(thy_core_preview(instrument->core,plan.notes,plan.count,
                        instrument->transpose[0],&plan.shape,resolved,&count)==THY_OK && count) {
                        for(unsigned n=0;n<7u;++n)instrument->voicing_previous[n]=plan.notes[n];
                        instrument->voicing_previous_mask=mask;
                    }
                }
            }
        }
        if(!instrument->musical_attack && logical<48u && instrument->live_update){
            int16_t resolved[THY_MAX_NOTES];size_t count=0u;
            if(thy_instrument_preview_source(instrument,logical,resolved,&count)==THY_OK)
            {
                if(mute && count)thy_instrument_capture_silent_input(instrument,electrical,logical,resolved,count);
                thy_instrument_follow_chord(instrument,logical,resolved,count,instrument->transpose[0]);
            }
        }
        instrument->held_pressure_enabled[electrical] = route.pressure_enabled;
        capture_source_color(instrument, electrical, logical);
    }
    return result;
}

int thy_instrument_release_pad(thy_instrument *instrument, uint8_t electrical)
{
    if (!valid(instrument) || electrical >= THY_PAD_COUNT) return THY_INVALID_ARGUMENT;
    int filtered = THY_OK;
    if (instrument->pad_filter && instrument->pad_filter(instrument->pad_filter_context,
        electrical, 0, &filtered)) return filtered;
    int result = instrument->musical_release ?
        instrument->musical_release(instrument->musical_context, electrical) :
        thy_core_release(instrument->core, electrical);
    if (result == THY_OK) {
        instrument->context_held[electrical] = 0u;
        instrument->held_color_valid[electrical] = 0u;
        instrument->held_pressure_enabled[electrical] = 0u;
        instrument->held_input_valid[electrical] = 0u;
    }
    return result;
}

int thy_instrument_advance(thy_instrument *instrument, uint32_t now_ms)
{
    if (!valid(instrument)) return THY_INVALID_ARGUMENT;
    return error(instrument, thy_core_advance(instrument->core, now_ms));
}

int thy_instrument_sample(thy_instrument *instrument, uint8_t electrical,
    uint16_t raw, uint32_t now_ms)
{
    thy_instrument_sensor *sensor;
    thy_instrument_response sampled_response;
    thy_instrument_velocity sampled_velocity;
    const thy_instrument_response *response;
    const thy_instrument_velocity *velocity;
    int sampling_override=0;
    int attack_level, release_level;
    uint8_t confirmations;
    int result;
    if (!valid(instrument) || electrical >= THY_PAD_COUNT || raw > 4095u)
        return THY_INVALID_ARGUMENT;
    if(instrument->mapping[electrical]>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    sensor = &instrument->sensors[electrical];
    response=&sensor->response;
    velocity=instrument->velocity_overridden[electrical]?
        &instrument->pad_velocity[electrical]:&instrument->cluster_velocity[instrument->mapping[electrical]/48u];
    if(instrument->sampling_provider){
        sampled_response=*response;sampled_velocity=*velocity;
        sampling_override=instrument->sampling_provider(instrument->sampling_provider_context,
            electrical,&sampled_response,&sampled_velocity);
        if(sampling_override<0 || sampling_override>1 || (sampling_override &&
            (!valid_response(&sampled_response) || !thy_instrument_velocity_valid(&sampled_velocity))))
            return THY_INVALID_ARGUMENT;
        if(sampling_override){response=&sampled_response;velocity=&sampled_velocity;}
    }
    result = thy_instrument_advance(instrument, now_ms);
    if (result != THY_OK) return result;
    uint32_t elapsed=now_ms-sensor->last_sample_ms;
    int32_t rise=((int32_t)raw-sensor->raw)*
        (!sampling_override && sensor->detector_profile==THY_DETECTOR_RELATIVE?sensor->calibration.polarity:1);
    if(!sensor->held){
        if((uint32_t)(now_ms-sensor->strike_peak_ms)>THY_INSTRUMENT_STRIKE_WINDOW_MS)
            sensor->strike_peak_rate=0u;
        if(elapsed<=THY_INSTRUMENT_STRIKE_WINDOW_MS && rise>0){
            /* Millisecond timestamps cannot resolve faster publications.
             * Quantize their interval to 1ms rather than losing the attack. */
            uint32_t interval=elapsed?elapsed:1u;
            uint16_t rate=(uint16_t)(((uint32_t)rise+interval-1u)/interval);
            if(rate>=sensor->strike_peak_rate){sensor->strike_peak_rate=rate;sensor->strike_peak_ms=now_ms;}
        }
    }
    sensor->raw = raw;
    sensor->last_sample_ms = now_ms;
    sensor_levels_with_response(sensor,response,sampling_override);
    const thy_expression_profile *profile=instrument->expression_profile_overridden[electrical]?
        &instrument->pad_expression_profiles[electrical]:&instrument->expression_profiles[instrument->mapping[electrical]/48u];
    uint8_t aftertouch=sensor->pressure;
    if(profile->enabled) {
        uint16_t level=(!sampling_override && sensor->detector_profile==THY_DETECTOR_RELATIVE)?sensor->delta:raw;
        uint16_t low=(!sampling_override && sensor->detector_profile==THY_DETECTOR_RELATIVE)?0u:response->pressure_floor;
        uint16_t high=(!sampling_override && sensor->detector_profile==THY_DETECTOR_RELATIVE)?sensor->calibration.full_scale:response->pressure_full_scale;
        if(thy_expression_profile_axes(profile,level,low,high,response->pressure_curve,&sensor->pressure,&aftertouch))
            return error(instrument,THY_INVALID_ARGUMENT);
    }
    if (sensor->warmup_count < THY_INSTRUMENT_WARMUP_SAMPLES) {
        clear_strike(sensor);
        /* Startup is a quiet stability qualification, not eight arbitrary
         * samples that can average a held pad into an unusable baseline. */
        if ((sampling_override || sensor->detector_profile != THY_DETECTOR_RELATIVE) &&
            raw > response->release_raw) {
            sensor->warmup_count = 0u;
            sensor->warmup_sum = 0u;
            sensor->blocked = 1u;
            return THY_OK;
        }
        if (sensor->warmup_count == 0u) {
            sensor->warmup_min = raw;
            sensor->warmup_max = raw;
        } else {
            if (raw < sensor->warmup_min) sensor->warmup_min = raw;
            if (raw > sensor->warmup_max) sensor->warmup_max = raw;
            if (sensor->warmup_max - sensor->warmup_min > THY_INSTRUMENT_WARMUP_SPAN) {
                sensor->warmup_count = 0u;
                sensor->warmup_sum = 0u;
                sensor->warmup_min = raw;
                sensor->warmup_max = raw;
            }
        }
        sensor->warmup_sum += raw;
        ++sensor->warmup_count;
        if (sensor->warmup_count == THY_INSTRUMENT_WARMUP_SAMPLES) {
            sensor->baseline_q8 = (int32_t)(sensor->warmup_sum * 256u /
                                           THY_INSTRUMENT_WARMUP_SAMPLES);
            sensor->calibration.baseline =
                (uint16_t)((sensor->baseline_q8 + 128) / 256);
            sensor->blocked = 0u;
            sensor_levels_with_response(sensor,response,sampling_override);
        }
        return THY_OK;
    }
    if (sampling_override || sensor->detector_profile != THY_DETECTOR_RELATIVE) {
        attack_level = raw >= response->attack_raw;
        release_level = raw <= response->release_raw;
        confirmations = 2u;
    } else {
        attack_level = sensor->delta >= sensor->calibration.press_delta;
        release_level = sensor->delta <= sensor->calibration.release_delta;
        confirmations = 1u;
    }
    if (sensor->held != 0u) {
        sensor->attack_count = 0u;
        if (release_level != 0) ++sensor->release_count;
        else sensor->release_count = 0u;
        if (sensor->release_count >= confirmations) {
            result = thy_instrument_release_pad(instrument, electrical);
            sensor->held = 0u;
            sensor->blocked = 0u;
            sensor->sent_pressure = 0u;
            sensor->release_count = 0u;
            sensor->strike_ready=0u;sensor->strike_peak_rate=0u;sensor->strike_peak_ms=now_ms;
            ++instrument->releases;
            ++instrument->revision;
            record_transition(instrument, electrical, THY_PAD_TRANSITION_RELEASE, now_ms);
            return error(instrument, result);
        }
        if (sensor->release_count == 0u && sensor->blocked == 0u &&
            (instrument->held_pressure_enabled[electrical] != 0u || instrument->musical_pressure) &&
            ((profile->enabled?aftertouch:sensor->pressure) != sensor->sent_pressure ||
             (profile->enabled && instrument->musical_pressure))) {
            result = instrument->musical_pressure ?
                instrument->musical_pressure(instrument->musical_context, electrical, sensor->pressure,aftertouch) :
                thy_core_pressure(instrument->core, electrical, aftertouch);
            if (result == THY_OK) sensor->sent_pressure = profile->enabled?aftertouch:sensor->pressure;
            return error(instrument, result);
        }
    } else {
        sensor->release_count = 0u;
        if (attack_level != 0) ++sensor->attack_count;
        else sensor->attack_count = 0u;
        if (sensor->attack_count >= confirmations) {
            sensor->held = 1u;
            sensor->attack_count = 0u;
            sensor->sent_pressure = 0u;
            sensor->strike_velocity=thy_instrument_velocity_value(velocity,sensor->strike_peak_rate);
            sensor->strike_ready=(uint8_t)(velocity->mode==THY_VELOCITY_STRIKE && sensor->strike_velocity>0u);
            result = instrument->input_enabled != 0u ?
                     thy_instrument_press_pad(instrument, electrical) : THY_OK;
            sensor->strike_ready=0u;
            /* An invalid/gated press stays blocked until physical release. */
            sensor->blocked = result != THY_OK || instrument->input_enabled == 0u ? 1u : 0u;
            ++instrument->presses;
            ++instrument->revision;
            record_transition(instrument, electrical, THY_PAD_TRANSITION_PRESS, now_ms);
            return error(instrument, result);
        }
        /* Guard the signed difference, rather than clamping negatives to0:
         * a large negative coupled excursion must not drag baseline down and
         * turn its later rebound into an attack. Fractional drift is symmetric. */
        if (sensor->attack_count == 0u &&
            sensor->signed_delta > -(int32_t)(sensor->calibration.release_delta / 2u) &&
            sensor->signed_delta < (int32_t)(sensor->calibration.release_delta / 2u)) {
            int32_t target = (int32_t)raw * 256;
            sensor->baseline_q8 += (target - sensor->baseline_q8) / 64;
            sensor->calibration.baseline =
                (uint16_t)((sensor->baseline_q8 + 128) / 256);
            sensor_levels_with_response(sensor,response,sampling_override);
        }
    }
    return THY_OK;
}

static uint8_t cycle(uint8_t current, int direction, uint8_t count)
{
    return direction < 0 ? (uint8_t)((current + count - 1u) % count) :
                           (uint8_t)((current + 1u) % count);
}

static int8_t clamp_octave(int value)
{
    return (int8_t)(value < -3 ? -3 : value > 3 ? 3 : value);
}

static void adjust_menu(thy_instrument *instrument, int direction)
{
    thy_instrument_performance *settings = &instrument->performance;
    switch (instrument->menu_row) {
    case 0: /* Baseline seven-mode family; full catalogue is caller-owned. */
        settings->shortcut = (uint8_t)(2u + cycle(
            settings->shortcut >= 2u && settings->shortcut <= 8u ?
            (uint8_t)(settings->shortcut - 2u) : 0u, direction, 7u));
        ++instrument->layout_revision;
        break;
    case 1:
        settings->root = cycle(settings->root, direction, 12u);
        ++instrument->layout_revision;
        break;
    case 2: settings->inversion = cycle(settings->inversion, direction, 4u); break;
    case 3: settings->spread = cycle(settings->spread, direction, 4u); break;
    case 4: settings->strum ^= 1u; break;
    case 5:
        if (direction < 0) settings->strum_ms = settings->strum_ms > 8u ?
            (uint16_t)(settings->strum_ms - 8u) : 1u;
        else settings->strum_ms = settings->strum_ms < 992u ?
            (uint16_t)(settings->strum_ms + 8u) : 1000u;
        break;
    case 6: settings->strum_order = cycle(settings->strum_order, direction, 4u); break;
    case 7:
        settings->melody_layout = cycle(settings->melody_layout, direction, THY_LAYOUT_COUNT);
        ++instrument->layout_revision;
        break;
    case 8: settings->chord_octave = clamp_octave(settings->chord_octave + direction); break;
    case 9: settings->melody_octave = clamp_octave(settings->melody_octave + direction); break;
    case 10: settings->upper_octaves = cycle(settings->upper_octaves, direction, 4u); break;
    case 11: instrument->pressure_enabled ^= 1u; break;
    case 12:
        if (direction < 0 && instrument->fixed_velocity > 1u) --instrument->fixed_velocity;
        if (direction > 0 && instrument->fixed_velocity < 127u) ++instrument->fixed_velocity;
        break;
    case 14: case 15: case 16: case 17: case 18: {
        thy_instrument_response response = instrument->response;
        int amount = direction < 0 ? -16 : 16;
        int value;
        if (instrument->menu_row == 14u) {
            value = response.attack_raw + amount;
            response.attack_raw = (uint16_t)(value <= response.release_raw ?
                response.release_raw + 1u : value > response.pressure_full_scale ?
                response.pressure_full_scale : (unsigned)value);
        } else if (instrument->menu_row == 15u) {
            value = response.release_raw + amount;
            response.release_raw = (uint16_t)(value < response.pressure_floor ?
                response.pressure_floor : value >= response.attack_raw ?
                response.attack_raw - 1u : (unsigned)value);
        } else if (instrument->menu_row == 16u) {
            value = response.pressure_floor + amount;
            response.pressure_floor = (uint16_t)(value < 0 ? 0u :
                value > response.release_raw ? response.release_raw : (unsigned)value);
        } else if (instrument->menu_row == 17u) {
            value = response.pressure_full_scale + amount;
            response.pressure_full_scale = (uint16_t)(value < response.attack_raw ?
                response.attack_raw : value > 4095 ? 4095u : (unsigned)value);
        } else response.pressure_curve = cycle(response.pressure_curve, direction, 3u);
        /* A held/blocked pad rejects this edit; navigation stays usable and no
         * runtime transport/detection error is fabricated from a settings edit. */
        (void)thy_instrument_set_response(instrument, &response);
        break;
    }
    case 19: instrument->lights_enabled ^= 1u; break;
    case 20:
        if (direction < 0 && instrument->light_idle > 0u) --instrument->light_idle;
        if (direction > 0 && instrument->light_idle < 8u) ++instrument->light_idle;
        break;
    case 21:
        if (direction < 0 && instrument->light_held > 0u) --instrument->light_held;
        if (direction > 0 && instrument->light_held < 8u) ++instrument->light_held;
        break;
    case 22:
        if (direction < 0) instrument->light_fade_ms = instrument->light_fade_ms > 40u ?
            (uint16_t)(instrument->light_fade_ms - 40u) : 0u;
        else instrument->light_fade_ms = instrument->light_fade_ms < 460u ?
            (uint16_t)(instrument->light_fade_ms + 40u) : 500u;
        break;
    default: break;
    }
}

static void navigation(thy_instrument *instrument, uint8_t index)
{
    if (index == 2u) {
        instrument->menu_open ^= 1u;
        instrument->menu_editing = 0u;
    } else if (index == 0u) {
        if (instrument->menu_editing != 0u) instrument->menu_editing = 0u;
        else instrument->menu_open = 0u;
    } else if (index == 1u || index == 3u) {
        int direction = index == 1u ? -1 : 1;
        if (instrument->menu_open != 0u) {
            if (instrument->menu_editing != 0u) adjust_menu(instrument, direction);
            else instrument->menu_row = cycle(instrument->menu_row, direction,
                                               THY_INSTRUMENT_MENU_ROWS);
        }
    } else if (index == 4u && instrument->menu_open == 0u) {
        instrument->menu_open = 1u;
    } else if (instrument->menu_row == 13u) {
        thy_instrument_panic(instrument);
    } else {
        instrument->menu_editing ^= 1u;
    }
}

int thy_instrument_store_user(thy_instrument *instrument, uint8_t slot)
{
    if (!valid(instrument) || slot >= 3u) return THY_INVALID_ARGUMENT;
    instrument->users[slot] = instrument->performance;
    instrument->users_valid |= (uint8_t)(1u << slot);
    return THY_OK;
}

uint8_t thy_instrument_lower_mask(const thy_instrument *instrument)
{
    if (instrument == NULL || instrument->lower_mode > THY_LOWER_OCTAVE_LAYERS)
        return 0u;
    int selected = instrument->slot_scope == THY_SLOTS_SELECTED_PAD;
    if (selected && instrument->selected_chord >= 48u) return 0u;
    if (instrument->lower_mode == THY_LOWER_STORED_NOTES) {
        uint8_t excluded = selected ? instrument->excluded_slots[instrument->selected_chord] :
            instrument->global_excluded_slots;
        return (uint8_t)(~excluded & 0x7fu);
    }
    uint8_t layers = selected ? instrument->pad_octave_layers[instrument->selected_chord] :
        instrument->octave_layers;
    return layers != 0u ? layers : 8u;
}

uint8_t thy_instrument_lower_available(const thy_instrument *instrument)
{
    if (instrument == NULL || instrument->lower_mode > THY_LOWER_OCTAVE_LAYERS)
        return 0u;
    if (instrument->lower_mode == THY_LOWER_OCTAVE_LAYERS) return 0x7fu;
    if (instrument->layout == NULL) return 0u;
    if (instrument->chord_layout != THY_CHORD_LAYOUT_CLASSIC) {
        thy_chord_cell cell;
        if(instrument->slot_scope==THY_SLOTS_SELECTED_PAD)
            return thy_instrument_chord_cell(instrument,instrument->selected_chord,&cell)==THY_OK?cell.valid_mask:0u;
        uint8_t available=0u;
        for(uint8_t n=0u;n<48u;++n)if(thy_instrument_chord_cell(instrument,n,&cell)==THY_OK)available|=cell.valid_mask;
        return available;
    }
    if (instrument->slot_scope == THY_SLOTS_SELECTED_PAD)
        return instrument->selected_chord < 48u ?
            instrument->layout[instrument->selected_chord].valid_mask : 0u;
    uint8_t available = 0u;
    for (unsigned n = 0u; n < 48u; ++n) available |= instrument->layout[n].valid_mask;
    return available;
}

int thy_instrument_set_lower_mode(thy_instrument *instrument, uint8_t mode)
{
    if (!valid(instrument) || mode > THY_LOWER_OCTAVE_LAYERS)
        return THY_INVALID_ARGUMENT;
    if (instrument->lower_mode != mode) {
        instrument->lower_mode = mode;
        ++instrument->revision;
    }
    return THY_OK;
}

uint8_t thy_instrument_default_scale_shortcut(uint8_t key)
{
    static const uint8_t shortcuts[THY_SCALE_SHORTCUT_COUNT] = {
        56u, THY_SCALE_SHORTCUT_LAYOUT, 21u, 22u, 23u, 24u,
        25u, 26u, 27u, 14u, 35u, 60u
    };
    return key < THY_SCALE_SHORTCUT_COUNT ? shortcuts[key] : UINT8_MAX;
}

int thy_instrument_set_scale_shortcut(thy_instrument *instrument, uint8_t key,
    uint8_t assignment)
{
    if (!valid(instrument) || key >= THY_SCALE_SHORTCUT_COUNT ||
        assignment > THY_SCALE_SHORTCUT_LAYOUT+THY_LIBRARY_SCALES ||
        (assignment>THY_SCALE_SHORTCUT_LAYOUT&&!instrument->library.scales[assignment-71u].used)) return THY_INVALID_ARGUMENT;
    if (instrument->scale_shortcuts[key] != assignment) {
        instrument->scale_shortcuts[key] = assignment;
        ++instrument->revision;
    }
    return THY_OK;
}

uint8_t thy_instrument_scale_shortcut(const thy_instrument *instrument, uint8_t key)
{
    return instrument != NULL && key < THY_SCALE_SHORTCUT_COUNT ?
        instrument->scale_shortcuts[key] : UINT8_MAX;
}

int thy_instrument_control(thy_instrument *instrument, uint8_t bank,
    uint8_t index, int pressed)
{
    uint32_t bit;
    uint8_t action;
    if (!valid(instrument) || bank >= 4u || index >= control_counts[bank] ||
        (pressed != 0 && pressed != 1)) return THY_INVALID_ARGUMENT;
    bit = UINT32_C(1) << index;
    if (((instrument->control_masks[bank] & bit) != 0u) == (pressed != 0))
        return THY_OK;
    if (pressed != 0) {
        instrument->control_masks[bank] |= bit;
        instrument->controls_seen[bank] |= bit;
    } else instrument->control_masks[bank] &= ~bit;
    if (bank == 0u) action = THY_ACTION_NAVIGATION;
    else if (bank == 2u) action = THY_ACTION_ROOT;
    else if (bank == 3u) action = THY_ACTION_SHORTCUT;
    else action = index < 3u ? THY_ACTION_INVERSION : index == 3u ? THY_ACTION_STRUM :
                  index < 7u ? THY_ACTION_SPREAD : THY_ACTION_NOTE_SLOT;
    int consumed = instrument->control_filter &&
        instrument->control_filter(instrument->control_filter_context, bank, index, pressed);
    if (consumed) action = THY_ACTION_NONE;
    if (pressed != 0 && !consumed) {
        if (bank == 0u) {
            if (instrument->navigation != NULL)
                instrument->navigation(instrument->navigation_context, index);
            else navigation(instrument, index);
        }
        else if (bank == 2u) {
            if(instrument->performance.root!=index)thy_instrument_key_changed(instrument);
            instrument->performance.root = index;
            ++instrument->layout_revision;
        } else if (bank == 3u) {
            if (instrument->scale_shortcuts[index] == THY_SCALE_SHORTCUT_LAYOUT) {
                instrument->performance.melody_layout =
                    cycle(instrument->performance.melody_layout, 1, THY_LAYOUT_COUNT);
                instrument->engine_kind = THY_PLAY_ENGINE_PLAY;
            }
            else instrument->performance.shortcut = index;
            ++instrument->layout_revision;
        } else if (index < 3u) {
            uint8_t value = (uint8_t)(index + 1u);
            if(instrument->voicing_mode==1u){
                if(thy_instrument_drop_available(instrument)&(1u<<index))instrument->drop_mask^=(uint8_t)(1u<<index);
            } else if(!instrument->voicing_mode)instrument->performance.inversion = instrument->performance.inversion == value ? 0u : value;
        } else if (index == 3u) instrument->performance.strum ^= 1u;
        else if (index < 7u) {
            uint8_t value = (uint8_t)(index - 3u);
            instrument->performance.spread = instrument->performance.spread == value ? 0u : value;
        } else if (instrument->lower_mode == THY_LOWER_OCTAVE_LAYERS) {
            /* Layer edits never alter the captured pitches/routes of notes
             * already held. Both scopes keep an independent saved profile. */
            uint8_t *layers = instrument->slot_scope == THY_SLOTS_SELECTED_PAD ?
                (instrument->selected_chord < 48u ?
                    &instrument->pad_octave_layers[instrument->selected_chord] : NULL) :
                &instrument->octave_layers;
            if (layers != NULL) {
                *layers ^= (uint8_t)(1u << (index - 7u));
                if (*layers == 0u) *layers = 8u;
            }
        } else if ((thy_instrument_lower_available(instrument) &
                    (1u << (index - 7u))) != 0u) {
            if (instrument->slot_scope == THY_SLOTS_LEFT_SIDE)
                instrument->global_excluded_slots ^= (uint8_t)(1u << (index - 7u));
            else if (instrument->selected_chord < 48u)
                instrument->excluded_slots[instrument->selected_chord] ^=
                    (uint8_t)(1u << (index - 7u));
        }
    }
    ++instrument->revision;
    instrument->last_event = (thy_instrument_control_event){bank, index,
        (uint8_t)pressed, action, (int16_t)index, instrument->last_event.sequence + 1u};
    if (instrument->action != NULL)
        instrument->action(instrument->action_context, &instrument->last_event);
    return THY_OK;
}

int thy_instrument_controls(thy_instrument *instrument, uint8_t bank,
    uint32_t logical_mask)
{
    unsigned i;
    if (!valid(instrument) || bank >= 4u ||
        (logical_mask >> control_counts[bank]) != 0u) return THY_INVALID_ARGUMENT;
    for (i = 0u; i < control_counts[bank]; ++i) {
        int result = thy_instrument_control(instrument, bank, (uint8_t)i,
                                            (logical_mask & (1u << i)) != 0u);
        if (result != THY_OK) return result;
    }
    return THY_OK;
}

void thy_instrument_panic(thy_instrument *instrument)
{
    if(instrument)instrument->voicing_previous_mask=0u;
    unsigned i;
    if (!valid(instrument)) return;
    if (instrument->musical_reset) instrument->musical_reset(instrument->musical_context);
    thy_core_panic(instrument->core);
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        int newly_blocked = instrument->sensors[i].held != 0u &&
                            instrument->sensors[i].blocked == 0u;
        instrument->sensors[i].blocked = instrument->sensors[i].held;
        instrument->sensors[i].sent_pressure = 0u;
        clear_strike(&instrument->sensors[i]);
        instrument->context_held[i] = 0u;
        instrument->held_color_valid[i] = 0u;
        instrument->held_pressure_enabled[i] = 0u;
        instrument->held_input_valid[i] = 0u;
        if (newly_blocked)
            record_transition(instrument, (uint8_t)i, THY_PAD_TRANSITION_BLOCK,
                              instrument->core->now_ms);
    }
    ++instrument->revision;
}

int thy_instrument_set_input_enabled(thy_instrument *instrument, int enabled)
{
    if (!valid(instrument) || (enabled != 0 && enabled != 1))
        return THY_INVALID_ARGUMENT;
    if ((instrument->input_enabled != 0u) == (enabled != 0)) return THY_OK;
    instrument->input_enabled = (uint8_t)enabled;
    if (enabled == 0) thy_instrument_panic(instrument);
    else ++instrument->revision;
    return THY_OK;
}


int thy_instrument_get_expression_profile(const thy_instrument *i,uint8_t p,
    thy_expression_profile *profile,uint8_t *overridden)
{
    if(!valid(i)||p>=THY_PAD_COUNT||!profile||!overridden||i->mapping[p]>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
    *overridden=i->expression_profile_overridden[p];
    *profile=*overridden?i->pad_expression_profiles[p]:i->expression_profiles[i->mapping[p]/48u];
    return thy_expression_profile_valid(profile)?THY_OK:THY_INVALID_ARGUMENT;
}
static int expression_editable(const thy_instrument *i,uint8_t p,const thy_expression_profile *profile)
{
    const thy_instrument_sensor *s=&i->sensors[p];
    if(!velocity_editable(i,p))return 0;
    return !profile->enabled || (s->detector_profile==THY_DETECTOR_RELATIVE?
        s->calibration.full_scale>=2u:s->response.pressure_full_scale-s->response.pressure_floor>=2u);
}
int thy_instrument_set_cluster_expression_profile(thy_instrument *i,uint8_t side,const thy_expression_profile *profile)
{
    if(!valid(i)||side>1u||!thy_expression_profile_valid(profile))return THY_INVALID_ARGUMENT;
    for(unsigned p=0;p<THY_PAD_COUNT;++p)
        if(i->mapping[p]/48u==side&&!i->expression_profile_overridden[p]&&!expression_editable(i,(uint8_t)p,profile))return THY_INVALID_ARGUMENT;
    i->expression_profiles[side]=*profile;++i->revision;return THY_OK;
}
int thy_instrument_set_pad_expression_profile(thy_instrument *i,uint8_t p,const thy_expression_profile *profile)
{
    if(!valid(i)||p>=THY_PAD_COUNT||!thy_expression_profile_valid(profile)||!expression_editable(i,p,profile))return THY_INVALID_ARGUMENT;
    i->pad_expression_profiles[p]=*profile;i->expression_profile_overridden[p]=1u;++i->revision;return THY_OK;
}
int thy_instrument_clear_expression_profile(thy_instrument *i,uint8_t p)
{
    if(!valid(i)||p>=THY_PAD_COUNT||i->mapping[p]>=THY_PAD_COUNT||
        !expression_editable(i,p,&i->expression_profiles[i->mapping[p]/48u]))return THY_INVALID_ARGUMENT;
    i->expression_profile_overridden[p]=0u;++i->revision;return THY_OK;
}

int thy_instrument_expression_axes(const thy_instrument *i,uint8_t pad,uint16_t raw,
    uint8_t *pressure,uint8_t *aftertouch)
{
    if(!valid(i)||pad>=96u||raw>4095u||!pressure||!aftertouch)return THY_INVALID_ARGUMENT;
    thy_expression_profile profile;thy_instrument_response response;uint8_t custom;
    if(thy_instrument_get_expression_profile(i,pad,&profile,&custom)!=THY_OK ||
        thy_instrument_get_pad_response(i,pad,&response,&custom)!=THY_OK)return THY_INVALID_ARGUMENT;
    int override=0;
    if(i->sampling_provider){thy_instrument_velocity velocity;
        if(thy_instrument_get_velocity(i,pad,&velocity,&custom)!=THY_OK)return THY_INVALID_ARGUMENT;
        thy_instrument_response sampled=response;thy_instrument_velocity sampled_velocity=velocity;
        override=i->sampling_provider(i->sampling_provider_context,pad,&sampled,&sampled_velocity);
        if(override<0||override>1||(override&&(!valid_response(&sampled)||!thy_instrument_velocity_valid(&sampled_velocity))))return THY_INVALID_ARGUMENT;
        if(override)response=sampled;
    }
    if(!override&&i->sensors[pad].detector_profile==THY_DETECTOR_RELATIVE){
        int delta=((int)raw-i->sensors[pad].calibration.baseline)*i->sensors[pad].calibration.polarity;
        raw=(uint16_t)(delta<0?0:delta);response.pressure_floor=0u;
        response.pressure_full_scale=i->sensors[pad].calibration.full_scale;response.pressure_curve=0u;
    }
    return thy_expression_profile_axes(&profile,raw,response.pressure_floor,response.pressure_full_scale,
        response.pressure_curve,pressure,aftertouch)?THY_INVALID_ARGUMENT:THY_OK;
}
