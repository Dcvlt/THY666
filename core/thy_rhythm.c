/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_engine.h"
#include "thy_lighting.h"

static int valid_engine(const thy_engine *engine)
{
    return engine && engine->instrument && engine->instrument->core;
}

static int time_result(int result)
{
    if (result == THY_TIME_OK || result == THY_TIME_IGNORED) return THY_OK;
    if (result == THY_TIME_BACKWARD) return THY_ENGINE_TIME_BACKWARD;
    if (result == THY_TIME_OVERFLOW) return THY_ENGINE_TIME_OVERFLOW;
    return THY_INVALID_ARGUMENT;
}

static int live_slot(uint32_t source)
{
    return source == THY_ENGINE_ARP1 ? 0 : source == THY_ENGINE_ARP2 ? 1 :
        source == THY_ENGINE_PREVIEW ? 2 : -1;
}

static int track_slot(uint32_t source)
{
    return source >= THY_ENGINE_TRACK_BASE &&
        source < THY_ENGINE_TRACK_BASE + THY_TRANSPORT_TRACKS ?
        (int)(source - THY_ENGINE_TRACK_BASE) : -1;
}

/* Zero is a retired generation, never an alias for an earlier run. */
static uint32_t next_generation(uint32_t generation)
{
    return generation && generation < UINT32_MAX ? generation + 1u : 0u;
}

uint32_t thy_engine_source_generation(const thy_engine *engine, uint32_t source)
{
    if (!valid_engine(engine)) return 0u;
    int slot = live_slot(source);
    if (slot >= 0) return engine->live_generation[slot];
    slot = track_slot(source);
    return slot >= 0 ? engine->rhythm_transport.tracks[slot].generation : 0u;
}

static void reconcile(thy_engine *engine)
{
    thy_core *core = engine->instrument->core;
    if (engine->observed_clock_generation != engine->musical_clock.generation) {
        /* Source/loss boundaries cancel generators, never physical pad owners. */
        for (unsigned n = 0u; n < 2u; ++n) thy_arp_clear(&engine->arps[n], core);
        (void)thy_core_generated_release_all(core);
        for (unsigned n = 0u; n < 3u; ++n)
            engine->live_generation[n] = next_generation(engine->live_generation[n]);
        engine->observed_clock_generation = engine->musical_clock.generation;
    }
    for (unsigned n = 0u; n < THY_GENERATED_OWNER_COUNT; ++n) {
        const thy_generated_owner *owner = thy_core_generated_at(core, (uint16_t)n);
        if (!owner) continue;
        int track = track_slot(owner->source);
        if (track >= 0 && owner->run != engine->rhythm_transport.tracks[track].generation)
            (void)thy_core_generated_release(core, owner->handle);
    }
    engine->session.transport = engine->rhythm_transport.state == THY_TRANSPORT_PLAYING ?
        THY_ENGINE_PLAYING : THY_ENGINE_STOPPED;
    engine->session.clock = engine->musical_clock.status;
    engine->session.epoch = engine->rhythm_transport.generation;
    for (unsigned n = 0u; n < THY_TRANSPORT_TRACKS; ++n) {
        engine->session.tracks[n].generation = engine->rhythm_transport.tracks[n].generation;
        engine->session.tracks[n].playhead = engine->rhythm_transport.tracks[n].playhead;
        engine->session.tracks[n].phase = engine->rhythm_transport.tracks[n].running;
    }
}

static void arp_scale(const thy_engine *engine, thy_arp_config *config)
{
    const thy_instrument *i = engine->instrument;
    if (i->degree_count) {
        config->scale_count = i->degree_count;
        for (unsigned n = 0u; n < i->degree_count; ++n)
            config->scale[n] = (uint8_t)i->degrees[n];
    }
    config->root = i->performance.root;
}

