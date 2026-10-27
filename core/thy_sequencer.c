/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_sequencer.h"

/* Keep this module usable by the freestanding WASM and Cortex-M builds. */
static void seq_copy(void *to,const void *from,size_t count)
{uint8_t *d=to;const uint8_t *s=from;for(size_t n=0;n<count;++n)d[n]=s[n];}
static void seq_fill(void *to,int value,size_t count)
{uint8_t *d=to;for(size_t n=0;n<count;++n)d[n]=(uint8_t)value;}
static void seq_move(void *to,const void *from,size_t count)
{uint8_t *d=to;const uint8_t *s=from;if((uintptr_t)d<(uintptr_t)s)seq_copy(d,s,count);else while(count){--count;d[count]=s[count];}}
static int seq_compare(const void *a,const void *b,size_t count)
{const uint8_t *x=a,*y=b;for(size_t n=0;n<count;++n)if(x[n]!=y[n])return x[n]<y[n]?-1:1;return 0;}

_Static_assert(sizeof(thy_seq_event) == 16u, "portable event arena");
_Static_assert(sizeof(thy_sequencer) <= 30720u, "sequencer CCM budget");

static int fail(thy_sequencer *s, int error)
{ if (s) { s->last_error = (uint8_t)error; ++s->failures; } return error; }
static int type_of(const thy_seq_event *e) { return e->kind & 127u; }
static int valid_event(const thy_seq_event *e)
{
    return e && e->tick < THY_SEQ_MAX_TICK && e->duration &&
        e->duration <= THY_SEQ_MAX_TICK - e->tick && e->pitch < 128u &&
        e->value < 128u && type_of(e) <= THY_SEQ_BEND &&
        (type_of(e) != THY_SEQ_NOTE || e->value != 0u);
}
static int valid_track(const thy_seq_track *t)
{
    return t->loop_start < t->loop_end && t->loop_end <= t->content_end &&
        t->content_end <= THY_SEQ_MAX_TICK && t->channel < 16u &&
        t->destinations && t->destinations <= THY_ROUTE_ALL && t->velocity < 128u &&
        t->muted < 2u && t->processor <= THY_SEQ_ARP2 &&
        t->record_source <= THY_SEQ_SOURCE_USB && t->record_policy <= THY_SEQ_OVERDUB &&
        t->grid_ticks && t->grid_ticks <= (THY_SEQ_MAX_TICK + 47u) / 48u &&
        (uint64_t)t->loop_end-t->loop_start <= (uint64_t)t->grid_ticks*48u &&
        ((t->flags & THY_SEQ_QUANTIZE_MASK) >> THY_SEQ_QUANTIZE_SHIFT) <= 4u;
}
static void remember(thy_sequencer *s)
{
    if (s->transaction_open && s->transaction_saved) return;
    seq_copy(s->history, s->events, THY_SEQ_EVENTS * sizeof(*s->history));
    seq_copy(s->history_tracks, s->tracks, sizeof(s->history_tracks));
    s->history_count = s->count; s->history_valid = 1u; s->history_is_redo = 0u;
    if (s->transaction_open) s->transaction_saved = 1u;
}
static void changed(thy_sequencer *s, unsigned track)
{ ++s->revision; ++s->tracks[track].revision; s->last_error = THY_OK; }
static int editable(thy_sequencer *s, unsigned track)
{
    if (!s || track >= THY_SEQ_TRACKS) return THY_INVALID_ARGUMENT;
    if (s->capture.state >= THY_SEQ_RECORDING && s->capture.state <= THY_SEQ_FINISHING &&
        s->capture.track == track) return fail(s, THY_SEQ_BUSY);
    return THY_OK;
}
static int room_ids(const thy_sequencer *s, unsigned count)
{ return s->next_id && s->next_group && count <= 65536u - s->next_id && count <= 65536u - s->next_group; }
static uint16_t group_for(const thy_seq_event *events, unsigned n, uint16_t base)
{
    unsigned unique = 0u;
    for (unsigned i = 0; i <= n; ++i) {
        int first = 1;
        for (unsigned j = 0; j < i; ++j)
            if (events[i].group && events[i].group == events[j].group) { first = 0; break; }
        if (first) {
            if (i == n) return (uint16_t)(base + unique);
            ++unique;
        } else if (events[i].group == events[n].group) {
            for (unsigned j = 0; j < i; ++j)
                if (events[j].group == events[n].group) return group_for(events, j, base);
        }
    }
    return base;
}
static void append_batch(thy_sequencer *s, unsigned track,
    const thy_seq_event *events, unsigned count, int anchored, uint32_t tick, uint32_t duration)
{
    s->tracks[track].flags |= THY_SEQ_FLAG_CREATED;
    uint16_t base = s->next_group;
    unsigned groups = 0u;
    for (unsigned n = 0; n < count; ++n) {
        thy_seq_event e = events[n];
        e.track = (uint8_t)track; e.id = s->next_id++;
        e.group = group_for(events, n, base);
        unsigned group_number = e.group - base + 1u;
        if (group_number > groups) groups = group_number;
        if (anchored) { e.tick = tick; e.duration = duration; }
        s->events[s->count++] = e;
        if (e.tick + e.duration > s->tracks[track].content_end)
            s->tracks[track].content_end = e.tick + e.duration;
    }
    s->next_group = (uint16_t)(s->next_group + groups);
}
void thy_sequencer_init(thy_sequencer *s, thy_seq_event history[THY_SEQ_EVENTS])
{
    if (!s || !history) return;
    seq_fill(s, 0, sizeof(*s));
    s->history = history;
    seq_fill(history, 0, THY_SEQ_EVENTS * sizeof(*history)); s->next_id = s->next_group = 1u;
    for (unsigned t = 0; t < THY_SEQ_TRACKS; ++t) {
        s->tracks[t] = (thy_seq_track){.content_end=3840u, .loop_end=3840u,
            .channel=(uint8_t)(t % 16u), .destinations=THY_ROUTE_USB0|THY_ROUTE_DIN,
            .velocity=0u, .grid_ticks=240u}; /* 0 preserves stored velocity. */
    }
}
int thy_sequencer_select(thy_sequencer *s, uint8_t track)
{ if (!s || track >= THY_SEQ_TRACKS) return THY_INVALID_ARGUMENT; s->selected_track=track; return THY_OK; }
int thy_sequencer_track_exists(const thy_sequencer *s,uint8_t track)
{ return s && track<THY_SEQ_TRACKS && ((s->tracks[track].flags&THY_SEQ_FLAG_CREATED) || thy_sequencer_track_count(s,track)); }
int thy_sequencer_create(thy_sequencer *s,uint8_t track)
{
    if(!s || track>=THY_SEQ_TRACKS)return THY_INVALID_ARGUMENT;
    if(s->transaction_open || thy_sequencer_track_exists(s,track) ||
       (s->capture.state!=THY_SEQ_CAPTURE_OFF && s->capture.track==track))return THY_SEQ_BUSY;
    remember(s);
    s->tracks[track]=(thy_seq_track){.content_end=3840u,.loop_end=3840u,
        .channel=(uint8_t)(track%16u),.destinations=THY_ROUTE_USB0|THY_ROUTE_DIN,
        .grid_ticks=240u,.flags=THY_SEQ_FLAG_CREATED};
    changed(s,track);s->selected_track=track;return THY_OK;
}
int thy_sequencer_delete(thy_sequencer *s,uint8_t track)
{
    if(!s || track>=THY_SEQ_TRACKS)return THY_INVALID_ARGUMENT;
    if(!thy_sequencer_track_exists(s,track))return THY_SEQ_NOT_FOUND;
    if(s->transaction_open || (s->capture.state!=THY_SEQ_CAPTURE_OFF &&
       s->capture.track==track))return THY_SEQ_BUSY;
    remember(s);unsigned out=0;
    for(unsigned n=0;n<s->count;++n)if(s->events[n].track!=track)s->events[out++]=s->events[n];
    s->count=(uint16_t)out;
    s->tracks[track]=(thy_seq_track){.content_end=3840u,.loop_end=3840u,
        .channel=(uint8_t)(track%16u),.destinations=THY_ROUTE_USB0|THY_ROUTE_DIN,.grid_ticks=240u};
    changed(s,track);return THY_OK;
}
uint16_t thy_sequencer_track_count(const thy_sequencer *s, uint8_t track)
{ unsigned count=0; if(s) for(unsigned n=0;n<s->count;++n) count+=s->events[n].track==track; return (uint16_t)count; }
const thy_seq_event *thy_sequencer_event_at(const thy_sequencer *s, uint8_t track, uint16_t index)
{
    if(s) for(unsigned n=0;n<s->count;++n) if(s->events[n].track==track) {
        if(!index) return &s->events[n]; --index;
    }
    return 0;
}
const thy_seq_event *thy_sequencer_find(const thy_sequencer *s, uint16_t id)
{ if(s && id) for(unsigned n=0;n<s->count;++n) if(s->events[n].id==id) return &s->events[n]; return 0; }
int thy_sequencer_transaction_begin(thy_sequencer *s)
{
    if(!s) return THY_INVALID_ARGUMENT;
    if(s->transaction_open) return THY_SEQ_BUSY;
    s->transaction_open=1u; s->transaction_saved=0u; return THY_OK;
}
void thy_sequencer_transaction_end(thy_sequencer *s)
{ if(s) { s->transaction_open=0u; s->transaction_saved=0u; } }
int thy_sequencer_add(thy_sequencer *s, uint8_t track, const thy_seq_event *events, uint16_t count)
{
    int r=editable(s,track); if(r) return r;
    if(!events || !count) return THY_INVALID_ARGUMENT;
    if(count > THY_SEQ_EVENTS-s->count || !room_ids(s,count)) return fail(s,THY_SEQ_FULL);
    for(unsigned n=0;n<count;++n) if(!valid_event(&events[n])) return THY_INVALID_ARGUMENT;
    remember(s); append_batch(s,track,events,count,0,0,0); changed(s,track); return THY_OK;
}
int thy_sequencer_assign(thy_sequencer *s, uint8_t track, uint32_t tick,
    uint32_t duration, const thy_seq_event *events, uint16_t count, int replace)
{
    int r=editable(s,track); if(r) return r;
    if(!events || !count || !duration || replace<0 || replace>2 || tick>=THY_SEQ_MAX_TICK || duration>THY_SEQ_MAX_TICK-tick)
        return THY_INVALID_ARGUMENT;
    unsigned removed=0;
    for(unsigned n=0;n<s->count;++n) if(replace && s->events[n].track==track &&
        (replace==2 ? s->events[n].tick>=tick && s->events[n].tick<tick+duration : s->events[n].tick==tick) && type_of(&s->events[n])==THY_SEQ_NOTE) ++removed;
    if(count>THY_SEQ_EVENTS-s->count+removed || !room_ids(s,count)) return fail(s,THY_SEQ_FULL);
    for(unsigned n=0;n<count;++n) if(events[n].pitch>127u || !events[n].value ||
        events[n].value>127u || type_of(&events[n])!=THY_SEQ_NOTE) return THY_INVALID_ARGUMENT;
    remember(s); unsigned out=0;
    for(unsigned n=0;n<s->count;++n) if(!(replace && s->events[n].track==track &&
        (replace==2 ? s->events[n].tick>=tick && s->events[n].tick<tick+duration : s->events[n].tick==tick) && type_of(&s->events[n])==THY_SEQ_NOTE)) s->events[out++]=s->events[n];
    s->count=(uint16_t)out; append_batch(s,track,events,count,1,tick,duration); changed(s,track); return THY_OK;
}
int thy_sequencer_remove(thy_sequencer *s, uint16_t id)
{
    const thy_seq_event *e=thy_sequencer_find(s,id); if(!e) return THY_SEQ_NOT_FOUND;
    unsigned track=e->track,index=(unsigned)(e-s->events); int r=editable(s,track); if(r) return r;
    remember(s); seq_move(&s->events[index],&s->events[index+1u],(--s->count-index)*sizeof(*e));
    changed(s,track); return THY_OK;
}
int thy_sequencer_update(thy_sequencer *s, uint16_t id, uint32_t tick,
    uint32_t duration, uint8_t pitch, uint8_t velocity)
{
    const thy_seq_event *found=thy_sequencer_find(s,id); if(!found) return THY_SEQ_NOT_FOUND;
    thy_seq_event e=*found; e.tick=tick;e.duration=duration;e.pitch=pitch;e.value=velocity;
    if(!valid_event(&e)) return THY_INVALID_ARGUMENT;
    int r=editable(s,e.track);if(r)return r;
    unsigned index=(unsigned)(found-s->events);remember(s);s->events[index]=e;
    if(tick+duration>s->tracks[e.track].content_end)s->tracks[e.track].content_end=tick+duration;
    changed(s,e.track);return THY_OK;
}
int thy_sequencer_clear(thy_sequencer *s,uint8_t track)
{
    int r=editable(s,track);if(r)return r;remember(s);unsigned out=0;
    for(unsigned n=0;n<s->count;++n)if(s->events[n].track!=track)s->events[out++]=s->events[n];
    s->count=(uint16_t)out;changed(s,track);return THY_OK;
}
static int sync_loop(thy_sequencer *s,thy_transport *t,uint8_t track,uint32_t start,uint32_t end)
{
    thy_transport_track *run=&t->tracks[track];
    if(run->loop_start==start && end-start>=run->loop_length){
        thy_seq_run *runtime=&s->runs[track];
        if(run->running && runtime->initialized && runtime->generation==run->generation &&
            runtime->last_position>=runtime->ordinal && t->position>=runtime->last_position){
            uint64_t old_anchor=runtime->last_position-
                (runtime->last_position-runtime->ordinal)%run->loop_length;
            run->playhead=start+(uint32_t)((t->position-old_anchor)%(end-start));
            runtime->ordinal=old_anchor;
        }
        run->loop_length=end-start;return THY_TIME_OK;
    }
    return thy_transport_set_loop(t,track,start,end-start);
}
int thy_sequencer_loop(thy_sequencer *s,thy_transport *t,uint8_t track,uint32_t start,uint32_t end)
{
    int r=editable(s,track);if(r)return r;
    if(!t || start>=end || end>THY_SEQ_MAX_TICK)return THY_INVALID_ARGUMENT;
    if(s->tracks[track].loop_start==start && s->tracks[track].loop_end==end)return THY_OK;
    int extends=t->tracks[track].loop_start==start && end-start>=t->tracks[track].loop_length;
    if(!extends && (t->tracks[track].generation==UINT32_MAX || !t->tracks[track].generation))return THY_HANDLE_EXHAUSTED;
    remember(s);s->tracks[track].loop_start=start;s->tracks[track].loop_end=end;
    if(end>s->tracks[track].content_end)s->tracks[track].content_end=end;
    (void)sync_loop(s,t,track,start,end);changed(s,track);return THY_OK;
}
int thy_sequencer_grid(thy_sequencer *s,thy_transport *t,uint8_t track,uint32_t ticks)
{
    int r=editable(s,track);if(r)return r;
    if(!t || !ticks || ticks>(THY_SEQ_MAX_TICK+47u)/48u || s->transaction_open ||
       (s->capture.state!=THY_SEQ_CAPTURE_OFF && s->capture.track==track))return THY_SEQ_BUSY;
    thy_seq_track *cfg=&s->tracks[track];uint32_t old=cfg->grid_ticks,start=cfg->loop_start;
    if(old==ticks)return THY_OK;
    uint64_t length=((uint64_t)(cfg->loop_end-start)+old-1u)/old*ticks;
    uint64_t extent=start+(uint64_t)(cfg->content_end-start)*ticks/old;
    if(!length || length>THY_SEQ_MAX_TICK-start || extent>THY_SEQ_MAX_TICK)return THY_INVALID_ARGUMENT;
    for(unsigned n=0;n<s->count;++n)if(s->events[n].track==track && s->events[n].tick>=start){
        const thy_seq_event *e=&s->events[n];
        uint64_t onset=start+(uint64_t)(e->tick-start)*ticks/old;
        uint64_t duration=(uint64_t)e->duration*ticks/old;if(!duration)duration=1u;
        if(onset>=THY_SEQ_MAX_TICK || duration>THY_SEQ_MAX_TICK-onset)return THY_INVALID_ARGUMENT;
        if(onset+duration>extent)extent=onset+duration;
    }
    for(unsigned n=0;n<s->count;++n)if(s->events[n].track==track && s->events[n].tick<start &&
        (uint64_t)s->events[n].tick+s->events[n].duration>extent)
        extent=(uint64_t)s->events[n].tick+s->events[n].duration;
    if(t->tracks[track].generation==UINT32_MAX || !t->tracks[track].generation)return THY_HANDLE_EXHAUSTED;
    remember(s);
    for(unsigned n=0;n<s->count;++n)if(s->events[n].track==track && s->events[n].tick>=start){
        thy_seq_event *e=&s->events[n];e->tick=start+(uint32_t)((uint64_t)(e->tick-start)*ticks/old);
        e->duration=(uint32_t)((uint64_t)e->duration*ticks/old);if(!e->duration)e->duration=1u;
    }
    cfg->grid_ticks=ticks;cfg->loop_end=start+(uint32_t)length;
    cfg->content_end=(uint32_t)(extent>cfg->loop_end?extent:cfg->loop_end);
    (void)thy_transport_set_loop(t,track,start,(uint32_t)length);changed(s,track);return THY_OK;
}
int thy_sequencer_duplicate(thy_sequencer *s,uint8_t track,uint32_t start,uint32_t end,
    uint32_t destination,int extend_loop)
{
    int r=editable(s,track);if(r)return r;
    if(start>=end || destination>THY_SEQ_MAX_TICK-(end-start))return THY_INVALID_ARGUMENT;
    unsigned count=0;uint32_t extent=destination+end-start;
    for(unsigned n=0;n<s->count;++n)if(s->events[n].track==track && s->events[n].tick>=start && s->events[n].tick<end){
        uint32_t copied=destination+s->events[n].tick-start;
        if(s->events[n].duration>THY_SEQ_MAX_TICK-copied)return THY_INVALID_ARGUMENT;
        if(copied+s->events[n].duration>extent)extent=copied+s->events[n].duration;
        ++count;
    }
    if(count>THY_SEQ_EVENTS-s->count || !room_ids(s,count))return fail(s,THY_SEQ_FULL);
    if(!count)return THY_OK;
    remember(s);unsigned original=s->count;
    for(unsigned n=0;n<original;++n)if(s->events[n].track==track && s->events[n].tick>=start && s->events[n].tick<end){
        thy_seq_event e=s->events[n];e.tick=destination+e.tick-start;e.id=s->next_id++;
        /* The first copied member assigns a fresh group; all later members of
         * that same native group share it. Existing material is never aliased. */
        uint16_t group=0;
        for(unsigned j=0,copied=0;j<n;++j){
            if(s->events[j].track!=track || s->events[j].tick<start || s->events[j].tick>=end)continue;
            if(s->events[j].group==s->events[n].group){group=s->events[original+copied].group;break;}
            ++copied;
        }
        e.group=group?group:s->next_group++;s->events[s->count++]=e;
    }
    if(extent>s->tracks[track].content_end)s->tracks[track].content_end=extent;
    if(extend_loop && destination+end-start>s->tracks[track].loop_end)s->tracks[track].loop_end=destination+end-start;
    changed(s,track);return THY_OK;
}
int thy_sequencer_trim(thy_sequencer *s,uint8_t track,uint32_t start,uint32_t end)
{
    int r=editable(s,track);if(r)return r;
    if(start>=end || end>THY_SEQ_MAX_TICK)return THY_INVALID_ARGUMENT;
    remember(s);unsigned out=0;
    for(unsigned n=0;n<s->count;++n){thy_seq_event e=s->events[n];
        if(e.track==track){uint32_t stop=e.tick+e.duration;if(stop<=start || e.tick>=end)continue;
            if(e.tick<start){e.duration=stop-start;e.tick=start;}if(e.tick+e.duration>end)e.duration=end-e.tick;}
        s->events[out++]=e;
    }
    s->count=(uint16_t)out;s->tracks[track].content_end=end;
    if(s->tracks[track].loop_start<start)s->tracks[track].loop_start=start;
    if(s->tracks[track].loop_end>end)s->tracks[track].loop_end=end;
    if(s->tracks[track].loop_start>=s->tracks[track].loop_end){s->tracks[track].loop_start=start;s->tracks[track].loop_end=end;}
    changed(s,track);return THY_OK;
}
static uint64_t sustain_union(const thy_sequencer *s)
{uint64_t all=0;for(unsigned t=0;t<THY_SEQ_TRACKS;++t)all|=s->sustain_tracks[t];return all;}
static void release_sustain_track(thy_sequencer *s,thy_core *core,unsigned track)
{
    uint64_t old=s->sustain_tracks[track];if(!old)return;s->sustain_tracks[track]=0;
    uint64_t retained=sustain_union(s);
    for(unsigned key=0;key<THY_DEST_COUNT*16u;++key){uint64_t bit=UINT64_C(1)<<key;
        if(!(old&bit) || (retained&bit))continue;
        if(!(s->sustain_baseline&bit))(void)thy_core_sustain(core,(uint8_t)(1u<<(key/16u)),(uint8_t)(key%16u),0);
        s->sustain_baseline&=~bit;
    }
}
void thy_sequencer_release(thy_sequencer *s,thy_core *core)
{
    if(!s || !core)return;
    for(unsigned t=0;t<THY_SEQ_TRACKS;++t)release_sustain_track(s,core,t);
    for(unsigned n=0;n<THY_SEQ_VOICES;++n)if(s->voices[n].handle){
        (void)thy_core_generated_release(core,s->voices[n].handle);s->voices[n]=(thy_seq_voice){0};}
    s->voice_count=0;seq_fill(s->runs,0,sizeof(s->runs));
}
int thy_sequencer_history(thy_sequencer *s,thy_core *core,thy_transport *t,int redo)
{
    if(!s || !core || !t)return THY_INVALID_ARGUMENT;
    if(s->capture.state!=THY_SEQ_CAPTURE_OFF || s->transaction_open)return THY_SEQ_BUSY;
    if(!s->history_valid || !!redo!=!!s->history_is_redo)return THY_SEQ_NO_HISTORY;
    thy_sequencer_release(s,core);
    for(unsigned n=0;n<THY_SEQ_EVENTS;++n){thy_seq_event e=s->events[n];s->events[n]=s->history[n];s->history[n]=e;}
    for(unsigned n=0;n<THY_SEQ_TRACKS;++n){thy_seq_track v=s->tracks[n];s->tracks[n]=s->history_tracks[n];s->history_tracks[n]=v;
        (void)thy_transport_set_loop(t,(uint8_t)n,s->tracks[n].loop_start,s->tracks[n].loop_end-s->tracks[n].loop_start);}
    uint16_t count=s->count;s->count=s->history_count;s->history_count=count;
    s->history_is_redo=(uint8_t)!s->history_is_redo;++s->revision;return THY_OK;
}

