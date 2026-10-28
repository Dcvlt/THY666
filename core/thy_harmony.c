/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_harmony.h"
#include "thy_core.h"

/* Canonical names describe these EXACT sets, not spellings copied blindly
 * from the archive. For example 0x451 is 7b5, not an archived 7#5 alias.
 * Rootless/malformed archive metadata never creates a rootless template.
 * Tier 0 is a common complete chord, 1 an addition/alteration, 2 an explicit
 * omitted fifth. Within the same tier, a bass-root match breaks ambiguity.
 * Sorted masks permit bounded binary search: at most seven comparisons for
 * each of the twelve possible roots. Quality indices are deliberately private.
 */
#define SHAPES(X) \
    X(DIM,       0x049u,0u,"DIM") \
    X(MAJB5,     0x051u,1u,"MAJ b5") \
    X(FIFTH,     0x081u,0u,"5") \
    X(SUS2,      0x085u,0u,"SUS2") \
    X(MIN,       0x089u,0u,"MIN") \
    X(MINADDB9,  0x08bu,1u,"MIN ADDb9") \
    X(MAJ,       0x091u,0u,"MAJ") \
    X(ADD9,      0x095u,1u,"ADD9") \
    X(SUS4,      0x0a1u,0u,"SUS4") \
    X(ADD11,     0x0b1u,1u,"ADD11") \
    X(ADD9ADD11, 0x0b5u,1u,"ADD9 ADD11") \
    X(AUG,       0x111u,0u,"AUG") \
    X(DIMADDB6,  0x149u,1u,"DIM ADDb6") \
    X(MIN6ADD9N5,0x20du,2u,"MIN6 ADD9 NO5") \
    X(SIXADD9N5, 0x215u,2u,"6 ADD9 NO5") \
    X(SIXADD11N5,0x231u,2u,"6 ADD11 NO5") \
    X(DIM7,      0x249u,0u,"DIM7") \
    X(DIM7ADD9,  0x24du,1u,"DIM7 ADD9") \
    X(SIXB5,     0x251u,1u,"6 b5") \
    X(DIM7ADD11, 0x26du,1u,"DIM7 ADD9 ADD11") \
    X(MIN6,      0x289u,0u,"MIN6") \
    X(MIN6ADD9,  0x28du,1u,"MIN6 ADD9") \
    X(SIX,       0x291u,0u,"6") \
    X(SIXADD9,   0x295u,1u,"6 ADD9") \
    X(SIXSUS4ADD9,0x2a5u,1u,"6 SUS4 ADD9") \
    X(SIXADD11,  0x2b1u,1u,"6 ADD11") \
    X(MIN6SH5,   0x309u,1u,"MIN6 #5") \
    X(AUGADD6,   0x311u,1u,"AUG ADD6") \
    X(MIN7N5,    0x409u,2u,"MIN7 NO5") \
    X(NINEN5,    0x415u,2u,"9 NO5") \
    X(SEVENSH9N5,0x419u,2u,"7 #9 NO5") \
    X(MIN7B5,    0x449u,0u,"MIN7 b5") \
    X(SEVENB5,   0x451u,1u,"7 b5") \
    X(SEVENB5SH9,0x459u,1u,"7 b5 #9") \
    X(SEVENSUS2, 0x485u,1u,"7 SUS2") \
    X(MIN7,      0x489u,0u,"MIN7") \
    X(MIN7B9,    0x48bu,1u,"MIN7 b9") \
    X(MIN9,      0x48du,1u,"MIN9") \
    X(SEVEN,     0x491u,0u,"7") \
    X(SEVENB9,   0x493u,1u,"7 b9") \
    X(NINE,      0x495u,1u,"9") \
    X(SEVENSH9,  0x499u,1u,"7 #9") \
    X(SEVENB9SH9,0x49bu,1u,"7 b9 #9") \
    X(SEVENSUS4, 0x4a1u,1u,"7 SUS4") \
    X(NINESUS4,  0x4a5u,1u,"9 SUS4") \
    X(MIN7ADD11, 0x4a9u,1u,"MIN7 ADD11") \
    X(MIN11,     0x4adu,1u,"MIN11") \
    X(SEVENADD11,0x4b1u,1u,"7 ADD11") \
    X(ELEVEN,    0x4b5u,1u,"11") \
    X(NINESH11,  0x4d5u,1u,"9 #11") \
    X(MIN7SH5,   0x509u,1u,"MIN7 #5") \
    X(SEVENSH5,  0x511u,1u,"7 #5") \
    X(MIN7ADD13N5,0x609u,2u,"MIN7 ADD13 NO5") \
    X(SEVENADD13N5,0x611u,2u,"7 ADD13 NO5") \
    X(SEVENADD13,0x691u,1u,"7 ADD13") \
    X(THIRTEENSUS4,0x6a5u,1u,"13 SUS4") \
    X(MIN13,     0x6adu,1u,"MIN13") \
    X(THIRTEEN,  0x6b5u,1u,"13") \
    X(MAJ7B5,    0x851u,1u,"MAJ7 b5") \
    X(MINMAJ7,   0x889u,1u,"MIN MAJ7") \
    X(MAJ7,      0x891u,0u,"MAJ7") \
    X(MAJ9,      0x895u,1u,"MAJ9") \
    X(MAJ7ADD11, 0x8b1u,1u,"MAJ7 ADD11") \
    X(MAJ11,     0x8b5u,1u,"MAJ11") \
    X(MAJ7SH5,   0x911u,1u,"MAJ7 #5") \
    X(MAJ9SH5,   0x915u,1u,"MAJ9 #5") \
    X(MAJ9SH5SH11,0x955u,1u,"MAJ9 #5 #11") \
    X(MAJ7ADD13N5,0xa11u,2u,"MAJ7 ADD13 NO5") \
    X(MAJ7ADD13, 0xa91u,1u,"MAJ7 ADD13") \
    X(MAJ13,     0xab5u,1u,"MAJ13")