static void advance_arps(thy_engine *engine)
{
    int ready = thy_clock_ready(&engine->musical_clock);
    for (unsigned n = 0u; n < 2u; ++n) {
        thy_arp *arp = &engine->arps[n];
        thy_arp_config config = arp->config;
        arp_scale(engine, &config);
        int changed = config.root != arp->config.root ||
            config.scale_count != arp->config.scale_count;
        for (unsigned k = 0u; k < config.scale_count; ++k)
            if (config.scale[k] != arp->config.scale[k]) changed = 1;
        if (changed)
            (void)thy_arp_configure(arp, engine->instrument->core, &config,
                engine->musical_clock.tick, engine->live_generation[n], ready);
        (void)thy_arp_advance(arp, engine->instrument->core,
            engine->musical_clock.tick, engine->live_generation[n], ready);
    }
}

static int advance_shared_time(thy_engine *engine, uint32_t now_ms, int process_arps)
{
    if (!valid_engine(engine)) return THY_INVALID_ARGUMENT;
    thy_clock clock = engine->musical_clock;
    thy_transport transport = engine->rhythm_transport;
    if (!engine->rhythm_epoch_set) clock.now_ms = now_ms;
    int result = thy_clock_advance(&clock, now_ms);
    if (result < 0) return time_result(result);
    result = thy_transport_advance(&transport, &clock);
    if (result < 0) return time_result(result);
    result = thy_core_advance(engine->instrument->core, now_ms);
    if (result != THY_OK) return result;
    engine->musical_clock = clock;
    engine->rhythm_transport = transport;
    engine->rhythm_epoch_set = 1u;
    reconcile(engine);
    if (process_arps) {
        advance_arps(engine); thy_engine_sequencer_tick(engine);
        thy_engine_repeat_tick(engine, now_ms);
        thy_engine_calibration_tick(engine);
        thy_engine_expression_tick(engine, now_ms);
        thy_engine_harmony_tick(engine);
    }
    return THY_OK;
}

int thy_engine_advance(thy_engine *engine, uint32_t now_ms)
{
    return advance_shared_time(engine, now_ms, 1);
}

int thy_engine_clock_configure(thy_engine *engine, uint32_t milli_bpm,
    uint8_t source, uint8_t numerator, uint8_t denominator, uint8_t quantize)
{
    if (!valid_engine(engine)) return THY_INVALID_ARGUMENT;
    thy_clock clock = engine->musical_clock;
    thy_transport transport = engine->rhythm_transport;
    if (thy_clock_set_tempo(&clock, milli_bpm, clock.now_ms) < 0 ||
        thy_clock_set_meter(&clock, numerator, denominator) < 0 ||
        thy_clock_set_source(&clock, source, clock.now_ms) < 0 ||
        thy_transport_set_quantize(&transport, quantize) < 0)
        return THY_INVALID_ARGUMENT;
    int result = thy_transport_advance(&transport, &clock);
    if (result < 0) return time_result(result);
    engine->musical_clock = clock;
    engine->rhythm_transport = transport;
    reconcile(engine);
    return THY_OK;
}

int thy_engine_transport_start(thy_engine *engine)
{
    if (!valid_engine(engine)) return THY_INVALID_ARGUMENT;
    if (engine->expression_config.enabled) return THY_SEQ_BUSY;
    if(engine->sequencer.capture.state==THY_SEQ_QUEUED ||
        engine->sequencer.capture.state==THY_SEQ_RECORDING ||
        engine->sequencer.capture.state==THY_SEQ_FINISHING) return THY_SEQ_BUSY;
    for(unsigned n=0;n<THY_SEQ_TRACKS;++n) {
        int content=thy_sequencer_track_count(&engine->sequencer,(uint8_t)n)>0u;
        int selected=!engine->sequencer_ui.solo || n==engine->sequencer.selected_track;
        /* Keep explicit transport-only foundation tracks usable. Song tracks
         * additionally follow the current preview scope. Never retain an old
         * solo track in a later All start. */
        if(content || thy_sequencer_track_exists(&engine->sequencer,(uint8_t)n))
            (void)thy_transport_set_enabled(&engine->rhythm_transport,(uint8_t)n,
                (uint8_t)(content && selected && !(engine->sequencer.tracks[n].flags&THY_SEQ_FLAG_DISABLED)));
        else if(engine->sequencer_ui.solo)
            (void)thy_transport_set_enabled(&engine->rhythm_transport,(uint8_t)n,0u);
    }
    if (engine->sequencer.capture.state == THY_SEQ_ARMED)
        (void)thy_transport_set_enabled(&engine->rhythm_transport, engine->sequencer.capture.track, 1u);
    thy_transport transport = engine->rhythm_transport;
    int result = thy_transport_start(&transport, &engine->musical_clock);
    if (result < 0) return time_result(result);
    engine->rhythm_transport = transport;
    reconcile(engine);
    thy_engine_sequencer_tick(engine);
    return time_result(result);
}