static uint64_t boundary(uint64_t position,uint8_t quantize,const thy_clock *clock)
{
    uint32_t quantum=quantize==THY_TRANSPORT_BAR?clock->bar_ticks:
        quantize==THY_TRANSPORT_BEAT?clock->beat_ticks:1u;
    if(quantize==THY_TRANSPORT_IMMEDIATE)return position;
    uint64_t delta=quantum-position%quantum;
    return UINT64_MAX-position<delta?UINT64_MAX:position+delta;
}
void thy_sequencer_cancel(thy_sequencer *s)
{if(s){s->capture=(thy_seq_capture){0};seq_fill(s->inputs,0,sizeof(s->inputs));}}
int thy_sequencer_record(thy_sequencer *s,thy_transport *t,const thy_clock *clock,
    uint8_t track,uint8_t source,uint8_t policy,uint32_t start,uint32_t end)
{
    if(!s || !t || !clock || track>=THY_SEQ_TRACKS || source>THY_SEQ_SOURCE_USB ||
        policy>THY_SEQ_OVERDUB || (end && (start>=end || end>THY_SEQ_MAX_TICK)))return THY_INVALID_ARGUMENT;
    if(s->capture.state==THY_SEQ_ARMED || s->capture.state==THY_SEQ_QUEUED){thy_sequencer_cancel(s);return THY_OK;}
    if(s->capture.state==THY_SEQ_RECORDING || s->capture.state==THY_SEQ_FINISHING)
        return thy_sequencer_finish(s,t,clock,0);
    int growing=!end && !thy_sequencer_track_count(s,track);
    if(!end && !growing){start=s->tracks[track].loop_start;end=s->tracks[track].loop_end;}
    unsigned q=(s->tracks[track].flags & THY_SEQ_QUANTIZE_MASK) >> THY_SEQ_QUANTIZE_SHIFT;
    s->capture=(thy_seq_capture){.state=t->state==THY_TRANSPORT_PLAYING?THY_SEQ_QUEUED:THY_SEQ_ARMED,
        .track=track,.source=source,.policy=policy,.region_start=start,.region_end=end,
        .growing=(uint8_t)growing,.quantize=t->launch_quantize,.flags=s->tracks[track].flags,
        .quantize_ticks=(uint16_t)(q ? 960u >> (q-1u) : 0u),
        .queued_position=boundary(t->position,t->launch_quantize,clock)};
    return THY_OK;
}
static void begin_capture(thy_sequencer *s,uint64_t position)
{
    s->capture.state=THY_SEQ_RECORDING;s->capture.start_position=position;s->capture.count=0;
    s->capture.end_position=s->capture.growing?UINT64_MAX:
        position+s->capture.region_end-s->capture.region_start;
    seq_fill(s->inputs,0,sizeof(s->inputs));
    s->capture.start_epoch=++s->capture_epoch;
}
static int lane_replaced(const thy_sequencer *s,const thy_seq_event *e)
{
    if(type_of(e)==THY_SEQ_NOTE)return 1;
    for(unsigned n=0;n<s->capture.count;++n)if(type_of(&s->take[n])==type_of(e) &&
        (type_of(e)!=THY_SEQ_CC || s->take[n].pitch==e->pitch))return 1;
    return 0;
}
static int commit_take(thy_sequencer *s,uint64_t endpoint)
{
    thy_seq_capture *c=&s->capture;
    if(c->failed){c->state=THY_SEQ_FAILED;return fail(s,THY_SEQ_CAPTURE_FAILED);}
    uint64_t elapsed=endpoint-c->start_position;
    if(!elapsed){thy_sequencer_cancel(s);return THY_OK;}
    if(elapsed>THY_SEQ_MAX_TICK-c->region_start){c->state=THY_SEQ_FAILED;return fail(s,THY_SEQ_FULL);}
    uint32_t end=c->region_start+(uint32_t)elapsed;
    for(unsigned n=0;n<THY_SEQ_TAKE_EVENTS;++n)if(s->inputs[n].active){
        thy_seq_event *e=&s->take[s->inputs[n].event_index];e->duration=end>e->tick?end-e->tick:1u;s->inputs[n].active=0;}
    unsigned resulting=c->count,splits=0;
    for(unsigned n=0;n<s->count;++n){const thy_seq_event *e=&s->events[n];
        if(c->policy==THY_SEQ_OVERDUB || e->track!=c->track || !lane_replaced(s,e) ||
            e->tick>=end || e->tick+e->duration<=c->region_start){++resulting;continue;}
        if(e->tick<c->region_start)++resulting;
        if(e->tick+e->duration>end){++resulting;if(e->tick<c->region_start)++splits;}
    }
    if(resulting>THY_SEQ_EVENTS || !room_ids(s,c->count+splits)){c->state=THY_SEQ_FAILED;return fail(s,THY_SEQ_FULL);}
    unsigned original=s->count,out=0;
    /* Splits append to the old array's free tail, so preflight also guarantees
     * a bounded safe in-place publication. Compact retained left/outside first. */
    thy_seq_event right[THY_SEQ_TAKE_EVENTS];unsigned rights=0;
    /* At most 128 staged notes but old crossing splits could exceed that.
     * Reject before mutation if their scratch requirement exceeds the budget. */
    unsigned needed_right=0;
    for(unsigned n=0;n<original;++n){const thy_seq_event *e=&s->events[n];if(c->policy==THY_SEQ_REPLACE &&
        e->track==c->track && lane_replaced(s,e) && e->tick<end && e->tick+e->duration>end &&
        e->tick<c->region_start)++needed_right;}
    if(needed_right>THY_SEQ_TAKE_EVENTS){c->state=THY_SEQ_FAILED;return fail(s,THY_SEQ_FULL);}
    remember(s);
    for(unsigned n=0;n<original;++n){thy_seq_event e=s->events[n];uint32_t stop=e.tick+e.duration;
        if(c->policy==THY_SEQ_OVERDUB || e.track!=c->track || !lane_replaced(s,&e) || e.tick>=end || stop<=c->region_start)
            s->events[out++]=e;
        else if(e.tick<c->region_start){
            if(stop>end){thy_seq_event tail=e;tail.tick=end;tail.duration=stop-end;tail.id=s->next_id++;right[rights++]=tail;}
            e.duration=c->region_start-e.tick;s->events[out++]=e;
        }else if(stop>end){e.tick=end;e.duration=stop-end;s->events[out++]=e;}
    }
    for(unsigned n=0;n<rights;++n)s->events[out++]=right[n];
    s->count=(uint16_t)out;append_batch(s,c->track,s->take,c->count,0,0,0);
    unsigned target=c->track;
    if(c->growing){s->tracks[target].loop_start=c->region_start;s->tracks[target].loop_end=end;
        if(end>s->tracks[target].content_end)s->tracks[target].content_end=end;}
    changed(s,target);thy_sequencer_cancel(s);return THY_OK;
}
int thy_sequencer_finish(thy_sequencer *s,const thy_transport *t,const thy_clock *clock,int immediate)
{
    if(!s || !t || !clock)return THY_INVALID_ARGUMENT;
    if(s->capture.state==THY_SEQ_ARMED || s->capture.state==THY_SEQ_QUEUED){thy_sequencer_cancel(s);return THY_OK;}
    if(s->capture.state!=THY_SEQ_RECORDING && s->capture.state!=THY_SEQ_FINISHING)return THY_OK;
    uint64_t end=immediate?t->position:boundary(t->position,s->capture.quantize,clock);
    if(end>s->capture.end_position)end=s->capture.end_position;
    if(immediate)return commit_take(s,end);
    s->capture.end_position=end;s->capture.state=THY_SEQ_FINISHING;return THY_OK;
}
static int capturing(const thy_sequencer *s,uint8_t source,uint64_t position)
{
    return s && (s->capture.state==THY_SEQ_RECORDING || s->capture.state==THY_SEQ_FINISHING) &&
        source==s->capture.source && position<s->capture.end_position && !s->capture.failed;
}
int thy_sequencer_input_on(thy_sequencer *s,uint8_t source,uint32_t identity,uint16_t group,
    uint8_t role,uint8_t pitch,uint8_t velocity,uint64_t position)
{
    if(!s || !identity || role>1u || pitch>127u || !velocity || velocity>127u)return THY_INVALID_ARGUMENT;
    if(!capturing(s,source,position))return THY_OK;
    for(unsigned n=0;n<THY_SEQ_TAKE_EVENTS;++n)if(s->inputs[n].active && s->inputs[n].source==source &&
        s->inputs[n].identity==identity)return THY_OK;
    if(s->capture.count>=THY_SEQ_TAKE_EVENTS){s->capture.failed=1;return fail(s,THY_SEQ_FULL);}
    unsigned slot=0;while(slot<THY_SEQ_TAKE_EVENTS && s->inputs[slot].active)++slot;
    if(slot==THY_SEQ_TAKE_EVENTS){s->capture.failed=1;return fail(s,THY_SEQ_FULL);}
    uint64_t offset=position>s->capture.start_position?position-s->capture.start_position:0;
    if(s->capture.quantize_ticks) offset=((offset+s->capture.quantize_ticks/2u)/s->capture.quantize_ticks)*s->capture.quantize_ticks;
    if(s->capture.end_position!=UINT64_MAX && offset>=s->capture.end_position-s->capture.start_position)
        offset=s->capture.end_position-s->capture.start_position-1u;
    if(offset>=THY_SEQ_MAX_TICK-s->capture.region_start){s->capture.failed=1;return fail(s,THY_SEQ_FULL);}
    uint16_t index=s->capture.count++;
    s->take[index]=(thy_seq_event){.tick=s->capture.region_start+(uint32_t)offset,.duration=1u,
        .group=group?group:(uint16_t)(index+1u),.kind=(uint8_t)(THY_SEQ_NOTE|(role?128u:0u)),
        .pitch=pitch,.value=velocity,.track=s->capture.track};
    s->inputs[slot]=(thy_seq_input){.identity=identity,.event_index=index,.source=source,.active=1u};return THY_OK;
}
int thy_sequencer_input_off(thy_sequencer *s,uint8_t source,uint32_t identity,uint64_t position)
{
    if(!s)return THY_INVALID_ARGUMENT;
    for(unsigned n=0;n<THY_SEQ_TAKE_EVENTS;++n)if(s->inputs[n].active && s->inputs[n].source==source &&
        s->inputs[n].identity==identity){thy_seq_event *e=&s->take[s->inputs[n].event_index];
        uint64_t stop=position>s->capture.end_position?s->capture.end_position:position;
        uint64_t offset=stop>s->capture.start_position?stop-s->capture.start_position:0;
        if(s->capture.quantize_ticks) offset=((offset+s->capture.quantize_ticks/2u)/s->capture.quantize_ticks)*s->capture.quantize_ticks;
        if(s->capture.end_position!=UINT64_MAX && offset>s->capture.end_position-s->capture.start_position)
            offset=s->capture.end_position-s->capture.start_position;
        uint64_t end=s->capture.region_start+offset;
        e->duration=end>e->tick && end-e->tick<=THY_SEQ_MAX_TICK?(uint32_t)(end-e->tick):1u;
        s->inputs[n].active=0u;break;}
    return THY_OK;
}
int thy_sequencer_input_event(thy_sequencer *s,uint8_t source,uint8_t type,uint8_t d1,uint8_t d2,uint64_t position)
{
    if(!s || type==THY_SEQ_NOTE || type>THY_SEQ_BEND || d1>127u || d2>127u)return THY_INVALID_ARGUMENT;
    if(!capturing(s,source,position))return THY_OK;
    if(type==THY_SEQ_PRESSURE || type==THY_SEQ_CHANNEL_PRESSURE){
        if(!(s->capture.flags&THY_SEQ_FLAG_SEND_PRESSURE))return THY_OK;
    }else if(!(s->capture.flags&THY_SEQ_FLAG_RECORD_CONTROLLERS))return THY_OK;
    uint64_t offset=position>s->capture.start_position?position-s->capture.start_position:0;
    if(offset>=THY_SEQ_MAX_TICK-s->capture.region_start){s->capture.failed=1;return fail(s,THY_SEQ_FULL);}
    uint32_t tick=s->capture.region_start+(uint32_t)offset;
    /* Continuous lanes retain their last observation in each 60-tick bucket.
     * Exact note lifetimes, Program Change and sustain edges are never thinned. */
    if(type!=THY_SEQ_PROGRAM && !(type==THY_SEQ_CC && d1==64u))
        for(unsigned n=s->capture.count;n>0;--n){thy_seq_event *e=&s->take[n-1u];
            if(type_of(e)!=type || ((type==THY_SEQ_CC || type==THY_SEQ_PRESSURE) && e->pitch!=d1))continue;
            unsigned old=type==THY_SEQ_BEND?e->pitch+128u*e->value:type==THY_SEQ_CHANNEL_PRESSURE?e->pitch:e->value;
            unsigned value=type==THY_SEQ_BEND?d1+128u*d2:type==THY_SEQ_CHANNEL_PRESSURE?d1:d2;
            unsigned delta=old>value?old-value:value-old;
            if(delta<(type==THY_SEQ_BEND?128u:3u))return THY_OK;
            if(e->tick/60u==tick/60u){e->tick=tick;e->pitch=d1;e->value=d2;return THY_OK;}
            break;
        }
    if(s->capture.count>=THY_SEQ_TAKE_EVENTS){s->capture.failed=1;return fail(s,THY_SEQ_FULL);}
    s->take[s->capture.count++]=(thy_seq_event){.tick=tick,
        .duration=1u,.kind=type,.pitch=d1,.value=d2,.track=s->capture.track};return THY_OK;
}

