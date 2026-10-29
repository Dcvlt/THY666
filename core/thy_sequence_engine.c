/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_engine.h"
#include "thy_lighting.h"

static const uint8_t routes[] = {5u,4u,1u,2u,3u,6u,7u};
static const uint32_t grids[] = {960u,480u,240u,120u};
static int good(const thy_engine *e) { return e && e->instrument && e->instrument->core; }
static int state_result(int r) { return r == THY_OK ? THY_STATE_OK : r == THY_SEQ_BUSY ? THY_STATE_BLOCKED : THY_STATE_ADAPTER_ERROR; }

int thy_engine_sequencer_options_close(thy_engine *e)
{
    if(!good(e))return THY_STATE_INVALID;
    thy_sequencer_ui *u=&e->sequencer_ui;
    if(!u->options_open)return THY_STATE_OK;
    uint8_t group=thy_menu_rows[THY_ROW_SEQUENCER_OPEN].group;
    u->options_cursor=e->state.cursors[group];
    e->state.cursors[group]=u->options_saved_cursor;
    if(thy_sequencer_track_exists(&e->sequencer,u->options_track))
        (void)thy_sequencer_select(&e->sequencer,u->options_track);
    else u->view=THY_SEQ_VIEW_TRACKS;
    u->options_open=0u;
    thy_state_play(&e->state);++u->revision;
    return THY_STATE_OK;
}

static int options_open(thy_engine *e,uint8_t track,int recording)
{
    thy_sequencer_ui *u=&e->sequencer_ui;
    if(!u->open || u->view!=THY_SEQ_VIEW_STEPS ||
       !thy_sequencer_track_exists(&e->sequencer,track))return THY_STATE_UNAVAILABLE;
    if(u->options_open && u->options_track!=track)return THY_STATE_BLOCKED;
    uint8_t group=thy_menu_rows[THY_ROW_SEQUENCER_OPEN].group;
    if(!u->options_open){
        u->options_track=track;
        u->options_saved_cursor=e->state.cursors[group];
        u->options_open=1u;
    }
    uint16_t row=recording?THY_ROW_SEQUENCER_SOURCE:
        (uint16_t)(thy_menu_groups[group].first_row+u->options_cursor);
    if(row>=THY_MENU_ROW_COUNT || thy_menu_rows[row].group!=group ||
       !thy_state_row_visible(&e->state,row))row=THY_ROW_SEQUENCER_LENGTH;
    u->help_open=u->delete_pending=0u;
    ++u->revision;
    return thy_state_focus(&e->state,group,row);
}

