/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_companion.h"
#include "thy_studio_ui.h"
#include "thy_views.h"
#include "thy_harmony_workspace.h"
#include "thy_ui.h"
#include "thy_crc32.h"

_Static_assert(THY_GUIDANCE_LIMIT<=24u,"Harmony scale rows must fit the640-byte wire snapshot");
_Static_assert(THY_HARMONY_WORKSPACE_CHOICES<=24u,"Harmony chord choices must fit the shared recommendation area");
_Static_assert(THY_COMPANION_PAD_MAP_BYTES<=THY_COMPANION_SNAPSHOT_BYTES,"Pad labels reuse visual staging");
_Static_assert(THY_COMPANION_PERFORMANCE_BYTES<=THY_COMPANION_SNAPSHOT_BYTES,"Performance reuses visual staging");
_Static_assert(THY_COMPANION_PERFORMANCE_VOICES+THY_MPE_MEMBERS*THY_COMPANION_PERFORMANCE_RECORD_BYTES==THY_COMPANION_PERFORMANCE_TRACKS,"Performance voice bounds");
_Static_assert(THY_COMPANION_PERFORMANCE_TRACKS+THY_SEQ_TRACKS*THY_COMPANION_PERFORMANCE_RECORD_BYTES==THY_COMPANION_PERFORMANCE_BYTES,"Performance track bounds");

