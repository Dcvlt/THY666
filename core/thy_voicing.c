/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_voicing.h"

int thy_voicing_drop(const int16_t notes[THY_VOICING_SLOTS],uint8_t valid_mask,
    uint8_t drop_mask,int16_t out[THY_VOICING_SLOTS])
{
    if(!notes || !out || valid_mask>127u || drop_mask>7u)return THY_INVALID_ARGUMENT;
    for(unsigned slot=0u;slot<THY_VOICING_SLOTS;++slot)
        if((valid_mask&(1u<<slot)) && (notes[slot]<0 || notes[slot]>127))
            return THY_INVALID_ARGUMENT;
    int16_t result[THY_VOICING_SLOTS];
    for(unsigned slot=0u;slot<THY_VOICING_SLOTS;++slot) {
        result[slot]=notes[slot];
        if(!(valid_mask&(1u<<slot)))continue;
        unsigned rank=0u;
        for(unsigned other=0u;other<THY_VOICING_SLOTS;++other) {
            if(!(valid_mask&(1u<<other)) || notes[other]>=notes[slot])continue;
            unsigned earlier=0u;
            while(earlier<other && (!(valid_mask&(1u<<earlier)) ||
                notes[earlier]!=notes[other]))++earlier;
            if(earlier==other)++rank;
        }
        if(rank>0u && rank<=3u && (drop_mask&(1u<<(rank-1u)))) {
            if(notes[slot]<12)return THY_INVALID_ARGUMENT;
            result[slot]=(int16_t)(notes[slot]-12);
        }
    }
    for(unsigned slot=0u;slot<THY_VOICING_SLOTS;++slot)out[slot]=result[slot];
    return THY_OK;
}

static unsigned distance(int a,int b){return (unsigned)(a>b?a-b:b-a);}
static unsigned unique(const int16_t *notes,uint8_t mask,int16_t *sorted)
{
    unsigned count=0;
    for(unsigned n=0;n<7u;++n)if(mask&(1u<<n)) {
        unsigned at=0;while(at<count && sorted[at]<notes[n])++at;
        if(at<count && sorted[at]==notes[n])continue;
        for(unsigned k=count;k>at;--k)sorted[k]=sorted[k-1u];
        sorted[at]=notes[n];++count;
    }
    return count;
}
/* Nearest whole octave, with exact half-octave ties retaining the smaller
 * correction. Centroid differences are represented without floating point. */
static int centroid_octaves(int delta,unsigned voices)
{
    unsigned magnitude=(unsigned)(delta<0?-delta:delta),step=12u*voices;
    unsigned octaves=magnitude/step;
    if((magnitude%step)*2u>step)++octaves;
    return delta<0?-(int)octaves:(int)octaves;
}