int thy_engine_sequencer_open(thy_engine *e,uint8_t view)
{
    if(!good(e) || view>THY_SEQ_VIEW_STEPS) return THY_STATE_INVALID;
    if(e->expression_config.enabled||e->instrument->guidance.open)return THY_STATE_BLOCKED;
    thy_engine_calibration_close(e);
    if(e->calibration_open)return THY_STATE_BLOCKED;
    (void)thy_engine_sequencer_options_close(e);
    thy_state_play(&e->state); e->arp_quick_open=0u;
    thy_sequencer_ui_open(&e->sequencer_ui,view);
    e->instrument->engine_kind=THY_PLAY_ENGINE_SEQUENCER;
    e->session.playing_mode=THY_PLAY_ENGINE_SEQUENCER;
    ++e->instrument->revision; return THY_STATE_OK;
}
int thy_engine_sequencer_close(thy_engine *e)
{
    if(!good(e)) return THY_STATE_INVALID;
    (void)thy_engine_sequencer_options_close(e);
    thy_sequencer_ui_close(&e->sequencer_ui); thy_state_play(&e->state);
    e->instrument->engine_kind=THY_PLAY_ENGINE_PLAY;
    e->session.playing_mode=THY_PLAY_ENGINE_PLAY;
    ++e->instrument->revision; return THY_STATE_OK;
}
int thy_engine_sequencer_record(thy_engine *e)
{
    if(!good(e)) return THY_INVALID_ARGUMENT;
    if(e->expression_config.enabled)return THY_SEQ_BUSY;
    unsigned n=e->sequencer.selected_track;
    const thy_seq_track *t=&e->sequencer.tracks[n];
    return thy_sequencer_record(&e->sequencer,&e->rhythm_transport,&e->musical_clock,
        (uint8_t)n,t->record_source,t->record_policy,
        e->sequencer_ui.region_enabled?e->sequencer_ui.region_start:0u,
        e->sequencer_ui.region_enabled?e->sequencer_ui.region_end:0u);
}
int thy_engine_sequencer_finish(thy_engine *e,int immediate)
{ return good(e)?thy_sequencer_finish(&e->sequencer,&e->rhythm_transport,&e->musical_clock,immediate):THY_INVALID_ARGUMENT; }
int thy_engine_sequencer_loop(thy_engine *e,uint8_t track,uint32_t start,uint32_t end)
{ return good(e)?thy_sequencer_loop(&e->sequencer,&e->rhythm_transport,track,start,end):THY_INVALID_ARGUMENT; }
void thy_engine_sequencer_changed(thy_engine *e,uint8_t track)
{
    if(!good(e) || track>=THY_SEQ_TRACKS) return;
    ++e->instrument->revision;
}
int thy_engine_sequencer_track_configure(thy_engine *e,uint8_t n,const thy_seq_track *t)
{
    if(!good(e) || !t || n>=THY_SEQ_TRACKS || t->channel>15u || !t->destinations ||
        t->destinations>7u || t->velocity>127u || t->muted>1u || t->processor>2u ||
        t->record_source>THY_SEQ_SOURCE_USB || t->record_policy>1u ||
        !t->grid_ticks || t->grid_ticks>(THY_SEQ_MAX_TICK+47u)/48u ||
        ((t->flags&THY_SEQ_QUANTIZE_MASK)>>THY_SEQ_QUANTIZE_SHIFT)>4u ||
        t->content_end>THY_SEQ_MAX_TICK || t->loop_end>t->content_end ||
        t->loop_start>=t->loop_end) return THY_INVALID_ARGUMENT;
    if((e->sequencer.capture.state==THY_SEQ_RECORDING || e->sequencer.capture.state==THY_SEQ_FINISHING) && e->sequencer.capture.track==n)
        return THY_SEQ_BUSY;
    thy_seq_track *old=&e->sequencer.tracks[n];
    if(old->muted!=t->muted || old->processor!=t->processor)
        (void)thy_engine_generated_clear(e,THY_ENGINE_TRACK_BASE+n);
    *old=*t; ++old->revision; ++e->sequencer.revision; ++e->instrument->revision;
    return THY_OK;
}
int thy_engine_sequencer_song_import(thy_engine *e,const uint8_t *bytes,size_t count)
{
    if(!good(e) || thy_sequencer_validate(bytes,count)!=THY_OK) return THY_SEQ_CORRUPT;
    /* Import must not finish and overwrite an armed or staged take. Besides
     * preserving its content, this keeps journal recall out of the take's
     * larger commit scratch frame. Explicit Stop/Finish comes first. */
    if(e->sequencer.capture.state!=THY_SEQ_CAPTURE_OFF) return THY_SEQ_BUSY;
    uint8_t opened=e->sequencer_ui.open, view=e->sequencer_ui.view;
    (void)thy_engine_sequencer_options_close(e);
    thy_sequencer_ui_close(&e->sequencer_ui);
    (void)thy_engine_transport_stop(e);
    int result=thy_sequencer_decode(&e->sequencer,bytes,count);
    if(result!=THY_OK) return result;
    for(unsigned n=0;n<THY_SEQ_TRACKS;++n) {
        const thy_seq_track *t=&e->sequencer.tracks[n];
        if(t->loop_end>t->loop_start)
            (void)thy_transport_set_loop(&e->rhythm_transport,(uint8_t)n,t->loop_start,t->loop_end-t->loop_start);
        (void)thy_transport_set_enabled(&e->rhythm_transport,(uint8_t)n,
            thy_sequencer_track_count(&e->sequencer,(uint8_t)n)>0u && !(t->flags&THY_SEQ_FLAG_DISABLED));
    }
    if(opened) thy_sequencer_ui_open(&e->sequencer_ui,view);
    ++e->instrument->revision; return THY_OK;
}
int thy_engine_sequencer_action(thy_engine *e,uint8_t cmd,uint8_t track)
{
    if(!good(e))return THY_INVALID_ARGUMENT;
    thy_sequencer *s=&e->sequencer;thy_sequencer_ui *u=&e->sequencer_ui;
    if(u->options_open && (cmd==THY_SEQ_UI_CREATE_TRACK || cmd==THY_SEQ_UI_DELETE_TRACK))
        return THY_SEQ_BUSY;
    if(u->options_open)track=u->options_track;
    if(cmd==THY_SEQ_UI_CREATE_TRACK || cmd==THY_SEQ_UI_DELETE_TRACK)
        for(unsigned p=0;p<96u;++p)if(u->pads[p].held &&
            (u->pads[p].role==THY_SEQ_PAD_INPUT_NOTES || u->pads[p].role==THY_SEQ_PAD_INPUT_CHORDS))return THY_SEQ_BUSY;
    switch(cmd) {
    case THY_SEQ_UI_TRANSPORT: return e->rhythm_transport.state==THY_TRANSPORT_PLAYING?
        thy_engine_transport_stop(e):thy_engine_transport_start(e);
    case THY_SEQ_UI_RECORD: {
        if(track>=THY_SEQ_TRACKS || !thy_sequencer_track_exists(s,track) || u->gesture_active)return THY_SEQ_BUSY;
        const thy_seq_track *t=&s->tracks[track];
        int stopped=e->rhythm_transport.state!=THY_TRANSPORT_PLAYING;
        int r=thy_sequencer_record(s,&e->rhythm_transport,&e->musical_clock,track,
            t->record_source,t->record_policy,t->loop_start,t->loop_end);
        if(r==THY_OK && s->capture.state!=THY_SEQ_CAPTURE_OFF &&
           u->open && u->view==THY_SEQ_VIEW_TRACKS)
            (void)thy_engine_sequencer_open(e,THY_SEQ_VIEW_STEPS);
        if(r==THY_OK && stopped && s->capture.state==THY_SEQ_ARMED){
            u->solo=1u; r=thy_engine_transport_start(e);
            if(r!=THY_OK)thy_sequencer_cancel(s);
        } else if(r==THY_OK && s->capture.state==THY_SEQ_QUEUED &&
                  !e->rhythm_transport.tracks[track].running &&
                  e->rhythm_transport.tracks[track].pending!=THY_TRANSPORT_PENDING_START) {
            r=thy_engine_track_toggle(e,track);
            if(r!=THY_OK)thy_sequencer_cancel(s);
        }
        return r;
    }
    case THY_SEQ_UI_LAUNCH:
        return thy_sequencer_track_exists(s,track)?thy_engine_track_toggle(e,track):THY_SEQ_NOT_FOUND;
    case THY_SEQ_UI_CREATE_TRACK: {
        if(u->gesture_active)return THY_SEQ_BUSY;
        if(track>=THY_SEQ_TRACKS || thy_sequencer_track_exists(s,track)) {
            for(track=0u;track<THY_SEQ_TRACKS && thy_sequencer_track_exists(s,track);++track){}
        }
        if(track>=THY_SEQ_TRACKS)return THY_SEQ_FULL;
        if(!e->rhythm_transport.tracks[track].generation ||
            e->rhythm_transport.tracks[track].generation==UINT32_MAX)return THY_HANDLE_EXHAUSTED;
        int r=thy_sequencer_create(s,track);
        if(r==THY_OK){
            const thy_seq_track *t=&s->tracks[track];
            (void)thy_transport_set_loop(&e->rhythm_transport,track,t->loop_start,t->loop_end-t->loop_start);
            u->track_new=0u;u->selected_step=0u;u->selected_event=0u;u->overdub=0u;
            ++u->revision;thy_engine_sequencer_changed(e,track);
        }
        return r;
    }
    case THY_SEQ_UI_DELETE_TRACK: {
        if(track>=THY_SEQ_TRACKS || u->gesture_active)return THY_SEQ_BUSY;
        thy_transport_track *run=&e->rhythm_transport.tracks[track];
        if(!run->generation || run->generation==UINT32_MAX)return THY_HANDLE_EXHAUSTED;
        int r=thy_sequencer_delete(s,track);
        if(r==THY_OK){run->running=run->enabled=run->pending=0u;run->pending_position=0u;++run->generation;
            thy_engine_sequencer_tick(e);++u->revision;thy_engine_sequencer_changed(e,track);}
        return r;
    }
    case THY_SEQ_UI_PLAY_TRACK:
    case THY_SEQ_UI_PLAY_ALL: {
        if(cmd==THY_SEQ_UI_PLAY_TRACK && !thy_sequencer_track_exists(s,track))return THY_SEQ_NOT_FOUND;
        uint8_t solo=(uint8_t)(cmd==THY_SEQ_UI_PLAY_TRACK);
        if(e->rhythm_transport.state==THY_TRANSPORT_PLAYING){
            int same=u->solo==solo && (!solo || e->rhythm_transport.tracks[track].running);
            int r=thy_engine_transport_stop(e);if(r!=THY_OK || same)return r;
        }
        (void)thy_sequencer_select(s,track<THY_SEQ_TRACKS?track:s->selected_track);
        u->solo=solo;++u->revision;return thy_engine_transport_start(e);
    }
    case THY_SEQ_UI_CLEAR_STEP: return thy_sequencer_ui_clear_step(u);
    case THY_SEQ_UI_OVERDUB: return thy_sequencer_ui_set_overdub(u,!u->overdub);
    case THY_SEQ_UI_INPUT_TOGGLE:
        return thy_sequencer_ui_configure(u,THY_SEQ_UI_INPUT,!u->input_layout);
    case THY_SEQ_UI_INPUT_CONTEXT:
        if(u->input_layout==THY_SEQ_INPUT_CHORDS)
            return thy_state_set(&e->state,THY_ROW_PERFORMANCE_SCALE,(e->scale+1u)%e->state.scale_count);
        return thy_state_set(&e->state,THY_ROW_PERFORMANCE_MELODYLAYOUT,
            (e->instrument->performance.melody_layout+1u)%9u);
    case THY_SEQ_UI_OCTAVE_DOWN:
    case THY_SEQ_UI_OCTAVE_UP: {
        uint16_t row=u->input_layout==THY_SEQ_INPUT_CHORDS?THY_ROW_PERFORMANCE_CHORDOCTAVE:THY_ROW_PERFORMANCE_MELODYOCTAVE;
        int32_t v=0;(void)thy_state_get(&e->state,row,&v);
        v+=cmd==THY_SEQ_UI_OCTAVE_UP?1:-1;
        if(v<thy_menu_rows[row].min || v>thy_menu_rows[row].max)return THY_INVALID_ARGUMENT;
        return thy_state_set(&e->state,row,v);
    }
    case THY_SEQ_UI_HELP:
        if(!u->open)(void)thy_engine_sequencer_open(e,THY_SEQ_VIEW_TRACKS);
        (void)thy_engine_sequencer_options_close(e);
        thy_state_play(&e->state);u->help_open=1u;u->help_page=0u;++u->revision;return THY_OK;
    case THY_SEQ_UI_UNDO:
    case THY_SEQ_UI_REDO: {
        int result=thy_sequencer_history(s,e->instrument->core,&e->rhythm_transport,cmd==THY_SEQ_UI_REDO);
        if(result==THY_OK && u->options_open && !thy_sequencer_track_exists(s,u->options_track))
            (void)thy_engine_sequencer_options_close(e);
        return result;
    }
    case THY_SEQ_UI_PLAY: return thy_engine_sequencer_close(e);
    case THY_SEQ_UI_TRACK_OPTIONS:
    case THY_SEQ_UI_RECORD_OPTIONS:
        return options_open(e,track,cmd==THY_SEQ_UI_RECORD_OPTIONS);
    case THY_SEQ_UI_LOOP_CHANGED: thy_engine_sequencer_changed(e,track); return THY_OK;
    default: return THY_INVALID_ARGUMENT;
    }
}
static int command(void *context,uint8_t cmd,uint8_t track)
{return thy_engine_sequencer_action(context,cmd,track);}
void thy_engine_sequencer_init(thy_engine *e)
{
    thy_sequencer_init(&e->sequencer, e->sequencer.history);
    thy_sequencer_ui_init(&e->sequencer_ui,&e->sequencer,&e->rhythm_transport,
        &e->musical_clock,command,e);
    for(unsigned p=0;p<96u;++p) {
        e->input_pitches[p][0]=e->input_pitches[p][1]=0u;
        e->captured_strum[p][0]=e->captured_strum[p][1]=0u;
        e->input_identity[p]=0u; e->input_velocity[p]=0u;
        e->input_role[p]=e->input_assignment[p]=e->input_strummed[p]=0u;
        e->captured_pressure[p]=0u;
    }
    for(unsigned n=0;n<THY_GENERATED_OWNER_COUNT;++n) e->captured_generated[n]=0u;
    for(unsigned c=0;c<16u;++c) for(unsigned p=0;p<128u;++p) e->usb_input_velocity[c][p]=0u;
    e->input_serial=e->observed_capture_epoch=0u;
    e->capture_pressure_ms=0u;
    e->remote_record=e->remote_position=e->remote_channel=0u;
    e->song_save_requested=e->song_recall_requested=e->song_status=0u;
}
int thy_engine_sequencer_pad(void *context,uint8_t electrical,int pressed,int *result)
{
    thy_engine *e=context;
    if(!good(e) || !result || electrical>=96u) return 0;
    if(thy_engine_calibration_pad(e,electrical,pressed,result))return 1;
    if(thy_engine_sensitivity_pad(e,electrical,pressed,result)) return 1;
    uint8_t logical=thy_instrument_role_pad(e->instrument,e->instrument->mapping[electrical]);
    if(logical>=96u) return 0;
    /* Track options keep the editor surface; global menus use Play routing.
     * Releases always retain the role acquired by their original press. */
    if(pressed && e->state.level!=THY_STATE_PLAY && !e->sequencer_ui.options_open) return 0;
    int consumed=thy_sequencer_ui_pad(&e->sequencer_ui,logical,pressed);
    if(consumed) { *result=THY_OK; return 1; }
    uint8_t track,pitch; uint32_t tick;
    int pad_role=thy_sequencer_ui_pad_role(&e->sequencer_ui,logical,&track,&tick,&pitch);
    int input=pad_role==THY_SEQ_PAD_INPUT_NOTES?1:pad_role==THY_SEQ_PAD_INPUT_CHORDS?2:0;
    if(pressed && input) {
        uint8_t source=(uint8_t)(logical%48u+(input==1?48u:0u));
        *result=thy_instrument_press_source(e->instrument,electrical,source);
        return 1;
    }
    return 0;
}
static uint32_t note_identity(const thy_engine *e,unsigned pad,unsigned pitch)
{ return e->input_identity[pad]*128u+pitch; }
void thy_engine_sequencer_input(thy_engine *e,uint8_t pad,const int16_t *notes,
    size_t count,uint8_t velocity,uint8_t role)
{
    if(!good(e) || pad>=96u) return;
    e->input_pitches[pad][0]=e->input_pitches[pad][1]=0u;
    if(e->input_serial<UINT32_MAX/256u) ++e->input_serial;
    else { e->sequencer.capture.failed=1u; return; }
    e->input_identity[pad]=e->input_serial;
    e->input_velocity[pad]=velocity; e->input_role[pad]=role;
    unsigned logical=thy_instrument_role_pad(e->instrument,e->instrument->mapping[pad]);
    e->input_assignment[pad]=(uint8_t)thy_sequencer_ui_assign(&e->sequencer_ui,
        (uint8_t)logical,notes,count,velocity,role);
    for(size_t k=0;k<count;++k) {
        unsigned pitch=(unsigned)notes[k]; if(pitch>127u) continue;
        e->input_pitches[pad][pitch/64u]|=UINT64_C(1)<<(pitch%64u);
        if(!e->input_assignment[pad])
            (void)thy_sequencer_input_on(&e->sequencer,THY_SEQ_SOURCE_PADS,
                note_identity(e,pad,pitch),(uint16_t)(e->input_serial%65535u+1u),
                role,(uint8_t)pitch,velocity,e->rhythm_transport.position);
    }
}
void thy_engine_sequencer_release_input(thy_engine *e,uint8_t pad)
{
    if(!good(e) || pad>=96u) return;
    for(unsigned pitch=0;pitch<128u;++pitch) {
        if(e->input_pitches[pad][pitch/64u]&(UINT64_C(1)<<(pitch%64u)))
            (void)thy_sequencer_input_off(&e->sequencer,THY_SEQ_SOURCE_PADS,
                note_identity(e,pad,pitch),e->rhythm_transport.position);
        if(e->captured_strum[pad][pitch/64u]&(UINT64_C(1)<<(pitch%64u))) {
            (void)thy_sequencer_input_off(&e->sequencer,THY_SEQ_SOURCE_STRUM,
                note_identity(e,pad,pitch),e->rhythm_transport.position);
            (void)thy_sequencer_input_off(&e->sequencer,THY_SEQ_SOURCE_LIVE,
                note_identity(e,pad,pitch),e->rhythm_transport.position);
        }
    }
    e->input_pitches[pad][0]=e->input_pitches[pad][1]=0u;
    e->captured_strum[pad][0]=e->captured_strum[pad][1]=0u;
    e->input_identity[pad]=0u; e->input_assignment[pad]=e->input_strummed[pad]=0u;
}
static int assigned_output(const thy_engine *e,const thy_generated_owner *o)
{
    if(o->source<1u || o->source>2u) return 0;
    const thy_arp *a=&e->arps[o->source-1u];
    for(unsigned k=0;k<THY_GENERATED_OWNER_COUNT;++k) if(a->outputs[k].handle==o->handle) {
        unsigned phrase=a->outputs[k].phrase;
        unsigned selected=THY_ARP_POOL;uint64_t newest=0u;
        if(a->config.grouping==THY_ARP_HYBRID)
            for(unsigned n=0;n<THY_ARP_INPUTS;++n){const thy_arp_input *input=&a->inputs[n];
                if(input->held && input->role && (input->pitches[0] || input->pitches[1]) && input->order>newest){
                    newest=input->order;selected=n;
                }
            }
        for(unsigned n=0;n<THY_ARP_INPUTS;++n)if(e->input_assignment[n] && a->inputs[n].held){
            unsigned target=a->config.grouping==THY_ARP_COMBINED?THY_ARP_POOL:
                a->config.grouping==THY_ARP_EACH || a->inputs[n].role?n:selected;
            if(target==phrase)return 1;
        }
        return 0;
    }
    return 0;
}
void thy_engine_sequencer_capture(thy_engine *e)
{
    if(!good(e)) return;
    thy_sequencer *s=&e->sequencer; thy_core *core=e->instrument->core;
    if(s->capture.state!=THY_SEQ_RECORDING && s->capture.state!=THY_SEQ_FINISHING) return;
    int snapshot=e->observed_capture_epoch!=s->capture.start_epoch;
    if(snapshot) {
        e->observed_capture_epoch=s->capture.start_epoch;
        for(unsigned p=0;p<96u;++p) {
            e->captured_strum[p][0]=e->captured_strum[p][1]=0u;
            e->captured_pressure[p]=0u;
        }
        for(unsigned n=0;n<THY_GENERATED_OWNER_COUNT;++n) e->captured_generated[n]=0u;
    }
    uint64_t position=snapshot?s->capture.start_position:e->rhythm_transport.position;
    unsigned source=s->capture.source;
    if(source==THY_SEQ_SOURCE_USB) {
        if(snapshot && e->remote_record) for(unsigned channel=0;channel<16u;++channel)
            for(unsigned pitch=0;pitch<128u;++pitch) if(e->usb_input_velocity[channel][pitch])
                (void)thy_sequencer_input_on(s,THY_SEQ_SOURCE_USB,channel*128u+pitch+1u,
                    0u,0u,(uint8_t)pitch,e->usb_input_velocity[channel][pitch],position);
        return;
    }
    int pressure_due=snapshot || (uint32_t)(e->musical_clock.now_ms-e->capture_pressure_ms)>=20u;
    if(pressure_due) e->capture_pressure_ms=e->musical_clock.now_ms;
    if(source==THY_SEQ_SOURCE_PADS || source==THY_SEQ_SOURCE_STRUM || source==THY_SEQ_SOURCE_LIVE)
      for(unsigned p=0;p<96u;++p) {
        if(source==THY_SEQ_SOURCE_STRUM && !e->input_strummed[p]) continue;
        if(snapshot && source==THY_SEQ_SOURCE_PADS && !e->input_assignment[p] && e->input_identity[p])
            for(unsigned pitch=0;pitch<128u;++pitch)
                if(e->input_pitches[p][pitch/64u]&(UINT64_C(1)<<(pitch%64u)))
                    (void)thy_sequencer_input_on(s,THY_SEQ_SOURCE_PADS,note_identity(e,p,pitch),
                        (uint16_t)(e->input_identity[p]%65535u+1u),e->input_role[p],
                        (uint8_t)pitch,e->input_velocity[p],position);
        if(pressure_due && e->input_identity[p] && !e->input_assignment[p] &&
            (s->capture.source==THY_SEQ_SOURCE_PADS || s->capture.source==THY_SEQ_SOURCE_STRUM ||
             s->capture.source==THY_SEQ_SOURCE_LIVE)) {
            uint8_t value=e->instrument->sensors[p].pressure;
            /* The legacy note-pressure stream records Aftertouch. With an
             * optional split profile, the lower pressure stage is distinct. */
            thy_expression_profile profile;uint8_t custom;
            if(thy_instrument_get_expression_profile(e->instrument,(uint8_t)p,&profile,&custom)==THY_OK && profile.enabled) {
                uint8_t primary=value;
                (void)thy_instrument_expression_axes(e->instrument,(uint8_t)p,
                    e->instrument->sensors[p].raw,&primary,&value);
            }
            unsigned previous=e->captured_pressure[p];
            unsigned delta=value>previous?value-previous:previous-value;
            if(delta>=4u || (!value && previous)) {
                for(unsigned pitch=0u;pitch<128u;++pitch)
                    if(e->input_pitches[p][pitch/64u]&(UINT64_C(1)<<(pitch%64u)))
                        (void)thy_sequencer_input_event(s,s->capture.source,THY_SEQ_PRESSURE,
                            (uint8_t)pitch,value,position);
                e->captured_pressure[p]=value;
            }
        }
        if(source==THY_SEQ_SOURCE_PADS) continue;
        const thy_pad_owner *owner=&core->owners[p];
        uint64_t active[2]={0u,0u};
        if(!e->input_assignment[p]) for(unsigned k=0;k<owner->activated;++k)
            active[(thy_pad_owner_key(owner, (uint8_t)(k))&127u)/64u]|=UINT64_C(1)<<(thy_pad_owner_key(owner, (uint8_t)(k))&63u);
        if(active[0]==e->captured_strum[p][0] && active[1]==e->captured_strum[p][1]) continue;
        for(unsigned pitch=0;pitch<128u;++pitch) {
            uint64_t bit=UINT64_C(1)<<(pitch%64u);
            int before=!!(e->captured_strum[p][pitch/64u]&bit), now=!!(active[pitch/64u]&bit);
            if(before==now || !e->input_identity[p]) continue;
            {
                if(now) (void)thy_sequencer_input_on(s,(uint8_t)source,note_identity(e,p,pitch),
                    (uint16_t)(e->input_identity[p]%65535u+1u),e->input_role[p],
                    (uint8_t)pitch,owner->velocity,position);
                else (void)thy_sequencer_input_off(s,(uint8_t)source,note_identity(e,p,pitch),position);
            }
        }
        e->captured_strum[p][0]=active[0]; e->captured_strum[p][1]=active[1];
    }
    if(source==THY_SEQ_SOURCE_PADS || source==THY_SEQ_SOURCE_STRUM) return;
    int any_assignment=0;
    for(unsigned p=0;p<96u;++p) if(e->input_assignment[p]) { any_assignment=1; break; }
    for(unsigned n=0;n<THY_GENERATED_OWNER_COUNT;++n) {
        const thy_generated_owner *o=thy_core_generated_at(core,(uint16_t)n);
        uint32_t handle=o && o->source>=1u && o->source<=2u &&
            (source==THY_SEQ_SOURCE_LIVE || source==o->source) &&
            o->handle<UINT32_C(0x80000000)?o->handle:0u;
        uint32_t old=e->captured_generated[n];
        /* An editor gesture may contribute to a Hybrid phrase whose chord
         * belongs to an independent live pad. Exclude its complete output
         * lifetime, including the tail after the assigning finger releases. */
        if(handle) {
            if((old&UINT32_C(0x7fffffff))==handle) handle=old;
            else if(any_assignment && assigned_output(e,o)) handle|=UINT32_C(0x80000000);
        }
        if(old==handle) continue;
        if(old && !(old&UINT32_C(0x80000000))) for(unsigned source=THY_SEQ_SOURCE_ARP1;source<=THY_SEQ_SOURCE_LIVE;++source)
            (void)thy_sequencer_input_off(s,(uint8_t)source,UINT32_C(0x80000000)|old,position);
        if(handle && handle<UINT32_C(0x80000000)) {
            (void)thy_sequencer_input_on(s,(uint8_t)o->source,UINT32_C(0x80000000)|handle,
                0u,0u,(uint8_t)(o->keys[0]&127u),o->velocity,position);
            (void)thy_sequencer_input_on(s,THY_SEQ_SOURCE_LIVE,UINT32_C(0x80000000)|handle,
                0u,0u,(uint8_t)(o->keys[0]&127u),o->velocity,position);
        }
        e->captured_generated[n]=handle;
    }
}
void thy_engine_sequencer_tick(thy_engine *e)
{
    thy_arp_config configs[2]={e->arps[0].config,e->arps[1].config};
    (void)thy_sequencer_advance(&e->sequencer,e->instrument->core,
        &e->rhythm_transport,&e->musical_clock,configs);
    thy_engine_sequencer_capture(e);
    e->session.capture=e->sequencer.capture.state==THY_SEQ_ARMED || e->sequencer.capture.state==THY_SEQ_QUEUED?
        THY_ENGINE_CAPTURE_ARMED:e->sequencer.capture.state==THY_SEQ_RECORDING ||
        e->sequencer.capture.state==THY_SEQ_FINISHING?THY_ENGINE_RECORDING:THY_ENGINE_CAPTURE_OFF;
}
void thy_engine_sequencer_usb_clear(thy_engine *e)
{
    if(!good(e)) return;
    for(unsigned c=0;c<16u;++c) for(unsigned p=0;p<128u;++p) {
        if(e->usb_input_velocity[c][p]) (void)thy_sequencer_input_off(&e->sequencer,
            THY_SEQ_SOURCE_USB,c*128u+p+1u,e->rhythm_transport.position);
        e->usb_input_velocity[c][p]=0u;
    }
}
int thy_engine_receive_midi(thy_engine *e,const uint8_t *message,uint8_t length,uint32_t now)
{
    if(!good(e) || !message || !length) return THY_INVALID_ARGUMENT;
    if(message[0]>=0xf0u) {
        if(message[0]==0xf2u && e->remote_position && length==3u && message[1]<128u && message[2]<128u) {
            if(e->sequencer.capture.state!=THY_SEQ_CAPTURE_OFF) return THY_SEQ_BUSY;
            return thy_transport_song_position(&e->rhythm_transport,(uint16_t)(message[1]|(uint16_t)message[2]<<7),&e->musical_clock)<0?THY_INVALID_ARGUMENT:THY_OK;
        }
        return thy_engine_receive_time_deferred(e,message,length,now);
    }
    if(message[0]<0x80u || message[0]>0xefu) return THY_INVALID_ARGUMENT;
    uint8_t channel=message[0]&15u, kind=message[0]&0xf0u;
    uint8_t expected=kind==0xc0u || kind==0xd0u?2u:3u;
    if(length!=expected || message[1]>127u || (expected==3u && message[2]>127u)) return THY_INVALID_ARGUMENT;
    if(!e->remote_record || (e->remote_channel && e->remote_channel!=channel+1u)) return THY_OK;
    int result=thy_engine_advance(e,now); if(result!=THY_OK) return result;
    if(kind==0x90u && message[2]) {
        if(e->usb_input_velocity[channel][message[1]])
            (void)thy_sequencer_input_off(&e->sequencer,THY_SEQ_SOURCE_USB,
                channel*128u+message[1]+1u,e->rhythm_transport.position);
        e->usb_input_velocity[channel][message[1]]=message[2];
        return thy_sequencer_input_on(&e->sequencer,THY_SEQ_SOURCE_USB,
            channel*128u+message[1]+1u,0u,0u,message[1],message[2],e->rhythm_transport.position);
    }
    if(kind==0x80u || kind==0x90u) {
        e->usb_input_velocity[channel][message[1]]=0u;
        return thy_sequencer_input_off(&e->sequencer,THY_SEQ_SOURCE_USB,
            channel*128u+message[1]+1u,e->rhythm_transport.position);
    }
    uint8_t type=kind==0xb0u?THY_SEQ_CC:kind==0xa0u?THY_SEQ_PRESSURE:
        kind==0xc0u?THY_SEQ_PROGRAM:kind==0xd0u?THY_SEQ_CHANNEL_PRESSURE:THY_SEQ_BEND;
    return thy_sequencer_input_event(&e->sequencer,THY_SEQ_SOURCE_USB,type,message[1],length>2u?message[2]:0u,e->rhythm_transport.position);
}
int thy_engine_sequencer_color(const thy_engine *e,uint8_t logical,uint16_t rgb[3])
{
    if(!good(e) || !rgb || logical>=96u || !e->sequencer_ui.open ||
       (e->state.level!=THY_STATE_PLAY && !e->sequencer_ui.options_open)) return 0;
    uint8_t physical=logical;
    logical=thy_instrument_role_pad(e->instrument,physical);
    if(logical>=96u)return 0;
    if(!e->instrument->lights_enabled) { rgb[0]=rgb[1]=rgb[2]=0u; return 1; }
    if(thy_sequencer_ui_color(&e->sequencer_ui,logical,rgb)) {
        /* Step content uses the same selected pitch palette as the playing
         * surface. White remains reserved for its playhead/physical feedback. */
        const thy_sequencer_ui *u=&e->sequencer_ui;
        if(logical<48u && u->view==THY_SEQ_VIEW_STEPS && !u->help_open &&
           !(rgb[0]==rgb[1] && rgb[1]==rgb[2] && rgb[0]>=2000u)) {
            uint8_t step=(uint8_t)((logical%4u)*12u+logical/4u),note;
            if(thy_sequencer_ui_step_note(u,step,&note)) {
                const thy_instrument *i=e->instrument;
                uint32_t color=thy_lighting_note_color_palette(note,i->light_palette);
                for(unsigned c=0;c<3u;++c){
                    unsigned value=((color>>(16u-c*8u))&255u)*16u*i->light_idle/8u;
                    if(step==u->selected_step)value+=350u;
                    rgb[c]=(uint16_t)(value>4095u?4095u:value);
                }
            }
        }
        return 1;
    }
    uint8_t track,pitch; uint32_t tick;
    int pad_role=thy_sequencer_ui_pad_role(&e->sequencer_ui,logical,&track,&tick,&pitch);
    int input=pad_role==THY_SEQ_PAD_INPUT_NOTES?1:pad_role==THY_SEQ_PAD_INPUT_CHORDS?2:0;
    if(!input) return 0;
    uint8_t source=(uint8_t)(logical%48u+(input==1?48u:0u));
    const thy_instrument *i=e->instrument;
    unsigned electrical=0u;
    while(electrical<96u && i->mapping[electrical]!=physical) ++electrical;
    if(electrical>=96u) return 0;
    int held=e->input_identity[electrical]!=0u;
    int16_t notes[THY_MAX_NOTES]; size_t count=0u;
    int note=-1;
    if(held) {
        const thy_pad_owner *owner=&i->core->owners[electrical];
        note=owner->count?(int)(thy_pad_owner_key(owner, (uint8_t)(0))&127u):i->held_input_first[electrical];
        if(input==2 && i->light_chord_mode==THY_LIGHTING_CHORD_BASS) {
            note=owner->count?note:i->held_input_bass[electrical];
            for(unsigned n=1u;n<owner->count;++n) if((int)(thy_pad_owner_key(owner, (uint8_t)(n))&127u)<note) note=thy_pad_owner_key(owner, (uint8_t)(n))&127u;
        }
    } else if(thy_instrument_preview_source(i,source,notes,&count)==THY_OK && count) {
        note=notes[0];
        if(input==2 && i->light_chord_mode==THY_LIGHTING_CHORD_BASS)
            for(size_t n=1u;n<count;++n) if(notes[n]<note) note=notes[n];
    }
    if(input==2 && i->light_chord_mode!=THY_LIGHTING_CHORD_BASS) {
        int16_t root; unsigned degree=UINT8_MAX;
        int valid=0;
        if(held && i->held_color_valid[electrical]) {
            root=i->held_color_root[electrical]; degree=i->held_color_degree[electrical]; valid=1;
        } else if(!held && thy_lighting_chord_root(i,source,&root)==THY_OK) {
            uint8_t resolved=THY_CHORD_NO_DEGREE;
            (void)thy_instrument_chord_degree(i,source,&resolved);degree=resolved;valid=1;
        }
        if(valid) {
            static const uint8_t degrees[7]={0u,2u,4u,5u,7u,9u,11u};
            note=i->light_chord_mode==THY_LIGHTING_CHORD_DEGREE && degree<7u?degrees[degree]:root;
        }
    }
    uint32_t color=note<0?0u:thy_lighting_note_color_palette((uint8_t)note,i->light_palette);
    unsigned level=held && i->light_held_feedback?i->light_held:i->light_idle;
    if(held && i->light_held_feedback && i->light_held_style==THY_LIGHTING_HELD_WHITE && color) color=UINT32_C(0xffffff);
    if(!held && input==1 && note>=0) {
        unsigned pc=(unsigned)note%12u;
        if(i->light_key_contrast && i->performance.melody_layout!=THY_LAYOUT_ALL_LIT &&
            (pc==1u || pc==3u || pc==6u || pc==8u || pc==10u))
            level=(level*i->light_key_contrast_percent+50u)/100u;
        if(i->performance.melody_layout==THY_LAYOUT_IN_KEY_LIT) {
            if(!thy_instrument_note_in_key(i,note)) color=0u;
        }
    }
    for(unsigned c=0;c<3u;++c) rgb[c]=(uint16_t)(((color>>(16u-c*8u))&255u)*16u*level/8u);
    return 1;
}