int thy_engine_transport_stop(thy_engine *engine)
{
    if (!valid_engine(engine)) return THY_INVALID_ARGUMENT;
    (void)thy_sequencer_stop(&engine->sequencer, engine->instrument->core,
        &engine->rhythm_transport, &engine->musical_clock);
    thy_transport transport = engine->rhythm_transport;
    int result = thy_transport_stop(&transport, &engine->musical_clock);
    if (result < 0) return time_result(result);
    engine->rhythm_transport = transport;
    reconcile(engine);
    return time_result(result);
}

int thy_engine_track_toggle(thy_engine *engine, uint8_t track)
{
    if (!valid_engine(engine)) return THY_INVALID_ARGUMENT;
    thy_transport transport = engine->rhythm_transport;
    int result = thy_transport_toggle_track(&transport, track,
        &engine->musical_clock);
    if (result < 0) return time_result(result);
    engine->rhythm_transport = transport;
    reconcile(engine);
    return time_result(result);
}

int thy_engine_track_loop(thy_engine *engine, uint8_t track,
    uint32_t start, uint32_t length)
{
    if (!valid_engine(engine)) return THY_INVALID_ARGUMENT;
    if (start > THY_SEQ_MAX_TICK || !length || length > THY_SEQ_MAX_TICK-start)
        return THY_INVALID_ARGUMENT;
    return thy_engine_sequencer_loop(engine,track,start,start+length);
}

static int receive_time(thy_engine *engine, const uint8_t *message,
    uint8_t length, uint32_t now_ms, int process_arps)
{
    if (!valid_engine(engine) || !message) return THY_INVALID_ARGUMENT;
    if (length && engine->expression_config.enabled &&
        (message[0]==0xfau || message[0]==0xfbu)) return THY_SEQ_BUSY;
    thy_clock clock = engine->musical_clock;
    thy_transport transport = engine->rhythm_transport;
    if (!engine->rhythm_epoch_set) clock.now_ms = now_ms;
    /* Preflight both timelines before any live strum or cleanup is published. */
    int result = thy_transport_receive_midi(&transport, &clock, message, length, now_ms);
    if (result < 0) return time_result(result);
    if (result == THY_TIME_IGNORED) return THY_OK;
    if((message[0]==0xf2u && engine->sequencer.capture.state!=THY_SEQ_CAPTURE_OFF) ||
        (message[0]==0xfau && (engine->sequencer.capture.state==THY_SEQ_QUEUED ||
         engine->sequencer.capture.state==THY_SEQ_RECORDING ||
         engine->sequencer.capture.state==THY_SEQ_FINISHING))) return THY_SEQ_BUSY;
    result = thy_core_advance(engine->instrument->core, now_ms);
    if (result != THY_OK) return result;
    if(message[0]==0xfcu)
        (void)thy_sequencer_stop(&engine->sequencer,engine->instrument->core,
            &engine->rhythm_transport,&engine->musical_clock);
    engine->musical_clock = clock;
    engine->rhythm_transport = transport;
    engine->rhythm_epoch_set = 1u;
    reconcile(engine);
    if (process_arps) { advance_arps(engine); thy_engine_sequencer_tick(engine); }
    return THY_OK;
}

int thy_engine_receive_time(thy_engine *engine, const uint8_t *message,
    uint8_t length, uint32_t now_ms)
{
    return receive_time(engine, message, length, now_ms, 1);
}