static int lead_centered(const int16_t notes[7],uint8_t mask,
    const int16_t previous[7],uint8_t previous_mask,int16_t low,int16_t high,
    uint8_t root_pc,uint8_t root_bass,int16_t out[7])
{
    if(!notes || !previous || !out || mask>127u || previous_mask>127u ||
        low<0 || high>127 || low>high || root_bass>1u || (root_bass && root_pc>11u))
        return THY_INVALID_ARGUMENT;
    for(unsigned n=0;n<7u;++n)
        if(((mask&(1u<<n)) && (notes[n]<0 || notes[n]>127)) ||
           ((previous_mask&(1u<<n)) && (previous[n]<0 || previous[n]>127)))
            return THY_INVALID_ARGUMENT;
    int16_t source[7],prior[7],best[7],result[7];
    unsigned count=unique(notes,mask,source),old=unique(previous,previous_mask,prior);
    for(unsigned n=0;n<7u;++n)result[n]=notes[n];
    int source_sum=0,prior_sum=0,identity=!old;
    for(unsigned n=0u;n<count;++n){source_sum+=source[n];if(source[n]<low || source[n]>high)identity=0;}
    if(root_bass && count && (unsigned)source[0]%12u!=root_pc)identity=0;
    if(count && old) {
        for(unsigned n=0u;n<old;++n)prior_sum+=prior[n];
        /* Prior motion is compared in the authored chord's octave register.
         * The captured history itself is untouched. Otherwise small changes
         * around a repeated progression can walk every voice to MIDI0/127. */
        int shift=12*centroid_octaves(source_sum*(int)old-prior_sum*(int)count,count*old);
        for(unsigned n=0u;n<old;++n)prior[n]=(int16_t)(prior[n]+shift);
    }
    if(count && !identity) {
        uint32_t best_cost=UINT32_MAX;
        /* All cyclic voice orders, all legal octaves, strictly ascending voices.
         * Cost is motion first, distance from authored register second. At most
         * 7*7*11 candidates and as many prefix-minimum visits; no recursion, heap or mutable global state. */
        for(unsigned rotation=0;rotation<count;++rotation) {
            if(root_bass && (unsigned)source[rotation]%12u!=root_pc)continue;
            uint32_t costs[2][11];uint8_t parent[7][11];
            for(unsigned voice=0;voice<count;++voice) {
                unsigned index=(voice+rotation)%count,pc=(unsigned)source[index]%12u;
                unsigned rank=old?(count==1u?(old-1u)/2u:voice*(old-1u)/(count-1u)):0u;
                int target=old?prior[rank]:source[index];
                uint32_t prefix=UINT32_MAX;unsigned k=0;uint8_t from=0;
                unsigned last_pc=(unsigned)source[(voice+count-1u+rotation)%count]%12u;
                for(unsigned octave=0;octave<11u;++octave) {
                    unsigned pitch=pc+12u*octave;
                    uint32_t cost=UINT32_MAX;
                    if(pitch>=(unsigned)low && pitch<=(unsigned)high) {
                        uint32_t local=distance((int)pitch,target)*1024u+
                            distance((int)pitch,source[index]);
                        if(!voice)cost=local;
                        else {
                            while(k<11u && last_pc+12u*k<pitch) {
                                uint32_t before=costs[(voice-1u)&1u][k];
                                if(before<prefix){prefix=before;from=(uint8_t)k;}
                                ++k;
                            }
                            if(prefix!=UINT32_MAX)cost=prefix+local;
                        }
                    }
                    costs[voice&1u][octave]=cost;parent[voice][octave]=from;
                }
            }
            for(unsigned octave=0;octave<11u;++octave)
                if(costs[(count-1u)&1u][octave]<best_cost) {
                    best_cost=costs[(count-1u)&1u][octave];unsigned k=octave;
                    for(unsigned voice=count;voice-->0;) {
                        unsigned index=(voice+rotation)%count;
                        best[index]=(int16_t)(source[index]%12+12*(int)k);
                        k=parent[voice][k];
                    }
                }
        }
        if(best_cost==UINT32_MAX)return THY_INVALID_ARGUMENT;
        /* Keep the selected intervals, but place the whole result as near
         * the authored centroid as its legal range permits. This is a small
         * integer translation, not a per-note clamp or a new DP dimension. */
        int sum=0,minimum=127,maximum=0;
        for(unsigned n=0u;n<count;++n){sum+=best[n];if(best[n]<minimum)minimum=best[n];if(best[n]>maximum)maximum=best[n];}
        int octaves=centroid_octaves(source_sum-sum,count);
        while(minimum+12*octaves<low)++octaves;
        while(maximum+12*octaves>high)--octaves;
        for(unsigned n=0u;n<count;++n)best[n]=(int16_t)(best[n]+12*octaves);
        for(unsigned n=0;n<7u;++n)if(mask&(1u<<n))
            for(unsigned k=0;k<count;++k)if(notes[n]==source[k]){result[n]=best[k];break;}
    }
    for(unsigned n=0;n<7u;++n)out[n]=result[n];
    return THY_OK;
}

int thy_voicing_lead(const int16_t notes[7],uint8_t mask,
    const int16_t previous[7],uint8_t previous_mask,int16_t out[7])
{return lead_centered(notes,mask,previous,previous_mask,0,127,0u,0u,out);}

