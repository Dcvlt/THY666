/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_engine.h"
#include "thy_engines.h"
#include "thy_voicing.h"
#include <string.h>

static uint8_t pc(int n) { n%=12;return (uint8_t)(n<0?n+12:n); }
static unsigned bits(uint16_t n) { unsigned v=0;for(;n;n&=(uint16_t)(n-1u))++v;return v; }
static void changed(thy_engine *e)
{
    if(++e->harmony.revision==0u)e->harmony.revision=1u;
    e->harmony.changed_ms=e->instrument->core->now_ms;
}
static uint16_t note_mask(const int16_t *notes,size_t n)
{uint16_t m=0;for(size_t k=0;k<n;++k)if(notes[k]>=0&&notes[k]<128)m|=(uint16_t)(1u<<pc(notes[k]));return m;}
static uint32_t hash(uint32_t h,const void *data,size_t n)
{const uint8_t *p=data;while(n--){h^=*p++;h*=16777619u;}return h;}
static uint32_t signature(const thy_engine *e)
{
    const thy_instrument *i=e->instrument;uint32_t h=2166136261u;
#define H(v) h=hash(h,&(v),sizeof(v))
    /* Candidate harmony depends on the key, not authored chord storage or
     * per-pad voicings. Keep this foreground signature small and bounded. */
    H(e->scale);H(i->layout_revision);H(i->performance.root);H(i->transpose[0]);
    H(i->degrees);H(i->degree_count);H(i->user_scale);H(i->cluster_swap);
#undef H
    return h?h:1u;
}
int thy_engine_harmony_active(const thy_engine *e)
{return e&&e->instrument&&e->instrument->engine_kind==THY_PLAY_ENGINE_HARMONY;}
void thy_engine_harmony_label(uint16_t mask,uint8_t root,char out[32])
{
    int16_t notes[12];size_t count=0;thy_harmony result;
    if(root>11u)root=0u;
    for(unsigned n=0;n<12u;++n)if(mask&(1u<<((root+n)%12u)))notes[count++]=(int16_t)(60u+root+n);
    if(thy_harmony_from_notes(notes,count,root,&result)!=THY_HARMONY_OK||
        thy_harmony_format(&result,out,32u)!=THY_HARMONY_OK)out[0]=0;
}
void thy_engine_harmony_melody_label(const thy_engine *e,char out[32])
{
    static const char *const names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    unsigned at=0;out[0]=0;if(!e)return;
    for(unsigned p=0;p<12u;++p)if(e->harmony.right_mask&(1u<<p)){
        unsigned len=(unsigned)strlen(names[p]);if(at+len+(at?1u:0u)>31u)break;
        if(at)out[at++]=' ';for(unsigned n=0;n<len;++n)out[at++]=names[p][n];out[at]=0;
    }
}
static void text(char out[32],const char *s)
{unsigned n=0;while(s&&s[n]&&n<31u){out[n]=s[n];++n;}out[n]=0;}
void thy_engine_harmony_key_label(const thy_engine *e,int target,char out[32])
{
    static const char *const roots[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    if(!e||!e->instrument){out[0]=0;return;}
    const thy_harmony_workspace *w=&e->harmony;
    const char *name=target?thy_guidance_scale_name(w->target_family):
        thy_guidance_scale_name((unsigned)thy_guidance_catalog_family(e->scale));
    if(!target&&e->instrument->user_scale)name=(const char *)e->instrument->library.scales[e->instrument->user_scale-1u].name;
    unsigned root=target?w->target_root:w->source_root;if(root>11u)root=0;
    text(out,roots[root]);unsigned n=(unsigned)strlen(out);out[n++]=' ';
    if(!name)name="Custom key";for(unsigned k=0;name[k]&&n<31u;++k)out[n++]=name[k];out[n]=0;
}
static uint8_t degree(const thy_engine *e,unsigned root)
{
    const thy_instrument *i=e->instrument;
    for(unsigned n=0;n<i->degree_count;++n)
        if(pc(e->harmony.source_root+i->degrees[n])==root)return (uint8_t)n;
    return UINT8_MAX;
}
/* Independent interval vocabulary, not a subset of the performance layout. */
static const uint16_t qualities[]={0x091u,0x089u,0x491u,0x891u,
    0x489u,0x085u,0x0a1u,0x049u,0x249u,0x449u,0x111u,0x291u,
    0x289u,0x895u,0x495u,0x48du};
static uint16_t rotate(uint16_t mask,unsigned root)
{return (uint16_t)(((mask<<root)|(mask>>(12u-root)))&4095u);}
int thy_engine_harmony_slot(uint8_t logical)
{return logical<48u?(int)logical:-1;}
void thy_engine_harmony_invalidate(thy_engine *e)
{e->harmony.dirty=1u;e->harmony.job_active=0u;++e->harmony.revision;}
int thy_engine_harmony_chord_plan(void *context,uint8_t logical,int16_t notes[7],uint8_t *valid)
{
    thy_engine *e=context;
    if(!thy_engine_harmony_active(e)||logical>=48u)return 0;
    if(!notes||!valid)return 1;
    *valid=0;for(unsigned n=0;n<7u;++n)notes[n]=0;
    const thy_harmony_workspace *w=&e->harmony;unsigned root=logical/4u,used=0;
    uint16_t mask=w->cache_ready?w->pad_masks[logical]:rotate(qualities[logical%4u],root);
    for(unsigned n=0;n<12u&&used<7u;++n)if(mask&(1u<<pc(root+n))){
        notes[used]=(int16_t)(60+root+n-e->instrument->transpose[0]);
        *valid|=(uint8_t)(1u<<used++);
    }
    return 1;
}
void thy_engine_harmony_reason(const thy_engine *e,unsigned choice,char out[32])
{
    if(!e||choice>=e->harmony.count){text(out,"Play a chord");return;}
    const thy_harmony_choice *c=&e->harmony.choices[choice];
    if(c->reason==THY_HARMONY_RESOLVE)text(out,"Resolves the previous chord");
    else if(c->reason==THY_HARMONY_SECONDARY){
        static const char *const names[]={"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
        text(out,"Leads to ");const char *s=names[(c->root+5u)%12u];unsigned n=9u;
        while(*s&&n<31u)out[n++]=*s++;out[n]=0;
    }
    else if(c->reason==THY_HARMONY_PARALLEL)text(out,"Parallel major / minor");
    else if(c->reason==THY_HARMONY_BORROWED)text(out,"Outside key; home unchanged");
    else if(c->reason==THY_HARMONY_EXTENSION)text(out,"Another color on this root");
    else if(c->reason==THY_HARMONY_PATTERN_NEXT)text(out,c->degree==0u?"Resolve to the tonic":c->degree==4u?"Dominant tension":"Familiar next step");
    else if(c->shared){text(out,"Shared notes: ");unsigned n=(unsigned)strlen(out);out[n++]=(char)('0'+c->shared);out[n]=0;}
    else text(out,"Fits the current key");
}
void thy_engine_harmony_reset(thy_engine *e)
{
    if(!e)return;thy_harmony_workspace *w=&e->harmony;
    uint8_t prefs[]={w->pattern,w->highlights,w->motion,w->view,w->control_bank,w->octave_target,
        w->right_mode,w->analysis,w->bass_root,w->low_note,w->high_note,w->direction,w->pad_map,w->hint_contrast,
        w->outside,w->stay_in_key,w->lock_map,w->palette};
    uint32_t revision=e->harmony.revision;
    e->harmony=(thy_harmony_workspace){.revision=revision,.current_root=UINT8_MAX,.current_pad=UINT8_MAX,
        .selected_suggestion=UINT8_MAX,.target_catalog=UINT8_MAX,.preview_scale=UINT8_MAX,
        .pattern=prefs[0],.highlights=prefs[1],.motion=prefs[2],.view=prefs[3],.control_bank=prefs[4],
        .octave_target=prefs[5],.right_mode=prefs[6],.analysis=prefs[7],.bass_root=prefs[8],
        .low_note=prefs[9],.high_note=prefs[10],.direction=prefs[11],.pad_map=prefs[12],.hint_contrast=prefs[13],
        .outside=prefs[14],.stay_in_key=prefs[15],.lock_map=prefs[16],.palette=prefs[17],.dirty=1u,.discovery_dirty=1u};
    if(e->instrument){e->harmony.target_root=pc(e->instrument->performance.root+e->instrument->transpose[0]);
        int f=thy_guidance_catalog_family(e->scale);e->harmony.target_family=(uint8_t)(f<0?0:f);changed(e);}
}
static void roles(thy_engine *e)
{
    thy_harmony_workspace *w=&e->harmony;
    for(unsigned p=0;p<48u;++p){uint64_t bit=UINT64_C(1)<<p;uint8_t role=THY_HARMONY_OTHER;
        if(w->cache_ready&&w->pad_masks[p]){
            for(unsigned n=0;n<w->count;++n)if(w->choices[n].pads&bit)role=THY_HARMONY_SUGGESTED;
            if(w->selected_suggestion<w->count&&(w->choices[w->selected_suggestion].pads&bit))role=THY_HARMONY_SELECTED;
            if(w->base_mask&&w->pad_masks[p]==w->base_mask)role=THY_HARMONY_CURRENT;
        }w->pad_roles[p]=role;
    }
}
uint8_t thy_engine_harmony_pad_role(const thy_engine *e,uint8_t logical)
{return thy_engine_harmony_active(e)&&logical<48u&&e->harmony.highlights?e->harmony.pad_roles[logical]:THY_HARMONY_OTHER;}
/* Integer tonal centroid: pitch-class relatedness, not actual voice leading. */
static unsigned distance(uint16_t a,uint16_t b)
{
    static const int8_t cosine[12]={16,14,8,0,-8,-14,-16,-14,-8,0,8,14};
    static const uint8_t harmonics[3]={7,3,4};
    unsigned na=bits(a),nb=bits(b),result=0;if(!na||!nb)return 0;
    for(unsigned h=0;h<3u;++h)for(unsigned axis=0;axis<2u;++axis){
        int x=0,y=0;for(unsigned n=0;n<12u;++n){int v=cosine[(n*harmonics[h]+axis*3u)%12u];
            if(a&(1u<<n))x+=v;if(b&(1u<<n))y+=v;}
        int d=x*12/(int)na-y*12/(int)nb;result+=(unsigned)(d<0?-d:d);
    }
    return result/6u;
}
static int score(thy_engine *e,uint16_t mask,unsigned root,uint8_t *reason)
{
    thy_harmony_workspace *w=&e->harmony;
    uint16_t input=w->ranking_mask?w->ranking_mask:w->key_mask;
    unsigned outside=bits(mask&~w->key_mask),shared=bits(mask&input);
    unsigned current=w->context_root<12u?w->context_root:w->source_root;
    unsigned move=(root+12u-current)%12u;
    uint16_t relative=rotate(mask,(12u-root)%12u),from=rotate(w->base_mask?w->base_mask:w->ranking_mask,(12u-current)%12u);
    int value=600+(int)shared*24-(int)distance(input,mask)*2-(int)bits(mask)*10;
    *reason=outside?THY_HARMONY_BORROWED:THY_HARMONY_IN_KEY;
    if(outside)value-=(int)outside*(120-(int)w->outside*2);
    if(shared&&!outside)*reason=THY_HARMONY_COMMON_TONES;
    if(w->stay_in_key&&outside)return -30000;
    if((from&0x491u)==0x491u&&move==5u&&(relative==0x091u||relative==0x089u||relative==0x891u||relative==0x489u)){
        value+=w->direction==1u?420:310;*reason=THY_HARMONY_RESOLVE;
    }else if(w->base_mask&&move==5u&&!outside){value+=200;*reason=THY_HARMONY_PATTERN_NEXT;}
    else if(w->base_mask&&(move==7u||move==9u||move==2u)&&!outside){value+=100;*reason=THY_HARMONY_PATTERN_NEXT;}
    if(relative==0x491u && (w->key_mask&(1u<<((root+5u)%12u)))){
        value+=w->direction==3u?240:80;if(*reason!=THY_HARMONY_RESOLVE)*reason=THY_HARMONY_SECONDARY;
    }
    if(root==current&&w->base_mask){value-=120;*reason=THY_HARMONY_EXTENSION;
        if((from==0x091u&&relative==0x089u)||(from==0x089u&&relative==0x091u)){
            value+=w->direction==2u?260:90;*reason=THY_HARMONY_PARALLEL;
        }else if(w->direction==2u&&mask!=w->base_mask)value+=160;
    }
    if(w->direction==1u&&root==w->source_root&&!outside)value+=130;
    if(w->direction==3u&&outside)value+=120;
    for(unsigned n=0;n<w->history_count;++n)if(w->history[n]==mask)value-=120/(int)(n+1u);
    if(mask==w->base_mask)value-=600;
    if(w->analysis==1u&&w->melody_accepted)value+=(int)bits(mask&w->ranking_mask)*18;
    return value;
}
static void prepare_root(thy_engine *e,unsigned root)
{
    thy_harmony_workspace *w=&e->harmony;
    uint16_t best[4]={0};int values[4]={-32000,-32000,-32000,-32000};uint8_t why[4]={0};
    if(w->lock_map&&w->cache_ready&&w->palette==w->job_palette){
        for(unsigned row=0;row<4u;++row){unsigned p=root*4u+row;best[row]=w->pad_masks[p];values[row]=score(e,best[row],root,&why[row]);}
    }else if(w->palette){
        for(unsigned row=0;row<4u;++row){best[row]=rotate(qualities[(w->palette-1u)*4u+row],root);values[row]=score(e,best[row],root,&why[row]);}
    }else{
        for(unsigned q=0;q<sizeof qualities/sizeof qualities[0];++q){
            uint16_t mask=rotate(qualities[q],root);uint8_t reason;int value=score(e,mask,root,&reason);
            int map_value=value<=-30000?-(int)q-2000:value;
            for(unsigned row=0;row<4u;++row)if(map_value>values[row]){
                for(unsigned k=3u;k>row;--k){best[k]=best[k-1u];values[k]=values[k-1u];why[k]=why[k-1u];}
                best[row]=mask;values[row]=map_value;why[row]=reason;break;
            }
        }
        if(w->cache_ready){uint16_t ordered[4]={0};unsigned used=0;
            for(unsigned row=0;row<4u;++row)for(unsigned q=0;q<4u;++q)
                if(!(used&(1u<<q))&&w->pad_masks[root*4u+row]==best[q]){ordered[row]=best[q];used|=1u<<q;break;}
            for(unsigned row=0;row<4u;++row)if(!ordered[row])for(unsigned q=0;q<4u;++q)if(!(used&(1u<<q))){ordered[row]=best[q];used|=1u<<q;break;}
            memcpy(best,ordered,sizeof best);
        }
        for(unsigned row=0;row<4u;++row)values[row]=score(e,best[row],root,&why[row]);
    }
    for(unsigned row=0;row<4u;++row){unsigned p=root*4u+row;
        w->pending_masks[p]=best[row];w->pending_roots[p]=(uint8_t)root;
        w->pending_scores[p]=(int16_t)values[row];w->pending_reasons[p]=why[row];
    }
}
static void publish(thy_engine *e)
{
    thy_harmony_workspace *w=&e->harmony;unsigned roots=0;uint64_t used=0;
    memcpy(w->pad_masks,w->pending_masks,sizeof w->pad_masks);memcpy(w->pad_roots,w->pending_roots,sizeof w->pad_roots);w->count=0;
    for(unsigned n=0;n<THY_HARMONY_WORKSPACE_CHOICES;++n){int best=-30000;unsigned pad=48;
        for(unsigned p=0;p<48u;++p){if(used&(UINT64_C(1)<<p))continue;
            if(w->pad_masks[p]==w->base_mask||w->pending_scores[p]<=-30000)continue;
            unsigned root=p/4u;int value=w->pending_scores[p]-(roots&(1u<<root)?160:0);
            if(value>best){best=value;pad=p;}}
        if(pad==48u)break;unsigned root=pad/4u;uint16_t mask=w->pad_masks[pad];
        w->choices[w->count++]=(thy_harmony_choice){.pads=UINT64_C(1)<<pad,.mask=mask,.root=(uint8_t)root,
            .shared=(uint8_t)bits(mask&w->ranking_mask),.degree=degree(e,root),.reason=w->pending_reasons[pad],.pad=(uint8_t)pad};
        used|=UINT64_C(1)<<pad;roots|=1u<<root;
    }
    w->cache_ready=1u;w->cache_cursor=48u;w->selected_suggestion=w->count?0u:UINT8_MAX;
    if(w->page==THY_HARMONY_CHORDS)w->cursor=0u;
    w->job_active=w->dirty=0u;w->job_palette=w->palette;roles(e);changed(e);++e->instrument->revision;
}
static void target(thy_engine *e)
{
    thy_harmony_workspace *w=&e->harmony;
    w->target_mask=thy_guidance_scale_mask(w->target_family,w->target_root);
    int catalog=thy_guidance_playable_catalog(w->target_family);
    w->target_catalog=(uint8_t)(catalog<0?UINT8_MAX:catalog);
    w->can_apply=(uint8_t)(catalog>=0&&(unsigned)catalog<e->state.scale_count);
    if(w->key_mask&&w->target_mask)(void)thy_guidance_modulation(w->key_mask,w->target_mask,w->input_mask,&w->comparison);
    else w->comparison=(thy_modulation){0};
}
void thy_engine_harmony_tick(thy_engine *e)
{
    if(!thy_engine_harmony_active(e))return;
    thy_instrument *i=e->instrument;thy_harmony_workspace *w=&e->harmony;
    uint32_t sig=signature(e);
    if(sig!=w->layout_signature){w->layout_signature=sig;w->cache_cursor=0;w->dirty=1;w->job_active=0;
        w->source_root=pc(i->performance.root+i->transpose[0]);w->source_family=(uint8_t)e->scale;w->key_mask=0;
        for(unsigned n=0;n<i->degree_count;++n)w->key_mask|=(uint16_t)(1u<<pc(w->source_root+i->degrees[n]));
        target(e);changed(e);
    }
    /* Passing taps do not replace the last accepted harmonic context. */
    if(w->right_mask&&!w->melody_accepted&&w->analysis==0u&&!w->pinned&&
       (uint32_t)(i->core->now_ms-w->melody_since)>=180u){
        w->melody_accepted=1u;w->ranking_mask=(uint16_t)(w->base_mask|w->right_mask);thy_engine_harmony_invalidate(e);
    }
    if(w->dirty&&!w->chord_held&&!w->right_mask){
        if(!w->job_active){w->job_active=1u;w->job_root=0u;w->context_root=w->current_root;
            if(!w->base_mask&&bits(w->ranking_mask)>=3u){int16_t notes[12];size_t count=0;thy_harmony name;
                for(unsigned p=0;p<12u;++p)if(w->ranking_mask&(1u<<p))notes[count++]=(int16_t)(60u+p);
                if(thy_harmony_from_notes(notes,count,UINT8_MAX,&name)==THY_HARMONY_OK)w->context_root=name.root;}
        }
        /* One root / sixteen candidates per foreground call, never in the ISR. */
        prepare_root(e,w->job_root++);w->cache_cursor=(uint8_t)(w->job_root*4u);
        if(w->job_root==12u)publish(e);
    }
    if(w->page==THY_HARMONY_SCALES&&w->discovery_dirty){size_t count=0,total=0;
        if(w->input_mask)(void)thy_guidance_scales(w->input_mask,w->scales,THY_GUIDANCE_LIMIT,&count,&total);
        w->scale_count=(uint8_t)count;w->scale_total=(uint16_t)total;w->discovery_dirty=0;
        if(w->cursor>=count)w->cursor=0;changed(e);
    }
}
static void combined(thy_engine *e)
{
    thy_harmony_workspace *w=&e->harmony;uint16_t right=0;
    for(unsigned n=0;n<48u;++n)right|=w->right_held[n];w->right_mask=right;
    if(!w->pinned)w->input_mask=(uint16_t)(w->base_mask|(w->analysis==0u?right:0u));
    w->dirty=w->discovery_dirty=1u;w->job_active=0u;target(e);changed(e);
}
void thy_engine_harmony_input(thy_engine *e,uint8_t logical,const int16_t *notes,size_t count)
{
    if(!thy_engine_harmony_active(e)||logical>=96u||!notes||!count||count>THY_MAX_NOTES)return;
    thy_harmony_workspace *w=&e->harmony;uint16_t mask=note_mask(notes,count);if(!mask)return;
    if(logical>=48u){w->right_held[logical-48u]=mask;w->melody_since=e->instrument->core->now_ms;w->melody_accepted=0u;combined(e);return;}
    w->chord_held|=UINT64_C(1)<<logical;
    if(w->pinned)return;
    uint8_t root=(uint8_t)(logical/4u);
    thy_harmony name;if(thy_harmony_from_notes(notes,count,root,&name)==THY_HARMONY_OK&&name.root<12u)root=name.root;
    if(w->base_mask==mask&&w->current_root==root&&w->current_pad==logical){w->ranking_mask=w->input_mask;thy_engine_harmony_invalidate(e);return;}
    if(w->base_mask!=mask){
        for(unsigned n=3;n>0;--n){w->history[n]=w->history[n-1];w->history_roots[n]=w->history_roots[n-1];}
        w->history[0]=mask;w->history_roots[0]=root;if(w->history_count<4u)++w->history_count;
    }
    w->base_mask=mask;w->current_root=root;w->current_pad=logical;w->cursor=0;combined(e);w->ranking_mask=w->input_mask;
}
void thy_engine_harmony_release(thy_engine *e,uint8_t logical)
{
    if(!e||logical>=96u)return;
    if(logical<48u){e->harmony.chord_held&=~(UINT64_C(1)<<logical);return;}
    if(e->harmony.right_held[logical-48u]){e->harmony.right_held[logical-48u]=0;combined(e);}
}
int thy_engine_harmony_melody_plan(void *context,uint8_t logical,int16_t *note)
{
    thy_engine *e=context;if(!thy_engine_harmony_active(e)||logical<48u||logical>=96u)return 0;
    const thy_instrument *i=e->instrument;const thy_harmony_workspace *w=&e->harmony;
    unsigned p=logical-48u,column=p/4u,row=p%4u;
    int anchor=i->melody_anchor;
    if(w->right_mode){
        /* Follow the retained chord, never the melody additions themselves.
         * Without a chord the current key is a stable, useful fallback. */
        uint16_t mask=w->base_mask?w->base_mask:w->key_mask;
        uint8_t root=w->base_mask?w->current_root:w->source_root;
        int degrees[12];unsigned count=0;
        for(unsigned n=0;n<12u;++n)if(mask&(1u<<pc(root+n)))degrees[count++]=(int)n;
        if(!count)return 0;
        anchor=anchor-anchor%12+root-i->transpose[1];
        *note=(int16_t)(anchor+degrees[column%count]+12*(int)(column/count)-12*(int)row);
    }else *note=(int16_t)(anchor+(int)column-12*(int)row);
    return *note>=0&&*note<128?1:-1;
}
int thy_engine_harmony_voicing_plan(void *context,uint8_t logical,const int16_t notes[7],uint8_t mask,int16_t out[7])
{
    thy_engine *e=context;if(!thy_engine_harmony_active(e)||logical>=48u)return 0;
    thy_instrument *i=e->instrument;thy_harmony_workspace *w=&e->harmony;
    if(w->mapping_preview){memcpy(out,notes,7u*sizeof(*out));return 1;}
    uint8_t root=pc((int)(logical/4u)-i->transpose[0]);
    /* Constraints are on the pre-transpose voicing. A rootless or impossible
     * range keeps the authored chord rather than dropping a voice. */
    int rc=thy_voicing_lead_constrained(notes,mask,i->voicing_previous,i->voicing_previous_mask,
        w->low_note,w->high_note,root,w->bass_root,out);
    if(rc!=THY_OK){memcpy(out,notes,7u*sizeof(*out));return 1;}return 1;
}
static int apply_key(thy_engine *e)
{
    thy_harmony_workspace *w=&e->harmony;thy_instrument *i=e->instrument;
    target(e);if(!w->can_apply)return THY_STATE_UNAVAILABLE;
    if(e->state.editing||i->library_editor.open||i->guidance.open||e->calibration_open||e->sequencer_ui.open)
        return THY_STATE_BLOCKED;
    /* Validate all destination state before one publication. Owners, queued
     * strum and generated sources retain their press-time pitches/routes. */
    uint16_t relative=thy_guidance_scale_mask(w->target_family,0u);
    if(!relative||!(relative&1u))return THY_STATE_INVALID;
    uint8_t root=pc((int)w->target_root-i->transpose[0]);
    /* A deliberate destination keeps the last physical LEFT voicing as its
     * continuity reference. This never retunes a held owner. */
    i->live_degree_count=0u;i->user_scale=0;e->scale=w->target_catalog;i->performance.root=root;
    i->catalog_degree_count=i->degree_count=0;
    for(unsigned n=0;n<12u;++n)if(relative&(1u<<n))
        i->catalog_degrees[i->catalog_degree_count++]=i->degrees[i->degree_count++]=(int16_t)n;
    ++i->layout_revision;++i->revision;++e->state.revision;
    w->layout_signature=0;w->dirty=1;changed(e);return THY_STATE_OK;
}
int thy_engine_harmony_action(thy_engine *e,uint8_t action,int32_t value)
{
    if(!e||!e->instrument)return THY_STATE_INVALID;
    if(e->state.reset_open)return THY_STATE_BLOCKED;
    if(action==THY_HARMONY_ACTIVATE){if(value!=0&&value!=1)return THY_STATE_INVALID;
        return thy_engine_workspace(e,value?THY_PLAY_ENGINE_HARMONY:THY_PLAY_ENGINE_PLAY);}
    if(!thy_engine_harmony_active(e))return THY_STATE_BLOCKED;
    thy_harmony_workspace *w=&e->harmony;
    switch(action){
    case THY_HARMONY_PAGE:if(value<0||value>THY_HARMONY_KEY)return THY_STATE_INVALID;
        w->page=(uint8_t)value;w->cursor=0;w->discovery_dirty=1u;break;
    case THY_HARMONY_CURSOR:{unsigned count=w->page==THY_HARMONY_CHORDS?w->count:w->page==THY_HARMONY_SCALES?w->scale_count:2u;
        if(value<0||(unsigned)value>=count)return THY_STATE_INVALID;
        w->cursor=(uint8_t)value;if(w->page==THY_HARMONY_CHORDS){w->selected_suggestion=w->cursor;roles(e);}break;}
    case THY_HARMONY_TARGET_ROOT:if(value<0||value>11)return THY_STATE_INVALID;w->target_root=(uint8_t)value;target(e);break;
    case THY_HARMONY_TARGET_FAMILY:if(value<0||(size_t)value>=thy_guidance_scale_count())return THY_STATE_INVALID;
        w->target_family=(uint8_t)value;target(e);break;
    case THY_HARMONY_COMPARE_SCALE:if(value<0||(unsigned)value>=w->scale_count)return THY_STATE_INVALID;
        w->target_root=w->scales[value].root;w->target_family=w->scales[value].family;
        w->page=THY_HARMONY_KEY;w->cursor=0;target(e);break;
    case THY_HARMONY_APPLY_KEY:return value==1?apply_key(e):THY_STATE_INVALID;
    case THY_HARMONY_PIN_INPUT:if(value<0||value>1)return THY_STATE_INVALID;w->pinned=(uint8_t)value;if(!value)combined(e);w->ranking_mask=w->input_mask;break;
    case THY_HARMONY_CLEAR_INPUT:w->history_count=0;w->melody_accepted=0;w->base_mask=w->input_mask=0;w->current_root=w->current_pad=UINT8_MAX;combined(e);w->ranking_mask=w->input_mask;break;
    case THY_HARMONY_CAPTURE_SELECTED:{int16_t notes[THY_MAX_NOTES];size_t count=0;
        if(e->instrument->selected_chord>=48u||thy_instrument_preview_source(e->instrument,e->instrument->selected_chord,notes,&count)!=THY_OK||!count)return THY_STATE_UNAVAILABLE;
        uint8_t pin=w->pinned;uint64_t held=w->chord_held;w->pinned=0;
        thy_engine_harmony_input(e,e->instrument->selected_chord,notes,count);
        w->chord_held=held;w->pinned=pin;break;}
    case THY_HARMONY_CAPTURE_HELD:w->input_mask=(uint16_t)(w->base_mask|w->right_mask);w->ranking_mask=w->input_mask;w->pinned=1;w->dirty=w->discovery_dirty=1;w->job_active=0;target(e);break;
    default:return THY_STATE_INVALID;
    }
    changed(e);return THY_STATE_OK;
}
int thy_engine_harmony_control(thy_engine *e,uint8_t bank,uint8_t index,int pressed)
{
    if(!thy_engine_harmony_active(e)||e->arp_quick_open||e->state.level!=THY_STATE_PLAY)return 0;
    thy_harmony_workspace *w=&e->harmony;thy_instrument *i=e->instrument;
    if(bank==2u){if(pressed){(void)thy_engine_harmony_action(e,THY_HARMONY_TARGET_ROOT,index);w->preview_scale=UINT8_MAX;w->page=THY_HARMONY_KEY;}return 1;}
    if(bank==1u&&index>=7u){
        if(pressed){unsigned key=index-7u;
            if(w->control_bank){i->lower_mode=THY_LOWER_OCTAVE_LAYERS;i->slot_scope=THY_SLOTS_LEFT_SIDE;
                i->octave_layers^=(uint8_t)(1u<<key);if(!i->octave_layers)i->octave_layers=8u;
                ++i->layout_revision;
            }else if(key<w->count){w->cursor=w->selected_suggestion=(uint8_t)key;w->page=THY_HARMONY_CHORDS;roles(e);}
            changed(e);
        }return 1;
    }
    if(bank!=3u)return 0;
    if(!pressed)return 1;
    if(index==0u)w->control_bank^=1u;
    else if(index==1u)w->right_mode^=1u;
    else if(index>=2u&&index<=8u){unsigned catalog=thy_instrument_scale_shortcut(i,index);
        int family=thy_guidance_catalog_family(catalog);
        if(family>=0){w->target_family=(uint8_t)family;w->preview_scale=(uint8_t)(index-2u);w->page=THY_HARMONY_KEY;target(e);}
    }else if(index==11u)w->octave_target^=1u;
    else if(index==9u||index==10u){
        uint16_t row=w->octave_target?THY_ROW_PERFORMANCE_MELODYOCTAVE:THY_ROW_PERFORMANCE_CHORDOCTAVE;
        int32_t value=0;if(thy_state_get(&e->state,row,&value)==THY_STATE_OK){
            int32_t next=value+(index==9u?-1:1);
            if(next>=thy_menu_rows[row].min&&next<=thy_menu_rows[row].max)(void)thy_state_set(&e->state,row,next);
        }
    }
    changed(e);return 1;
}
int thy_engine_harmony_navigate(thy_engine *e,uint8_t action)
{
    if(!thy_engine_harmony_active(e)||action>THY_STATE_RIGHT)return THY_STATE_INVALID;
    thy_harmony_workspace *w=&e->harmony;
    if(action==THY_STATE_SELECT){w->view_picker=0u;thy_state_open(&e->state);return THY_STATE_OK;}
    if(action==THY_STATE_RIGHT)return thy_state_focus(&e->state,thy_menu_rows[THY_ROW_HARMONY_DIRECTION].group,THY_STATE_NO_ROW);
    if(action==THY_STATE_LEFT){
        if(w->page!=THY_HARMONY_CHORDS){w->page=THY_HARMONY_CHORDS;changed(e);return THY_STATE_OK;}
        return thy_state_set(&e->state,THY_ROW_HARMONY_PALETTE,(w->palette+1u)%5u);
    }
    unsigned count=w->page==THY_HARMONY_CHORDS?w->count:w->page==THY_HARMONY_SCALES?w->scale_count:12u;
    if(!count)return THY_STATE_OK;
    if(w->page==THY_HARMONY_KEY)return thy_engine_harmony_action(e,THY_HARMONY_TARGET_ROOT,(w->target_root+12u+(action==THY_STATE_UP?-1:1))%12u);
    return thy_engine_harmony_action(e,THY_HARMONY_CURSOR,(w->cursor+count+(action==THY_STATE_UP?-1:1))%count);
}
