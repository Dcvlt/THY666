/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_settings.h"
#include "thy_crc32.h"

/* Keep large codec workspaces in separate call frames under target LTO.
 * Inlining encode/apply into the permanent foreground main frame overlaps
 * them with validation and sequencer take buffers. */
#if defined(__GNUC__) || defined(__clang__)
#define THY_SETTINGS_NOINLINE __attribute__((noinline))
#else
#define THY_SETTINGS_NOINLINE
#endif

/* Stable legacy field order. V2 inserts a discriminator after catalog bytes
 * 0..2 and appends lower-control preferences. V3 changes the discriminator
 * version and appends explicit Arp/clock preferences. V4 appends engines,
 * physical scale shortcuts and per-view presentation. V5 appends the Fifths
 * view and unifies the old playing workspace identities. V6 appends sequencer
 * preferences and its independent song. V7 appends velocity parameters before
 * that song. V8 separates detector/aftertouch ranges and appends linked-release
 * policies. V9 appends cluster defaults and persistent acquisition guards; inactive pad entries keep
 * their legacy-global canonical encoding. V10 appends explicit MPE configuration.
 * V11 appends nine ordinary channel-expression policies plus two cluster
 * dual-stage expression profiles and96literal per-pad profiles/override flags.
 * V12 appends ten chord policies and a streamed 740-byte named musical library.
 * V13 extends the view-info enum with Musical names without adding bytes.
 * V14 extends the voicing policy with automatic leading, without adding bytes.
 * V15 adds octave fold and guitar register without adding bytes.
 * V16-V18 append Harmony preferences; V19 appends physical-half playing
 * policies and MPE pressure destinations after those unchanged preferences.
 * Every prior field/song offset is frozen for its original version. */
_Static_assert(THY_SETTINGS_RECORD_BYTES<=THY_SETTINGS_WRITE_SLOT_SPAN*THY_SETTINGS_SLOT_BYTES,
    "settings record must fit its reserved journal region");
#define V4_VIEW_COUNT 8u
#define CHORD_BYTES(X) \
    X(keep_transpose,1) X(live_update,1) X(live_lock_scale,1) X(live_mute_chord,1) \
    X(chord_layout,THY_CHORD_LAYOUT_USER) X(chord_quality,THY_CHORD_QUALITY_COUNT-1u) \
    X(six_bank,THY_LIBRARY_SIX_SETS-1u) X(voicing_mode,4) X(drop_mask,7) X(user_scale,THY_LIBRARY_SCALES)
#define HARMONY_BYTES(X) \
    X(pattern,3) X(highlights,1) X(motion,1) X(view,3) X(control_bank,1) \
    X(octave_target,1) X(right_mode,1) X(analysis,2) X(bass_root,1) X(low_note,127) X(high_note,127)
#define HARMONY_V17_BYTES(X) X(direction,3) X(pad_map,1) X(hint_contrast,100)
#define HARMONY_V18_BYTES(X) X(outside,100) X(stay_in_key,1) X(lock_map,1) X(palette,4)
#define CONFIG_BYTES(X) \
    X(fixed_velocity,127) X(pressure_enabled,1) X(lights_enabled,1) \
    X(light_idle,8) X(light_held,8) X(light_chord_mode,2) \
    X(light_key_contrast,1) X(light_palette,1) X(light_held_style,1) \
    X(light_held_feedback,1) X(display_view,THY_VIEW_COUNT-1u) X(center_poll_ms,80) \
    X(context_controls,1) X(animations_enabled,1) X(routing_profile,2) \
    X(global_excluded_slots,127) X(slot_scope,1) X(context_location,1) \
    X(light_key_contrast_percent,100) X(display_counts,1) X(display_motion,1) \
    X(center_brightness,8) X(context_mask,63) X(display_pretty_counts,1)