int thy_engine_receive_time_deferred(thy_engine *engine, const uint8_t *message,
    uint8_t length, uint32_t now_ms)
{
    return receive_time(engine, message, length, now_ms, 0);
}

int thy_engine_usb_reset(thy_engine *engine, uint32_t now_ms)
{
    if (!valid_engine(engine)) return THY_INVALID_ARGUMENT;
    if(engine->channel_expression)thy_channel_expression_usb_reset(engine->channel_expression);
    thy_engine_repeat_cancel(engine);
    thy_engine_sequencer_usb_clear(engine);
    /* Internal DIN/live ownership is independent of a USB session boundary. */
    if (engine->musical_clock.source != THY_CLOCK_SOURCE_USB) return THY_OK;
    thy_clock clock = engine->musical_clock;
    thy_transport transport = engine->rhythm_transport;
    if (!engine->rhythm_epoch_set) clock.now_ms = now_ms;
    int result = thy_clock_advance(&clock, now_ms);
    if (result < 0) return time_result(result);
    clock.generation = next_generation(clock.generation);
    clock.generation_tick = clock.tick;
    clock.status = THY_CLOCK_WAITING;
    clock.last_pulse_interval_ms = 0u;
    result = thy_transport_advance(&transport, &clock);
    if (result < 0) return time_result(result);
    engine->musical_clock = clock;
    engine->rhythm_transport = transport;
    engine->rhythm_epoch_set = 1u;
    reconcile(engine);
    return THY_OK;
}

int thy_engine_generated_acquire(thy_engine *engine, uint32_t source,
    uint32_t run, uint8_t note, uint8_t channel, uint8_t destinations,
    uint8_t velocity, thy_generated_handle *handle)
{
    if (!valid_engine(engine) || !handle ||
        (live_slot(source) < 0 && track_slot(source) < 0)) return THY_INVALID_ARGUMENT;
    if (engine->expression_config.enabled) return THY_SEQ_BUSY;
    uint32_t current = thy_engine_source_generation(engine, source);
    if (!current || current != run) return THY_ENGINE_STALE_RUN;
    if (!thy_clock_ready(&engine->musical_clock) || !engine->musical_clock.generation)
        return THY_ENGINE_SOURCE_NOT_READY;
    int track = track_slot(source);
    if (track >= 0 && (engine->rhythm_transport.state != THY_TRANSPORT_PLAYING ||
        engine->rhythm_transport.suspended || !engine->rhythm_transport.tracks[track].running))
        return THY_ENGINE_SOURCE_NOT_READY;
    return thy_core_generated_acquire(engine->instrument->core, source, run,
        note, channel, destinations, velocity, handle);
}

int thy_engine_generated_clear(thy_engine *engine, uint32_t source)
{
    if (!valid_engine(engine)) return THY_INVALID_ARGUMENT;
    uint32_t generation = thy_engine_source_generation(engine, source);
    if (!generation) return THY_INVALID_ARGUMENT;
    int result = thy_core_generated_release_source(engine->instrument->core, source, generation);
    int slot = live_slot(source);
    if (slot >= 0) {
        if (slot < 2) thy_arp_clear(&engine->arps[slot], engine->instrument->core);
        engine->live_generation[slot] = next_generation(generation);
    }
    else {
        slot = track_slot(source);
        engine->rhythm_transport.tracks[slot].generation = next_generation(generation);
    }
    reconcile(engine);
    return result;
}

void thy_engine_panic(thy_engine *engine)
{
    if (!valid_engine(engine)) return;
    thy_engine_calibration_close(engine);
    thy_engine_repeat_cancel(engine);
    (void)thy_engine_transport_stop(engine);
    engine->live_generation[2] = next_generation(engine->live_generation[2]);
    thy_instrument_panic(engine->instrument);
}