struct harmony_names {
#define NAME_FIELD(id,mask,tier,label) char id[sizeof(label)];
    SHAPES(NAME_FIELD)
#undef NAME_FIELD
};
static const struct harmony_names names={
#define NAME_VALUE(id,mask,tier,label) label,
    SHAPES(NAME_VALUE)
#undef NAME_VALUE
};
typedef struct harmony_shape { uint16_t mask_tier, name; } harmony_shape;
static const harmony_shape shapes[]={
#define SHAPE_VALUE(id,mask,tier,label) {(uint16_t)((mask)|((tier)<<12u)),(uint16_t)offsetof(struct harmony_names,id)},
    SHAPES(SHAPE_VALUE)
#undef SHAPE_VALUE
};
#undef SHAPES
static const char roots[12][3]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
_Static_assert(sizeof(harmony_shape)==4u,"harmony patterns must stay compact");
_Static_assert(sizeof shapes/sizeof shapes[0]<UINT8_MAX,"quality must fit a byte");
_Static_assert(sizeof names<UINT16_MAX,"name offset must fit uint16");
_Static_assert(sizeof(thy_harmony)<=16u,"harmony result must stay small");

size_t thy_harmony_pattern_count(void) { return sizeof shapes/sizeof shapes[0]; }
size_t thy_harmony_table_bytes(void) { return sizeof shapes+sizeof names+sizeof roots; }
const char *thy_harmony_quality_name(uint8_t quality)
{
    return quality<thy_harmony_pattern_count()?(const char *)&names+shapes[quality].name:NULL;
}
static int root_valid(uint8_t root) { return root<12u || root==THY_HARMONY_NO_ROOT; }
static unsigned classes(uint16_t mask)
{
    unsigned count=0u;
    while(mask) { mask&=(uint16_t)(mask-1u);++count; }
    return count;
}
static uint16_t relative(uint16_t mask,unsigned root)
{
    return (uint16_t)(((unsigned)mask>>root)|((unsigned)mask<<(12u-root)))&0x0fffu;
}
static uint8_t find_shape(uint16_t mask)
{
    size_t first=0u,last=thy_harmony_pattern_count();
    while(first<last) {
        size_t middle=first+(last-first)/2u;
        uint16_t value=shapes[middle].mask_tier&0x0fffu;
        if(value<mask)first=middle+1u;else last=middle;
    }
    return first<thy_harmony_pattern_count() && (shapes[first].mask_tier&0x0fffu)==mask?
        (uint8_t)first:THY_HARMONY_NO_QUALITY;
}