typedef struct config {
    thy_instrument_performance performance, users[3];
    thy_instrument_response response, pad_response[THY_PAD_COUNT];
    thy_instrument_calibration calibration[THY_PAD_COUNT];
    uint8_t profile[THY_PAD_COUNT], overridden[THY_PAD_COUNT];
    uint8_t excluded[48], route_channels[9], route_destinations[9];
    uint8_t route_velocity[9], route_pressure[9], play_nav[4], users_valid;
    uint8_t lower_mode, octave_layers, pad_octave_layers[48];
    uint16_t scale, light_fade_ms;
    int16_t transpose[2];
    int8_t bass_octave;
    thy_arp_config arps[2];
    uint32_t tempo;
    uint8_t clock_source, numerator, denominator, quantize, arp_target, arp_quick_keys;
    uint8_t engine_kind, scale_shortcuts[THY_SCALE_SHORTCUT_COUNT];
#define FIELD(name,max) uint8_t harmony_##name;
    HARMONY_BYTES(FIELD) HARMONY_V17_BYTES(FIELD) HARMONY_V18_BYTES(FIELD)
#undef FIELD
    uint8_t cluster_swap;
    thy_view_config display_config[THY_VIEW_COUNT];
    uint32_t seq_grid, seq_window, seq_region_start, seq_region_end;
    uint8_t seq_layout, seq_swapped, seq_input, seq_pitch, seq_track;
    uint8_t seq_region, remote_record, remote_position, remote_channel;
    thy_instrument_velocity velocity, pad_velocity[THY_PAD_COUNT];
    uint8_t velocity_overridden[THY_PAD_COUNT];
    thy_instrument_release_policy release_policy,pad_release_policy[THY_PAD_COUNT];
    thy_instrument_response cluster_response[2];
    thy_instrument_velocity cluster_velocity[2];
    thy_instrument_release_policy cluster_release_policy[2];
    uint16_t sensitivity_guard[THY_PAD_COUNT];
    thy_mpe_config expression;
    thy_engine_performance_config playing[2];
    thy_engine_channel_config channel_routes[THY_INSTRUMENT_ROUTE_TARGETS];
    thy_expression_profile expression_profiles[2],pad_expression_profiles[THY_PAD_COUNT];
    uint8_t expression_profile_overridden[THY_PAD_COUNT];
#define FIELD(name,max) uint8_t name;
    CHORD_BYTES(FIELD)
#undef FIELD
#define FIELD(name,max) uint8_t name;
    CONFIG_BYTES(FIELD)
#undef FIELD
} config;
typedef struct stream { uint8_t *out; const uint8_t *in; size_t size, at; int valid; } stream;
static void byte(stream *s, uint8_t *v) {
    if (s->at >= s->size) { s->valid = 0; return; }
    if (s->out) s->out[s->at] = *v; else *v = s->in[s->at];
    ++s->at;
}
static void word(stream *s, uint16_t *v) {
    uint8_t lo = (uint8_t)*v, hi = (uint8_t)(*v >> 8);
    byte(s,&lo); byte(s,&hi); *v = (uint16_t)(lo | (uint16_t)hi << 8);
}
static void dword(stream *s, uint32_t *v) {
    uint16_t lo=(uint16_t)*v, hi=(uint16_t)(*v>>16);
    word(s,&lo); word(s,&hi); *v=(uint32_t)lo | (uint32_t)hi<<16;
}
/* Derived scale/root and all input/owner/timeline state are deliberately absent. */
static void arp_parameters(stream *s, thy_arp_config *a) {
    dword(s,&a->rate_ticks);
    for(unsigned n=0;n<8u;++n) {uint8_t v=(uint8_t)a->offsets[n];byte(s,&v);a->offsets[n]=(int8_t)v;}
    byte(s,&a->enabled); byte(s,&a->type); byte(s,&a->grouping); byte(s,&a->source_zones);
    byte(s,&a->layer); byte(s,&a->start); byte(s,&a->restart_on_change);
    byte(s,&a->gate_percent); byte(s,&a->swing_percent); byte(s,&a->step_mask);
    byte(s,&a->pattern); byte(s,&a->channel); byte(s,&a->destinations);
}
static void config_defaults(config *c) {
    *c=(config){0}; c->octave_layers=8u;
    c->keep_transpose=c->live_lock_scale=1u;
    for(unsigned n=0;n<48u;++n) c->pad_octave_layers[n]=8u;
    for(unsigned n=0;n<2u;++n) thy_arp_default_config(&c->arps[n]);
    c->tempo=THY_CLOCK_DEFAULT_TEMPO; c->numerator=4u; c->denominator=4u;
    c->quantize=THY_TRANSPORT_BAR; c->arp_quick_keys=1u;
    c->engine_kind=THY_PLAY_ENGINE_PLAY;
    c->harmony_pattern=c->harmony_highlights=c->harmony_motion=1u;
    c->harmony_octave_target=1u;c->harmony_high_note=127u;c->harmony_view=1u;c->harmony_hint_contrast=65u;c->harmony_outside=35u;
    for(unsigned n=0;n<THY_SCALE_SHORTCUT_COUNT;++n)
        c->scale_shortcuts[n]=thy_instrument_default_scale_shortcut((uint8_t)n);
    thy_views_defaults(c->display_config);
    c->seq_grid=240u; c->seq_pitch=60u; c->seq_region_end=3840u;
    thy_instrument_velocity_defaults(&c->velocity);
    for(unsigned n=0;n<THY_PAD_COUNT;++n) c->pad_velocity[n]=c->velocity;
    thy_instrument_release_defaults(&c->release_policy);
    for(unsigned n=0;n<THY_PAD_COUNT;++n)c->pad_release_policy[n]=c->release_policy;
    thy_mpe_defaults(&c->expression);
    for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n)c->channel_routes[n].range=2u;
    for(unsigned n=0;n<2u;++n)c->expression_profiles[n]=(thy_expression_profile){0u,70u,0u,0u};
    for(unsigned n=0;n<THY_PAD_COUNT;++n)c->pad_expression_profiles[n]=(thy_expression_profile){0u,70u,0u,0u};
}
static void expression_profile_parameters(stream *s,thy_expression_profile *p) {
    byte(s,&p->enabled);byte(s,&p->split);byte(s,&p->pressure_curve);byte(s,&p->aftertouch_curve);
}
static void playing_parameters(stream *s,config *c) {
    for(unsigned n=0;n<2u;++n){
        thy_engine_performance_config *p=&c->playing[n];
        byte(s,&p->velocity);byte(s,&p->aftertouch);byte(s,&p->bend);byte(s,&p->range);
    }
    for(unsigned n=0;n<2u;++n)byte(s,&c->expression.pressure_output[n]);
}
static int playing_valid(const config *c) {
    for(unsigned n=0;n<2u;++n){
        const thy_engine_performance_config *p=&c->playing[n];
        if(p->velocity>127u||p->aftertouch>3u||p->bend>3u||p->range>24u)return 0;
    }
    return thy_mpe_config_valid(&c->expression);
}
static void performance_tail(stream *s, thy_instrument_performance *p) {
    byte(s,&p->shortcut); byte(s,&p->melody_layout);
    byte(s,&p->guitar_window); byte(s,&p->inversion); byte(s,&p->spread);
    byte(s,&p->strum); byte(s,&p->strum_order); byte(s,&p->upper_octaves);
    uint8_t chord = (uint8_t)p->chord_octave, melody = (uint8_t)p->melody_octave;
    byte(s,&chord); byte(s,&melody); p->chord_octave=(int8_t)chord; p->melody_octave=(int8_t)melody;
    word(s,&p->strum_ms);
}
static void performance(stream *s, thy_instrument_performance *p) {
    byte(s,&p->root); performance_tail(s,p);
}
static void response(stream *s, thy_instrument_response *r) {
    word(s,&r->attack_raw); word(s,&r->release_raw); word(s,&r->pressure_floor);
    word(s,&r->pressure_full_scale); byte(s,&r->pressure_curve);
}
static void velocity_parameters(stream *s,thy_instrument_velocity *v) {
    word(s,&v->soft_rate); word(s,&v->hard_rate);
    byte(s,&v->mode); byte(s,&v->curve); byte(s,&v->minimum); byte(s,&v->maximum);
}
static void release_parameters(stream *s,thy_instrument_release_policy *policy) {
    byte(s,&policy->linked);word(s,&policy->gap);
}
static void transfer(stream *s, config *c, unsigned version) {
    word(s,&c->scale); byte(s,&c->performance.root);
    if(version>=2u) {
        const uint8_t marker[4]={0xf2u,0x54u,0x48u,(uint8_t)version};
        for(unsigned n=0;n<4u;++n) {
            uint8_t value=marker[n]; byte(s,&value);
            if(value!=marker[n]) s->valid=0;
        }
    }
    performance_tail(s,&c->performance);
    for (unsigned n=0;n<3u;++n) performance(s,&c->users[n]);
    byte(s,&c->users_valid); response(s,&c->response);
    for (unsigned n=0;n<2u;++n) {uint16_t v=(uint16_t)c->transpose[n]; word(s,&v); c->transpose[n]=(int16_t)v;}
    word(s,&c->light_fade_ms);
    uint8_t bass=(uint8_t)c->bass_octave; byte(s,&bass); c->bass_octave=(int8_t)bass;
#define FIELD(name,max) byte(s,&c->name);
    CONFIG_BYTES(FIELD)
#undef FIELD
    for (unsigned n=0;n<4u;++n) byte(s,&c->play_nav[n]);
    for (unsigned n=0;n<48u;++n) byte(s,&c->excluded[n]);
    for (unsigned n=0;n<9u;++n) {
        byte(s,&c->route_channels[n]); byte(s,&c->route_destinations[n]);
        byte(s,&c->route_velocity[n]); byte(s,&c->route_pressure[n]);
    }
    for (unsigned n=0;n<THY_PAD_COUNT;++n) {
        thy_instrument_calibration *a=&c->calibration[n];
        byte(s,&c->profile[n]); byte(s,&c->overridden[n]); response(s,&c->pad_response[n]);
        word(s,&a->baseline); word(s,&a->press_delta); word(s,&a->release_delta); word(s,&a->full_scale);
        uint8_t polarity=(uint8_t)a->polarity; byte(s,&polarity); a->polarity=(int8_t)polarity;
    }
    if(version>=2u) {
        byte(s,&c->lower_mode); byte(s,&c->octave_layers);
        for(unsigned n=0;n<48u;++n) byte(s,&c->pad_octave_layers[n]);
    }
    if(version>=3u) {
        for(unsigned n=0;n<2u;++n) arp_parameters(s,&c->arps[n]);
        dword(s,&c->tempo); byte(s,&c->clock_source); byte(s,&c->numerator);
        byte(s,&c->denominator); byte(s,&c->quantize);
        byte(s,&c->arp_target); byte(s,&c->arp_quick_keys);
    }
    if(version>=4u) {
        byte(s,&c->engine_kind);
        for(unsigned n=0;n<THY_SCALE_SHORTCUT_COUNT;++n) byte(s,&c->scale_shortcuts[n]);
        const unsigned views=version>=5u?THY_VIEW_COUNT:V4_VIEW_COUNT;
        for(unsigned n=0;n<views;++n) {
            thy_view_config *v=&c->display_config[n];
            byte(s,&v->slots[0]); byte(s,&v->slots[1]); byte(s,&v->header); byte(s,&v->motion);
            for(unsigned row=0;row<THY_VIEW_DETAIL_ROWS;++row) byte(s,&v->detail_rows[row]);
        }
    }
    if(version>=6u) {
        byte(s,&c->seq_layout); byte(s,&c->seq_swapped); byte(s,&c->seq_input);
        byte(s,&c->seq_pitch); byte(s,&c->seq_track);
        dword(s,&c->seq_grid); dword(s,&c->seq_window);
        dword(s,&c->seq_region_start); dword(s,&c->seq_region_end);
        byte(s,&c->seq_region); byte(s,&c->remote_record);
        byte(s,&c->remote_position); byte(s,&c->remote_channel);
    }
    if(version>=7u) {
        velocity_parameters(s,&c->velocity);
        for(unsigned n=0;n<THY_PAD_COUNT;++n) {
            byte(s,&c->velocity_overridden[n]); velocity_parameters(s,&c->pad_velocity[n]);
        }
    }
    if(version>=8u) {
        release_parameters(s,&c->release_policy);
        for(unsigned n=0;n<THY_PAD_COUNT;++n)release_parameters(s,&c->pad_release_policy[n]);
    }
    if(version>=9u)for(unsigned n=0;n<2u;++n){
        response(s,&c->cluster_response[n]);velocity_parameters(s,&c->cluster_velocity[n]);
        release_parameters(s,&c->cluster_release_policy[n]);
    }
    if(version>=9u)for(unsigned n=0;n<THY_PAD_COUNT;++n)word(s,&c->sensitivity_guard[n]);
    if(version>=10u) {
        byte(s,&c->expression.enabled);byte(s,&c->expression.upper_zone);
        byte(s,&c->expression.members);byte(s,&c->expression.bend_range);
        byte(s,&c->expression.manager_bend_range);byte(s,&c->expression.glide);
        byte(s,&c->expression.pad_window);byte(s,&c->expression.semitone_window);
    }
    if(version>=11u)for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n){
        byte(s,&c->channel_routes[n].aftertouch);byte(s,&c->channel_routes[n].bend);
        byte(s,&c->channel_routes[n].range);
    }
    if(version>=11u) {
        for(unsigned n=0;n<2u;++n)expression_profile_parameters(s,&c->expression_profiles[n]);
        for(unsigned n=0;n<THY_PAD_COUNT;++n){
            byte(s,&c->expression_profile_overridden[n]);
            expression_profile_parameters(s,&c->pad_expression_profiles[n]);
        }
    }
    if(version>=12u) {
#define FIELD(name,max) byte(s,&c->name);
        CHORD_BYTES(FIELD)
#undef FIELD
    }
}
static int response_valid(const thy_instrument_response *r,unsigned version) {
    if(version<8u)return r->pressure_floor<=r->release_raw && r->release_raw<r->attack_raw &&
        r->attack_raw<=r->pressure_full_scale && r->pressure_full_scale<=4095u && r->pressure_curve<=2u;
    return r->release_raw<r->attack_raw && r->attack_raw<=4095u &&
        r->pressure_floor<r->pressure_full_scale && r->pressure_full_scale<=4095u && r->pressure_curve<=2u;
}
static int release_valid(const thy_instrument_release_policy *policy,const thy_instrument_response *r) {
    return thy_instrument_release_policy_valid(policy) && (!policy->linked ||
        r->release_raw==(r->attack_raw>policy->gap?r->attack_raw-policy->gap:0u));
}
static int performance_valid(const thy_instrument_performance *p,unsigned version) {
    return p->root<12u && p->shortcut<(version>=4u?THY_SCALE_SHORTCUT_COUNT:9u) && p->melody_layout<THY_LAYOUT_COUNT && p->guitar_window<3u &&
        p->inversion<=7u && p->spread<=3u && p->strum<=1u && p->strum_order<=3u &&
        p->upper_octaves<=3u && p->chord_octave>=-3 && p->chord_octave<=3 &&
        p->melody_octave>=-3 && p->melody_octave<=3 && p->strum_ms<=1000u;
}
static int valid_config(const config *c, uint16_t scales, unsigned version) {
    if (!scales || c->scale>=scales || !performance_valid(&c->performance,version) ||
        c->users_valid>7u || !response_valid(&c->response,version) || !c->fixed_velocity ||
        c->center_poll_ms<8u || (c->center_poll_ms&1u) || c->light_fade_ms>500u ||
        c->bass_octave < -3 || c->bass_octave > 3 || c->lower_mode>1u ||
        !c->octave_layers || c->octave_layers>127u || c->arp_target>1u ||
        c->arp_quick_keys>1u || c->tempo<THY_CLOCK_TEMPO_MIN || c->tempo>THY_CLOCK_TEMPO_MAX ||
        c->clock_source>THY_CLOCK_SOURCE_USB || !c->numerator || c->numerator>16u ||
        !c->denominator || c->denominator>32u || (c->denominator&(c->denominator-1u)) ||
        c->quantize>THY_TRANSPORT_BAR) return 0;
    for(unsigned n=0;n<2u;++n) if(!thy_arp_config_valid(&c->arps[n])) return 0;
#define FIELD(name,max) if(c->name>max) return 0;
    CONFIG_BYTES(FIELD)
    CHORD_BYTES(FIELD)
#undef FIELD
    if(version<14u && c->voicing_mode>1u)return 0;
    if(version<15u && c->voicing_mode>2u)return 0;
    if(version>=4u) {
        /* The old v4 workspace byte accepted Scales=0 and Chromatic=1. Both
         * migrate to Play only after their original range is validated. */
        if((version==4u?c->engine_kind>1u:version==5u?c->engine_kind!=THY_PLAY_ENGINE_PLAY:
            version<16u?(c->engine_kind!=THY_PLAY_ENGINE_PLAY&&c->engine_kind!=THY_PLAY_ENGINE_SEQUENCER):!thy_play_engine_available(c->engine_kind)) ||
            !thy_view_available(c->display_view) || (version==4u && c->display_view>=V4_VIEW_COUNT)) return 0;
        for(unsigned n=0;n<THY_SCALE_SHORTCUT_COUNT;++n)
            if(c->scale_shortcuts[n]>THY_SCALE_SHORTCUT_LAYOUT+(version>=12u?THY_LIBRARY_SCALES:0u)) return 0;
        const unsigned views=version>=5u?THY_VIEW_COUNT:V4_VIEW_COUNT;
        for(unsigned n=0;n<views;++n) {
            const thy_view_config *v=&c->display_config[n];
            if(!thy_view_config_valid(v))return 0;
            /* Legacy payloads may not borrow later enum meanings simply
             * because the byte layout still has the same length. */
            if(version<=12u) {
                if(v->slots[0]>=THY_VIEW_INFO_MUSICAL_NAMES || v->slots[1]>=THY_VIEW_INFO_MUSICAL_NAMES)return 0;
                for(unsigned row=0;row<THY_VIEW_DETAIL_ROWS;++row)
                    if(v->detail_rows[row]>=THY_VIEW_INFO_MUSICAL_NAMES)return 0;
            }
        }
    } else if(c->display_view>5u) return 0;
    for(unsigned n=0;n<3u;++n) if(!performance_valid(&c->users[n],version)) return 0;
    for(unsigned n=0;n<2u;++n) if(c->transpose[n]<-127 || c->transpose[n]>127) return 0;
    for(unsigned n=0;n<4u;++n) if(c->play_nav[n]>(version>=4u?THY_PLAY_ENGINES:version>=3u?THY_PLAY_ARPS:6u)) return 0;
    for(unsigned n=0;n<48u;++n) if(c->excluded[n]>127u || !c->pad_octave_layers[n] ||
        c->pad_octave_layers[n]>127u) return 0;
    for(unsigned n=0;n<9u;++n) if(c->route_channels[n]>15u || !c->route_destinations[n] ||
        c->route_destinations[n]>THY_ROUTE_ALL || c->route_velocity[n]>127u || c->route_pressure[n]>2u) return 0;
    for(unsigned n=0;n<THY_PAD_COUNT;++n) {
        const thy_instrument_calibration *a=&c->calibration[n];
        if(c->profile[n]>2u || c->overridden[n]>1u || !response_valid(&c->pad_response[n],version) ||
            a->baseline>4095u || !a->release_delta || a->release_delta>=a->press_delta ||
            a->press_delta>a->full_scale || a->full_scale>4095u || (a->polarity!=1 && a->polarity!=-1)) return 0;
        if(!c->overridden[n]) {
            const thy_instrument_response *r=&c->pad_response[n], *global=&c->response;
            if(r->attack_raw!=global->attack_raw || r->release_raw!=global->release_raw ||
                r->pressure_floor!=global->pressure_floor || r->pressure_full_scale!=global->pressure_full_scale ||
                r->pressure_curve!=global->pressure_curve) return 0;
        }
    }
    if(version>=6u && (c->seq_layout>2u || c->seq_swapped>1u || c->seq_input>1u ||
        c->seq_pitch>120u || c->seq_track>=THY_SEQ_TRACKS ||
        (c->seq_grid!=120u && c->seq_grid!=240u && c->seq_grid!=480u && c->seq_grid!=960u) ||
        c->seq_window>THY_SEQ_MAX_TICK-24u*960u || c->seq_region>1u ||
        c->seq_region_start>THY_SEQ_MAX_TICK || c->seq_region_end>THY_SEQ_MAX_TICK ||
        (c->seq_region && c->seq_region_end<=c->seq_region_start) ||
        c->remote_record>1u || c->remote_position>1u || c->remote_channel>16u)) return 0;
    if(!thy_instrument_velocity_valid(&c->velocity))return 0;
    for(unsigned n=0;n<THY_PAD_COUNT;++n) {
        const thy_instrument_velocity *v=&c->pad_velocity[n],*global=&c->velocity;
        if(c->velocity_overridden[n]>1u || !thy_instrument_velocity_valid(v))return 0;
        if(!c->velocity_overridden[n] && (v->soft_rate!=global->soft_rate ||
            v->hard_rate!=global->hard_rate || v->mode!=global->mode ||
            v->curve!=global->curve || v->minimum!=global->minimum || v->maximum!=global->maximum))return 0;
    }
    if(version>=8u) {
        if(!release_valid(&c->release_policy,&c->response))return 0;
        for(unsigned n=0;n<THY_PAD_COUNT;++n) {
            const thy_instrument_release_policy *p=&c->pad_release_policy[n];
            if(!release_valid(p,&c->pad_response[n]))return 0;
            if(!c->overridden[n] && (p->linked!=c->release_policy.linked || p->gap!=c->release_policy.gap))return 0;
        }
    }
    if(version>=9u)for(unsigned n=0;n<2u;++n)
        if(!response_valid(&c->cluster_response[n],version) ||
            !thy_instrument_velocity_valid(&c->cluster_velocity[n]) ||
            !release_valid(&c->cluster_release_policy[n],&c->cluster_response[n]))return 0;
    if(version>=9u)for(unsigned n=0;n<THY_PAD_COUNT;++n)
        if(c->sensitivity_guard[n]>4094u)return 0;
    for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n)
        if(c->channel_routes[n].aftertouch>3u || c->channel_routes[n].bend>2u ||
            !c->channel_routes[n].range || c->channel_routes[n].range>24u)return 0;
    for(unsigned n=0;n<2u;++n)if(!thy_expression_profile_valid(&c->expression_profiles[n]))return 0;
    for(unsigned n=0;n<THY_PAD_COUNT;++n)
        if(c->expression_profile_overridden[n]>1u || !thy_expression_profile_valid(&c->pad_expression_profiles[n]))return 0;
    if(!playing_valid(c) || (c->expression.enabled &&
        (c->arps[0].enabled || c->arps[1].enabled || c->engine_kind==THY_PLAY_ENGINE_SEQUENCER)))return 0;
    return 1;
}
static size_t parameter_bytes(unsigned version) {
    return version>=19u?THY_SETTINGS_V19_PARAMETER_BYTES:version>=18u?THY_SETTINGS_V18_PARAMETER_BYTES:version>=17u?THY_SETTINGS_V17_PARAMETER_BYTES:version>=16u?THY_SETTINGS_V16_PARAMETER_BYTES:version>=12u?THY_SETTINGS_V12_PARAMETER_BYTES:version>=11u?THY_SETTINGS_V11_PARAMETER_BYTES:version>=10u?THY_SETTINGS_V10_PARAMETER_BYTES:version>=9u?THY_SETTINGS_V9_PARAMETER_BYTES:version>=8u?THY_SETTINGS_V8_PARAMETER_BYTES:version>=7u?THY_SETTINGS_V7_PARAMETER_BYTES:THY_SETTINGS_V6_PARAMETER_BYTES;
}
int thy_settings_payload_version(const uint8_t *in,size_t count) {
    if(!in) return 0u;
    if(count==THY_SETTINGS_V1_PAYLOAD_BYTES) return 1u;
    if(count==THY_SETTINGS_V2_PAYLOAD_BYTES && in[3]==0xf2u && in[4]==0x54u &&
        in[5]==0x48u && in[6]==0x02u) return 2u;
    if(count==THY_SETTINGS_V3_PAYLOAD_BYTES && in[3]==0xf2u && in[4]==0x54u &&
        in[5]==0x48u && in[6]==0x03u) return 3u;
    if(count==THY_SETTINGS_V4_PAYLOAD_BYTES && in[3]==0xf2u && in[4]==0x54u &&
        in[5]==0x48u && in[6]==0x04u) return 4u;
    if(count==THY_SETTINGS_V5_PAYLOAD_BYTES && in[3]==0xf2u && in[4]==0x54u &&
        in[5]==0x48u && in[6]==0x05u) return 5u;
    if(count>=THY_SETTINGS_V6_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==6u) return 6u;
    if(count>=THY_SETTINGS_V7_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==7u) return 7u;
    if(count>=THY_SETTINGS_V8_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==8u) return 8u;
    if(count>=THY_SETTINGS_V9_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==9u) return 9u;
    if(count>=THY_SETTINGS_V10_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==10u) return 10u;
    if(count>=THY_SETTINGS_V11_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==11u) return 11u;
    if(count>=THY_SETTINGS_V12_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==12u) return 12u;
    if(count>=THY_SETTINGS_V13_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==13u) return 13u;
    if(count>=THY_SETTINGS_V14_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==14u) return 14u;
    if(count>=THY_SETTINGS_V15_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==15u) return 15u;
    if(count>=THY_SETTINGS_V16_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==16u) return 16u;
    if(count>=THY_SETTINGS_V18_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==18u) return 18u;
    if(count>=THY_SETTINGS_V17_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==17u) return 17u;
    if(count>=THY_SETTINGS_V19_PARAMETER_BYTES+32u && count<=THY_SETTINGS_PAYLOAD_MAX &&
        in[3]==0xf2u && in[4]==0x54u && in[5]==0x48u && in[6]==19u) return 19u;
    return 0u;
}
/* Read the validated byte projection directly. No 740-byte automatic library
 * enters the large codec snapshot or restore frames. */