static int synchronize_input(thy_engine *engine)
{
    /* Raw sensor sampling advances the physical clock before its press/release
     * callback. The shared host/runtime clock observes that same timestamp. */
    thy_core *core = engine->instrument->core;
    int result=advance_shared_time(engine, core->clock_initialized ? core->now_ms :
        engine->musical_clock.now_ms, 0);
    if(result==THY_OK) {
        (void)thy_sequencer_capture_advance(&engine->sequencer,&engine->rhythm_transport,&engine->musical_clock);
        thy_engine_sequencer_capture(engine);
    }
    return result;
}

int thy_engine_press_shaped(thy_engine *engine, uint8_t pad, const int16_t *notes,
    size_t count, int16_t transpose, uint8_t channel, uint8_t destinations,
    uint8_t velocity, const thy_shape *shape)
{
    if (!valid_engine(engine) || pad >= THY_PAD_COUNT || channel > 15u ||
        !destinations || destinations > 7u || !velocity || velocity > 127u)
        return THY_INVALID_ARGUMENT;
    thy_instrument *i = engine->instrument;
    thy_core *core = i->core;
    if (thy_core_pad_held(core, pad)) return THY_OK;
    thy_shape plain = {0};
    if (!shape) shape = &plain;
    int16_t resolved[THY_MAX_NOTES]; size_t resolved_count = 0u;
    int result = thy_core_preview(core, notes, count, transpose, shape,
        resolved, &resolved_count);
    if (result != THY_OK) return result;
    result = synchronize_input(engine);
    if (result != THY_OK) return result;
    unsigned logical = thy_instrument_role_pad(i,i->mapping[pad]);
    if (logical >= THY_PAD_COUNT) return THY_INVALID_ARGUMENT;
    unsigned physical_side=i->mapping[pad]/48u;
    if(engine->performance[physical_side].velocity){
        thy_instrument_velocity sensor;uint8_t custom;
        if(thy_instrument_get_velocity(i,pad,&sensor,&custom)!=THY_OK)return THY_INVALID_ARGUMENT;
        if(sensor.mode==THY_VELOCITY_FIXED){velocity=engine->performance[physical_side].velocity;i->sensors[pad].strike_velocity=velocity;}
    }
    int input = thy_sequencer_ui_input(&engine->sequencer_ui, (uint8_t)logical);
    uint8_t role = input ? (uint8_t)(input == 2) : (uint8_t)(logical < 48u);
    unsigned musical_source = input ? logical % 48u + (input == 1 ? 48u : 0u) : logical;
    int muted=role && i->live_update && i->live_mute_chord;
    if (engine->expression_config.enabled) {
        if (!engine->expression) return THY_ENGINE_SOURCE_NOT_READY;
        int consumed=0;
        uint8_t reference=resolved_count ? (uint8_t)resolved[0] : 0u;
        /* Glide distance must not change when strum reverses attack order. */
        for(size_t n=1;n<resolved_count;++n)
            if(resolved[n]<reference)reference=(uint8_t)resolved[n];
        thy_expression_profile profile;uint8_t custom;
        if(thy_instrument_get_expression_profile(i,pad,&profile,&custom)!=THY_OK)return THY_INVALID_ARGUMENT;
        uint8_t secondary=engine->expression_config.pressure_output[physical_side]==THY_MPE_PRESSURE&&!profile.enabled?64u:0u;
        result=thy_mpe_press_ex(engine->expression,pad,i->mapping[pad],reference,
            resolved,muted?0u:resolved_count,destinations,velocity,shape->strum_ms,&consumed,secondary);
        if(result==THY_OK&&!consumed)engine->input_velocity[pad]=velocity;
        if(result==THY_OK && !consumed && muted && resolved_count)
            thy_instrument_capture_silent_input(i,pad,(uint8_t)musical_source,resolved,resolved_count);
        if(result==THY_OK && !consumed && role)
            thy_instrument_follow_chord(i,(uint8_t)musical_source,resolved,resolved_count,transpose);
        if(result==THY_OK && !consumed)
            thy_engine_harmony_input(engine,(uint8_t)musical_source,resolved,resolved_count);
        if (result==THY_OK && resolved_count && !consumed &&
            shape->strum_order==THY_STRUM_ALTERNATING) core->alternating_down^=1u;
        return result;
    }
    unsigned zone = (logical / 48u) * 3u + ((logical % 48u) / 16u);
    uint8_t admitted = 0u, layer = 0u;
    for (unsigned n = 0u; n < 2u; ++n) {
        const thy_arp_config *config = &engine->arps[n].config;
        if (config->enabled && (config->source_zones & (1u << zone))) {
            if (!engine->live_generation[n]) return THY_ENGINE_STALE_RUN;
            if (resolved_count && thy_clock_ready(&engine->musical_clock) &&
                engine->arps[n].press_order == UINT64_MAX) return THY_OWNER_LIMIT;
            admitted |= (uint8_t)(1u << n);
            layer |= config->layer;
        }
    }
    /* Reserve/capture the physical contribution before admitting either Arp.
     * Valid Arp admissions cannot fail for output pressure: their scheduler
     * records/skips a whole attack instead of publishing a partial chord. */
    uint8_t target=thy_instrument_route_target(i,(uint8_t)musical_source);
    thy_engine_channel_config response=thy_engine_performance_response(engine,pad,target);
    uint8_t flags=thy_channel_expression_flags(response.aftertouch,response.bend);
    uint16_t queue_before=core->queue_count;
    if(engine->channel_expression && !muted && (!admitted||layer) && resolved_count) {
        result=thy_channel_expression_prepare_attack(engine->channel_expression,channel,destinations,flags);
        if(result!=THY_OK)return result;
    }
    if ((!admitted || layer) && !muted)
        result = thy_core_press_shaped(core, pad, notes, count, transpose,
            channel, destinations, velocity, shape);
    else result = thy_core_press(core, pad, NULL, 0u, 0, channel, destinations, velocity);
    if (result != THY_OK) {core->queue_count=queue_before;return result;}
    if(engine->channel_expression)(void)thy_channel_expression_capture(engine->channel_expression,pad,flags);
    engine->input_strummed[pad]=(uint8_t)(role && shape->strum_ms!=0u);
    i->held_input_valid[pad] = 0u;
    if (((admitted && !layer) || muted) && resolved_count)
        thy_instrument_capture_silent_input(i,pad,(uint8_t)musical_source,resolved,resolved_count);
    for (unsigned n = 0u; n < 2u; ++n)
        if (admitted & (1u << n))
            (void)thy_arp_press(&engine->arps[n], core, pad, role,
                resolved, resolved_count, velocity, engine->musical_clock.tick,
                engine->live_generation[n], thy_clock_ready(&engine->musical_clock));
    thy_engine_sequencer_input(engine,pad,resolved,resolved_count,velocity,role);
    if(role)thy_instrument_follow_chord(i,(uint8_t)musical_source,resolved,resolved_count,transpose);
    thy_engine_harmony_input(engine,(uint8_t)musical_source,resolved,resolved_count);
    thy_engine_sequencer_capture(engine);
    return THY_OK;
}