int thy_harmony_recognize(const uint64_t pitches[2],uint8_t preferred_root,thy_harmony *output)
{
    if(!pitches || !output || !root_valid(preferred_root))return THY_HARMONY_INVALID;
    thy_harmony result={.lowest=UINT8_MAX,.highest=UINT8_MAX,.root=THY_HARMONY_NO_ROOT,
        .bass=THY_HARMONY_NO_ROOT,.quality=THY_HARMONY_NO_QUALITY};
    for(unsigned pitch=0u;pitch<128u;++pitch)if((pitches[pitch/64u]>>(pitch%64u))&1u) {
        if(!result.notes)result.lowest=(uint8_t)pitch;
        result.highest=(uint8_t)pitch;++result.notes;
        result.pitch_classes|=(uint16_t)(1u<<(pitch%12u));
    }
    if(!result.notes) { *output=result;return THY_HARMONY_OK; }
    result.bass=(uint8_t)(result.lowest%12u);
    result.classes=(uint8_t)classes(result.pitch_classes);
    if(result.classes==1u) {
        result.kind=THY_HARMONY_NOTE;result.root=result.bass;result.matches=1u;
        result.confidence=THY_HARMONY_CONFIDENCE_EXACT;
        if(preferred_root!=THY_HARMONY_NO_ROOT)
            result.flags=preferred_root==result.root?THY_HARMONY_ROOT_USED:THY_HARMONY_ROOT_REJECTED;
        *output=result;return THY_HARMONY_OK;
    }
    uint8_t best=THY_HARMONY_NO_QUALITY,best_root=THY_HARMONY_NO_ROOT;
    unsigned best_score=UINT16_MAX;
    for(unsigned root=0u;root<12u;++root) {
        if(!(result.pitch_classes&(1u<<root)))continue;
        uint8_t quality=find_shape(relative(result.pitch_classes,root));
        if(quality==THY_HARMONY_NO_QUALITY)continue;
        ++result.matches;
        /* Explicit exact context wins; otherwise favour complete vocabulary,
         * then the actual bass. Root number is the final stable tie break. */
        unsigned score=((unsigned)shapes[quality].mask_tier>>12u)*32u+
            (root!=result.bass?16u:0u)+root;
        if(root==preferred_root) { best=quality;best_root=(uint8_t)root;best_score=0u; }
        else if((best==THY_HARMONY_NO_QUALITY || best_root!=preferred_root) && score<best_score) {
            best=quality;best_root=(uint8_t)root;best_score=score;
        }
    }
    if(best==THY_HARMONY_NO_QUALITY)result.kind=THY_HARMONY_UNKNOWN;
    else {
        result.kind=THY_HARMONY_CHORD;result.root=best_root;result.quality=best;
        result.confidence=best_root==preferred_root?THY_HARMONY_CONFIDENCE_CONTEXT:
            result.matches==1u?THY_HARMONY_CONFIDENCE_EXACT:THY_HARMONY_CONFIDENCE_AMBIGUOUS;
        if(best_root!=result.bass)result.flags|=THY_HARMONY_SLASH_BASS;
    }
    if(preferred_root!=THY_HARMONY_NO_ROOT)
        result.flags|=result.kind==THY_HARMONY_CHORD && result.root==preferred_root?
            THY_HARMONY_ROOT_USED:THY_HARMONY_ROOT_REJECTED;
    *output=result;return THY_HARMONY_OK;
}