static int library_references_valid(const config *c,const uint8_t *library) {
    if(c->user_scale && !library[(c->user_scale-1u)*sizeof(thy_library_scale)+offsetof(thy_library_scale,used)]) return 0;
    for(unsigned n=0;n<THY_SCALE_SHORTCUT_COUNT;++n) {
        unsigned code=c->scale_shortcuts[n];
        if(code>THY_SCALE_SHORTCUT_LAYOUT &&
            !library[(code-THY_SCALE_SHORTCUT_LAYOUT-1u)*sizeof(thy_library_scale)+offsetof(thy_library_scale,used)]) return 0;
    }
    if(c->chord_layout==THY_CHORD_LAYOUT_SIX_USER &&
        !library[offsetof(thy_library,six)+c->six_bank*sizeof(thy_library_six)+offsetof(thy_library_six,used)]) return 0;
    return 1;
}
static int decode(config *c,const uint8_t *in,size_t count,uint16_t scales) {
    unsigned version=(unsigned)thy_settings_payload_version(in,count); if(!version) return 0;
    config_defaults(c);
    size_t parameters=version>=6u?parameter_bytes(version):count;
    stream s={NULL,in,parameters,0,1}; transfer(&s,c,version);
    if(!s.valid || !valid_config(c,scales,version)) return 0;
    if(version>=12u) {
        if(s.at!=THY_SETTINGS_V12_LIBRARY_OFFSET ||
            !thy_library_payload_validate(in+s.at,THY_LIBRARY_PAYLOAD_BYTES) ||
            !library_references_valid(c,in+s.at)) return 0;
        s.at+=THY_LIBRARY_PAYLOAD_BYTES;
    }
    if(version>=16u){
#define FIELD(name,max) byte(&s,&c->harmony_##name);if(c->harmony_##name>max)return 0;
        HARMONY_BYTES(FIELD)
#undef FIELD
        byte(&s,&c->cluster_swap);if(c->cluster_swap>1u||c->harmony_low_note>c->harmony_high_note)return 0;
    }
    if(version>=17u){
#define FIELD(name,max) byte(&s,&c->harmony_##name);if(c->harmony_##name>max)return 0;
        HARMONY_V17_BYTES(FIELD)
#undef FIELD
    }
    if(version==17u&&c->harmony_direction>2u)return 0;
    if(version>=18u){
#define FIELD(name,max) byte(&s,&c->harmony_##name);if(c->harmony_##name>max)return 0;
        HARMONY_V18_BYTES(FIELD)
#undef FIELD
    }
    if(version>=19u)playing_parameters(&s,c);
    return s.valid && playing_valid(c) && s.at==parameters &&
        (version<6u || thy_sequencer_validate(in+parameters,count-parameters)==THY_OK);
}
/* Legacy presentation flags remain part of the frozen byte stream. Convert
 * them only after validating their original ranges, before publishing values. */