static int musical_attack(void *context, uint8_t pad, const int16_t *notes,
    size_t count, int16_t transpose, const thy_instrument_route *route,
    const thy_shape *shape)
{
    return thy_engine_press_shaped(context, pad, notes, count, transpose,
        route->channel, route->destinations, route->velocity, shape);
}

static int musical_release(void *context, uint8_t pad)
{
    thy_engine *engine = context;
    thy_engine_harmony_release(engine,thy_instrument_role_pad(engine->instrument,engine->instrument->mapping[pad]));
    if (engine->expression_config.enabled && engine->expression)
        return thy_mpe_release(engine->expression,pad);
    /* Release is never rejected because a timing source went away. It removes
     * captured contributions even after filters, Layer or enable were edited. */
    (void)synchronize_input(engine);
    thy_engine_sequencer_capture(engine);
    thy_engine_sequencer_release_input(engine,pad);
    for (unsigned n = 0u; n < 2u; ++n)
        (void)thy_arp_release(&engine->arps[n], engine->instrument->core, pad,
            engine->musical_clock.tick, engine->live_generation[n],
            thy_clock_ready(&engine->musical_clock));
    int result=thy_core_release(engine->instrument->core, pad);
    if(engine->channel_expression)thy_channel_expression_release(engine->channel_expression,pad);
    thy_engine_sequencer_capture(engine);
    return result;
}