int thy_voicing_lead_constrained(const int16_t notes[7],uint8_t mask,
    const int16_t previous[7],uint8_t previous_mask,int16_t low,int16_t high,
    uint8_t root_pc,uint8_t root_bass,int16_t out[7])
{return lead_centered(notes,mask,previous,previous_mask,low,high,root_pc,root_bass,out);}

int thy_voicing_fold(const int16_t notes[7],uint8_t mask,int16_t out[7])
{
    if(!notes || !out || mask>127u)return THY_INVALID_ARGUMENT;
    int anchor=127;
    for(unsigned n=0;n<7u;++n)if(mask&(1u<<n)) {
        if(notes[n]<0 || notes[n]>127)return THY_INVALID_ARGUMENT;
        if(notes[n]<anchor)anchor=notes[n];
    }
    if(anchor>116)anchor=116;
    for(unsigned n=0;n<7u;++n)
        out[n]=(mask&(1u<<n))?(int16_t)(anchor+(notes[n]-anchor+132)%12):notes[n];
    return THY_OK;
}

int thy_voicing_guitar(const int16_t notes[7],uint8_t mask,int16_t out[7])
{
    static const uint8_t tuning[6]={40,45,50,55,59,64};
    if(!notes || !out || mask>127u)return THY_INVALID_ARGUMENT;
    uint8_t pcs[7];unsigned count=0;
    for(unsigned n=0;n<7u;++n)if(mask&(1u<<n)) {
        if(notes[n]<0 || notes[n]>127)return THY_INVALID_ARGUMENT;
        unsigned pc=(unsigned)notes[n]%12u,k=0;
        while(k<count && pcs[k]!=pc)++k;
        if(k==count)pcs[count++]=(uint8_t)pc;
    }
    /* A seven-pitch-class chord cannot fit six strings. Preserve it intact,
     * rather than silently deleting a tone or failing the musical attack. */
    if(!count || count>6u) {
        for(unsigned n=0;n<7u;++n)out[n]=notes[n];
        return THY_OK;
    }
    uint16_t costs[2][64];uint8_t chosen[6][64];
    unsigned states=1u<<count;
    for(unsigned m=0;m<states;++m)costs[0][m]=m?UINT16_MAX:0;
    /* One distinct pitch class per string, optional muted strings. Minimize
     * total fret number, then register; each string uses frets 0..11. At most
     * 6*64*6 candidates, no recursion or mutable global workspace. This is a
     * guitar-register voicing, not a claim of ergonomic human fingering. */
    for(unsigned string=0;string<6u;++string) {
        unsigned before=string&1u,after=before^1u;
        for(unsigned m=0;m<states;++m){costs[after][m]=costs[before][m];chosen[string][m]=255u;}
        for(unsigned m=0;m<states;++m)if(costs[before][m]!=UINT16_MAX)
            for(unsigned pc=0;pc<count;++pc)if(!(m&(1u<<pc))) {
                unsigned fret=(pcs[pc]+12u-tuning[string]%12u)%12u;
                unsigned cost=costs[before][m]+fret*128u+tuning[string]+fret;
                unsigned next=m|(1u<<pc);
                if(cost<costs[after][next]){costs[after][next]=(uint16_t)cost;chosen[string][next]=(uint8_t)pc;}
            }
    }
    int16_t pitch[6];unsigned m=states-1u;
    for(unsigned string=6u;string-->0;) {
        unsigned pc=chosen[string][m];
        if(pc!=255u){pitch[pc]=(int16_t)(tuning[string]+(pcs[pc]+12u-tuning[string]%12u)%12u);m&=~(1u<<pc);}
    }
    for(unsigned n=0;n<7u;++n) {
        int16_t value=notes[n];
        if(mask&(1u<<n))for(unsigned pc=0;pc<count;++pc)
            if((unsigned)value%12u==pcs[pc]){value=pitch[pc];break;}
        out[n]=value;
    }
    return THY_OK;
}