static void migrate_legacy(config *c,unsigned version) {
    if(version<18u){
        c->harmony_direction=c->harmony_direction==2u?3u:0u;
        c->harmony_view=1u;c->harmony_pad_map=0u;
    }
    if(version<8u) {
        c->release_policy=(thy_instrument_release_policy){(uint16_t)(c->response.attack_raw-c->response.release_raw),1u};
        for(unsigned n=0;n<THY_PAD_COUNT;++n)c->pad_release_policy[n]=(thy_instrument_release_policy){
            (uint16_t)(c->pad_response[n].attack_raw-c->pad_response[n].release_raw),1u};
    }
    if(version<9u)for(unsigned n=0;n<2u;++n){
        c->cluster_response[n]=c->response;c->cluster_velocity[n]=c->velocity;
        c->cluster_release_policy[n]=c->release_policy;
    }
    if(version<6u && c->play_nav[0]==THY_PLAY_HIDDEN) c->play_nav[0]=THY_PLAY_ENGINES;
    if(version>=5u) return;
    /* Layout stays independent. Converting the workspace identity must never
     * turn a saved scale-degree/Guitar layout into chromatic semitones. */
    c->engine_kind=THY_PLAY_ENGINE_PLAY;
    if(version>=4u) return;
    if(!thy_view_available(c->display_view)) c->display_view=THY_VIEW_DETAILED;
    c->display_config[THY_VIEW_SIMPLE].slots[1]=c->display_counts?THY_VIEW_INFO_COUNTS:THY_VIEW_INFO_OFF;
    c->display_config[THY_VIEW_CHLADNI].slots[1]=c->display_pretty_counts?THY_VIEW_INFO_COUNTS:THY_VIEW_INFO_OFF;
    c->display_config[THY_VIEW_CHLADNI].motion=c->display_motion;
}
THY_SETTINGS_NOINLINE int thy_settings_encode(const thy_engine *e,uint8_t *out,size_t capacity,size_t *used) {
    if(!e || !e->instrument || !out || !used) return 0;
    const thy_instrument *i=e->instrument; config c; config_defaults(&c);
    c.scale=e->scale; c.performance=i->performance; c.users_valid=i->users_valid;
#define FIELD(name,max) c.name=i->name;
    CHORD_BYTES(FIELD)
#undef FIELD
    c.response=i->response; c.light_fade_ms=i->light_fade_ms; c.bass_octave=i->bass_octave;
    c.velocity=i->velocity_response;
    c.release_policy=i->release_policy;
    c.expression=e->expression_config;
    for(unsigned n=0;n<2u;++n)c.playing[n]=e->performance[n];
    for(unsigned n=0;n<2u;++n)c.expression_profiles[n]=i->expression_profiles[n];
    for(unsigned n=0;n<THY_PAD_COUNT;++n){
        c.expression_profile_overridden[n]=i->expression_profile_overridden[n];
        c.pad_expression_profiles[n]=i->pad_expression_profiles[n];
    }
    for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n)c.channel_routes[n]=e->channel_routes[n];
    for(unsigned n=0;n<2u;++n){
        c.cluster_response[n]=i->cluster_response[n];c.cluster_velocity[n]=i->cluster_velocity[n];
        c.cluster_release_policy[n]=i->cluster_release_policy[n];
    }
    c.lower_mode=i->lower_mode; c.octave_layers=i->octave_layers;
    for(unsigned n=0;n<2u;++n) {
        /* Only this explicit parameter stream participates in persistence. */
        uint8_t encoded[25]; thy_arp_config current=e->arps[n].config;
        stream write={encoded,NULL,sizeof encoded,0,1}; arp_parameters(&write,&current);
        stream read={NULL,encoded,sizeof encoded,0,1}; arp_parameters(&read,&c.arps[n]);
    }
    c.tempo=e->musical_clock.tempo_milli_bpm; c.clock_source=e->musical_clock.source;
    c.numerator=e->musical_clock.numerator; c.denominator=e->musical_clock.denominator;
    c.quantize=e->rhythm_transport.launch_quantize;
    c.arp_target=e->arp_target; c.arp_quick_keys=e->arp_quick_keys;
    c.engine_kind=i->engine_kind;