static void release_voice(thy_sequencer *s,thy_core *c,unsigned n)
{if(s->voices[n].handle){(void)thy_core_generated_release(c,s->voices[n].handle);s->voices[n]=(thy_seq_voice){0};--s->voice_count;}}
static int emit_batch(thy_sequencer *s,thy_core *core,unsigned track,uint32_t generation,
    const uint8_t *pitches,const uint8_t *velocities,const uint16_t *ids,unsigned count,
    uint64_t off,int generated)
{
    if(!count)return THY_OK;
    const thy_seq_track *t=&s->tracks[track];
    unsigned routes=!!(t->destinations&1u)+!!(t->destinations&2u)+!!(t->destinations&4u);
    if(count>THY_SEQ_VOICES-s->voice_count || count>THY_GENERATED_OWNER_COUNT-core->generated_count ||
        count>UINT32_MAX-core->generated_last_handle)return fail(s,THY_OWNER_LIMIT);
    int prepared=thy_core_prepare_generated(core,t->channel,t->destinations);
    if(prepared!=THY_OK)return fail(s,prepared);
    if((uint32_t)thy_core_queued(core)+thy_core_reserved(core)+2u*count*routes>THY_QUEUE_CAPACITY)
        return fail(s,THY_QUEUE_FULL);
    for(unsigned n=0;n<count;++n){unsigned slot=0;while(s->voices[slot].handle)++slot;
        thy_generated_handle handle=0;int r=thy_core_generated_acquire(core,16u+track,generation,
            pitches[n],t->channel,t->destinations,t->velocity?t->velocity:velocities[n],&handle);
        if(r)return fail(s,r); /* Preflight above guarantees no partial admission. */
        s->voices[slot]=(thy_seq_voice){.off_position=off,.handle=handle,.generation=generation,
            .event_id=ids?ids[n]:0u,.track=(uint8_t)track,.generated=(uint8_t)generated};++s->voice_count;++s->attacks;
    }
    return THY_OK;
}
static int owned_sustain(thy_sequencer *s,thy_core *core,unsigned track,int enabled)
{
    const thy_seq_track *t=&s->tracks[track];uint64_t bits=0;
    for(unsigned dest=0;dest<THY_DEST_COUNT;++dest)if(t->destinations&(1u<<dest))bits|=UINT64_C(1)<<(dest*16u+t->channel);
    if(enabled){
        int result=thy_core_sustain(core,t->destinations,t->channel,1);if(result)return result;
        /* Preserve an external pedal that was already active before the first
         * sequencer lease. The caller captures those bits before enabling. */
        s->sustain_tracks[track]|=bits;
        return THY_OK;
    }
    uint64_t old=s->sustain_tracks[track];s->sustain_tracks[track]&=~bits;
    uint64_t retained=sustain_union(s);
    for(unsigned key=0;key<THY_DEST_COUNT*16u;++key){uint64_t bit=UINT64_C(1)<<key;
        if(!(bits&old&bit) || (retained&bit))continue;
        if(!(s->sustain_baseline&bit))(void)thy_core_sustain(core,(uint8_t)(1u<<(key/16u)),(uint8_t)(key%16u),0);
        s->sustain_baseline&=~bit;
    }
    return THY_OK;
}
static int emit_message(thy_sequencer *s,thy_core *core,unsigned track,const thy_seq_event *e)
{
    const thy_seq_track *t=&s->tracks[track];
    unsigned kind=(unsigned)type_of(e);
    if((kind==THY_SEQ_PRESSURE || kind==THY_SEQ_CHANNEL_PRESSURE) && !(t->flags&THY_SEQ_FLAG_SEND_PRESSURE))return THY_OK;
    if(kind==THY_SEQ_CC && e->pitch==64u){
        uint64_t managed=sustain_union(s),baseline=s->sustain_baseline;
        if(e->value>=64u)for(unsigned dest=0;dest<THY_DEST_COUNT;++dest)if(t->destinations&(1u<<dest)){
            unsigned key=dest*16u+t->channel;uint64_t bit=UINT64_C(1)<<key;
            if(!(managed&bit) && core->sustain[key])baseline|=bit;
        }
        int r=owned_sustain(s,core,track,e->value>=64u);if(!r)s->sustain_baseline=baseline&sustain_union(s);return r;
    }
    if(kind==THY_SEQ_CC)return thy_core_cc(core,t->destinations,t->channel,e->pitch,e->value);
    if(kind==THY_SEQ_BEND)return thy_core_bend(core,t->destinations,t->channel,e->pitch+128u*e->value);
    unsigned routes=!!(t->destinations&1u)+!!(t->destinations&2u)+!!(t->destinations&4u);
    if(thy_core_queued(core)+thy_core_reserved(core)+routes>THY_QUEUE_CAPACITY)return THY_QUEUE_FULL;
    uint8_t status=(uint8_t)((kind==THY_SEQ_PROGRAM?0xc0u:kind==THY_SEQ_CHANNEL_PRESSURE?0xd0u:0xa0u)|t->channel);
    for(unsigned dest=0;dest<THY_DEST_COUNT;++dest)if(t->destinations&(1u<<dest)){
        unsigned tail=(core->queue_head+core->queue_count)%THY_QUEUE_CAPACITY;
        core->queue[tail]=(thy_midi_event){.destination=(uint8_t)dest,.status=status,.data1=e->pitch,
            .data2=kind==THY_SEQ_PRESSURE?e->value:0u};++core->queue_count;}
    return THY_OK;
}
static uint64_t occurrence(const thy_seq_event *e,const thy_transport_track *t,uint64_t now)
{
    uint64_t anchor=now-(t->playhead-t->loop_start),relative=e->tick-t->loop_start;
    uint64_t due=anchor+relative;
    if(due>now){if(anchor<t->loop_length)return UINT64_MAX;due-=t->loop_length;}
    return due;
}
static int due_since(uint64_t due,uint64_t previous,uint64_t now,int fresh)
{return due!=UINT64_MAX && due<=now && (due>previous || (fresh && due==previous));}
static int active_event(const thy_seq_event *e,const thy_transport_track *t,uint64_t now,uint64_t *onset)
{
    if(type_of(e)!=THY_SEQ_NOTE || e->tick<t->loop_start || e->tick>=t->loop_start+t->loop_length)return 0;
    uint64_t due=occurrence(e,t,now);if(due==UINT64_MAX)return 0;
    uint32_t duration=e->duration,remaining=t->loop_start+t->loop_length-e->tick;
    if(duration>remaining)duration=remaining;
    if(now-due>=duration)return 0;*onset=due;return 1;
}
static unsigned pattern_index(uint64_t ordinal,unsigned count,unsigned pattern)
{
    if(count<=1u)return 0u;
    if(pattern==THY_ARP_DOWN)return count-1u-(unsigned)(ordinal%count);
    if(pattern==THY_ARP_UP_DOWN || pattern==THY_ARP_DOWN_UP){unsigned i=(unsigned)(ordinal%(2u*count-2u));
        if(i>=count)i=2u*count-2u-i;return pattern==THY_ARP_DOWN_UP?count-1u-i:i;}
    return (unsigned)(ordinal%count);
}
static int shifted_pitch(const thy_arp_config *cfg,int pitch,int offset)
{
    int rel=pitch-cfg->root,octave=rel/12,pc=rel%12;if(pc<0){pc+=12;--octave;}
    int degree=0;for(unsigned n=0;n<cfg->scale_count;++n)if(cfg->scale[n]<=pc)degree=(int)n;
    int index=octave*cfg->scale_count+degree+offset,oct=index/cfg->scale_count,step=index%cfg->scale_count;
    if(step<0){step+=cfg->scale_count;--oct;}return cfg->root+12*oct+cfg->scale[step];
}
static int arp_phrase(thy_sequencer *s,thy_core *core,const thy_transport_track *run,
    unsigned track,const thy_arp_config *cfg,uint16_t group,int include_singles,
    uint64_t anchor,uint64_t now,uint64_t previous,int fresh)
{
    uint32_t rate=cfg->rate_ticks,long_step=rate+rate*cfg->swing_percent/100u;
    if(now<anchor)return THY_OK;uint64_t elapsed=now-anchor,pair=elapsed/(2u*rate);
    unsigned odd=elapsed%(2u*rate)>=long_step;uint64_t ordinal=2u*pair+odd;
    uint64_t due=anchor+pair*(2u*rate)+(odd?long_step:0u);
    if(!due_since(due,previous,now,fresh))return THY_OK;
    uint32_t gate=(odd?2u*rate-long_step:long_step)*cfg->gate_percent/100u;if(!gate)gate=1u;
    if(now-due>60u || now-due>=gate || !(cfg->step_mask&(1u<<(ordinal%8u)))){++s->skipped;return THY_OK;}
    uint64_t bits[2]={0,0};uint8_t velocity=96u;
    for(unsigned n=0;n<s->count;++n){const thy_seq_event *e=&s->events[n];uint64_t at;
        if(e->track!=track || !active_event(e,run,now,&at))continue;
        int chord=!!(e->kind&128u);
        if(cfg->grouping==THY_ARP_COMBINED || (cfg->grouping==THY_ARP_EACH?e->group==group:
            ((chord && e->group==group) || (!chord && include_singles)))){
            bits[e->pitch/64u]|=UINT64_C(1)<<(e->pitch%64u);velocity=e->value;}}
    uint8_t pitches[128],velocities[128];unsigned count=0;
    if(cfg->pattern==THY_ARP_AS_PLAYED){
        uint64_t included[2]={0,0};
        for(unsigned n=0;n<s->count;++n){const thy_seq_event *e=&s->events[n];uint64_t at;
            if(e->track!=track || !active_event(e,run,now,&at))continue;
            int admitted=cfg->grouping==THY_ARP_COMBINED || (cfg->grouping==THY_ARP_EACH?e->group==group:
                (((e->kind&128u) && e->group==group) || (!(e->kind&128u) && include_singles)));
            uint64_t bit=UINT64_C(1)<<(e->pitch%64u);
            if(admitted && !(included[e->pitch/64u]&bit)){included[e->pitch/64u]|=bit;pitches[count++]=e->pitch;}
        }
    }else for(unsigned pitch=0;pitch<128u;++pitch)if(bits[pitch/64u]&(UINT64_C(1)<<(pitch%64u)))pitches[count++]=(uint8_t)pitch;
    if(!count)return THY_OK;
    if(cfg->type==THY_ARP_NOTES){pitches[0]=pitches[pattern_index(ordinal,count,cfg->pattern)];count=1u;}
    else{unsigned out=0;int offset=cfg->offsets[pattern_index(ordinal,8u,cfg->pattern)];uint64_t seen[2]={0,0};
        for(unsigned n=0;n<count;++n){int p=shifted_pitch(cfg,pitches[n],offset);
            if(p>=0 && p<128 && !(seen[(unsigned)p/64u]&(UINT64_C(1)<<((unsigned)p%64u)))){
                seen[(unsigned)p/64u]|=UINT64_C(1)<<((unsigned)p%64u);pitches[out++]=(uint8_t)p;}}
        count=out;}
    uint16_t groups[128];
    for(unsigned n=0;n<count;++n){velocities[n]=velocity;groups[n]=group;}
    return emit_batch(s,core,track,run->generation,pitches,velocities,groups,count,due+gate,1);
}
static int process_arp(thy_sequencer *s,thy_core *core,unsigned track,
    const thy_transport_track *run,const thy_arp_config *cfg,uint64_t now,uint64_t previous,int fresh)
{
    if(!cfg || !thy_arp_config_valid(cfg))return THY_INVALID_ARGUMENT;
    uint16_t groups[THY_SEQ_EVENTS];unsigned count=0;uint64_t newest=0,earliest=UINT64_MAX;uint16_t selected=0;
    uint32_t signature=2166136261u;
    for(unsigned n=0;n<s->count;++n){const thy_seq_event *e=&s->events[n];uint64_t onset;
        if(e->track!=track || !active_event(e,run,now,&onset))continue;
        signature=(signature^e->id)*16777619u;
        signature=(signature^(uint32_t)onset)*16777619u;
        signature=(signature^e->pitch)*16777619u;
        if(onset<earliest)earliest=onset;
        if(!(e->kind&128u) && cfg->grouping!=THY_ARP_EACH)continue;
        if((e->kind&128u) && (!selected || onset>=newest)){newest=onset;selected=e->group;}
        unsigned j=0;while(j<count && groups[j]!=e->group)++j;if(j==count)groups[count++]=e->group;
    }
    if(earliest==UINT64_MAX){s->runs[track].anchor=UINT64_MAX;s->runs[track].input_signature=0u;return THY_OK;}
    if(cfg->restart_on_change && s->runs[track].input_signature && s->runs[track].input_signature!=signature){
        s->runs[track].restart_anchor=now;s->runs[track].anchor=UINT64_MAX;
        for(unsigned v=0;v<THY_SEQ_VOICES;++v)if(s->voices[v].handle && s->voices[v].track==track &&
            s->voices[v].generated)release_voice(s,core,v);
    }
    s->runs[track].input_signature=signature;
    if(cfg->grouping==THY_ARP_COMBINED || !count){
        if(s->runs[track].anchor==UINT64_MAX){uint64_t anchor=earliest;
            if(anchor<s->runs[track].restart_anchor)anchor=s->runs[track].restart_anchor;
            s->runs[track].anchor=cfg->start==THY_ARP_GRID?
                anchor+(cfg->rate_ticks-anchor%cfg->rate_ticks)%cfg->rate_ticks:anchor;}
        return arp_phrase(s,core,run,track,cfg,0u,1,s->runs[track].anchor,now,previous,fresh);
    }
    int result=THY_OK;
    for(unsigned g=0;g<count;++g){uint64_t anchor=UINT64_MAX;
        for(unsigned n=0;n<s->count;++n){const thy_seq_event *e=&s->events[n];uint64_t onset;
            if(e->track==track && e->group==groups[g] && ((e->kind&128u) || cfg->grouping==THY_ARP_EACH) && active_event(e,run,now,&onset) && onset<anchor)anchor=onset;}
        if(anchor<s->runs[track].restart_anchor)anchor=s->runs[track].restart_anchor;
        if(cfg->start==THY_ARP_GRID)anchor+=(cfg->rate_ticks-anchor%cfg->rate_ticks)%cfg->rate_ticks;
        if(groups[g]==selected)s->runs[track].anchor=anchor;
        int r=arp_phrase(s,core,run,track,cfg,groups[g],groups[g]==selected,anchor,now,previous,fresh);if(r)result=r;
    }
    return result;
}
int thy_sequencer_capture_advance(thy_sequencer *s,thy_transport *transport,const thy_clock *clock)
{
    if(!s || !transport || !clock)return THY_INVALID_ARGUMENT;
    uint64_t now=transport->position;int ready=thy_clock_ready(clock) && !transport->suspended && transport->state==THY_TRANSPORT_PLAYING;
    if(ready && s->capture.state==THY_SEQ_ARMED)begin_capture(s,now);
    if(ready && s->capture.state==THY_SEQ_QUEUED && now>=s->capture.queued_position)begin_capture(s,s->capture.queued_position);
    if((s->capture.state==THY_SEQ_RECORDING || s->capture.state==THY_SEQ_FINISHING) && now>=s->capture.end_position){
        unsigned target=s->capture.track;int r=commit_take(s,s->capture.end_position);
        if(!r)(void)thy_transport_set_loop(transport,(uint8_t)target,s->tracks[target].loop_start,s->tracks[target].loop_end-s->tracks[target].loop_start);
        return r;
    }
    return THY_OK;
}
int thy_sequencer_advance(thy_sequencer *s,thy_core *core,thy_transport *transport,
    const thy_clock *clock,const thy_arp_config presets[2])
{
    if(!s || !core || !transport || !clock)return THY_INVALID_ARGUMENT;
    uint64_t now=transport->position;int ready=thy_clock_ready(clock) && !transport->suspended && transport->state==THY_TRANSPORT_PLAYING;
    (void)thy_sequencer_capture_advance(s,transport,clock);
    /* Publish benign extension before computing any owned release: a stored
     * note that crosses the old end can now finish in the extended range. */
    for(unsigned track=0;track<THY_SEQ_TRACKS;++track){const thy_seq_track *t=&s->tracks[track];
        if(transport->tracks[track].loop_start!=t->loop_start || transport->tracks[track].loop_length!=t->loop_end-t->loop_start)
            (void)sync_loop(s,transport,(uint8_t)track,t->loop_start,t->loop_end);
    }
    for(unsigned track=0;track<THY_SEQ_TRACKS;++track)if(s->sustain_tracks[track] &&
        (!ready || !transport->tracks[track].running || s->tracks[track].muted ||
        s->runs[track].generation!=transport->tracks[track].generation || !thy_sequencer_track_count(s,(uint8_t)track)))
        release_sustain_track(s,core,track);
    /* Edits and transport changes are resolved before any new admission. */
    for(unsigned n=0;n<THY_SEQ_VOICES;++n)if(s->voices[n].handle){thy_seq_voice *v=&s->voices[n];
        const thy_transport_track *run=&transport->tracks[v->track];
        const thy_seq_event *e=v->event_id && !v->generated?thy_sequencer_find(s,v->event_id):0;
        int source_present=!v->generated;
        if(v->generated)for(unsigned at=0;at<s->count;++at){const thy_seq_event *input=&s->events[at];uint64_t onset;
            if(input->track==v->track && (!v->event_id || input->group==v->event_id) && active_event(input,run,now,&onset)){source_present=1;break;}}
        if(e && !v->generated && e->tick==v->event_tick) {
            uint32_t duration=e->duration,remaining=run->loop_start+run->loop_length-e->tick;
            if(duration>remaining)duration=remaining;
            v->off_position=v->on_position+duration;
        }
        if(!ready || !run->running || s->tracks[v->track].muted || run->generation!=v->generation ||
            s->runs[v->track].processor!=s->tracks[v->track].processor ||
            (!source_present) ||
            (!v->generated && v->event_id && (!e || e->track!=v->track || e->tick!=v->event_tick || e->pitch!=
                (thy_core_generated_find(core,v->handle)?thy_core_generated_find(core,v->handle)->keys[0]%128u:255u))) ||
            !thy_core_generated_find(core,v->handle) || v->off_position<=now)release_voice(s,core,n);
    }
    int result=THY_OK;
    for(unsigned track=0;track<THY_SEQ_TRACKS;++track){thy_seq_run *runtime=&s->runs[track];thy_transport_track *run=&transport->tracks[track];
        int fresh=!runtime->initialized || runtime->generation!=run->generation;
        if(!ready || !run->running){runtime->initialized=0;continue;}
        uint64_t previous=fresh?now-(run->playhead-run->loop_start):runtime->last_position;
        if(now<previous)fresh=1;
        if(fresh){runtime->anchor=UINT64_MAX;runtime->ordinal=now-(run->playhead-run->loop_start);
            runtime->restart_anchor=0u;runtime->input_signature=0u;}
        if((s->tracks[track].flags&THY_SEQ_FLAG_ONESHOT) && now-runtime->ordinal>=run->loop_length){
            run->running=0;run->enabled=0;runtime->initialized=0;
            release_sustain_track(s,core,track);
            for(unsigned v=0;v<THY_SEQ_VOICES;++v)if(s->voices[v].handle && s->voices[v].track==track)release_voice(s,core,v);
            continue;
        }
        if(s->tracks[track].muted){runtime->generation=run->generation;runtime->processor=s->tracks[track].processor;
            runtime->initialized=1u;runtime->last_position=now;continue;}
        if(s->tracks[track].processor && presets){int r=process_arp(s,core,track,run,&presets[s->tracks[track].processor-1u],now,previous,fresh);if(r)result=r;}
        if(!s->tracks[track].processor || !presets || presets[s->tracks[track].processor-1u].layer)
        for(unsigned n=0;n<s->count;++n){const thy_seq_event *e=&s->events[n];
            if(e->track!=track || e->tick<run->loop_start || e->tick>=run->loop_start+run->loop_length)continue;
            uint64_t due=occurrence(e,run,now);if(!due_since(due,previous,now,fresh))continue;
            uint32_t duration=e->duration,remaining=run->loop_start+run->loop_length-e->tick;if(duration>remaining)duration=remaining;
            if(now-due>60u || (type_of(e)==THY_SEQ_NOTE && now-due>=duration)){++s->skipped;continue;}
            if(type_of(e)==THY_SEQ_NOTE){
                /* All simultaneous members of one source group preflight as a
                 * batch, then publish together. Different groups remain layers. */
                int first=1;for(unsigned j=0;j<n;++j)if(s->events[j].track==track && s->events[j].group==e->group &&
                    s->events[j].tick==e->tick && type_of(&s->events[j])==THY_SEQ_NOTE){first=0;break;}
                if(!first)continue;
                uint8_t pitches[128],velocities[128];uint16_t ids[128];unsigned count=0;
                for(unsigned j=n;j<s->count;++j){const thy_seq_event *member=&s->events[j];if(member->track==track && member->group==e->group &&
                    member->tick==e->tick && type_of(member)==THY_SEQ_NOTE){if(count<128u){pitches[count]=member->pitch;velocities[count]=member->value;ids[count]=member->id;}++count;}}
                if(count>128u){result=fail(s,THY_OWNER_LIMIT);continue;}
                int r=emit_batch(s,core,track,run->generation,pitches,velocities,ids,count,due+duration,0);if(r)result=r;
                /* Durations are individual even for a grouped chord. */
                for(unsigned v=0;v<THY_SEQ_VOICES;++v)if(s->voices[v].handle && s->voices[v].track==track && !s->voices[v].generated &&
                    s->voices[v].generation==run->generation){const thy_seq_event *ve=thy_sequencer_find(s,s->voices[v].event_id);
                    if(ve && ve->group==e->group && ve->tick==e->tick){uint32_t d=ve->duration;if(d>remaining)d=remaining;
                        s->voices[v].off_position=due+d;s->voices[v].on_position=due;s->voices[v].event_tick=ve->tick;}}
            }else{int r=emit_message(s,core,track,e);if(r){fail(s,r);result=r;}}
        }
        runtime->generation=run->generation;runtime->processor=s->tracks[track].processor;runtime->initialized=1u;runtime->last_position=now;
    }
    return result;
}
int thy_sequencer_stop(thy_sequencer *s,thy_core *core,const thy_transport *t,const thy_clock *clock)
{
    if(!s || !core || !t || !clock)return THY_INVALID_ARGUMENT;
    int result=thy_sequencer_finish(s,t,clock,1);thy_sequencer_release(s,core);
    if(s->capture.state==THY_SEQ_ARMED || s->capture.state==THY_SEQ_QUEUED || s->capture.state==THY_SEQ_FAILED)thy_sequencer_cancel(s);
    return result;
}