static void musical_reset(void *context)
{
    thy_engine *engine = context;
    for(unsigned n=0u;n<96u;++n)thy_engine_harmony_release(engine,(uint8_t)n);
    if (engine->expression) thy_mpe_release_all(engine->expression);
    if (engine->channel_expression) thy_channel_expression_release_all(engine->channel_expression);
    thy_sequencer_cancel(&engine->sequencer);
    thy_sequencer_release(&engine->sequencer,engine->instrument->core);
    (void)thy_transport_stop(&engine->rhythm_transport,&engine->musical_clock);
    for(unsigned p=0;p<96u;++p) {
        engine->input_pitches[p][0]=engine->input_pitches[p][1]=0u;
        engine->captured_strum[p][0]=engine->captured_strum[p][1]=0u;
        engine->input_identity[p]=0u; engine->input_assignment[p]=engine->input_strummed[p]=0u;
    }
    thy_engine_sequencer_usb_clear(engine);
    for (unsigned n = 0u; n < 2u; ++n) {
        thy_arp_clear(&engine->arps[n], engine->instrument->core);
        engine->live_generation[n] = next_generation(engine->live_generation[n]);
    }
}

static int light_overlay(void *context,uint8_t logical,uint16_t components[3])
{
    thy_engine *e=context;
    uint8_t role=thy_instrument_role_pad(e->instrument,logical);
    if(thy_engine_harmony_active(e)&&e->harmony.cache_ready&&role<48u&&
        e->harmony.pad_masks[role]&&!thy_instrument_context_pad(e->instrument,role)) {
        unsigned pad=0;while(pad<96u&&e->instrument->mapping[pad]!=logical)++pad;
        if(pad<96u&&!e->instrument->sensors[pad].held&&!e->instrument->core->owners[pad].held) {
            uint8_t root=e->harmony.pad_roots[role];if(root>=12u)root=0;
            uint8_t hint=thy_engine_harmony_pad_role(e,role);
            int result=thy_lighting_harmony_color(thy_lighting_note_color_palette(root,e->instrument->light_palette),hint,components);
            unsigned factor=100u;
            if(hint==THY_HARMONY_OTHER&&e->harmony.highlights)factor=100u-e->harmony.hint_contrast*60u/100u;
            else if(hint==THY_HARMONY_SELECTED&&e->harmony.motion&&e->instrument->animations_enabled){
                unsigned phase=e->instrument->core->now_ms%1600u;if(phase>800u)phase=1600u-phase;factor=75u+phase/32u;
            }
            for(unsigned c=0;c<3u;++c)components[c]=(uint16_t)(components[c]*factor/100u);
            return result;
        }
    }
    if(thy_engine_harmony_active(e)&&role>=48u&&
       !thy_instrument_context_pad(e->instrument,role)) {
        unsigned pad=0;while(pad<96u&&e->instrument->mapping[pad]!=logical)++pad;
        if(pad<96u&&!e->instrument->sensors[pad].held&&!e->instrument->core->owners[pad].held) {
            int16_t note=0;
            if(thy_engine_harmony_melody_plan(e,role,&note)==1) {
                unsigned pitch=(unsigned)((note+e->instrument->transpose[1])%12+12)%12u;
                unsigned level=!e->harmony.highlights?1792u:(e->harmony.base_mask&(1u<<pitch))?3072u:
                    (e->harmony.key_mask&(1u<<pitch))?2048u:1024u;
                int black=pitch==1u||pitch==3u||pitch==6u||pitch==8u||pitch==10u;
                if(e->instrument->light_key_contrast&&black){
                    level=level*e->instrument->light_key_contrast_percent/100u;
                    /* The physical LEDs have eight amplitudes: a nonzero
                     * contrast setting must not erase an available note. */
                    if(level&&level<512u)level=512u;
                }
                uint32_t color=thy_lighting_note_color_palette((uint8_t)pitch,e->instrument->light_palette);
                for(unsigned c=0;c<3u;++c)components[c]=(uint16_t)((((color>>(16u-c*8u))&255u)*level+127u)/255u);
                return 1;
            }
        }
    }
    if(e->expression_config.enabled && e->expression && e->instrument->light_held_feedback) {
        unsigned pad=0;
        while(pad<96u && e->instrument->mapping[pad]!=logical)++pad;
        if(pad<96u && thy_mpe_consumed(e->expression,(uint8_t)pad)) {
            uint32_t color=UINT32_C(0xffffff);
            for(unsigned n=0;n<THY_MPE_MEMBERS;++n) {
                const thy_mpe_voice *v=thy_mpe_voice_at(e->expression,(uint8_t)n);
                if(v && v->state==THY_MPE_HELD && v->modifier==pad+1u) {
                    if(e->instrument->light_held_style!=THY_LIGHTING_HELD_WHITE)
                        color=thy_lighting_note_color_palette((uint8_t)((int)v->reference+v->interval),e->instrument->light_palette);
                    break;
                }
            }
            for(unsigned c=0;c<3u;++c) {
                unsigned value=((color>>(16u-c*8u))&255u)*e->instrument->light_held*512u/255u;
                components[c]=(uint16_t)(value>4095u?4095u:value);
            }
            return 1;
        }
    }
    return thy_engine_sequencer_color(e,logical,components);
}