#define FIELD(name,max) c.harmony_##name=e->harmony.name;
    HARMONY_BYTES(FIELD) HARMONY_V17_BYTES(FIELD) HARMONY_V18_BYTES(FIELD)
#undef FIELD
    c.cluster_swap=i->cluster_swap;
    /* Preserve the frozen v6 prefix, but retire competing old canvas modes.
     * Per-track editor grids now live in independently versioned THYS v2. */
    c.seq_layout=0u; c.seq_swapped=0u;
    c.seq_input=e->sequencer_ui.input_layout; c.seq_pitch=e->sequencer_ui.base_pitch;
    c.seq_track=e->sequencer.selected_track; c.seq_grid=240u;
    c.seq_window=0u; c.seq_region=0u;
    c.seq_region_start=0u; c.seq_region_end=3840u;
    c.remote_record=e->remote_record; c.remote_position=e->remote_position; c.remote_channel=e->remote_channel;
    for(unsigned n=0;n<THY_SCALE_SHORTCUT_COUNT;++n) c.scale_shortcuts[n]=i->scale_shortcuts[n];
    for(unsigned n=0;n<THY_VIEW_COUNT;++n) c.display_config[n]=i->display_config[n];
    for(unsigned n=0;n<48u;++n) c.pad_octave_layers[n]=i->pad_octave_layers[n];
#define FIELD(name,max) c.name=i->name;
    CONFIG_BYTES(FIELD)
#undef FIELD
    for(unsigned n=0;n<3u;++n) c.users[n]=i->users[n];
    for(unsigned n=0;n<2u;++n) c.transpose[n]=i->transpose[n];
    for(unsigned n=0;n<4u;++n) c.play_nav[n]=i->play_nav[n];
    for(unsigned n=0;n<48u;++n) c.excluded[n]=i->excluded_slots[n];
    for(unsigned n=0;n<9u;++n) {
        /* Split routes intentionally use the legacy live side arrays. The
         * route_channels[1/2] backing cells are unused/stale by contract. */
        c.route_channels[n]=(n==1u || n==2u)?i->channels[n-1u]:i->route_channels[n];
        c.route_destinations[n]=(n==1u || n==2u)?i->destinations[n-1u]:i->route_destinations[n];
        c.route_velocity[n]=i->route_velocity[n]; c.route_pressure[n]=i->route_pressure[n];
    }
    for(unsigned n=0;n<THY_PAD_COUNT;++n) {
        const thy_instrument_sensor *p=&i->sensors[n]; c.profile[n]=p->detector_profile;
        c.overridden[n]=p->response_overridden;
        c.pad_response[n]=c.overridden[n]?p->response:c.response;c.calibration[n]=p->calibration;
        /* Quiet tracking is acquisition runtime, not a user preference. */
        c.calibration[n].baseline=0;
        c.velocity_overridden[n]=i->velocity_overridden[n];
        c.pad_velocity[n]=c.velocity_overridden[n]?i->pad_velocity[n]:c.velocity;
        c.pad_release_policy[n]=c.overridden[n]?i->pad_release_policy[n]:c.release_policy;
        c.sensitivity_guard[n]=i->sensitivity_guard[n];
    }
    if(!valid_config(&c,e->state.scale_count,THY_SETTINGS_VERSION) ||
        !thy_library_valid(&i->library) || !library_references_valid(&c,(const uint8_t *)&i->library)) return 0;
#define FIELD(name,max) if(c.harmony_##name>max)return 0;
    HARMONY_BYTES(FIELD) HARMONY_V17_BYTES(FIELD) HARMONY_V18_BYTES(FIELD)
#undef FIELD
    if(c.cluster_swap>1u||c.harmony_low_note>c.harmony_high_note||
        capacity<THY_SETTINGS_V19_PARAMETER_BYTES+thy_sequencer_song_size(&e->sequencer)) return 0;
    stream s={out,NULL,capacity,0,1}; transfer(&s,&c,THY_SETTINGS_VERSION);
    if(!s.valid || s.at!=THY_SETTINGS_V12_LIBRARY_OFFSET ||
        thy_library_payload_encode(&i->library,out+s.at,capacity-s.at)!=THY_LIBRARY_OK) return 0;
    s.at+=THY_LIBRARY_PAYLOAD_BYTES;
#define FIELD(name,max) byte(&s,&c.harmony_##name);
    HARMONY_BYTES(FIELD)
#undef FIELD
    byte(&s,&c.cluster_swap);
#define FIELD(name,max) byte(&s,&c.harmony_##name);
    HARMONY_V17_BYTES(FIELD) HARMONY_V18_BYTES(FIELD)
