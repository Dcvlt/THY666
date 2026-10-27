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
int thy_voicing_lead(const int16_t notes[7],uint8_t mask,
    const int16_t previous[7],uint8_t previous_mask,int16_t out[7])
{
    if(!notes || !previous || !out || mask>127u || previous_mask>127u)
        return THY_INVALID_ARGUMENT;
    for(unsigned n=0;n<7u;++n)
        if(((mask&(1u<<n)) && (notes[n]<0 || notes[n]>127)) ||
           ((previous_mask&(1u<<n)) && (previous[n]<0 || previous[n]>127)))
            return THY_INVALID_ARGUMENT;
    int16_t source[7],prior[7],best[7],result[7];
    unsigned count=unique(notes,mask,source),old=unique(previous,previous_mask,prior);
    for(unsigned n=0;n<7u;++n)result[n]=notes[n];
    if(count && old) {
        uint32_t best_cost=UINT32_MAX;
        /* All cyclic voice orders, all legal octaves, strictly ascending voices.
         * Cost is motion first, distance from authored register second. At most
         * 7*7*11 candidates and as many prefix-minimum visits; no recursion, heap or mutable global state. */
        for(unsigned rotation=0;rotation<count;++rotation) {
            uint32_t costs[2][11];uint8_t parent[7][11];
            for(unsigned voice=0;voice<count;++voice) {
                unsigned index=(voice+rotation)%count,pc=(unsigned)source[index]%12u;
                unsigned target=count==1u?(old-1u)/2u:voice*(old-1u)/(count-1u);
                uint32_t prefix=UINT32_MAX;unsigned k=0;uint8_t from=0;
                unsigned last_pc=(unsigned)source[(voice+count-1u+rotation)%count]%12u;
                for(unsigned octave=0;octave<11u;++octave) {
                    unsigned pitch=pc+12u*octave;
                    uint32_t cost=UINT32_MAX;
                    if(pitch<=127u) {
                        uint32_t local=distance((int)pitch,prior[target])*1024u+
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
        for(unsigned n=0;n<7u;++n)if(mask&(1u<<n))
            for(unsigned k=0;k<count;++k)if(notes[n]==source[k]){result[n]=best[k];break;}
    }
    for(unsigned n=0;n<7u;++n)out[n]=result[n];
    return THY_OK;
}

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