int thy_harmony_from_notes(const int16_t *notes,size_t count,uint8_t preferred_root,thy_harmony *output)
{
    if((!notes && count) || !output || !root_valid(preferred_root))return THY_HARMONY_INVALID;
    uint64_t pitches[2]={0u,0u};
    for(size_t n=0;n<count;++n) {
        if(notes[n]<0 || notes[n]>127)return THY_HARMONY_INVALID;
        unsigned pitch=(unsigned)notes[n];pitches[pitch/64u]|=UINT64_C(1)<<(pitch%64u);
    }
    return thy_harmony_recognize(pitches,preferred_root,output);
}
int thy_harmony_from_keys(const uint16_t *keys,size_t count,uint8_t preferred_root,thy_harmony *output)
{
    if((!keys && count) || !output || !root_valid(preferred_root))return THY_HARMONY_INVALID;
    uint64_t pitches[2]={0u,0u};
    for(size_t n=0;n<count;++n) {
        if(keys[n]>=THY_KEY_COUNT)return THY_HARMONY_INVALID;
        unsigned pitch=keys[n]%128u;pitches[pitch/64u]|=UINT64_C(1)<<(pitch%64u);
    }
    return thy_harmony_recognize(pitches,preferred_root,output);
}

static size_t length(const char *text)
{
    size_t n=0u;while(text[n])++n;return n;
}
static int valid_result(const thy_harmony *r)
{
    if(!r || r->kind>THY_HARMONY_UNKNOWN || r->flags>7u ||
        (r->flags&(THY_HARMONY_ROOT_USED|THY_HARMONY_ROOT_REJECTED))==3u)return 0;
    if(r->kind==THY_HARMONY_EMPTY)return !r->notes && !r->classes && !r->pitch_classes &&
        r->lowest==UINT8_MAX && r->highest==UINT8_MAX && r->root==THY_HARMONY_NO_ROOT &&
        r->bass==THY_HARMONY_NO_ROOT && r->quality==THY_HARMONY_NO_QUALITY &&
        r->confidence==THY_HARMONY_CONFIDENCE_NONE && !r->matches && !r->flags;
    if(!r->notes || r->notes>128u || r->lowest>r->highest || r->highest>127u ||
        !r->classes || r->classes!=classes(r->pitch_classes) || r->pitch_classes>0x0fffu ||
        r->notes<r->classes || r->notes>(unsigned)r->highest-r->lowest+1u ||
        r->bass!=r->lowest%12u || !(r->pitch_classes&(1u<<r->bass)) ||
        !(r->pitch_classes&(1u<<(r->highest%12u))))return 0;
    if(r->kind==THY_HARMONY_NOTE)return r->classes==1u && r->root==r->bass &&
        r->quality==THY_HARMONY_NO_QUALITY && r->matches==1u &&
        r->confidence==THY_HARMONY_CONFIDENCE_EXACT && !(r->flags&THY_HARMONY_SLASH_BASS);
    if(r->classes<2u)return 0;
    if(r->kind==THY_HARMONY_UNKNOWN)return r->root==THY_HARMONY_NO_ROOT &&
        r->quality==THY_HARMONY_NO_QUALITY && !r->matches &&
        r->confidence==THY_HARMONY_CONFIDENCE_NONE && !(r->flags&(THY_HARMONY_ROOT_USED|THY_HARMONY_SLASH_BASS));
    if(r->root>=12u || r->quality>=thy_harmony_pattern_count() || !r->matches || r->matches>12u ||
        relative(r->pitch_classes,r->root)!=(shapes[r->quality].mask_tier&0x0fffu) ||
        !!(r->flags&THY_HARMONY_SLASH_BASS)!=(r->root!=r->bass))return 0;
    return (r->confidence==THY_HARMONY_CONFIDENCE_CONTEXT && (r->flags&THY_HARMONY_ROOT_USED)) ||
        (r->confidence==THY_HARMONY_CONFIDENCE_EXACT && r->matches==1u && !(r->flags&THY_HARMONY_ROOT_USED)) ||
        (r->confidence==THY_HARMONY_CONFIDENCE_AMBIGUOUS && r->matches>1u && !(r->flags&THY_HARMONY_ROOT_USED));
}
static char *put(char *out,const char *text)
{
    while(*text)*out++=*text++;return out;
}
static unsigned digits(unsigned n) { return n>=100u?3u:n>=10u?2u:1u; }
static char *put_number(char *out,unsigned value)
{
    char reversed[3];unsigned count=0u;
    do { reversed[count++]=(char)('0'+value%10u);value/=10u; }while(value);
    while(count)*out++=reversed[--count];return out;
}
int thy_harmony_format(const thy_harmony *source,char *output,size_t capacity)
{
    if(!output || !valid_result(source))return THY_HARMONY_INVALID;
    thy_harmony r=*source; /* A caller may format over its result after validation. */
    size_t needed;
    if(r.kind==THY_HARMONY_EMPTY)needed=5u;
    else if(r.kind==THY_HARMONY_UNKNOWN)needed=digits(r.notes)+6u;
    else if(r.kind==THY_HARMONY_NOTE)needed=length(roots[r.root])+
        (r.notes>1u?8u:r.lowest<12u?2u:1u);
    else needed=length(roots[r.root])+1u+length(thy_harmony_quality_name(r.quality))+
        ((r.flags&THY_HARMONY_SLASH_BASS)?1u+length(roots[r.bass]):0u);
    if(needed+1u>capacity)return THY_HARMONY_TOO_SMALL;
    char *at=output;
    if(r.kind==THY_HARMONY_EMPTY)at=put(at,"READY");
    else if(r.kind==THY_HARMONY_UNKNOWN) { at=put_number(at,r.notes);at=put(at," NOTES"); }
    else if(r.kind==THY_HARMONY_NOTE) {
        at=put(at,roots[r.root]);
        if(r.notes>1u)at=put(at," OCTAVES");
        else if(r.lowest<12u)at=put(at,"-1");
        else *at++=(char)('0'+r.lowest/12u-1u);
    } else {
        at=put(at,roots[r.root]);*at++=' ';at=put(at,thy_harmony_quality_name(r.quality));
        if(r.flags&THY_HARMONY_SLASH_BASS) { *at++='/';at=put(at,roots[r.bass]); }
    }
    *at=0;return THY_HARMONY_OK;
}

