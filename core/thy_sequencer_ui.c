/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_sequencer_ui.h"

static void zero(void *p,size_t n) { uint8_t *b=p;while(n--) *b++=0u; }
static int report(thy_sequencer_ui *u,int r)
{ u->last_error=(uint8_t)r;++u->revision;return r; }
static int command(thy_sequencer_ui *u,uint8_t kind,uint8_t track)
{ return report(u,u->command?u->command(u->context,kind,track):THY_SEQ_BUSY); }
static uint8_t selected(const thy_sequencer_ui *u)
{ return u->options_open?u->options_track:u->sequencer->selected_track; }
static int exists(const thy_sequencer_ui *u,uint8_t t)
{ return thy_sequencer_track_exists(u->sequencer,t); }
static uint32_t grid(const thy_sequencer_ui *u,uint8_t t)
{ return u->sequencer->tracks[t].grid_ticks?u->sequencer->tracks[t].grid_ticks:240u; }
static int capturing(const thy_sequencer_ui *u)
{
    unsigned s=u->sequencer->capture.state;
    return s>=THY_SEQ_ARMED && s<=THY_SEQ_FINISHING;
}
static uint8_t physical_step(unsigned p) { return (uint8_t)((p%4u)*12u+(p%48u)/4u); }
static uint8_t step_pad(unsigned s) { return (uint8_t)((s%12u)*4u+s/12u); }
uint32_t thy_sequencer_ui_step_tick(const thy_sequencer_ui *u,uint8_t step)
{
    if(!u || !u->sequencer || step>=48u) return THY_SEQ_MAX_TICK;
    uint32_t start=u->sequencer->tracks[selected(u)].loop_start;
    uint32_t offset=(uint32_t)step*grid(u,selected(u));
    return offset>THY_SEQ_MAX_TICK-start?THY_SEQ_MAX_TICK:start+offset;
}
static const thy_seq_event *first_note(const thy_sequencer_ui *u,
    uint8_t track,uint32_t tick,uint32_t span)
{
    const thy_seq_event *found=NULL;
    for(unsigned n=0;n<u->sequencer->count;++n) {
        const thy_seq_event *e=&u->sequencer->events[n];
        if(e->track==track && (e->kind&127u)==THY_SEQ_NOTE &&
            e->tick>=tick && e->tick-tick<span && (!found || e->pitch<found->pitch)) found=e;
    }
    return found;
}
int thy_sequencer_ui_step_note(const thy_sequencer_ui *u,uint8_t step,uint8_t *pitch)
{
    if(!u || !u->sequencer || !pitch || step>=48u) return 0;
    const thy_seq_event *e=first_note(u,selected(u),thy_sequencer_ui_step_tick(u,step),grid(u,selected(u)));
    if(!e) return 0;
    *pitch=e->pitch;return 1;
}
static void select_step(thy_sequencer_ui *u,unsigned step)
{
    u->selected_step=(uint8_t)(step%48u);u->selected_pad=step_pad(u->selected_step);
    u->grid_ticks=grid(u,selected(u));u->window_tick=u->sequencer->tracks[selected(u)].loop_start;
    u->overdub=u->sequencer->tracks[selected(u)].record_policy==THY_SEQ_OVERDUB;
    const thy_seq_event *e=first_note(u,selected(u),thy_sequencer_ui_step_tick(u,u->selected_step),u->grid_ticks);
    u->selected_event=e?e->id:0u;
}
static void end_gesture(thy_sequencer_ui *u)
{
    if(u->gesture_active) thy_sequencer_transaction_end(u->sequencer);
    u->gesture_active=u->gesture_written=0u;
    for(unsigned p=0;p<96u;++p) u->pads[p].cell_kind=0u;
}
static int contributors(const thy_sequencer_ui *u)
{ for(unsigned p=48u;p<96u;++p) if(u->pads[p].held && u->pads[p].cell_kind) return 1;return 0; }
static int input_held(const thy_sequencer_ui *u)
{
    for(unsigned p=0;p<96u;++p) if(u->pads[p].held &&
        (u->pads[p].role==THY_SEQ_PAD_INPUT_NOTES || u->pads[p].role==THY_SEQ_PAD_INPUT_CHORDS)) return 1;
    return 0;
}
static void normalize_track(thy_sequencer_ui *u)
{
    if(exists(u,selected(u))) return;
    for(unsigned t=0;t<THY_SEQ_TRACKS;++t) if(exists(u,(uint8_t)t)) {
        (void)thy_sequencer_select(u->sequencer,(uint8_t)t);u->track_new=0u;return;
    }
    u->track_new=1u;
}
static void move_track(thy_sequencer_ui *u,int direction)
{
    uint8_t rows[THY_SEQ_TRACKS+1u];unsigned count=0u,cursor=0u;
    for(unsigned t=0;t<THY_SEQ_TRACKS;++t) if(exists(u,(uint8_t)t)) {
        if(!u->track_new && t==selected(u)) cursor=count;
        rows[count++]=(uint8_t)t;
    }
    if(count<THY_SEQ_TRACKS) { if(u->track_new) cursor=count;rows[count++]=255u; }
    if(!count) return;
    cursor=(cursor+count+(direction<0?-1:1))%count;
    u->track_new=rows[cursor]==255u;
    if(!u->track_new) (void)thy_sequencer_select(u->sequencer,rows[cursor]);
    select_step(u,u->selected_step);
}
static uint8_t role(const thy_sequencer_ui *u,unsigned pad,uint8_t *track,uint32_t *tick,uint8_t *pitch)
{
    *track=selected(u);*tick=thy_sequencer_ui_step_tick(u,physical_step(pad));*pitch=u->base_pitch;
    if(u->view==THY_SEQ_VIEW_TRACKS || u->help_open || !exists(u,*track)) return THY_SEQ_PAD_SEPARATOR;
    if(pad>=48u) {
        *tick=thy_sequencer_ui_step_tick(u,u->selected_step);
        return u->input_layout==THY_SEQ_INPUT_CHORDS?THY_SEQ_PAD_INPUT_CHORDS:THY_SEQ_PAD_INPUT_NOTES;
    }
    return THY_SEQ_PAD_EDITOR;
}
void thy_sequencer_ui_init(thy_sequencer_ui *u,thy_sequencer *s,
    thy_transport *t,const thy_clock *c,thy_seq_ui_command_fn fn,void *ctx)
{
    if(!u) return;
    zero(u,sizeof *u);u->sequencer=s;u->transport=t;u->clock=c;u->command=fn;u->context=ctx;
    u->grid_ticks=240u;u->base_pitch=60u;u->region_end=3840u;
}
void thy_sequencer_ui_open(thy_sequencer_ui *u,uint8_t view)
{
    if(!u || !u->sequencer || view>THY_SEQ_VIEW_STEPS) return;
    u->track_new=u->help_open=u->delete_pending=u->options_open=0u;normalize_track(u);
    u->open=1u;u->view=u->track_new?THY_SEQ_VIEW_TRACKS:view;
    u->layout=THY_SEQ_SPLIT;u->swapped=0u;u->last_error=0u;select_step(u,u->selected_step);++u->revision;
}
void thy_sequencer_ui_close(thy_sequencer_ui *u)
{
    if(!u) return;
    for(unsigned p=0;p<96u;++p) if(u->pads[p].held) u->pads[p].consumed=1u;
    end_gesture(u);u->open=u->modifier=u->help_open=u->delete_pending=u->options_open=0u;++u->revision;
}
int thy_sequencer_ui_configure(thy_sequencer_ui *u,uint8_t field,uint32_t value)
{
    if(!u || !u->sequencer) return THY_INVALID_ARGUMENT;
    switch(field) {
    case THY_SEQ_UI_LAYOUT:if(value!=THY_SEQ_SPLIT) return THY_INVALID_ARGUMENT;u->layout=0u;break;
    case THY_SEQ_UI_SWAPPED:if(value) return THY_INVALID_ARGUMENT;u->swapped=0u;break;
    case THY_SEQ_UI_INPUT:if(value>THY_SEQ_INPUT_CHORDS) return THY_INVALID_ARGUMENT;u->input_layout=(uint8_t)value;break;
    case THY_SEQ_UI_GRID: {
        if(value!=120u && value!=240u && value!=480u && value!=960u) return THY_INVALID_ARGUMENT;
        if(u->gesture_active || capturing(u)) return report(u,THY_SEQ_BUSY);
        int r=thy_sequencer_grid(u->sequencer,u->transport,selected(u),value);
        if(r==THY_OK) { select_step(u,u->selected_step);(void)command(u,THY_SEQ_UI_LOOP_CHANGED,selected(u)); }
        return report(u,r);
    }
    case THY_SEQ_UI_WINDOW:if(value) return THY_INVALID_ARGUMENT;break;
    case THY_SEQ_UI_PITCH:if(value>120u) return THY_INVALID_ARGUMENT;u->base_pitch=(uint8_t)value;break;
    case THY_SEQ_UI_REGION:if(value>1u) return THY_INVALID_ARGUMENT;u->region_enabled=(uint8_t)value;break;
    case THY_SEQ_UI_REGION_START:if(value>THY_SEQ_MAX_TICK) return THY_INVALID_ARGUMENT;u->region_start=value;break;
    case THY_SEQ_UI_REGION_END:if(value>THY_SEQ_MAX_TICK) return THY_INVALID_ARGUMENT;u->region_end=value;break;
    case THY_SEQ_UI_SELECTED_STEP:if(value>=48u) return THY_INVALID_ARGUMENT;u->selected_step=(uint8_t)value;break;
    case THY_SEQ_UI_OVERDUB_VALUE:
        if(value>1u) return THY_INVALID_ARGUMENT;
        if(u->gesture_active || capturing(u)) return report(u,THY_SEQ_BUSY);
        return thy_sequencer_ui_set_overdub(u,(int)value);
    case THY_SEQ_UI_STEP_COUNT: {
        if(!value || value>48u || u->track_new) return THY_INVALID_ARGUMENT;
        if(u->gesture_active || capturing(u)) return report(u,THY_SEQ_BUSY);
        uint32_t start=u->sequencer->tracks[selected(u)].loop_start;
        if(value>(THY_SEQ_MAX_TICK-start)/grid(u,selected(u))) return THY_INVALID_ARGUMENT;
        int r=thy_sequencer_loop(u->sequencer,u->transport,selected(u),start,start+value*grid(u,selected(u)));
        if(r==THY_OK) (void)command(u,THY_SEQ_UI_LOOP_CHANGED,selected(u));
        return report(u,r);
    }
    default:return THY_INVALID_ARGUMENT;
    }
    select_step(u,u->selected_step);return report(u,THY_OK);
}
int thy_sequencer_ui_navigate(thy_sequencer_ui *u,uint8_t action)
{
    if(!u || !u->open || action>THY_STATE_RIGHT) return 0;
    if(action==THY_STATE_SELECT) {
        thy_sequencer_ui_close(u);(void)command(u,THY_SEQ_UI_PLAY,selected(u));return 1;
    }
    if(u->delete_pending) {
        if(action==THY_STATE_RIGHT) {
            if(input_held(u) || u->gesture_active) (void)report(u,THY_SEQ_BUSY);
            else { (void)command(u,THY_SEQ_UI_DELETE_TRACK,selected(u));normalize_track(u);select_step(u,0u); }
        }
        u->delete_pending=0u;++u->revision;return 1;
    }
    if(u->help_open) {
        if(action==THY_STATE_LEFT) u->help_open=0u;
        else if(action==THY_STATE_UP) u->help_page=(uint8_t)((u->help_page+4u)%5u);
        else if(action==THY_STATE_DOWN || action==THY_STATE_RIGHT) u->help_page=(uint8_t)((u->help_page+1u)%5u);
    } else if(action==THY_STATE_LEFT) {
        if(u->view==THY_SEQ_VIEW_STEPS) u->view=THY_SEQ_VIEW_TRACKS;
        else if(!u->track_new) u->delete_pending=1u;
    } else if(u->view==THY_SEQ_VIEW_TRACKS) {
        if(action==THY_STATE_UP || action==THY_STATE_DOWN) move_track(u,action==THY_STATE_UP?-1:1);
        else if(action==THY_STATE_RIGHT) {
            if(u->track_new && (input_held(u) || u->gesture_active)) (void)report(u,THY_SEQ_BUSY);
            else {
                int r=u->track_new?command(u,THY_SEQ_UI_CREATE_TRACK,selected(u)):THY_OK;
                if(r==THY_OK && exists(u,selected(u))) {
                    u->track_new=0u;u->view=THY_SEQ_VIEW_STEPS;select_step(u,0u);
                }
            }
        }
    } else if(action==THY_STATE_UP || action==THY_STATE_DOWN)
        select_step(u,(u->selected_step+(action==THY_STATE_UP?47u:1u))%48u);
    else if(action==THY_STATE_RIGHT) (void)command(u,THY_SEQ_UI_TRACK_OPTIONS,selected(u));
    ++u->revision;return 1;
}
int thy_sequencer_ui_control(thy_sequencer_ui *u,uint8_t bank,uint8_t index,int pressed)
{
    if(!u || bank!=3u || index>=12u) return 0;
    uint16_t bit=(uint16_t)(1u<<index);int was=(u->control_down&bit)!=0u;
    if(!pressed) { u->control_down&=(uint16_t)~bit;return was || u->open; }
    if(!u->open) return 0;
    if(was) return 1;
    u->control_down|=bit;
    if(u->help_open && index!=8u) u->help_open=0u;
    u->delete_pending=0u;uint8_t track=selected(u);
    if(index==0u) (void)command(u,THY_SEQ_UI_PLAY_ALL,track);
    else if(index==1u) { if(!u->track_new) { end_gesture(u);(void)command(u,THY_SEQ_UI_RECORD,track); } }
    else if(index==2u) { if(!u->track_new) (void)command(u,THY_SEQ_UI_PLAY_TRACK,track); }
    else if(index==3u) { if(u->view==THY_SEQ_VIEW_STEPS) (void)command(u,THY_SEQ_UI_CLEAR_STEP,track); }
    else if(index==4u) { if(!u->track_new) (void)command(u,THY_SEQ_UI_OVERDUB,track); }
    else if(index==5u) (void)command(u,THY_SEQ_UI_INPUT_TOGGLE,track);
    else if(index==6u || index==7u) {
        if(u->gesture_active) (void)report(u,THY_SEQ_BUSY);
        else { (void)command(u,index==6u?THY_SEQ_UI_UNDO:THY_SEQ_UI_REDO,track);normalize_track(u);select_step(u,u->selected_step); }
    } else if(index==8u) { u->help_open=1u;u->help_page=0u;(void)command(u,THY_SEQ_UI_HELP,track); }
    else if(index==9u || index==10u)
        (void)command(u,index==9u?THY_SEQ_UI_OCTAVE_DOWN:THY_SEQ_UI_OCTAVE_UP,track);
    else (void)command(u,THY_SEQ_UI_INPUT_CONTEXT,track);
    ++u->revision;return 1;
}
int thy_sequencer_ui_pad(thy_sequencer_ui *u,uint8_t pad,int pressed)
{
    if(!u || !u->sequencer || pad>=96u) return 0;
    thy_seq_pad_gesture *g=&u->pads[pad];
    if(!pressed) {
        if(!g->held) return 0;
        unsigned old=g->role;g->held=g->cell_kind=0u;
        if(u->gesture_active && !contributors(u)) end_gesture(u);
        return old!=THY_SEQ_PAD_INPUT_NOTES && old!=THY_SEQ_PAD_INPUT_CHORDS;
    }
    if(g->held) return g->role!=THY_SEQ_PAD_INPUT_NOTES && g->role!=THY_SEQ_PAD_INPUT_CHORDS;
    if(!u->open) return 0;
    zero(g,sizeof *g);g->role=role(u,pad,&g->track,&g->tick,&g->pitch);
    g->duration=grid(u,g->track);g->held=1u;
    if(g->role==THY_SEQ_PAD_EDITOR) {
        select_step(u,physical_step(pad));u->delete_pending=0u;++u->revision;return 1;
    }
    if(g->role!=THY_SEQ_PAD_INPUT_NOTES && g->role!=THY_SEQ_PAD_INPUT_CHORDS) return 1;
    if(!capturing(u)) {
        if(!u->gesture_active) {
            int r=thy_sequencer_transaction_begin(u->sequencer);
            if(r!=THY_OK) { (void)report(u,r);return 0; }
            u->edit_track=g->track;u->edit_tick=g->tick;u->edit_duration=g->duration;u->edit_overdub=u->overdub;
            u->gesture_active=1u;u->gesture_written=0u;
        }
        g->track=u->edit_track;g->tick=u->edit_tick;g->duration=u->edit_duration;g->cell_kind=1u;
    }
    return 0;
}
int thy_sequencer_ui_input(const thy_sequencer_ui *u,uint8_t pad)
{
    if(!u || pad>=96u || !u->pads[pad].held) return 0;
    return u->pads[pad].role==THY_SEQ_PAD_INPUT_NOTES?1:u->pads[pad].role==THY_SEQ_PAD_INPUT_CHORDS?2:0;
}
int thy_sequencer_ui_pad_role(const thy_sequencer_ui *u,uint8_t pad,uint8_t *track,uint32_t *tick,uint8_t *pitch)
{
    if(!u || !u->sequencer || pad>=96u || !track || !tick || !pitch) return THY_SEQ_PAD_NONE;
    if(u->pads[pad].held) {
        *track=u->pads[pad].track;*tick=u->pads[pad].tick;*pitch=u->pads[pad].pitch;return u->pads[pad].role;
    }
    return u->open?role(u,pad,track,tick,pitch):THY_SEQ_PAD_NONE;
}
int thy_sequencer_ui_assign(thy_sequencer_ui *u,uint8_t source,const int16_t *notes,size_t count,uint8_t velocity,uint8_t chord)
{
    if(!u || !u->open || !notes || !count || source>=96u || !thy_sequencer_ui_input(u,source) ||
        !u->gesture_active || !u->pads[source].cell_kind || capturing(u)) return 0;
    const thy_seq_pad_gesture *g=&u->pads[source];
    if(count>28u) { (void)report(u,THY_SEQ_FULL);return 1; }
    thy_seq_event events[28];unsigned admitted=0u;
    for(size_t n=0;n<count;++n) if(notes[n]>=0 && notes[n]<=127) {
        int duplicate=0;
        for(unsigned k=0;k<admitted;++k) if(events[k].pitch==(uint8_t)notes[n]) duplicate=1;
        if(!duplicate) events[admitted++]=(thy_seq_event){g->tick,g->duration,0u,chord?1u:0u,g->track,
            (uint8_t)(THY_SEQ_NOTE|(chord?128u:0u)),(uint8_t)notes[n],velocity};
    }
    if(!admitted) return 1;
    int r=thy_sequencer_assign(u->sequencer,g->track,g->tick,g->duration,events,(uint16_t)admitted,
        !u->gesture_written && !u->edit_overdub?2:0);
    if(r==THY_OK) {
        u->gesture_written=1u;
        if(g->tick+g->duration>u->sequencer->tracks[g->track].loop_end)
            r=thy_sequencer_loop(u->sequencer,u->transport,g->track,u->sequencer->tracks[g->track].loop_start,g->tick+g->duration);
        if(r==THY_OK) (void)command(u,THY_SEQ_UI_LOOP_CHANGED,g->track);
        select_step(u,u->selected_step);
    }
    (void)report(u,r);return 1;
}
int thy_sequencer_ui_clear_step(thy_sequencer_ui *u)
{
    if(!u || !u->sequencer || u->view!=THY_SEQ_VIEW_STEPS || u->track_new) return THY_INVALID_ARGUMENT;
    if(u->gesture_active || capturing(u)) return report(u,THY_SEQ_BUSY);
    uint8_t t=selected(u);uint32_t tick=thy_sequencer_ui_step_tick(u,u->selected_step);
    int r=thy_sequencer_transaction_begin(u->sequencer);
    if(r!=THY_OK) return report(u,r);
    for(unsigned n=0;n<u->sequencer->count;) {
        const thy_seq_event *e=&u->sequencer->events[n];
        if(e->track==t && e->tick>=tick && e->tick-tick<grid(u,t)) {
            r=thy_sequencer_remove(u->sequencer,e->id);if(r!=THY_OK) break;
        } else ++n;
    }
    thy_sequencer_transaction_end(u->sequencer);select_step(u,u->selected_step);
    if(r==THY_OK) (void)command(u,THY_SEQ_UI_LOOP_CHANGED,t);
    return report(u,r);
}
int thy_sequencer_ui_set_overdub(thy_sequencer_ui *u,int enabled)
{
    if(!u || !u->sequencer || (enabled!=0 && enabled!=1) || !exists(u,selected(u))) return THY_INVALID_ARGUMENT;
    if(u->gesture_active || capturing(u)) return report(u,THY_SEQ_BUSY);
    thy_seq_track *track=&u->sequencer->tracks[selected(u)];
    uint8_t policy=enabled?THY_SEQ_OVERDUB:THY_SEQ_REPLACE;
    if(track->record_policy!=policy) { track->record_policy=policy;++track->revision;++u->sequencer->revision; }
    u->overdub=(uint8_t)enabled;return report(u,THY_OK);
}
int thy_sequencer_ui_color(const thy_sequencer_ui *u,uint8_t pad,uint16_t rgb[3])
{
    if(!u || !u->open || !u->sequencer || !rgb || pad>=96u) return 0;
    uint8_t t,pitch;uint32_t tick;
    unsigned kind=(unsigned)thy_sequencer_ui_pad_role(u,pad,&t,&tick,&pitch);
    if(u->view==THY_SEQ_VIEW_TRACKS || u->help_open) { rgb[0]=rgb[1]=rgb[2]=0u;return 1; }
    if(kind==THY_SEQ_PAD_INPUT_NOTES || kind==THY_SEQ_PAD_INPUT_CHORDS) return 0;
    rgb[0]=rgb[1]=rgb[2]=0u;
    if(kind!=THY_SEQ_PAD_EDITOR) return 1;
    const thy_seq_event *e=first_note(u,t,tick,grid(u,t));
    if(e) {
        static const uint8_t colors[12][3]={{0,255,0},{85,255,0},{255,255,0},{255,85,0},
            {255,0,0},{255,0,85},{255,0,255},{85,0,255},{0,0,255},{0,85,255},{0,255,255},{0,255,85}};
        for(unsigned c=0;c<3u;++c) rgb[c]=(uint16_t)colors[e->pitch%12u][c]*4u;
    } else rgb[2]=32u;
    if(physical_step(pad)==u->selected_step) for(unsigned c=0;c<3u;++c) rgb[c]=(uint16_t)(rgb[c]+350u);
    const thy_transport_track *run=&u->transport->tracks[t];
    if(run->running && run->playhead>=tick && run->playhead-tick<grid(u,t)) rgb[0]=rgb[1]=rgb[2]=2000u;
    if(u->pads[pad].held) rgb[0]=rgb[1]=rgb[2]=2500u;
    return 1;
}
uint32_t thy_sequencer_ui_scene_key(const thy_sequencer_ui *u)
{ return u && u->open?(u->help_open?0x70000u|u->help_page:0x60000u|u->view):UINT32_MAX; }