#undef FIELD
    playing_parameters(&s,&c);
    size_t song_bytes=0u;
    if(thy_sequencer_encode(&e->sequencer,out+s.at,capacity-s.at,&song_bytes)!=THY_OK) return 0;
    *used=s.at+song_bytes; return 1;
}
/* Keep the semantic-validation snapshot out of journal caller frames. Without
 * this boundary -Os can inline its large config alongside encode's snapshot,
 * needlessly stacking two copies on the MCU's reserved interrupt stack. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
THY_SETTINGS_NOINLINE int thy_settings_validate(const uint8_t *in,size_t count,uint16_t scales) {config c; return decode(&c,in,count,scales);}
int thy_settings_catalog(const uint8_t *in,size_t count,uint16_t *scale,uint8_t *root) {
    if(!in || count<3u || !scale || !root) return 0;
    *scale=(uint16_t)(in[0]|(uint16_t)in[1]<<8); *root=in[2]; return 1;
}
static int can_apply_config(const thy_engine *e,const config *c) {
    if(!e || !e->instrument || !e->instrument->core || e->state.reset_open) return 0;
    const thy_instrument *i=e->instrument;
    if(i->input_enabled || i->core->sounding_count || i->core->queue_count || i->core->pending_keys ||
        i->core->sustain_count || i->core->generated_count) return 0;
    if(i->catalog_degree_count>12u) return 0;
    for(unsigned n=0;n<i->catalog_degree_count;++n)
        if(i->catalog_degrees[n]<0 || i->catalog_degrees[n]>11 ||
            (!n && i->catalog_degrees[n]) ||
            (n && i->catalog_degrees[n]<=i->catalog_degrees[n-1u])) return 0;
    uint64_t mapped[2]={0u,0u};
    for(unsigned n=0;n<THY_PAD_COUNT;++n){
        if(i->mapping[n]>=THY_PAD_COUNT || i->sensors[n].held || i->core->owners[n].held)return 0;
        unsigned logical=i->mapping[n];uint64_t bit=UINT64_C(1)<<(logical%64u);
        if(mapped[logical/64u]&bit)return 0;
        mapped[logical/64u]|=bit;
        unsigned side=i->mapping[n]/48u;
        const thy_expression_profile *profile=c->expression_profile_overridden[n]?
            &c->pad_expression_profiles[n]:&c->expression_profiles[side];
        if(profile->enabled){
            const thy_instrument_response *response=c->overridden[n]?&c->pad_response[n]:&c->cluster_response[side];
            unsigned span=c->profile[n]==THY_DETECTOR_RELATIVE?c->calibration[n].full_scale:
                (unsigned)response->pressure_full_scale-response->pressure_floor;
            if(span<2u)return 0;
        }
    }
    for(unsigned n=0;n<2u;++n) if(e->arps[n].held_count || e->arps[n].phrase_count || e->arps[n].output_count) return 0;
    /* The optional expression workspace is runtime, never serialized. Its
     * retiring leases and consumed gestures must be quiet before any settings
     * mutate, even if the musical ledger itself has already emptied. */
    if(!thy_engine_channel_quiet(e) || (c->expression.enabled && (!e->expression || e->calibration_open)) ||
        (e->expression && !thy_mpe_quiet(e->expression)))return 0;
    return 1;
}
THY_SETTINGS_NOINLINE int thy_settings_can_apply(const thy_engine *e,const uint8_t *in,size_t count) {
    config c;
    return e && decode(&c,in,count,e->state.scale_count) && can_apply_config(e,&c);
}
THY_SETTINGS_NOINLINE int thy_settings_apply(thy_engine *e,const uint8_t *in,size_t count) {
    config c;
    if(!e || !decode(&c,in,count,e->state.scale_count) || !can_apply_config(e,&c))return 0;
    thy_instrument *i=e->instrument;
    unsigned version=(unsigned)thy_settings_payload_version(in,count);
    migrate_legacy(&c,version);
    if(version>=12u)
        (void)thy_library_payload_decode(&i->library,in+THY_SETTINGS_V12_LIBRARY_OFFSET,THY_LIBRARY_PAYLOAD_BYTES);
    else thy_library_defaults(&i->library);
#define FIELD(name,max) i->name=c.name;
    CHORD_BYTES(FIELD)
#undef FIELD
    i->voicing_previous_mask=0u;
    i->live_anchor=0; i->live_degree_count=0u;
    for(unsigned n=0;n<12u;++n)i->live_degrees[n]=0;
    i->library_editor=(thy_library_ui){0};
    i->guidance=(thy_guidance_ui){0};
    (void)thy_instrument_apply_scale(i);
    for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n)e->channel_routes[n]=c.channel_routes[n];
    for(unsigned n=0;n<2u;++n)i->expression_profiles[n]=c.expression_profiles[n];
    for(unsigned n=0;n<THY_PAD_COUNT;++n){
        i->expression_profile_overridden[n]=c.expression_profile_overridden[n];
        i->pad_expression_profiles[n]=c.pad_expression_profiles[n];
    }
    e->scale=c.scale; i->performance=c.performance; i->users_valid=c.users_valid;
    i->response=c.response; i->light_fade_ms=c.light_fade_ms; i->bass_octave=c.bass_octave;
    i->velocity_response=c.velocity;
    i->release_policy=c.release_policy;
    for(unsigned n=0;n<2u;++n){
        i->cluster_response[n]=c.cluster_response[n];i->cluster_velocity[n]=c.cluster_velocity[n];
        i->cluster_release_policy[n]=c.cluster_release_policy[n];
    }
    i->lower_mode=c.lower_mode; i->octave_layers=c.octave_layers;
    i->engine_kind=c.engine_kind;
    for(unsigned n=0;n<THY_SCALE_SHORTCUT_COUNT;++n) i->scale_shortcuts[n]=c.scale_shortcuts[n];
    for(unsigned n=0;n<THY_VIEW_COUNT;++n) i->display_config[n]=c.display_config[n];
    for(unsigned n=0;n<48u;++n) i->pad_octave_layers[n]=c.pad_octave_layers[n];
#define FIELD(name,max) i->name=c.name;
    CONFIG_BYTES(FIELD)
#undef FIELD
    for(unsigned n=0;n<3u;++n) i->users[n]=c.users[n];
    for(unsigned n=0;n<2u;++n) i->transpose[n]=c.transpose[n];
    for(unsigned n=0;n<4u;++n) i->play_nav[n]=c.play_nav[n];
    for(unsigned n=0;n<48u;++n) i->excluded_slots[n]=c.excluded[n];
    for(unsigned n=0;n<9u;++n) {
        i->route_channels[n]=c.route_channels[n]; i->route_destinations[n]=c.route_destinations[n];
        i->route_velocity[n]=c.route_velocity[n]; i->route_pressure[n]=c.route_pressure[n];
    }
    for(unsigned n=0;n<2u;++n) {i->channels[n]=i->route_channels[n+1u]; i->destinations[n]=i->route_destinations[n+1u];}
    for(unsigned n=0;n<THY_PAD_COUNT;++n) {
        thy_instrument_sensor *p=&i->sensors[n]; *p=(thy_instrument_sensor){0};
        p->detector_profile=c.profile[n]; p->response_overridden=c.overridden[n];
        unsigned side=i->mapping[n]/48u;
        i->sensitivity_guard[n]=c.sensitivity_guard[n];
        p->response=c.overridden[n]?c.pad_response[n]:c.cluster_response[side];p->calibration=c.calibration[n];
        i->velocity_overridden[n]=c.velocity_overridden[n];
        i->pad_velocity[n]=c.velocity_overridden[n]?c.pad_velocity[n]:c.cluster_velocity[side];
        i->pad_release_policy[n]=c.overridden[n]?c.pad_release_policy[n]:c.cluster_release_policy[side];
        /* All inputs establish a fresh quiet baseline, including explicitly
         * configured relative detectors. Learned ADC offsets are not settings. */
    }
    e->route_target=i->routing_profile==THY_ROUTING_WHOLE?0u:i->routing_profile==THY_ROUTING_SPLIT?1u:3u;
    /* Restore configuration into clean runtime. Keep callbacks and core handle
     * identities intact; advance source generations to invalidate idle-import
     * callbacks, retiring an exhausted generation rather than reusing it. */
    uint32_t old_clock=e->musical_clock.generation, old_transport=e->rhythm_transport.generation;
    uint32_t track_runs[THY_TRANSPORT_TRACKS];
    for(unsigned n=0;n<THY_TRANSPORT_TRACKS;++n) track_runs[n]=e->rhythm_transport.tracks[n].generation;
    thy_clock_init(&e->musical_clock,0u);
    e->musical_clock.tempo_milli_bpm=c.tempo;
    (void)thy_clock_set_meter(&e->musical_clock,c.numerator,c.denominator);
    e->musical_clock.source=c.clock_source;
    e->musical_clock.status=c.clock_source==THY_CLOCK_SOURCE_USB?THY_CLOCK_WAITING:THY_CLOCK_INTERNAL;
    e->musical_clock.generation=old_clock && old_clock<UINT32_MAX?old_clock+1u:0u;
    thy_transport_init(&e->rhythm_transport,&e->musical_clock);
    e->rhythm_transport.generation=old_transport && old_transport<UINT32_MAX?old_transport+1u:0u;
    e->rhythm_transport.launch_quantize=c.quantize;
    for(unsigned n=0;n<THY_TRANSPORT_TRACKS;++n) {
        e->rhythm_transport.tracks[n].generation=track_runs[n] && track_runs[n]<UINT32_MAX?track_runs[n]+1u:0u;
        e->session.tracks[n]=(thy_engine_track_runtime){.generation=e->rhythm_transport.tracks[n].generation};
    }
    for(unsigned n=0;n<3u;++n) {
        uint32_t old=e->live_generation[n]; e->live_generation[n]=old && old<UINT32_MAX?old+1u:0u;
    }
    e->rhythm_epoch_set=0u; e->observed_clock_generation=e->musical_clock.generation;
    e->session.epoch=e->rhythm_transport.generation;
    e->session.transport=THY_ENGINE_STOPPED; e->session.capture=THY_ENGINE_CAPTURE_OFF;
    e->session.playing_mode=c.engine_kind;
    e->session.clock=e->musical_clock.status;
    e->arp_target=c.arp_target; e->arp_quick_keys=c.arp_quick_keys;
    e->arp_quick_open=0u; e->arp_quick_cursor=0u;
    for(unsigned n=0;n<2u;++n) {
        thy_arp_init(&e->arps[n],THY_ENGINE_ARP1+n);
        e->arps[n].config=c.arps[n]; e->arps[n].config.root=i->performance.root;
        if(i->degree_count && i->degree_count<=12u) {
            e->arps[n].config.scale_count=i->degree_count;
            for(unsigned d=0;d<i->degree_count;++d) e->arps[n].config.scale[d]=(uint8_t)i->degrees[d];
        }
    }
    thy_engine_sequencer_init(e);
    if(version>=6u) {
        size_t parameters=parameter_bytes(version);
        (void)thy_sequencer_decode(&e->sequencer,in+parameters,count-parameters);
        e->sequencer_ui.layout=0u; e->sequencer_ui.swapped=0u;
        e->sequencer_ui.input_layout=c.seq_input; e->sequencer_ui.base_pitch=c.seq_pitch;
        e->sequencer_ui.grid_ticks=c.seq_grid; e->sequencer_ui.window_tick=0u;
        e->sequencer_ui.region_enabled=0u; e->sequencer_ui.region_start=0u;
        e->sequencer_ui.region_end=3840u; e->sequencer.selected_track=c.seq_track;
        e->remote_record=c.remote_record; e->remote_position=c.remote_position; e->remote_channel=c.remote_channel;
        for(unsigned n=0;n<THY_SEQ_TRACKS;++n) {
            if(in[parameters+4u]==1u) {
                thy_seq_track *legacy=&e->sequencer.tracks[n];
                uint32_t minimum=(legacy->loop_end-legacy->loop_start+47u)/48u;
                legacy->grid_ticks=c.seq_grid>minimum?c.seq_grid:minimum;
            }
            const thy_seq_track *t=&e->sequencer.tracks[n];
            if(t->loop_end>t->loop_start) (void)thy_transport_set_loop(&e->rhythm_transport,
                (uint8_t)n,t->loop_start,t->loop_end-t->loop_start);
            (void)thy_transport_set_enabled(&e->rhythm_transport,(uint8_t)n,
                thy_sequencer_track_count(&e->sequencer,(uint8_t)n)>0u && !(t->flags&THY_SEQ_FLAG_DISABLED));
        }
        e->sequencer_ui.grid_ticks=e->sequencer.tracks[c.seq_track].grid_ticks;
        e->sequencer_ui.overdub=e->sequencer.tracks[c.seq_track].record_policy==THY_SEQ_OVERDUB;
    }
    if(i->engine_kind==THY_PLAY_ENGINE_SEQUENCER) thy_sequencer_ui_open(&e->sequencer_ui,THY_SEQ_VIEW_TRACKS);
    /* Preflight above and the strict saved-mode dependency checks guarantee
     * this quiet configuration restore; it emits no RPN or musical messages. */
    (void)thy_engine_expression_configure(e,&c.expression);
    for(unsigned n=0;n<2u;++n)e->performance[n]=c.playing[n];
    e->velocity_target=0u; /* Transient Performance scope always reopens Both. */
    e->state.cursors[thy_menu_rows[THY_ROW_PERFORMANCE_VELOCITY].group]=0u;
    e->state.cursors[thy_menu_rows[THY_ROW_SENSITIVITY_TARGET].group]=0u;
    if(version<=12u) {
        /* These are runtime navigation hints, not persisted fields. Musical
         * setup moved between these two lists; preserve other categories. */
        e->state.cursors[thy_menu_rows[THY_ROW_CHORDS_LAYOUT].group]=0u;
    }