/* Shared merged interval vocabulary; playable archive layouts remain lossless. */
#include "thy_harmony_catalog.h"
static uint16_t guidance_rotate(uint16_t mask,unsigned root)
{return (uint16_t)(((unsigned)mask<<root | (unsigned)mask>>(12u-root))&4095u);}
static unsigned guidance_count(uint16_t mask)
{unsigned n=0;for(;mask;mask=(uint16_t)(mask&(mask-1u)))++n;return n;}
size_t thy_guidance_scale_count(void){return sizeof guidance_scales/sizeof guidance_scales[0];}
const char *thy_guidance_scale_name(unsigned family)
{return family<thy_guidance_scale_count()?guidance_scales[family].name:NULL;}
uint16_t thy_guidance_scale_mask(unsigned family,unsigned root)
{return family<thy_guidance_scale_count()&&root<12u?guidance_rotate(guidance_scales[family].mask,root):0u;}
int thy_guidance_catalog_family(unsigned catalog)
{return catalog<sizeof guidance_catalog_families?guidance_catalog_families[catalog]:-1;}
int thy_guidance_playable_catalog(unsigned family)
{for(unsigned n=0;n<sizeof guidance_catalog_families;++n)if(guidance_catalog_families[n]==family)return (int)n;return -1;}
int thy_guidance_chords(uint16_t key,uint8_t tonic,uint16_t current,
    thy_chord_suggestion *out,size_t capacity,size_t *count)
{
    if(!key || key>4095u || tonic>11u || !(key&(1u<<tonic)) || current>4095u ||
        !out || !count || !capacity || capacity>THY_GUIDANCE_LIMIT)return THY_HARMONY_INVALID;
    size_t used=0u;
    for(unsigned root=0;root<12u;++root)
        for(unsigned quality=0;quality<sizeof guidance_chords/sizeof guidance_chords[0];++quality) {
            uint16_t mask=guidance_rotate(guidance_chords[quality],root);
            if((mask&key)!=mask || mask==current)continue;
            unsigned shared=guidance_count((uint16_t)(mask&current));
            /* Retain familiar triads/sevenths ahead of dense extensions, so
             * expanding the vocabulary cannot bury basic harmonic choices. */
            unsigned score=(quality<11u?128u:0u)+shared*8u+
                (root==tonic?2u:root==(tonic+7u)%12u?1u:0u);
            /* A better root/template interpretation replaces a duplicate set. */
            size_t at=0;for(;at<used && out[at].mask!=mask;++at){}
            if(at<used) {
                if(out[at].score>=score)continue;
                for(size_t n=at+1u;n<used;++n)out[n-1u]=out[n];
                --used;
            }
            at=0;while(at<used && out[at].score>=score)++at;
            if(at>=capacity)continue;
            if(used<capacity)++used;
            for(size_t n=used-1u;n>at;--n)out[n]=out[n-1u];
            out[at]=(thy_chord_suggestion){mask,(uint8_t)root,(uint8_t)shared,(uint8_t)score};
        }
    *count=used;return THY_HARMONY_OK;
}
int thy_guidance_scales(uint16_t selected,thy_scale_match *out,size_t capacity,
    size_t *count,size_t *total)
{
    if(!selected || selected>4095u || !out || !count || !total || !capacity || capacity>THY_GUIDANCE_LIMIT)
        return THY_HARMONY_INVALID;
    size_t used=0,found=0;
    for(unsigned family=0;family<thy_guidance_scale_count();++family)
        for(unsigned root=0;root<12u;++root) {
            uint16_t mask=thy_guidance_scale_mask(family,root);
            if((mask&selected)!=selected)continue;
            ++found;
            unsigned extra=guidance_count((uint16_t)(mask&~selected));
            size_t at=0;while(at<used && out[at].extra<=extra)++at;
            if(at>=capacity)continue;
            if(used<capacity)++used;
            for(size_t n=used-1u;n>at;--n)out[n]=out[n-1u];
            out[at]=(thy_scale_match){mask,(uint8_t)family,(uint8_t)root,(uint8_t)extra};
        }
    *count=used;*total=found;return THY_HARMONY_OK;
}
int thy_guidance_modulation(uint16_t source,uint16_t target,uint16_t current,
    thy_modulation *out)
{
    if(!source || source>4095u || !target || target>4095u || current>4095u || !out)
        return THY_HARMONY_INVALID;
    thy_modulation result={(uint16_t)(source&target),(uint16_t)(target&~source),
        (uint16_t)(source&~target),0u,THY_HARMONY_NO_ROOT,0u};
    result.current_fits=(uint8_t)(current && (current&result.common)==current);
    unsigned best=0;
    for(unsigned root=0;root<12u;++root)for(unsigned q=0;q<3u;++q) {
        uint16_t mask=guidance_rotate(guidance_chords[q],root);
        if((mask&result.common)!=mask)continue;
        unsigned score=1u+guidance_count((uint16_t)(mask&current));
        if(score>best){best=score;result.pivot=mask;result.pivot_root=(uint8_t)root;}
    }
    *out=result;return THY_HARMONY_OK;
}