int thy_engine_sequencer_read(const thy_engine *e,uint16_t row,uint8_t instance,int32_t *value)
{
    if(!good(e) || !value || instance>=THY_SEQ_TRACKS) return THY_STATE_INVALID;
    const thy_sequencer *s=&e->sequencer; const thy_sequencer_ui *u=&e->sequencer_ui;
    const thy_seq_track *t=&s->tracks[instance];
    const thy_seq_event *event=thy_sequencer_find(s,u->selected_event);
    switch(row) {
    case THY_ROW_SEQUENCER_TEMPO: *value=(int32_t)(e->musical_clock.tempo_milli_bpm/1000u); break;
    case THY_ROW_SEQUENCER_LENGTH: {
        uint32_t length=(t->loop_end-t->loop_start+t->grid_ticks-1u)/t->grid_ticks;
        *value=(int32_t)(length>48u?48u:length);break;
    }
    case THY_ROW_SEQUENCER_TRACK: *value=s->selected_track+1; break;
    case THY_ROW_SEQUENCER_ACTIVE: *value=e->rhythm_transport.tracks[instance].enabled; break;
    case THY_ROW_SEQUENCER_MUTE: *value=t->muted; break;
    case THY_ROW_SEQUENCER_INTERACTION: *value=t->record_policy; break;
    case THY_ROW_SEQUENCER_SOURCE: *value=t->record_source; break;
    case THY_ROW_SEQUENCER_PROCESSOR: *value=t->processor; break;
    case THY_ROW_SEQUENCER_PLAYMODE: *value=!!(t->flags&THY_SEQ_FLAG_ONESHOT); break;
    case THY_ROW_SEQUENCER_QUANTIZE: *value=(t->flags>>1)&7u; break;
    case THY_ROW_SEQUENCER_RECORDCONTROLLERS: *value=!!(t->flags&THY_SEQ_FLAG_RECORD_CONTROLLERS); break;
    case THY_ROW_SEQUENCER_SENDPRESSURE: *value=!!(t->flags&THY_SEQ_FLAG_SEND_PRESSURE); break;
    case THY_ROW_SEQUENCER_CHANNEL: *value=t->channel+1; break;
    case THY_ROW_SEQUENCER_DESTINATIONS:
        for(unsigned n=0;n<sizeof routes;++n) if(routes[n]==t->destinations) { *value=(int32_t)n; return THY_STATE_OK; }
        return THY_STATE_INVALID;
    case THY_ROW_SEQUENCER_LAYOUT: *value=u->layout; break;
    case THY_ROW_SEQUENCER_SWAP: *value=u->swapped; break;
    case THY_ROW_SEQUENCER_INPUT: *value=u->input_layout; break;
    case THY_ROW_SEQUENCER_GRID:
        *value=2; for(unsigned n=0;n<4u;++n) if(t->grid_ticks==grids[n]) *value=(int32_t)n; break;
    case THY_ROW_SEQUENCER_LAUNCH: *value=e->rhythm_transport.launch_quantize; break;
    case THY_ROW_SEQUENCER_LOOPSTART: *value=(int32_t)t->loop_start; break;
    case THY_ROW_SEQUENCER_LOOPEND: *value=(int32_t)(t->loop_end?t->loop_end:3840u); break;
    case THY_ROW_SEQUENCER_REGION: *value=u->region_enabled; break;
    case THY_ROW_SEQUENCER_REGIONSTART: *value=(int32_t)u->region_start; break;
    case THY_ROW_SEQUENCER_REGIONEND: *value=(int32_t)(u->region_end?u->region_end:3840u); break;
    case THY_ROW_SEQUENCER_STEPLENGTH: *value=event?(int32_t)event->duration:(int32_t)u->grid_ticks; break;
    case THY_ROW_SEQUENCER_STEPVELOCITY: *value=event?event->value:96; break;
    default: *value=0; break;
    }
    return THY_STATE_OK;
}
int thy_engine_sequencer_write(thy_engine *e,uint16_t row,uint8_t instance,int32_t value)
{
    if(!good(e) || row>=THY_MENU_ROW_COUNT || instance>=THY_SEQ_TRACKS || value<0) return THY_STATE_INVALID;
    thy_sequencer *s=&e->sequencer; thy_sequencer_ui *u=&e->sequencer_ui;
    uint8_t target=u->options_open?u->options_track:s->selected_track;
    if(u->options_open && thy_menu_rows[row].instance_kind==THY_MENU_TRACK &&
       instance!=target)return THY_STATE_BLOCKED;
    thy_seq_track t=s->tracks[instance]; int result=THY_OK;
    switch(row) {
    case THY_ROW_SEQUENCER_OPEN: return thy_engine_sequencer_open(e,THY_SEQ_VIEW_TRACKS);
    case THY_ROW_SEQUENCER_HELP: return state_result(thy_engine_sequencer_action(e,THY_SEQ_UI_HELP,s->selected_track));
    case THY_ROW_SEQUENCER_TEMPO:
        return state_result(thy_engine_clock_configure(e,(uint32_t)value*1000u,e->musical_clock.source,
            e->musical_clock.numerator,e->musical_clock.denominator,e->rhythm_transport.launch_quantize));
    case THY_ROW_SEQUENCER_LENGTH:
        if(value<1 || value>48 || instance!=target)return THY_STATE_INVALID;
        return state_result(thy_sequencer_ui_configure(u,THY_SEQ_UI_STEP_COUNT,(uint32_t)value));
    case THY_ROW_SEQUENCER_STEPS: return thy_engine_sequencer_open(e,THY_SEQ_VIEW_STEPS);
    case THY_ROW_SEQUENCER_TRACK:
        if(u->options_open)return THY_STATE_BLOCKED;
        return state_result(thy_sequencer_select(s,(uint8_t)(value-1)));
    case THY_ROW_SEQUENCER_PLAY: return state_result(thy_engine_transport_start(e));
    case THY_ROW_SEQUENCER_STOP: return state_result(thy_engine_transport_stop(e));
    case THY_ROW_SEQUENCER_RECORD: return state_result(thy_engine_sequencer_record(e));
    case THY_ROW_SEQUENCER_SAVE: e->song_save_requested=1u; e->song_status=1u; return THY_STATE_OK;
    case THY_ROW_SEQUENCER_RECALL: e->song_recall_requested=1u; return THY_STATE_OK;
    case THY_ROW_SEQUENCER_UNDO: return state_result(thy_sequencer_history(s,e->instrument->core,&e->rhythm_transport,0));
    case THY_ROW_SEQUENCER_REDO: return state_result(thy_sequencer_history(s,e->instrument->core,&e->rhythm_transport,1));
    case THY_ROW_SEQUENCER_CLEARTRACK: return state_result(thy_sequencer_clear(s,target));
    case THY_ROW_SEQUENCER_CLEARSONG:
        if(s->capture.state!=THY_SEQ_CAPTURE_OFF) return THY_STATE_BLOCKED;
        (void)thy_engine_transport_stop(e);
        result=thy_sequencer_transaction_begin(s);
        if(result!=THY_OK) return state_result(result);
        for(unsigned n=0u;n<THY_SEQ_TRACKS;++n) {
            (void)thy_sequencer_clear(s,(uint8_t)n);s->tracks[n].flags&=(uint8_t)~THY_SEQ_FLAG_CREATED;
        }
        thy_sequencer_transaction_end(s); return THY_STATE_OK;
    case THY_ROW_SEQUENCER_DUPLICATE:
        t=s->tracks[target];
        if(t.loop_end-t.loop_start>24u*t.grid_ticks)return THY_STATE_BLOCKED;
        return state_result(thy_sequencer_duplicate(s,target,u->region_enabled?u->region_start:t.loop_start,
            u->region_enabled?u->region_end:t.loop_end,t.loop_end,1));
    case THY_ROW_SEQUENCER_TRIM:
        t=s->tracks[target];
        return state_result(thy_sequencer_trim(s,target,u->region_enabled?u->region_start:t.loop_start,
            u->region_enabled?u->region_end:t.loop_end));
    case THY_ROW_SEQUENCER_ACTIVE:
        t.flags=(uint8_t)(value?t.flags&~THY_SEQ_FLAG_DISABLED:t.flags|THY_SEQ_FLAG_DISABLED);
        break;
    case THY_ROW_SEQUENCER_LAYOUT: u->layout=(uint8_t)value; ++u->revision; return THY_STATE_OK;
    case THY_ROW_SEQUENCER_SWAP: u->swapped=(uint8_t)value; ++u->revision; return THY_STATE_OK;
    case THY_ROW_SEQUENCER_INPUT: u->input_layout=(uint8_t)value; ++u->revision; return THY_STATE_OK;
    case THY_ROW_SEQUENCER_GRID:
        if(value>3 || instance!=target)return THY_STATE_INVALID;
        return state_result(thy_sequencer_ui_configure(u,THY_SEQ_UI_GRID,grids[value]));
    case THY_ROW_SEQUENCER_LAUNCH:
        return state_result(thy_transport_set_quantize(&e->rhythm_transport,(uint8_t)value)<0?THY_INVALID_ARGUMENT:THY_OK);
    case THY_ROW_SEQUENCER_LOOPSTART:
        return state_result(thy_engine_sequencer_loop(e,instance,(uint32_t)value,t.loop_end?t.loop_end:3840u));
    case THY_ROW_SEQUENCER_LOOPEND: return state_result(thy_engine_sequencer_loop(e,instance,t.loop_start,(uint32_t)value));
    case THY_ROW_SEQUENCER_REGION: u->region_enabled=(uint8_t)value; return THY_STATE_OK;
    case THY_ROW_SEQUENCER_REGIONSTART: if(u->region_end && (uint32_t)value>=u->region_end) return THY_STATE_INVALID; u->region_start=(uint32_t)value; return THY_STATE_OK;
    case THY_ROW_SEQUENCER_REGIONEND: if((uint32_t)value<=u->region_start) return THY_STATE_INVALID; u->region_end=(uint32_t)value; return THY_STATE_OK;
    case THY_ROW_SEQUENCER_STEPLENGTH:
    case THY_ROW_SEQUENCER_STEPVELOCITY: {
        uint32_t id=e->state.editing && e->state.edit_row==row?e->state.edit_event:u->selected_event;
        const thy_seq_event *event=thy_sequencer_find(s,id);
        if(!event || event->track!=target) return THY_STATE_UNAVAILABLE;
        return state_result(thy_sequencer_update(s,event->id,event->tick,
            row==THY_ROW_SEQUENCER_STEPLENGTH?(uint32_t)value:event->duration,
            event->pitch,row==THY_ROW_SEQUENCER_STEPVELOCITY?(uint8_t)value:event->value));
    }
    case THY_ROW_SEQUENCER_CHANNEL: t.channel=(uint8_t)(value-1); break;
    case THY_ROW_SEQUENCER_DESTINATIONS: if(value>6) return THY_STATE_INVALID; t.destinations=routes[value]; break;
    case THY_ROW_SEQUENCER_SOURCE: t.record_source=(uint8_t)value; break;
    case THY_ROW_SEQUENCER_INTERACTION: t.record_policy=(uint8_t)value; break;
    case THY_ROW_SEQUENCER_MUTE: t.muted=(uint8_t)value; break;
    case THY_ROW_SEQUENCER_PROCESSOR: t.processor=(uint8_t)value; break;
    case THY_ROW_SEQUENCER_PLAYMODE: t.flags=(uint8_t)((t.flags&~1u)|(unsigned)value); break;
    case THY_ROW_SEQUENCER_QUANTIZE: if(value>4) return THY_STATE_INVALID; t.flags=(uint8_t)((t.flags&~14u)|((unsigned)value<<1)); break;
    case THY_ROW_SEQUENCER_RECORDCONTROLLERS:
    case THY_ROW_SEQUENCER_SENDPRESSURE: {
        unsigned bit=row==THY_ROW_SEQUENCER_RECORDCONTROLLERS?THY_SEQ_FLAG_RECORD_CONTROLLERS:THY_SEQ_FLAG_SEND_PRESSURE;
        t.flags=(uint8_t)(value?t.flags|bit:t.flags&~bit); break;
    }
    default: result=THY_INVALID_ARGUMENT; break;
    }
    if(result==THY_OK) result=thy_engine_sequencer_track_configure(e,instance,&t);
    if(result==THY_OK && row==THY_ROW_SEQUENCER_ACTIVE)
        (void)thy_transport_set_enabled(&e->rhythm_transport,instance,(uint8_t)value);
    return state_result(result);
}