#define FIELD(name,max) e->harmony.name=c.harmony_##name;
    HARMONY_BYTES(FIELD) HARMONY_V17_BYTES(FIELD) HARMONY_V18_BYTES(FIELD)
#undef FIELD
    i->cluster_swap=c.cluster_swap;
    thy_engine_harmony_reset(e);
    ++i->revision; return 1;
}
uint32_t thy_settings_crc32(const uint8_t *data,size_t bytes) {return thy_crc32(data,bytes);}
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned b=0;b<4u;++b) p[b]=(uint8_t)(n>>(b*8u));}
static uint32_t observe_begin(thy_settings_journal *j,unsigned phase) {
    const thy_settings_observer *o=j->observer;
    return o&&o->enabled&&*o->enabled&&o->begin&&o->end?o->begin(o->context,phase):0u;
}
static void observe_end(thy_settings_journal *j,unsigned phase,uint32_t stamp) {
    const thy_settings_observer *o=j->observer;
    if(o&&o->enabled&&*o->enabled&&o->begin&&o->end)o->end(o->context,phase,stamp);
}
static uint32_t journal_crc(thy_settings_journal *j,const uint8_t *data,size_t count) {
    uint32_t stamp=observe_begin(j,THY_SETTINGS_OBSERVE_CRC);
    uint32_t result=thy_settings_crc32(data,count);
    observe_end(j,THY_SETTINGS_OBSERVE_CRC,stamp);return result;
}
static int journal_validate(thy_settings_journal *j,const uint8_t *data,size_t count,uint16_t scales) {
    uint32_t stamp=observe_begin(j,THY_SETTINGS_OBSERVE_VALIDATE);
    int result=thy_settings_validate(data,count,scales);
    observe_end(j,THY_SETTINGS_OBSERVE_VALIDATE,stamp);return result;
}
static int journal_program(thy_settings_journal *j,uint32_t offset,const uint8_t word[4]) {
    uint32_t stamp=observe_begin(j,THY_SETTINGS_OBSERVE_PROGRAM);
    int result=j->io.program(j->io.context,offset,word);
    observe_end(j,THY_SETTINGS_OBSERVE_PROGRAM,stamp);return result;
}
static int journal_erase(thy_settings_journal *j,unsigned sector) {
    uint32_t stamp=observe_begin(j,THY_SETTINGS_OBSERVE_ERASE);
    int result=j->io.erase(j->io.context,sector);
    observe_end(j,THY_SETTINGS_OBSERVE_ERASE,stamp);return result;
}
static int read_slot(thy_settings_journal *j,unsigned slot) {
    if(!j->io.read(j->io.context,slot*THY_SETTINGS_SLOT_BYTES,j->record,THY_SETTINGS_HEADER_BYTES)) return -1;
    if(get32(j->record)!=UINT32_C(0x36594854) || get32(j->record+4)!=UINT32_C(0x31535436)) return 0;
    uint32_t bytes=get32(j->record+12);
    uint32_t version=get32(j->record+8);
    unsigned span=version>=6u?THY_SETTINGS_WRITE_SLOT_SPAN:1u;
    if(get32(j->record+28)!=UINT32_C(0x51afe666) || !bytes ||
        bytes>span*THY_SETTINGS_SLOT_BYTES-THY_SETTINGS_HEADER_BYTES ||
        slot%THY_SETTINGS_SLOTS_PER_SECTOR+span>THY_SETTINGS_SLOTS_PER_SECTOR) return 0;
    if(bytes>THY_SETTINGS_PAYLOAD_MAX) {
        if(version>=1u && version<=THY_SETTINGS_VERSION) return 0;
        /* A future payload can exceed our decode buffer and still fit its
         * journal slot. Validate its CRC in bounded chunks before blocking
         * writes; never erase it just because this firmware cannot decode it.
         * Corrupt/torn records retain ordinary fallback/recovery semantics. */
        uint8_t block[32]; uint32_t expected=get32(j->record+20);
        put32(j->record+20,0u); uint32_t crc=thy_crc32_update(UINT32_MAX,j->record,32u);
        put32(j->record+20,expected);
        for(uint32_t at=0u;at<bytes;) {
            size_t count=bytes-at<sizeof block?(size_t)(bytes-at):sizeof block;
            if(!j->io.read(j->io.context,slot*THY_SETTINGS_SLOT_BYTES+32u+at,block,count)) return -1;
            crc=thy_crc32_update(crc,block,count); at+=(uint32_t)count;
        }
        return ~crc==expected?2:0;
    }
    if(!j->io.read(j->io.context,slot*THY_SETTINGS_SLOT_BYTES+32u,j->record+32u,bytes)) return -1;
    /* CRC covers immutable header (including version, length, generation),
     * and payload. The CRC word itself is zero while calculating. */
    uint32_t crc=get32(j->record+20); put32(j->record+20,0u);
    uint32_t actual=journal_crc(j,j->record,32u+bytes); put32(j->record+20,crc);
    if(crc!=actual) return 0;
    if(version<1u || version>THY_SETTINGS_VERSION) return 2;
    return (uint32_t)thy_settings_payload_version(j->record+32u,bytes)==version ? 1 : 0;
}
THY_SETTINGS_NOINLINE int thy_settings_journal_init(thy_settings_journal *j,const thy_settings_io *io,uint16_t scales) {
    if(!j || !io || !io->read || !io->program || !io->erase) return 0;
    *j=(thy_settings_journal){0}; j->io=*io; j->newest_slot=THY_SETTINGS_NONE; j->status=THY_SETTINGS_DEFAULTS;
    for(unsigned slot=0;slot<THY_SETTINGS_SLOT_COUNT;++slot) {
        int result=read_slot(j,slot);
        if(result<0) {++j->errors; j->status=THY_SETTINGS_ERROR; return 0;}
        if(result==2) {j->status=THY_SETTINGS_UNSUPPORTED; return 0;}
        if(!result) {
            if(get32(j->record)==UINT32_C(0x36594854)) ++j->rejected;
            continue;
        }
        uint32_t bytes=get32(j->record+12), generation=get32(j->record+16);
        if(!journal_validate(j,j->record+32u,bytes,scales)) {++j->rejected; continue;}
        if(j->newest_slot==THY_SETTINGS_NONE || (int32_t)(generation-j->generation)>0) {
            j->newest_slot=slot; j->generation=generation; j->payload_bytes=(uint16_t)bytes;
        }
    }
    j->recovered=j->rejected!=0u;
    if(j->newest_slot==THY_SETTINGS_NONE) return 1;
    if(read_slot(j,j->newest_slot)!=1) {++j->errors; j->status=THY_SETTINGS_ERROR; return 0;}
    j->loaded=1u; j->status=THY_SETTINGS_SAVED;
    j->loaded_version=(uint8_t)get32(j->record+8);
    j->migration_pending=j->loaded_version!=THY_SETTINGS_VERSION;
    j->saved_crc=journal_crc(j,j->record+32u,j->payload_bytes);
    /* A rejected recognizable record means a previous save was interrupted
     * or damaged. Older valid records left by rotation do not imply recovery. */
    j->recovered=j->rejected!=0u;
    return 1;
}
static int blank_slot(thy_settings_journal *j,unsigned slot) {
    uint8_t block[32];
    if(slot%THY_SETTINGS_SLOTS_PER_SECTOR+THY_SETTINGS_WRITE_SLOT_SPAN>THY_SETTINGS_SLOTS_PER_SECTOR) return 0;
    for(unsigned at=0;at<THY_SETTINGS_SLOT_BYTES*THY_SETTINGS_WRITE_SLOT_SPAN;at+=sizeof block) {
        if(!j->io.read(j->io.context,slot*THY_SETTINGS_SLOT_BYTES+at,block,sizeof block)) return 0;
        for(unsigned n=0;n<sizeof block;++n) if(block[n]!=255u) return 0;
    }
    return 1;
}
static void fail(thy_settings_journal *j) {++j->errors; j->writing=0u; j->status=THY_SETTINGS_ERROR;}
THY_SETTINGS_NOINLINE void thy_settings_journal_task(thy_settings_journal *j,const thy_engine *e,uint32_t now,int idle) {
    if(!j || !e || j->status==THY_SETTINGS_ERROR || j->status==THY_SETTINGS_UNSUPPORTED) return;
    if(e->state.reset_open)idle=0;
    if(j->writing) {
        if(!idle) return;
        if(j->erase_pending) {
            if(!journal_erase(j,j->write_slot/THY_SETTINGS_SLOTS_PER_SECTOR)) {fail(j); return;}
            j->erase_pending=0u; if(!blank_slot(j,j->write_slot)) fail(j); return;
        }
        uint32_t at=j->write_offset;
        /* Skip commit until every body word is durable. */
        if(at==28u) at=32u;
        if(at>=j->write_bytes) at=28u;
        uint8_t verify[4]; const uint8_t *word=j->record+at;
        uint32_t offset=j->write_slot*THY_SETTINGS_SLOT_BYTES+at;
        if(!journal_program(j,offset,word) || !j->io.read(j->io.context,offset,verify,4u)) {fail(j); return;}
        for(unsigned n=0;n<4u;++n) if(verify[n]!=word[n]) {fail(j); return;}
        if(at==28u) {
            /* Full readback, CRC and semantic verification after commit. */
            if(read_slot(j,j->write_slot)!=1 || !journal_validate(j,j->record+32u,j->payload_bytes,e->state.scale_count)) {fail(j); return;}
            j->newest_slot=j->write_slot; j->generation=get32(j->record+16);
            j->saved_crc=journal_crc(j,j->record+32u,j->payload_bytes);
            j->loaded_version=THY_SETTINGS_VERSION; j->migration_pending=0u;
            j->loaded=1u; j->writing=0u; j->observed=0u; ++j->saves; j->status=THY_SETTINGS_SAVED;
        } else j->write_offset=at+4u;
        return;
    }
    size_t bytes=0;
    uint32_t stamp=observe_begin(j,THY_SETTINGS_OBSERVE_ENCODE);
    int encoded=thy_settings_encode(e,j->record+32u,THY_SETTINGS_PAYLOAD_MAX,&bytes);
    observe_end(j,THY_SETTINGS_OBSERVE_ENCODE,stamp);
    if(!encoded) {fail(j); return;}
    uint32_t crc=journal_crc(j,j->record+32u,bytes);
    if(j->loaded && !j->migration_pending && crc==j->saved_crc) {j->observed=0u; j->status=THY_SETTINGS_SAVED; return;}
    if(!j->observed || crc!=j->observed_crc) {j->observed=1u; j->observed_crc=crc; j->changed_ms=now; j->status=THY_SETTINGS_PENDING; return;}
    if(!idle || (uint32_t)(now-j->changed_ms)<1500u) return;
    unsigned sector=j->newest_slot==THY_SETTINGS_NONE?0u:j->newest_slot/THY_SETTINGS_SLOTS_PER_SECTOR;
    unsigned start=j->newest_slot==THY_SETTINGS_NONE?0u:
        j->newest_slot%THY_SETTINGS_SLOTS_PER_SECTOR+(j->loaded_version>=6u?THY_SETTINGS_WRITE_SLOT_SPAN:1u);
    start=(start+THY_SETTINGS_WRITE_SLOT_SPAN-1u)&~(THY_SETTINGS_WRITE_SLOT_SPAN-1u);
    unsigned slot;
    for(slot=start;slot<THY_SETTINGS_SLOTS_PER_SECTOR;slot+=THY_SETTINGS_WRITE_SLOT_SPAN)
        if(blank_slot(j,sector*THY_SETTINGS_SLOTS_PER_SECTOR+slot)) break;
    j->erase_pending=0u;
    if(slot==THY_SETTINGS_SLOTS_PER_SECTOR) {sector^=1u; slot=0u; j->erase_pending=1u;}
    /* A fresh reserved region may contain a previous application's tail. */
    if(j->newest_slot==THY_SETTINGS_NONE && slot==0u && !blank_slot(j,0u)) j->erase_pending=1u;
    j->write_slot=sector*THY_SETTINGS_SLOTS_PER_SECTOR+slot;
    for(unsigned n=0;n<32u;++n) j->record[n]=0;
    put32(j->record,UINT32_C(0x36594854)); put32(j->record+4,UINT32_C(0x31535436));
    put32(j->record+8,THY_SETTINGS_VERSION); put32(j->record+12,(uint32_t)bytes);
    put32(j->record+16,j->generation+1u); put32(j->record+28,UINT32_C(0x51afe666));
    put32(j->record+20,journal_crc(j,j->record,32u+bytes));
    j->payload_bytes=(uint16_t)bytes; j->write_bytes=(32u+(uint32_t)bytes+3u)&~3u;
    for(unsigned n=32u+(unsigned)bytes;n<j->write_bytes;++n) j->record[n]=255u;
    j->write_offset=0u; j->writing=1u; j->status=THY_SETTINGS_SAVING;
}

int thy_settings_journal_recall_song(thy_settings_journal *j,thy_engine *e)
{
    if(!j || !e || j->writing || j->newest_slot==THY_SETTINGS_NONE) return 0;
    if(read_slot(j,j->newest_slot)!=1 || get32(j->record+8)<6u) return 0;
    uint32_t bytes=get32(j->record+12);
    size_t parameters=parameter_bytes(get32(j->record+8));
    if(bytes<parameters ||
        !journal_validate(j,j->record+32u,bytes,e->state.scale_count)) return 0;
    int result=thy_engine_sequencer_song_import(e,j->record+32u+parameters,bytes-parameters);
    if(result!=THY_OK) return 0;
    j->observed=0u; return 1;
}