/* Small OLED presentation, using the same advisory engine as the Client. */
static void guide_copy(char out[32],const char *text)
{unsigned n=0;while(text&&text[n]&&n<31u){out[n]=text[n];++n;}out[n]=0;}
static const char *const guide_roots[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
void thy_guidance_notes_text(uint16_t mask,char out[32])
{
    unsigned at=0;out[0]=0;
    for(unsigned pc=0;pc<12u;++pc)if(mask&(1u<<pc)){
        const char *name=guide_roots[pc];if(at)out[at++]=' ';
        out[at++]=name[0];if(name[1])out[at++]=name[1];
    }
    out[at]=0;if(!mask)guide_copy(out,"None");
}
static void guide_label(uint16_t mask,uint8_t root,char out[32])
{
    int16_t notes[12];size_t count=0;thy_harmony name;
    if(root>11u)root=0;
    for(unsigned n=0;n<12u;++n)if(mask&(1u<<((root+n)%12u)))notes[count++]=(int16_t)(60u+root+n);
    if(thy_harmony_from_notes(notes,count,root,&name)==THY_HARMONY_OK &&
       thy_harmony_format(&name,out,32)==THY_HARMONY_OK)return;
    guide_copy(out,"No shared triad");
}
unsigned thy_guidance_ui_count(const thy_guidance_ui *u)
{return u->page==0u?6u:u->page==3u?6u:u->count;}
void thy_guidance_ui_refresh(thy_guidance_ui *u)
{
    size_t count=0,total=0;
    if(u->page==1u)(void)thy_guidance_chords(u->key,u->tonic,u->notes,u->rows.chords,24,&count);
    if(u->page==2u && u->notes)(void)thy_guidance_scales(u->notes,u->rows.scales,24,&count,&total);
    u->count=(uint8_t)count;u->total=(uint16_t)total;
    if(u->page==3u)(void)thy_guidance_modulation(u->key,
        thy_guidance_scale_mask(u->target_family,u->target_root),u->notes,&u->modulation);
    if(u->cursor>=thy_guidance_ui_count(u))u->cursor=0;
}
void thy_guidance_ui_navigate(thy_guidance_ui *u,uint8_t action)
{
    if(!u||!u->open||action>4u)return;
    /* LEFT=0 UP=1 CENTER=2 DOWN=3 RIGHT=4. CENTER always exits. */
    if(action==2u){u->open=0;u->editing=0;return;}
    if(action==0u){if(u->editing)u->editing=0;else if(u->page){u->page=0;u->cursor=0;}else u->open=0;return;}
    if(action==1u||action==3u){
        if(u->editing){unsigned max=u->cursor==0u?12u:(unsigned)thy_guidance_scale_count();
            uint8_t *value=u->cursor==0u?&u->target_root:&u->target_family;
            *value=(uint8_t)((*value+max+(action==1u?-1:1))%max);thy_guidance_ui_refresh(u);
        }else {unsigned count=thy_guidance_ui_count(u);if(count)u->cursor=(uint8_t)((u->cursor+count+(action==1u?-1:1))%count);}
        return;
    }
    if(u->page==0u&&u->cursor<3u){u->page=u->cursor+1u;u->cursor=0;thy_guidance_ui_refresh(u);}
    else if(u->page==2u&&u->cursor<u->count){
        thy_scale_match m=u->rows.scales[u->cursor];u->target_root=m.root;u->target_family=m.family;
        u->page=3;u->cursor=0;thy_guidance_ui_refresh(u);
    }else if(u->page==3u&&u->cursor<2u)u->editing=!u->editing;
}
void thy_guidance_ui_line(const thy_guidance_ui *u,unsigned row,char out[32])
{
    static const char *const menu[]={"Chord suggestions","Scale discovery","Modulation","Add held notes","Use selected chord","Clear captured notes"};
    if(u->page==0u){guide_copy(out,row<6u?menu[row]:"");return;}
    if(u->page==1u){if(row<u->count)guide_label(u->rows.chords[row].mask,u->rows.chords[row].root,out);else guide_copy(out,"No suggestions");return;}
    if(u->page==2u){
        if(row>=u->count){guide_copy(out,"Capture notes first");return;}
        const thy_scale_match *m=&u->rows.scales[row];const char *root=guide_roots[m->root];
        guide_copy(out,root);unsigned at=root[1]?2u:1u;out[at++]=' ';
        const char *name=thy_guidance_scale_name(m->family);for(unsigned n=0;name[n]&&at<31u;++n)out[at++]=name[n];out[at]=0;return;
    }
    if(row==0u){guide_copy(out,"To root: ");unsigned n=9;const char *root=guide_roots[u->target_root];out[n++]=root[0];if(root[1])out[n++]=root[1];out[n]=0;}
    else if(row==1u)guide_copy(out,thy_guidance_scale_name(u->target_family));
    else if(row==5u){if(u->modulation.pivot)guide_label(u->modulation.pivot,u->modulation.pivot_root,out);else guide_copy(out,"No shared triad");}
    else guide_copy(out,row==2u?"Common notes":row==3u?"Introduced notes":"Removed notes");
}