typedef struct {char *p;size_t used,cap;} writer;
static void add(writer *w,const char *p){while(*p && w->used+1u<w->cap)w->p[w->used++]=*p++;}
static void dec(writer *w,uint32_t n){char b[10];unsigned k=0;do{b[k++]=(char)('0'+n%10u);n/=10u;}while(n);while(k){char c[2]={b[--k],0};add(w,c);}}
static void signed_dec(writer *w,int32_t n){if(n<0){add(w,"-");dec(w,(uint32_t)(-(int64_t)n));}else dec(w,(uint32_t)n);}
static void hex(writer *w,uint32_t n,unsigned digits){static const char h[]="0123456789abcdef";while(digits){char c[2]={h[(n>>(--digits*4u))&15u],0};add(w,c);}}
static void field(writer *w,const char *name,uint32_t n){add(w,name);dec(w,n);}
static int same(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static int prefix(const char **p,const char *head){const char *q=*p;while(*head)if(*q++!=*head++)return 0;*p=q;return 1;}
static int number(const char **p,uint32_t *n){if(**p<'0'||**p>'9')return 0;uint32_t v=0;while(**p>='0'&&**p<='9'){unsigned d=(unsigned)(*(*p)++-'0');if(v>(UINT32_MAX-d)/10u)return 0;v=v*10u+d;}*n=v;return 1;}
static int integer(const char **p,int32_t *n){int minus=**p=='-';if(minus)++*p;uint32_t v;if(!number(p,&v)||v>(minus?UINT32_C(2147483648):INT32_MAX))return 0;*n=minus?(int32_t)(-(int64_t)v):(int32_t)v;return 1;}
static int space(const char **p){if(**p!=' ')return 0;++*p;return 1;}
static int schema(const char **p){const char *s=THY_MENU_SCHEMA_SHA256;for(unsigned n=0;n<64u;++n)if(!**p||*(*p)++!=s[n])return 0;return space(p);}
static void u16(uint8_t *p,unsigned at,uint16_t n){p[at]=(uint8_t)n;p[at+1u]=(uint8_t)(n>>8u);}
static void u32(uint8_t *p,unsigned at,uint32_t n){for(unsigned nbyte=0;nbyte<4u;++nbyte)p[at+nbyte]=(uint8_t)(n>>(8u*nbyte));}
static void u64(uint8_t *p,unsigned at,uint64_t n){u32(p,at,(uint32_t)n);u32(p,at+4u,(uint32_t)(n>>32u));}
static void text(uint8_t *p,unsigned at,unsigned limit,const char *s){if(s)for(unsigned n=0;n<limit && s[n];++n)p[at+n]=(uint8_t)s[n];}
static void bit(uint8_t *p,unsigned at,unsigned n){p[at+n/8u]|=(uint8_t)(1u<<(n&7u));}
void thy_companion_init(thy_companion *s,uint8_t *buffer,size_t cap,const thy_companion_identity *id,thy_companion_visual_fn visual,void *ctx)
{if(s)*s=(thy_companion){.buffer=buffer,.capacity=cap,.identity=id?*id:(thy_companion_identity){0},.visual=visual,.context=ctx};}
int thy_companion_expire(thy_companion *s,uint32_t now)
{if(s && s->active && ((uint32_t)(now-s->last_ms)>=THY_COMPANION_IDLE_MS || (uint32_t)(now-s->captured_ms)>=THY_COMPANION_MAX_AGE_MS)){s->active=0u;s->map_state=0u;return 1;}return 0;}

static void snapshot_header(thy_companion *s,uint32_t now,const char *magic,uint16_t bytes)
{
    uint8_t *p=s->buffer;for(unsigned n=0;n<bytes;++n)p[n]=0u;
    s->map_state=0u;
    text(p,0,8,magic);u16(p,8,1u);u16(p,10,bytes);u32(p,12,s->sequence+1u);u32(p,16,now);
    text(p,32,16,s->identity.build);text(p,48,24,s->identity.uid);text(p,72,64,THY_MENU_SCHEMA_SHA256);
}

static uint32_t pad_map_key(const thy_engine *e)
{
    const thy_instrument *i=e->instrument;uint32_t key=UINT32_C(2166136261);
    const thy_sequencer_ui *u=&e->sequencer_ui;
    unsigned track=u->options_open?u->options_track:e->sequencer.selected_track;
    /* Acquisition adds exactly one revision per counted edge. Pressure and
     * generated gates add none. UI gesture counters are deliberately absent:
     * only the actual surface roles and future note-planning inputs matter. */
    const uint32_t parts[]={i->revision-i->presses-i->releases,i->layout_revision,
        e->state.level,e->harmony.revision,u->open,u->view,u->input_layout,
        u->help_open,u->options_open,track,
        (uint32_t)thy_sequencer_track_exists(&e->sequencer,(uint8_t)track),
        e->calibration_open,e->sensitivity_pick,i->voicing_previous_mask,
        (uint32_t)i->live_anchor,i->live_degree_count};
    for(unsigned n=0;n<sizeof parts/sizeof parts[0];++n)key=(key^parts[n])*UINT32_C(16777619);
    for(unsigned n=0;n<7u;++n)key=(key^(uint16_t)i->voicing_previous[n])*UINT32_C(16777619);
    for(unsigned n=0;n<i->live_degree_count&&n<12u;++n)key=(key^(uint16_t)i->live_degrees[n])*UINT32_C(16777619);
    return key;
}
static void pad_map_label(const thy_engine *e,uint8_t physical,uint8_t record[16])
{
    const thy_instrument *i=e->instrument;uint8_t role=thy_instrument_role_pad(i,physical);
    int16_t notes[THY_MAX_NOTES];size_t count=0u;char label[32]={0};uint8_t kind=THY_COMPANION_PAD_HOLE;
    if(e->calibration_open||e->sensitivity_pick){
        kind=THY_COMPANION_PAD_CONTROL;text(record,1u,14u,e->calibration_open?"Calibrate":"Pick pad");record[0]=kind;return;
    }
    int source=role;
    if(e->sequencer_ui.open&&(e->state.level==THY_STATE_PLAY||e->sequencer_ui.options_open)){
        uint8_t track,pitch;uint32_t tick;
        int action=thy_sequencer_ui_preview_pad_role(&e->sequencer_ui,role,&track,&tick,&pitch);
        if(action==THY_SEQ_PAD_EDITOR){
            writer w={label,0u,sizeof label};add(&w,"Step ");dec(&w,(role%4u)*12u+role/4u+1u);
            kind=THY_COMPANION_PAD_STEP;goto finished;
        }
        if(action==THY_SEQ_PAD_INPUT_NOTES||action==THY_SEQ_PAD_INPUT_CHORDS)
            source=role%48u+(action==THY_SEQ_PAD_INPUT_NOTES?48u:0u);
        else return;
    }else{
        int control=thy_instrument_context_pad(i,role);
        if(control){
            static const char *const names[]={"Chord oct -","Chord oct +","Voicing","Bass octave","Note oct -","Note oct +"};
            kind=THY_COMPANION_PAD_CONTROL;text(record,1u,14u,names[control-1]);record[0]=kind;return;
        }
    }
    if(source<0||source>=96||thy_instrument_preview_source(i,(uint8_t)source,notes,&count)!=THY_OK||!count)return;
    kind=count==1u?THY_COMPANION_PAD_NOTE:THY_COMPANION_PAD_CHORD;
    if(count==1u)thy_ui_note_name(notes[0],label);
    else{
        thy_harmony chord;uint8_t root=UINT8_MAX;
        if(source<48){
            thy_chord_cell cell;
            if(thy_engine_harmony_active(e))root=(uint8_t)(source/4u);
            else if(thy_instrument_chord_cell(i,(uint8_t)source,&cell)==THY_OK)
                root=(uint8_t)(((cell.root+i->transpose[0])%12+12)%12);
        }
        if(thy_harmony_from_notes(notes,count,root,&chord)!=THY_HARMONY_OK||
            thy_harmony_format(&chord,label,sizeof label)!=THY_HARMONY_OK)return;
        unsigned length=0u;while(label[length])++length;
        if(length>14u){
            /* Preserve a complete musical identity, never cut an alteration. */
            unsigned used=0u;for(unsigned n=0u;n<length;++n)if(label[n]!=' ')label[used++]=label[n];label[used]=0;
            if(used>14u){writer w={label,0u,sizeof label};dec(&w,(uint32_t)count);add(&w," NOTES");label[w.used]=0;}
        }
    }
finished:
    record[0]=kind;text(record,1u,14u,label);
}
int thy_companion_service(thy_companion *s,const thy_engine *e,uint32_t now)
{
    if(!s||s->map_state!=1u)return 0;
    if(!s->active||!e||!e->instrument||pad_map_key(e)!=s->map_key){s->map_state=0u;s->active=0u;return 0;}
    if(thy_companion_expire(s,now))return 0;
    pad_map_label(e,s->map_cursor,s->buffer+THY_COMPANION_PAD_MAP_RECORDS+16u*s->map_cursor);
    if(++s->map_cursor<96u)return 0;
    s->captured_bytes=THY_COMPANION_PAD_MAP_BYTES;s->crc=thy_crc32(s->buffer,s->captured_bytes);
    s->map_state=2u;s->last_ms=now;return 1;
}
static void snapshot_finish(thy_companion *s,uint32_t now,uint16_t bytes)
{
    s->captured_bytes=bytes;s->crc=thy_crc32(s->buffer,bytes);++s->sequence;
    s->active=1u;s->captured_ms=s->last_ms=now;
}

static void performance_voice(uint8_t *p,const thy_core *core,const thy_mpe_voice *v)
{
    p[1]=p[2]=p[3]=p[4]=p[18]=p[19]=UINT8_MAX;
    if(!v || v->state==THY_MPE_FREE)return;
    p[0]=v->state;p[1]=v->channel;p[2]=v->pitch;p[3]=v->pad;p[4]=v->logical;
    p[5]=v->destinations;p[8]=v->velocity;p[9]=v->pressure_output;
    p[10]=v->sent_pressure;p[11]=v->sent_timbre;u16(p,12,v->sent_bend);
    p[14]=v->pressure;p[15]=v->timbre;u16(p,16,v->bend);
    p[18]=v->reference;p[19]=v->modifier?(uint8_t)(v->modifier-1u):UINT8_MAX;u32(p,20,v->started);
    if(v->pressure!=v->sent_pressure || v->timbre!=v->sent_timbre || v->bend!=v->sent_bend)
        p[7]|=THY_COMPANION_VOICE_EXPRESSION_PENDING;
    if(v->modifier)p[7]|=THY_COMPANION_VOICE_MODIFIER;
    for(unsigned destination=0;destination<THY_DEST_COUNT;++destination)if(v->destinations&(1u<<destination)){
        uint16_t key=thy_core_key((uint8_t)destination,v->channel,v->pitch);
        if(thy_core_refcount(core,key)||thy_core_deferred(core,key))p[6]|=(uint8_t)(1u<<destination);
        if(thy_core_deferred(core,key))p[7]|=THY_COMPANION_VOICE_SUSTAINED;
    }
    if(v->pad<THY_PAD_COUNT){
        const thy_pad_owner *owner=&core->owners[v->pad];
        /* Future attacks carry no ledger reference yet. Inspect the reserved
         * owner keys, never infer an attack from a leased member channel. */
        for(unsigned at=owner->activated;owner->held && at<owner->count;++at){
            uint16_t key=thy_pad_owner_key(owner,(uint8_t)at);
            if(key!=UINT16_MAX && key%2048u==(uint16_t)(v->channel*128u+v->pitch)){
                p[7]|=THY_COMPANION_VOICE_PENDING;break;
            }
        }
    }
}
static int performance_snapshot(thy_companion *s,const thy_engine *e,uint32_t now)
{
    if(!s->buffer || s->capacity<THY_COMPANION_PERFORMANCE_BYTES || s->sequence==UINT32_MAX)return 0;
    snapshot_header(s,now,"THYPER1",THY_COMPANION_PERFORMANCE_BYTES);
    uint8_t *p=s->buffer;const thy_core *core=e->instrument->core;
    const thy_sequencer *seq=&e->sequencer;const thy_transport *transport=&e->rhythm_transport;
    const thy_clock *clock=&e->musical_clock;const thy_mpe *mpe=e->expression;
    if(mpe && mpe->core!=core)mpe=NULL;
    u16(p,8,THY_COMPANION_PERFORMANCE_VERSION);u32(p,20,seq->revision);
    u32(p,24,transport->generation);u16(p,28,THY_CLOCK_PPQN);
    p[30]=mpe?mpe->config.bend_range:0u;p[31]=mpe?thy_mpe_manager(&mpe->config):UINT8_MAX;
    u64(p,136,transport->position);u32(p,144,clock->generation);u32(p,148,clock->tempo_milli_bpm);
    p[152]=(uint8_t)((mpe?THY_COMPANION_PERF_MPE_AVAILABLE:0u)|
        (mpe && mpe->config.enabled?THY_COMPANION_PERF_MPE_ENABLED:0u)|
        (mpe && mpe->config.upper_zone?THY_COMPANION_PERF_UPPER_ZONE:0u)|
        (transport->suspended?THY_COMPANION_PERF_SUSPENDED:0u)|
        (thy_clock_ready(clock)?THY_COMPANION_PERF_CLOCK_READY:0u));
    p[153]=transport->state;p[154]=clock->source;p[155]=clock->status;p[156]=seq->capture.state;
    p[157]=seq->capture.state==THY_SEQ_CAPTURE_OFF?UINT8_MAX:seq->capture.track;
    p[158]=seq->selected_track;p[159]=mpe?mpe->config.members:0u;
    for(unsigned n=0;n<THY_MPE_MEMBERS;++n)
        performance_voice(p+THY_COMPANION_PERFORMANCE_VOICES+n*THY_COMPANION_PERFORMANCE_RECORD_BYTES,
            core,mpe?thy_mpe_voice_at(mpe,(uint8_t)n):NULL);
    uint32_t created=0u;
    for(unsigned n=0;n<THY_SEQ_TRACKS;++n)if(seq->tracks[n].flags&THY_SEQ_FLAG_CREATED)created|=1u<<n;
    for(unsigned n=0;n<seq->count && n<THY_SEQ_EVENTS;++n)
        if(seq->events[n].track<THY_SEQ_TRACKS)created|=1u<<seq->events[n].track;
    for(unsigned n=0;n<THY_SEQ_TRACKS;++n){
        if(!(created&(1u<<n)))continue;
        const thy_seq_track *cfg=&seq->tracks[n];const thy_transport_track *run=&transport->tracks[n];
        unsigned at=THY_COMPANION_PERFORMANCE_TRACKS+n*THY_COMPANION_PERFORMANCE_RECORD_BYTES;
        u32(p,at,run->loop_start);u32(p,at+4u,run->loop_length);u32(p,at+8u,run->playhead);
        u32(p,at+12u,cfg->grid_ticks);u32(p,at+16u,run->generation);
        p[at+20u]=(uint8_t)(THY_COMPANION_TRACK_CREATED|
            (run->enabled?THY_COMPANION_TRACK_ENABLED:0u)|(run->running?THY_COMPANION_TRACK_RUNNING:0u)|
            (cfg->muted?THY_COMPANION_TRACK_MUTED:0u)|(cfg->flags&THY_SEQ_FLAG_ONESHOT?THY_COMPANION_TRACK_ONESHOT:0u)|
            (run->loop_length && cfg->grid_ticks && run->playhead>=run->loop_start &&
             run->playhead-run->loop_start<run->loop_length?THY_COMPANION_TRACK_POSITION:0u));
        p[at+21u]=run->pending;p[at+22u]=cfg->processor;
        p[at+23u]=seq->capture.state!=THY_SEQ_CAPTURE_OFF && seq->capture.track==n?seq->capture.state:THY_SEQ_CAPTURE_OFF;
    }
    snapshot_finish(s,now,THY_COMPANION_PERFORMANCE_BYTES);return 1;
}

static int harmony_snapshot(thy_companion *s,const thy_engine *e,uint32_t now)
{
    if(!s->buffer||s->capacity<THY_COMPANION_HARMONY_BYTES||s->sequence==UINT32_MAX)return 0;
    snapshot_header(s,now,"THYHRM1",THY_COMPANION_HARMONY_BYTES);
    uint8_t *p=s->buffer;const thy_harmony_workspace *h=&e->harmony;
    u16(p,8,THY_COMPANION_HARMONY_VERSION);
    p[27]=h->direction;p[28]=h->pad_map;p[29]=h->hint_contrast;p[30]=h->view_picker;
    u32(p,136,h->revision);u16(p,140,h->key_mask);u16(p,142,h->input_mask);u16(p,144,h->target_mask);
    u16(p,146,h->comparison.common);u16(p,148,h->comparison.added);u16(p,150,h->comparison.removed);
    u16(p,152,h->comparison.pivot);u16(p,154,h->scale_total);
    p[156]=(uint8_t)!!thy_engine_harmony_active(e);p[157]=h->page;p[158]=h->source_root;p[159]=h->source_family;
    p[160]=h->target_root;p[161]=h->target_family;p[162]=h->cursor;
    p[163]=h->page==THY_HARMONY_SCALES?h->scale_count:h->page==THY_HARMONY_CHORDS?h->count:0u;
    p[164]=(uint8_t)(!!h->pinned|(h->cache_ready?2u:0u));p[165]=h->comparison.pivot_root;p[166]=h->comparison.current_fits;
    p[167]=!!h->can_apply;p[168]=h->current_pad;p[169]=h->selected_suggestion;p[170]=h->target_catalog;p[171]=h->current_root;
    for(unsigned n=0;n<48u;++n){unsigned role=h->pad_roles[n];
        if(role==THY_HARMONY_CURRENT)bit(p,172,n);
        else if(role==THY_HARMONY_SELECTED)bit(p,178,n);
        else if(role==THY_HARMONY_SUGGESTED)bit(p,184,n);}
    p[190]=h->pattern;p[191]=(uint8_t)(!!h->highlights|(h->motion?2u:0u));
    thy_engine_harmony_label(h->input_mask,h->current_root,(char *)p+192);
    thy_engine_harmony_key_label(e,0,(char *)p+224);thy_engine_harmony_key_label(e,1,(char *)p+256);
    if(h->page==THY_HARMONY_CHORDS){
        unsigned count=h->count<THY_HARMONY_WORKSPACE_CHOICES?h->count:THY_HARMONY_WORKSPACE_CHOICES;p[163]=(uint8_t)count;
        if(h->cursor<count)thy_engine_harmony_label(h->choices[h->cursor].mask,h->choices[h->cursor].root,(char *)p+288);
        for(unsigned n=0;n<count;++n){const thy_harmony_choice *c=&h->choices[n];unsigned at=320u+n*8u;
            u16(p,at,c->mask);p[at+2u]=c->root;p[at+3u]=c->shared;p[at+4u]=c->degree;
            p[at+5u]=c->reason;p[at+6u]=c->pad;p[at+7u]=1u;}
    }else if(h->page==THY_HARMONY_SCALES){
        unsigned count=h->scale_count<THY_GUIDANCE_LIMIT?h->scale_count:THY_GUIDANCE_LIMIT;p[163]=(uint8_t)count;
        if(h->cursor<count){const thy_scale_match *m=&h->scales[h->cursor];char root[32]={0};
            if(m->root<12u)thy_guidance_notes_text((uint16_t)(1u<<m->root),root);
            writer label={(char *)p+288,0,32};add(&label,root);add(&label," ");
            const char *name=thy_guidance_scale_name(m->family);add(&label,name?name:"");label.p[label.used]=0;}
        for(unsigned n=0;n<count;++n){const thy_scale_match *m=&h->scales[n];unsigned at=320u+n*8u;
            u16(p,at,m->mask);p[at+2u]=m->root;p[at+3u]=m->family;p[at+4u]=m->extra;
            p[at+5u]=p[at+6u]=UINT8_MAX;p[at+7u]=1u;}
    }
    u16(p,512,h->base_mask);u16(p,514,h->right_mask);
    p[516]=h->view;p[517]=h->control_bank;p[518]=h->octave_target;p[519]=h->right_mode;
    p[520]=h->analysis;p[521]=h->bass_root;p[522]=h->low_note;p[523]=h->high_note;
    p[524]=!!e->instrument->cluster_swap;
    p[525]=e->instrument->octave_layers;
    int octave=e->instrument->performance.melody_octave;
    p[526]=octave>=-3&&octave<=3?(uint8_t)(1u<<(unsigned)(octave+3)):0u;
    for(unsigned n=0;n<48u;++n){unsigned physical=thy_instrument_physical_pad(e->instrument,(uint8_t)n);
        unsigned role=h->pad_roles[n];
        if(role==THY_HARMONY_CURRENT)bit(p,528,physical);
        else if(role==THY_HARMONY_SELECTED)bit(p,540,physical);
        else if(role==THY_HARMONY_SUGGESTED)bit(p,552,physical);
    }
    for(unsigned n=0;n<7u;++n)p[564+n]=thy_instrument_scale_shortcut(e->instrument,(uint8_t)(2u+n));
    p[571]=h->preview_scale;
    p[572]=e->instrument->voicing_mode;
    p[573]=h->count;p[574]=(uint8_t)e->instrument->performance.chord_octave;
    p[575]=(uint8_t)e->instrument->performance.melody_octave;
    thy_engine_harmony_label(h->base_mask,h->current_root,(char *)p+576);
    thy_engine_harmony_melody_label(e,(char *)p+608);
    for(unsigned n=0;n<48u;++n)u16(p,640u+n*2u,h->pad_masks[n]);
    for(unsigned n=0;n<4u;++n){u16(p,736u+n*2u,h->history[n]);p[744u+n]=h->history_roots[n];}
    p[748]=h->history_count;p[749]=h->outside;p[750]=h->stay_in_key;p[751]=h->lock_map;p[752]=h->palette;p[753]=h->dirty;
    thy_engine_harmony_reason(e,h->selected_suggestion,(char *)p+768);
    snapshot_finish(s,now,THY_COMPANION_HARMONY_BYTES);return 1;
}

static int snapshot(thy_companion *s,const thy_engine *e,uint32_t now)
{
    if(!s->buffer || s->capacity<THY_COMPANION_SNAPSHOT_BYTES || !s->visual || s->sequence==UINT32_MAX)return 0;
    snapshot_header(s,now,"THYCMP1",THY_COMPANION_SNAPSHOT_BYTES);uint8_t *p=s->buffer;
    uint32_t frame_ms=0;uint8_t visible=0;
    if(!s->visual(s->context,p+THY_COMPANION_OLED,p+THY_COMPANION_RGB,p+THY_COMPANION_CENTER,&frame_ms,&visible))return 0;
    const thy_instrument *i=e->instrument;const thy_state *state=&e->state;const thy_core *core=i->core;
    u32(p,16,now);u32(p,20,frame_ms);u32(p,24,i->revision);u32(p,28,state->revision);
    /* sounding_count includes owned and sustain-deferred keys. Pending strum
     * attacks and held Arp pools are not ledger entries; they stay separate. */
    if(core->sounding_count)
        for(unsigned n=0;n<THY_KEY_COUNT;++n)if(core->refs[n]||thy_core_deferred(core,(uint16_t)n))bit(p,THY_COMPANION_NOTES,n%128u);
    uint64_t pool[2]={0,0};
    for(unsigned a=0;a<2u;++a)if(e->arps[a].config.enabled)for(unsigned n=0;n<THY_ARP_INPUTS;++n)if(e->arps[a].inputs[n].held){
        pool[0]|=e->arps[a].inputs[n].pitches[0];pool[1]|=e->arps[a].inputs[n].pitches[1];}
    for(unsigned n=0;n<16u;++n)p[THY_COMPANION_POOL+n]=(uint8_t)(pool[n/8u]>>((n%8u)*8u));
    unsigned control=0;const unsigned counts[]={5u,14u,12u,12u};
    for(unsigned bank=0;bank<4u;++bank)for(unsigned n=0;n<counts[bank];++n,++control)if(i->control_masks[bank]&(1u<<n))bit(p,THY_COMPANION_CONTROLS,control);
    uint8_t seen[2]={0},fixed[2]={0},modes[2]={0};
    for(unsigned n=0;n<THY_PAD_COUNT;++n){
        unsigned logical=i->mapping[n];if(logical>=96u)return 0;unsigned side=logical/48u;
        const thy_instrument_sensor *sensor=&i->sensors[n];
        int held=sensor->held||core->owners[n].held||i->held_input_valid[n];
        for(unsigned a=0;a<2u;++a)if(e->arps[a].config.enabled && e->arps[a].inputs[n].held)held=1;
        if(held){bit(p,THY_COMPANION_HELD,logical);++p[1618];if(e->input_velocity[n]>p[1616+side])p[1616+side]=e->input_velocity[n];}
        if(sensor->pressure>p[1608+side])p[1608+side]=sensor->pressure;
        thy_instrument_velocity velocity;thy_instrument_route route;uint8_t custom;
        if(thy_instrument_get_velocity(i,(int)n,&velocity,&custom)!=THY_OK ||
           thy_instrument_get_pad_route(i,thy_engine_performance_route(e,(uint8_t)n),&route)!=THY_OK)return 0;
        if(e->performance[side].velocity)route.velocity=e->performance[side].velocity;
        if(!seen[side]){fixed[side]=route.velocity;modes[side]=velocity.mode;seen[side]=1u;}
        else {if(fixed[side]!=route.velocity)fixed[side]=0u;if(modes[side]!=velocity.mode)modes[side]=2u;}
        if(logical==s->selected_pad){p[1632]=(uint8_t)logical;p[1633]=(uint8_t)n;u16(p,1634,sensor->raw);u16(p,1636,(uint16_t)sensor->signed_delta);
            p[1638]=sensor->pressure;p[1639]=(uint8_t)(sensor->held|(sensor->blocked<<1u)|(sensor->warmup_count>=THY_INSTRUMENT_WARMUP_SAMPLES?4u:0u));
            p[1640]=sensor->strike_velocity;p[1641]=sensor->detector_profile;u16(p,1642,(uint16_t)sensor->attack_raw);u16(p,1644,(uint16_t)sensor->release_raw);u32(p,1646,sensor->last_sample_ms);}
    }
    for(unsigned n=0;n<128u;++n)if(p[THY_COMPANION_NOTES+n/8u]&(1u<<(n&7u)))++p[1619];
    const char *scale=i->user_scale && i->user_scale<=8u?(const char *)i->library.scales[i->user_scale-1u].name:
        e->scale<THY_MENU_DEFAULT_SCALE_COUNT?thy_menu_scale_names[e->scale]:"";
    thy_studio_ui_view_line(state,THY_VIEW_INFO_MUSICAL_NAMES,scale,1,1,(char *)p+THY_COMPANION_HARMONY);
    text(p,THY_COMPANION_SCALE_NAME,31,scale);
    p[1581]=state->group_index;p[1582]=state->level;p[1583]=state->editing;p[1584]=state->notice;
    p[1585]=state->info_open;p[1586]=state->about_open;p[1587]=state->design_editing;p[1588]=e->arp_quick_open;p[1589]=e->sequencer_ui.open;p[1590]=e->sequencer_ui.options_open;
    uint16_t row=thy_state_current_row(state);u16(p,1591,row);u16(p,1593,state->edit_row);p[1595]=state->edit_instance;
    p[1596]=row<THY_MENU_ROW_COUNT?thy_state_current_instance(state,row):UINT8_MAX;
    p[1597]=e->sensitivity_target;p[1598]=e->velocity_target;p[1599]=e->route_target;u32(p,1600,(uint32_t)state->edit_value);
    p[1604]=(uint8_t)(!!e->expression_config.enabled|(e->arps[0].config.enabled?2u:0u)|(e->arps[1].config.enabled?4u:0u)|
        (e->rhythm_transport.state==THY_TRANSPORT_PLAYING?8u:0u)|(e->sequencer.capture.state!=THY_SEQ_CAPTURE_OFF?16u:0u));
    p[1605]=i->performance.root;u16(p,1606,e->scale);p[1610]=i->expression_profiles[0].enabled;p[1611]=i->expression_profiles[1].enabled;
    p[1612]=fixed[0];p[1613]=fixed[1];p[1614]=modes[0];p[1615]=modes[1];u16(p,1620,thy_core_sounding(core));u16(p,1622,thy_core_generated_count(core));
    u32(p,1624,e->musical_clock.tempo_milli_bpm);p[1628]=e->musical_clock.source;p[1629]=e->musical_clock.status;p[1630]=e->rhythm_transport.state;p[1631]=e->sequencer.capture.state;
    u32(p,1650,i->layout_revision);u32(p,1654,s->identity.saved_generation?*s->identity.saved_generation:0u);p[1658]=!!visible;
    p[1659]=state->group_index<THY_MENU_GROUP_COUNT?state->cursors[state->group_index]:0u;
    p[1660]=e->sequencer.selected_track;p[1661]=i->display_view;p[1662]=i->engine_kind;
    p[THY_COMPANION_CLUSTER_SWAP]=!!i->cluster_swap;
    int32_t current=INT32_MIN;if(row<THY_MENU_ROW_COUNT)(void)thy_state_get(state,row,&current);u32(p,1664,(uint32_t)current);
    u32(p,THY_COMPANION_HARMONY_REVISION,e->harmony.revision);
    p[THY_COMPANION_RESET_OPEN]=!!state->reset_open;p[THY_COMPANION_RESET_CHOICE]=!!state->reset_confirm;
    p[THY_COMPANION_RESET_GROUP]=state->reset_group;p[THY_COMPANION_RESET_TRACK]=state->reset_track;
    u32(p,THY_COMPANION_RESET_EPOCH,state->reset_epoch);
    snapshot_finish(s,now,THY_COMPANION_SNAPSHOT_BYTES);s->captured=1u;s->last_visual_ms=now;return 1;
}

size_t thy_companion_command(thy_companion *s,thy_engine *e,int available,uint32_t now,const char *line,char *response,size_t capacity)
{
    if(!s||!e||!e->instrument||!e->instrument->core||!line||!response||capacity<THY_COMPANION_RESPONSE_BYTES)return 0;
    const char *p=line;uint32_t first=0,second=0,third=0;int32_t value=0;writer w={response,0,capacity};
    (void)thy_companion_expire(s,now);add(&w,"COMPANION/1 ");
    if(same(line,"companion caps")){
        add(&w,"CAPS build=");add(&w,s->identity.build?s->identity.build:"unknown");add(&w," uid=");add(&w,s->identity.uid?s->identity.uid:"unknown");
        add(&w," schema=");add(&w,THY_MENU_SCHEMA_SHA256);field(&w," bytes=",THY_COMPANION_SNAPSHOT_BYTES);field(&w," chunk=",THY_COMPANION_CHUNK_BYTES);field(&w," rows=",THY_MENU_ROW_COUNT);field(&w," writable=",!!available);
        add(&w," role=instrument backup=1 song=1 companion=1 harmony=1 harmony_bytes=");dec(&w,THY_COMPANION_HARMONY_BYTES);add(&w," reset=1 pad_map=1 pad_map_bytes=");dec(&w,THY_COMPANION_PAD_MAP_BYTES);
        add(&w," performance=1 performance_bytes=");dec(&w,THY_COMPANION_PERFORMANCE_BYTES);
    }else if(same(line,"companion status")){field(&w,"STATUS active=",s->active);field(&w," seq=",s->sequence);field(&w," selected=",s->selected_pad);field(&w," available=",!!available);}
    else if(prefix(&p,"companion release ")&&number(&p,&first)&&!*p){
        if(first!=s->sequence)add(&w,"ERR generation");else{s->active=0u;s->map_state=0u;add(&w,"RELEASED");}
    }else if(!available)add(&w,"ERR busy");
    else if(same(line,"companion snapshot")){
        if(s->active)add(&w,"ERR snapshot-active");
        else if(s->captured&&(uint32_t)(now-s->last_visual_ms)<THY_COMPANION_MIN_CAPTURE_MS)add(&w,"ERR rate");
        else if(!snapshot(s,e,now))add(&w,"ERR snapshot-unavailable");
        else{field(&w,"SNAPSHOT seq=",s->sequence);field(&w," bytes=",THY_COMPANION_SNAPSHOT_BYTES);add(&w," crc32=");hex(&w,s->crc,8u);}
    }else if(same(line,"companion performance snapshot")){
        if(s->active)add(&w,"ERR snapshot-active");
        else if(!performance_snapshot(s,e,now))add(&w,"ERR snapshot-unavailable");
        else{field(&w,"PERFORMANCE seq=",s->sequence);field(&w," bytes=",THY_COMPANION_PERFORMANCE_BYTES);add(&w," crc32=");hex(&w,s->crc,8u);}
    }else if(same(line,"companion pad-map")||
        (p=line,prefix(&p,"companion pad-map ")&&number(&p,&first)&&!*p)){
        int conditional=!same(line,"companion pad-map");
        if(s->active&&!s->map_state)add(&w,"ERR snapshot-active");
        else if(!s->active&&conditional&&first==pad_map_key(e)){
            add(&w,"PADMAP unchanged=1");field(&w," revision=",first);
        }
        else if(!s->active&&(!s->buffer||s->capacity<THY_COMPANION_PAD_MAP_BYTES||s->sequence==UINT32_MAX))add(&w,"ERR snapshot-unavailable");
        else{
            if(!s->active){
                snapshot_header(s,now,"THYMAP1",THY_COMPANION_PAD_MAP_BYTES);
                s->map_key=pad_map_key(e);u32(s->buffer,20,s->map_key);
                u32(s->buffer,24,e->instrument->layout_revision);u32(s->buffer,28,e->state.revision);
                ++s->sequence;s->active=1u;s->map_state=1u;s->map_cursor=0u;
                s->captured_bytes=0u;s->captured_ms=s->last_ms=now;
            }
            if(s->map_state==1u){add(&w,"PADMAP pending=1");field(&w," seq=",s->sequence);s->last_ms=now;}
            else{field(&w,"PADMAP seq=",s->sequence);field(&w," bytes=",THY_COMPANION_PAD_MAP_BYTES);add(&w," crc32=");hex(&w,s->crc,8u);s->last_ms=now;}
        }
    }else if(same(line,"companion harmony snapshot")){
        if(s->active)add(&w,"ERR snapshot-active");
        else if(!harmony_snapshot(s,e,now))add(&w,"ERR snapshot-unavailable");
        else{field(&w,"HARMONY seq=",s->sequence);field(&w," bytes=",THY_COMPANION_HARMONY_BYTES);add(&w," crc32=");hex(&w,s->crc,8u);}
    }else if((p=line,prefix(&p,"companion get "))&&number(&p,&first)&&space(&p)&&number(&p,&second)&&space(&p)&&number(&p,&third)&&!*p){
        if(!s->active||first!=s->sequence)add(&w,"ERR generation");
        else if(!third||third>THY_COMPANION_CHUNK_BYTES||second>s->captured_bytes||third>s->captured_bytes-second)add(&w,"ERR offset");
        else{s->last_ms=now;field(&w,"DATA seq=",first);field(&w," offset=",second);add(&w," hex=");for(unsigned n=0;n<third;++n)hex(&w,s->buffer[second+n],2u);}
    }else if((p=line,prefix(&p,"companion harmony "))&&schema(&p)&&number(&p,&first)&&space(&p)&&integer(&p,&value)&&!*p){
        if(first>THY_HARMONY_COMPARE_SCALE)add(&w,"ERR action");
        else if(e->state.reset_open)add(&w,"ERR reset-active");
        else if(e->state.editing)add(&w,"ERR draft-active");
        else{int result=thy_engine_harmony_action(e,(uint8_t)first,value);add(&w,"HARMONY result=");signed_dec(&w,result);
            field(&w," action=",first);add(&w," value=");signed_dec(&w,value);}
    }else if((p=line,prefix(&p,"companion harmony-control "))&&schema(&p)&&number(&p,&first)&&space(&p)&&number(&p,&second)&&!*p){
        if(!((first==1u&&second>=7u&&second<14u)||((first==2u||first==3u)&&second<12u)))add(&w,"ERR harmony-control");
        /* A preview must never fall through to the ordinary root/scale or Arp
         * handlers if the physical UI changed after the browser's last frame. */
        else if(!e->instrument->control_filter||!thy_engine_harmony_active(e)||e->state.level!=THY_STATE_PLAY||e->state.reset_open||e->state.editing||e->state.info_open||e->state.about_open||
            e->arp_quick_open||e->calibration_open||e->sequencer_ui.open||e->instrument->library_editor.open||e->instrument->guidance.open)
            add(&w,"ERR harmony-context");
        else if(e->instrument->control_masks[first]&(1u<<second))add(&w,"ERR control-held");
        else{int result=thy_instrument_control(e->instrument,(uint8_t)first,(uint8_t)second,1);
            int released=thy_instrument_control(e->instrument,(uint8_t)first,(uint8_t)second,0);
            add(&w,"HCONTROL result=");signed_dec(&w,result!=THY_OK?result:released);
            field(&w," bank=",first);field(&w," index=",second);}
    }else if((p=line,prefix(&p,"companion reset-confirm "))&&schema(&p)&&number(&p,&first)&&space(&p)&&number(&p,&second)&&!*p){
        if(second>1u)add(&w,"ERR reset-confirm");
        else if(!first||!e->state.reset_open||first!=e->state.reset_epoch)add(&w,"ERR reset-context");
        else if(second&&(e->instrument->control_masks[0]||e->instrument->control_masks[1]||e->instrument->control_masks[2]||e->instrument->control_masks[3]))add(&w,"ERR control-held");
        else{int result=thy_state_reset_resolve(&e->state,first,(int)second);
            add(&w,"RESET result=");signed_dec(&w,result);field(&w," epoch=",first);field(&w," confirmed=",second);}
    }else if((p=line,prefix(&p,"companion pad "))&&number(&p,&first)&&!*p){
        if(first>=96u)add(&w,"ERR pad");else{s->selected_pad=(uint8_t)first;field(&w,"PAD selected=",first);}
    }else if((p=line,prefix(&p,"companion read "))&&schema(&p)&&number(&p,&first)&&space(&p)&&number(&p,&second)&&!*p){
        if(first>=THY_MENU_ROW_COUNT||second>255u)add(&w,"ERR row");
        else{uint8_t instance=second==255u?thy_state_current_instance(&e->state,(uint16_t)first):(uint8_t)second;
            int result=thy_state_get_instance(&e->state,(uint16_t)first,instance,&value);
            if(result!=THY_STATE_OK)add(&w,"ERR read");else{field(&w,"VALUE row=",first);field(&w," instance=",instance);add(&w," value=");signed_dec(&w,value);}}
    }else if((p=line,prefix(&p,"companion set "))&&schema(&p)&&number(&p,&first)&&space(&p)&&number(&p,&second)&&space(&p)&&integer(&p,&value)&&!*p){
        if(first>=THY_MENU_ROW_COUNT||second>=thy_state_instance_count((uint16_t)first)||thy_menu_rows[first].status!=THY_MENU_LIVE||!thy_state_row_visible(&e->state,(uint16_t)first))add(&w,"ERR row");
        else if(e->state.editing)add(&w,"ERR draft-active");
        else{int result=thy_state_set_instance(&e->state,(uint16_t)first,(uint8_t)second,value);add(&w,"SET result=");signed_dec(&w,result);field(&w," row=",first);field(&w," instance=",second);}
    }else if((p=line,prefix(&p,"companion nav "))&&schema(&p)&&number(&p,&first)&&!*p){
        if(first>THY_STATE_RIGHT)add(&w,"ERR action");else if(e->state.reset_open)add(&w,"ERR reset-active");
        else{int result=thy_engine_navigate(e,(uint8_t)first);add(&w,"NAV result=");signed_dec(&w,result);}
    }else if((p=line,prefix(&p,"companion control "))&&schema(&p)&&number(&p,&first)&&space(&p)&&number(&p,&second)&&!*p){
        static const uint8_t counts[]={5u,14u,12u,12u};
        if(first>=4u||second>=counts[first])add(&w,"ERR control");
        else if(e->state.reset_open)add(&w,"ERR reset-active");
        else if(e->instrument->control_masks[first]&(1u<<second))add(&w,"ERR control-held");
        else{int result=thy_instrument_control(e->instrument,(uint8_t)first,(uint8_t)second,1);
            int released=thy_instrument_control(e->instrument,(uint8_t)first,(uint8_t)second,0);
            add(&w,"CONTROL result=");signed_dec(&w,result==THY_OK?released:result);}
    }else if((p=line,prefix(&p,"companion action "))&&schema(&p)&&number(&p,&first)&&space(&p)&&number(&p,&second)&&!*p){
        if(first>=THY_MENU_ROW_COUNT||thy_menu_rows[first].type!=THY_MENU_ACTION||thy_menu_rows[first].status!=THY_MENU_LIVE||!thy_state_row_visible(&e->state,(uint16_t)first)||second!=thy_state_current_instance(&e->state,(uint16_t)first))add(&w,"ERR row");
        else if(e->state.editing)add(&w,"ERR draft-active");
        else{int result=thy_state_begin_edit(&e->state,(uint16_t)first);add(&w,"ACTION result=");signed_dec(&w,result);}
    }else add(&w,"ERR command-or-schema");
    add(&w,"\r\n");response[w.used]=0;return w.used;
}