static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){put16(p,(uint16_t)v);put16(p+2,(uint16_t)(v>>16));}
static uint16_t get16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t get32(const uint8_t *p){return get16(p)|((uint32_t)get16(p+2)<<16);}
static uint32_t crc32(const uint8_t *p,size_t count)
{uint32_t crc=UINT32_MAX;for(size_t n=0;n<count;++n){crc^=p[n];for(unsigned bit=0;bit<8u;++bit)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}return ~crc;}
static void encode_track(uint8_t *p,const thy_seq_track *t)
{put32(p,t->content_end);put32(p+4,t->loop_start);put32(p+8,t->loop_end);put32(p+12,t->revision);
 p[16]=t->channel;p[17]=t->destinations;p[18]=t->velocity;p[19]=t->muted;p[20]=t->processor;p[21]=t->record_source;p[22]=t->record_policy;p[23]=t->flags;put32(p+24,t->grid_ticks);}
static thy_seq_track decode_track(const uint8_t *p,unsigned version)
{
 thy_seq_track t={.content_end=get32(p),.loop_start=get32(p+4),.loop_end=get32(p+8),.revision=get32(p+12),
 .channel=p[16],.destinations=p[17],.velocity=p[18],.muted=p[19],.processor=p[20],.record_source=p[21],.record_policy=p[22],.flags=p[23],
 .grid_ticks=version==2u?get32(p+24):240u};
 /* Legacy long songs retain every event and loop tick; choose a grid which
  * fits the loop into the editor instead of truncating imported content. */
 if(version==1u && t.loop_end>t.loop_start && (t.loop_end-t.loop_start)>48u*t.grid_ticks)
     t.grid_ticks=(t.loop_end-t.loop_start+47u)/48u;
 return t;
}
static void encode_event(uint8_t *p,const thy_seq_event *e)
{put32(p,e->tick);put32(p+4,e->duration);put16(p+8,e->id);put16(p+10,e->group);p[12]=e->track;p[13]=e->kind;p[14]=e->pitch;p[15]=e->value;}
static thy_seq_event decode_event(const uint8_t *p)
{return (thy_seq_event){.tick=get32(p),.duration=get32(p+4),.id=get16(p+8),.group=get16(p+10),.track=p[12],.kind=p[13],.pitch=p[14],.value=p[15]};}
size_t thy_sequencer_song_size(const thy_sequencer *s)
{return s?32u+THY_SEQ_TRACKS*28u+s->count*16u:0u;}
int thy_sequencer_encode(const thy_sequencer *s,uint8_t *out,size_t capacity,size_t *written)
{
    if(!s || !out || !written || s->count>THY_SEQ_EVENTS || capacity<thy_sequencer_song_size(s))return THY_INVALID_ARGUMENT;
    size_t size=thy_sequencer_song_size(s);seq_fill(out,0,32u);seq_copy(out,"THYS",4u);put16(out+4,2u);put16(out+6,32u);
    put32(out+8,(uint32_t)size);put16(out+16,s->count);put16(out+18,THY_SEQ_TRACKS);
    put16(out+20,s->next_id);put16(out+22,s->next_group);put32(out+24,s->revision);
    for(unsigned t=0;t<THY_SEQ_TRACKS;++t){thy_seq_track config=s->tracks[t];
        uint32_t needed=config.loop_end>config.loop_start?(config.loop_end-config.loop_start+47u)/48u:0u;
        if(config.grid_ticks && config.grid_ticks<needed)config.grid_ticks=needed;
        if(thy_sequencer_track_count(s,(uint8_t)t))config.flags|=THY_SEQ_FLAG_CREATED;
        encode_track(out+32u+t*28u,&config);}
    for(unsigned n=0;n<s->count;++n)encode_event(out+32u+THY_SEQ_TRACKS*28u+n*16u,&s->events[n]);
    put32(out+12,crc32(out+16,size-16u));
    int result=thy_sequencer_validate(out,size);if(result)return result;
    *written=size;return THY_OK;
}
int thy_sequencer_validate(const uint8_t *p,size_t size)
{
    if(!p || size<32u+THY_SEQ_TRACKS*24u || size>THY_SEQ_SONG_MAX)return THY_SEQ_CORRUPT;
    unsigned version=get16(p+4),stride=version==2u?28u:24u;
    if(seq_compare(p,"THYS",4u) ||
        (version!=1u && version!=2u) || get16(p+6)!=32u || get32(p+8)!=size || get16(p+18)!=THY_SEQ_TRACKS ||
        get16(p+16)>THY_SEQ_EVENTS || size!=32u+THY_SEQ_TRACKS*stride+get16(p+16)*16u ||
        get32(p+12)!=crc32(p+16,size-16u) || get32(p+28))return THY_SEQ_CORRUPT;
    for(unsigned t=0;t<THY_SEQ_TRACKS;++t){const uint8_t *tp=p+32u+t*stride;thy_seq_track track=decode_track(tp,version);
        if(!valid_track(&track) || (version==1u && (track.flags&128u)))return THY_SEQ_CORRUPT;}
    uint16_t max_id=0,max_group=0;
    for(unsigned n=0;n<get16(p+16);++n){const uint8_t *ep=p+32u+THY_SEQ_TRACKS*stride+n*16u;thy_seq_event e=decode_event(ep);
        if(!valid_event(&e) || e.track>=THY_SEQ_TRACKS || !e.id || !e.group)return THY_SEQ_CORRUPT;
        thy_seq_track track=decode_track(p+32u+e.track*stride,version);
        if(e.tick+e.duration>track.content_end || (version==2u && !(track.flags&THY_SEQ_FLAG_CREATED)))return THY_SEQ_CORRUPT;
        for(unsigned j=0;j<n;++j)if(get16(p+32u+THY_SEQ_TRACKS*stride+j*16u+8u)==e.id)return THY_SEQ_CORRUPT;
        if(e.id>max_id)max_id=e.id;if(e.group>max_group)max_group=e.group;
    }
    if((get16(p+20) && get16(p+20)<=max_id) || (get16(p+22) && get16(p+22)<=max_group))return THY_SEQ_CORRUPT;
    return THY_OK;
}
int thy_sequencer_decode(thy_sequencer *s,const uint8_t *bytes,size_t length)
{
    if(!s)return THY_INVALID_ARGUMENT;int r=thy_sequencer_validate(bytes,length);if(r)return r;
    thy_sequencer_init(s, s->history);s->count=get16(bytes+16);s->next_id=get16(bytes+20);s->next_group=get16(bytes+22);s->revision=get32(bytes+24);
    unsigned version=get16(bytes+4),stride=version==2u?28u:24u;
    for(unsigned t=0;t<THY_SEQ_TRACKS;++t)s->tracks[t]=decode_track(bytes+32u+t*stride,version);
    for(unsigned n=0;n<s->count;++n){s->events[n]=decode_event(bytes+32u+THY_SEQ_TRACKS*stride+n*16u);
        if(version==1u)s->tracks[s->events[n].track].flags|=THY_SEQ_FLAG_CREATED;}
    return THY_OK;
}