static int musical_pressure(void *context,uint8_t pad,uint8_t pressure,uint8_t aftertouch)
{ return thy_engine_expression_pressure_axes(context,pad,pressure,aftertouch); }

static int musical_quiet(void *context)
{
    const thy_engine *e=context;
    if(!valid_engine(e)||!thy_engine_library_quiet(e)||e->instrument->library_editor.open||
       e->sensitivity_pick)return 0;
    for(unsigned p=0;p<THY_PAD_COUNT;++p)
        if(e->input_identity[p]||e->sequencer_ui.pads[p].held||
           e->arps[0].inputs[p].held||e->arps[1].inputs[p].held)return 0;
    return 1;
}

void thy_engine_bind_music(thy_engine *engine)
{
    if (!valid_engine(engine)) return;
    thy_instrument_set_musical_handler(engine->instrument, musical_attack,
        musical_release, musical_reset, engine);
    engine->instrument->musical_pressure=musical_pressure;
    engine->instrument->musical_quiet=musical_quiet;
    engine->instrument->musical_melody_plan=thy_engine_harmony_melody_plan;
    engine->instrument->musical_chord_plan=thy_engine_harmony_chord_plan;
    engine->instrument->musical_voicing_plan=thy_engine_harmony_voicing_plan;
    thy_instrument_set_pad_filter(engine->instrument, thy_engine_sequencer_pad, engine);
    engine->instrument->light_overlay=light_overlay;
    engine->instrument->light_overlay_context=engine;
}

int thy_engine_arp_configure(thy_engine *engine, uint8_t index,
    const thy_arp_config *config)
{
    if (!valid_engine(engine) || index >= 2u || !config || !thy_arp_config_valid(config))
        return THY_INVALID_ARGUMENT;
    if (config->enabled && engine->expression_config.enabled) return THY_SEQ_BUSY;
    thy_arp_config edited = *config;
    arp_scale(engine, &edited);
    int result = thy_arp_configure(&engine->arps[index], engine->instrument->core,
        &edited, engine->musical_clock.tick, engine->live_generation[index],
        thy_clock_ready(&engine->musical_clock));
    return result == THY_ARP_TIME_BACKWARD ? THY_ENGINE_TIME_BACKWARD :
        result == THY_ARP_STALE_RUN ? THY_ENGINE_STALE_RUN : result;
}

int thy_engine_arp_clear(thy_engine *engine, uint8_t index)
{
    if (!valid_engine(engine) || index >= 2u) return THY_INVALID_ARGUMENT;
    thy_arp_clear(&engine->arps[index], engine->instrument->core);
    return THY_OK;
}
