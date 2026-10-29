/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "thy_voicing.h"

static unsigned assertions;
#define CHECK(x) do{++assertions;if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)

/* Independent two-voice oracle: enumerate all octave pairs, using a brute
 * whole-octave centroid search instead of production rounding/DP parents. */
static int centered_pair_reference(const int16_t input[7],const int16_t previous[7],
    int low,int high,unsigned root,unsigned root_bass,int16_t out[7])
{
    int sum=input[0]+input[1],prior_sum=previous[0]+previous[1];
    int reference_shift=0,reference_error=100000;
    for(int octave=-11;octave<=11;++octave) {
        int error=abs(prior_sum+24*octave-sum);
        if(error<reference_error || (error==reference_error && abs(octave)<abs(reference_shift))) {
            reference_error=error;reference_shift=octave;
        }
    }
    int prior_low=previous[0]<previous[1]?previous[0]:previous[1];
    int prior_high=previous[0]>previous[1]?previous[0]:previous[1];
    prior_low+=12*reference_shift;prior_high+=12*reference_shift;
    unsigned best=UINT32_MAX,best_rotation=UINT32_MAX;
    int best_low=128,best_high=128,a_best=0,b_best=0;
    for(int a=input[0]%12;a<128;a+=12)for(int b=input[1]%12;b<128;b+=12) {
        int lo=a<b?a:b,hi=a>b?a:b;
        if(a==b || lo<low || hi>high || (root_bass && (unsigned)(lo%12)!=root))continue;
        unsigned rotation=((a<b)==(input[0]<input[1]))?0u:1u;
        unsigned cost=(unsigned)(abs(lo-prior_low)+abs(hi-prior_high))*1024u+
            (unsigned)(abs(a-input[0])+abs(b-input[1]));
        if(cost<best || (cost==best && (rotation<best_rotation ||
           (rotation==best_rotation && (hi<best_high || (hi==best_high && lo<best_low)))))) {
            best=cost;best_rotation=rotation;best_low=lo;best_high=hi;a_best=a;b_best=b;
        }
    }
    if(best==UINT32_MAX)return THY_INVALID_ARGUMENT;
    int shift=0,error_best=100000;
    for(int octave=-11;octave<=11;++octave) {
        if(best_low+12*octave<low || best_high+12*octave>high)continue;
        int error=abs(a_best+b_best+24*octave-sum);
        if(error<error_best || (error==error_best && abs(octave)<abs(shift))) {
            error_best=error;shift=octave;
        }
    }
    memcpy(out,input,7u*sizeof(*out));
    out[0]=out[2]=(int16_t)(a_best+12*shift);out[1]=(int16_t)(b_best+12*shift);
    return THY_OK;
}

/* Independent oracle: sorted set plus selected-pitch lookup, rather than the
 * production helper's per-slot count of distinct smaller original pitches. */
static int reference(const int16_t notes[7],uint8_t valid,uint8_t drops,int16_t out[7])
{
    int16_t unique[7];unsigned count=0u;
    if(valid>127u || drops>7u)return THY_INVALID_ARGUMENT;
    for(unsigned n=0u;n<7u;++n)if(valid&(1u<<n)) {
        if(notes[n]<0 || notes[n]>127)return THY_INVALID_ARGUMENT;
        unsigned place=0u;
        while(place<count && unique[place]<notes[n])++place;
        if(place<count && unique[place]==notes[n])continue;
        for(unsigned after=count;after>place;--after)unique[after]=unique[after-1u];
        unique[place]=notes[n];++count;
    }
    for(unsigned rank=1u;rank<count && rank<=3u;++rank)
        if((drops&(1u<<(rank-1u))) && unique[rank]<12)return THY_INVALID_ARGUMENT;
    for(unsigned n=0u;n<7u;++n) {
        out[n]=notes[n];
        if(!(valid&(1u<<n)))continue;
        for(unsigned rank=1u;rank<count && rank<=3u;++rank)
            if((drops&(1u<<(rank-1u))) && notes[n]==unique[rank])out[n]-=12;
    }
    return THY_OK;
}

static int expected_drop(const int16_t original[7],uint8_t valid,uint8_t drops)
{
    int16_t out[7],expected[7],sentinel[7],alias[7];
    for(unsigned n=0u;n<7u;++n)sentinel[n]=(int16_t)(-300-(int)n);
    memcpy(out,sentinel,sizeof out);memcpy(alias,original,sizeof alias);
    int result=reference(original,valid,drops,expected);
    CHECK(thy_voicing_drop(original,valid,drops,out)==result);
    CHECK(!memcmp(out,result==THY_OK?expected:sentinel,sizeof out));
    CHECK(thy_voicing_drop(alias,valid,drops,alias)==result);
    CHECK(!memcmp(alias,result==THY_OK?expected:original,sizeof alias));
    return 1;
}

static int basic_drop_and_protected_bass(void)
{
    const int16_t original[7]={60,64,67,71,74,77,81};
    const int16_t second[7]={60,52,67,71,74,77,81};
    const int16_t third[7]={60,64,55,71,74,77,81};
    const int16_t fourth[7]={60,64,67,59,74,77,81};
    int16_t out[7];
    CHECK(thy_voicing_drop(original,127u,1u,out)==THY_OK);
    CHECK(!memcmp(out,second,sizeof out));
    CHECK(thy_voicing_drop(original,127u,2u,out)==THY_OK);
    CHECK(!memcmp(out,third,sizeof out));
    CHECK(thy_voicing_drop(original,127u,4u,out)==THY_OK);
    CHECK(!memcmp(out,fourth,sizeof out));
    CHECK(thy_voicing_drop(original,127u,7u,out)==THY_OK);
    CHECK(out[0]==60 && out[1]==52 && out[2]==55 && out[3]==59);
    CHECK(out[4]==74 && out[5]==77 && out[6]==81);
    const int16_t short_chord[7]={0,12,24,-32768,32767,-1,128};
    CHECK(thy_voicing_drop(short_chord,7u,4u,out)==THY_OK);
    CHECK(!memcmp(out,short_chord,sizeof out));
    CHECK(thy_voicing_drop(short_chord,7u,7u,out)==THY_OK);
    CHECK(out[0]==0 && out[1]==0 && out[2]==12);
    for(unsigned n=3u;n<7u;++n)CHECK(out[n]==short_chord[n]);
    return 1;
}

static int duplicates_holes_and_inactive_literals(void)
{
    const int16_t original[7]={67,-32768,60,64,67,60,32767};
    int16_t out[7];
    CHECK(thy_voicing_drop(original,0x3du,3u,out)==THY_OK);
    CHECK(out[0]==55 && out[2]==60 && out[3]==52 && out[4]==55 && out[5]==60);
    CHECK(out[1]==-32768 && out[6]==32767);
    /* An inactive duplicate does not move with its enabled counterpart. */
    const int16_t inactive[7]={60,64,64,67,71,64,64};
    CHECK(thy_voicing_drop(inactive,0x1bu,1u,out)==THY_OK);
    CHECK(out[1]==52 && out[2]==64 && out[5]==64 && out[6]==64);
    CHECK(out[0]==60 && out[3]==67 && out[4]==71);
    for(unsigned valid=0u;valid<128u;++valid)
        for(unsigned drops=0u;drops<8u;++drops)
            CHECK(expected_drop(inactive,(uint8_t)valid,(uint8_t)drops));
    const int16_t empty[7]={-32768,-128,-1,0,128,255,32767};
    CHECK(thy_voicing_drop(empty,0u,7u,out)==THY_OK);
    CHECK(!memcmp(out,empty,sizeof out));
    return 1;
}

static int permutations_use_pitch_rank_not_slot_index(void)
{
    /* Enumerate all7! slot orders; the first, second, third, fourth lowest
     * unique pitches retain their roles regardless of serialized position. */
    int16_t notes[7]={0,12,24,36,48,60,72};
    unsigned permutations=0u;
    for(;;) {
        for(unsigned drops=0u;drops<8u;++drops)
            CHECK(expected_drop(notes,127u,(uint8_t)drops));
        ++permutations;
        int pivot=5;
        while(pivot>=0 && notes[pivot]>=notes[pivot+1])--pivot;
        if(pivot<0)break;
        int successor=6;
        while(notes[successor]<=notes[pivot])--successor;
        int16_t swap=notes[pivot];notes[pivot]=notes[successor];notes[successor]=swap;
        for(int left=pivot+1,right=6;left<right;++left,--right) {
            swap=notes[left];notes[left]=notes[right];notes[right]=swap;
        }
    }
    CHECK(permutations==5040u);return 1;
}

static int exhaustive_duplicate_vectors_masks_and_drop_bits(void)
{
    /* Every seven-slot vector over zero/below-octave/one-octave boundaries,
     * every original hole mask, and every combination of the three policies. */
    static const int16_t alphabet[3]={0,11,12};
    for(unsigned vector=0u;vector<2187u;++vector) {
        unsigned digits=vector;int16_t notes[7];
        for(unsigned n=0u;n<7u;++n){notes[n]=alphabet[digits%3u];digits/=3u;}
        for(unsigned valid=0u;valid<128u;++valid)
            for(unsigned drops=0u;drops<8u;++drops)
                CHECK(expected_drop(notes,(uint8_t)valid,(uint8_t)drops));
    }
    return 1;
}

static int every_midi_bass_and_boundary_combination(void)
{
    for(int low=0;low<128;++low) {
        int16_t notes[7]={127,(int16_t)low,12,0,11,126,24};
        for(unsigned valid=0u;valid<128u;++valid)
            for(unsigned drops=0u;drops<8u;++drops)
                CHECK(expected_drop(notes,(uint8_t)valid,(uint8_t)drops));
    }
    const int16_t reversed[7]={127,115,103,91,79,67,55};
    int16_t out[7];
    CHECK(thy_voicing_drop(reversed,127u,7u,out)==THY_OK);
    CHECK(out[0]==127 && out[1]==115 && out[2]==103 && out[3]==79 &&
        out[4]==67 && out[5]==55 && out[6]==55);
    return 1;
}

static int invalid_arguments_and_underflow_are_atomic(void)
{
    const int16_t valid[7]={0,7,12,19,24,31,36};
    const int16_t sentinel[7]={901,902,903,904,905,906,907};
    int16_t out[7];memcpy(out,sentinel,sizeof out);
    CHECK(thy_voicing_drop(NULL,127u,0u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_drop(valid,127u,0u,NULL)==THY_INVALID_ARGUMENT);
    for(unsigned mask=128u;mask<256u;++mask)
        CHECK(thy_voicing_drop(valid,(uint8_t)mask,0u,out)==THY_INVALID_ARGUMENT);
    for(unsigned mask=8u;mask<256u;++mask)
        CHECK(thy_voicing_drop(valid,127u,(uint8_t)mask,out)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(out,sentinel,sizeof out));
    CHECK(thy_voicing_drop(valid,127u,1u,out)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(out,sentinel,sizeof out));
    int16_t alias[7];memcpy(alias,valid,sizeof alias);
    CHECK(thy_voicing_drop(alias,127u,7u,alias)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(alias,valid,sizeof alias));
    for(unsigned slot=0u;slot<7u;++slot) {
        int16_t corrupt[7];memcpy(corrupt,valid,sizeof corrupt);
        corrupt[slot]=-1;
        CHECK(thy_voicing_drop(corrupt,127u,0u,out)==THY_INVALID_ARGUMENT);
        CHECK(!memcmp(out,sentinel,sizeof out));
        corrupt[slot]=128;
        CHECK(thy_voicing_drop(corrupt,127u,0u,out)==THY_INVALID_ARGUMENT);
        CHECK(!memcmp(out,sentinel,sizeof out));
        corrupt[slot]=32767;
        CHECK(thy_voicing_drop(corrupt,127u,0u,out)==THY_INVALID_ARGUMENT);
        CHECK(!memcmp(out,sentinel,sizeof out));
        corrupt[slot]=-32768;
        CHECK(thy_voicing_drop(corrupt,127u,0u,out)==THY_INVALID_ARGUMENT);
        CHECK(!memcmp(out,sentinel,sizeof out));
    }
    return 1;
}

static int automatic_leading(void)
{
    const int16_t prior[7]={60,64,67,999,-999,0,0};
    const int16_t next[7]={65,69,72,999,-999,65,0};
    int16_t out[7];
    CHECK(thy_voicing_lead(next,39u,prior,7u,out)==THY_OK);
    CHECK(out[0]==65 && out[1]==69 && out[2]==60 && out[5]==65);
    CHECK(out[3]==999 && out[4]==-999 && out[6]==0);
    CHECK(thy_voicing_lead(out,39u,out,39u,out)==THY_OK);
    CHECK(out[0]==65 && out[1]==69 && out[2]==60);
    CHECK(thy_voicing_lead(next,39u,prior,0u,out)==THY_OK);
    CHECK(!memcmp(out,next,sizeof out));
    CHECK(thy_voicing_lead(next,128u,prior,7u,out)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(out,next,sizeof out));
    CHECK(thy_voicing_lead(next,127u,prior,7u,out)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(out,next,sizeof out));
    CHECK(thy_voicing_lead(next,7u,prior,127u,out)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(out,next,sizeof out));
    /* Enumerate all legal pairs against the octave-centered reference, then
     * apply the independent legal whole-chord register selection. */
    for(int root=0;root<128;root+=7)for(int third=1;third<12;third+=2) {
        int16_t input[7]={root,(root+third)%128,root,0,0,0,0};
        int16_t old[7]={36,79,0,0,0,0,0};
        CHECK(thy_voicing_lead(input,7u,old,3u,out)==THY_OK);
        int16_t expected[7];
        CHECK(centered_pair_reference(input,old,0,127,0u,0u,expected)==THY_OK);
        CHECK(!memcmp(out,expected,sizeof out));
        CHECK(out[0]%12==input[0]%12 && out[1]%12==input[1]%12);
    }
    return 1;
}

static int register_modes_preserve_all_pitch_sets(void)
{
    for(unsigned pcs=1;pcs<4096u;++pcs) {
        int16_t notes[7]={-9,-8,-7,-6,-5,-4,-3},out[7],alias[7];unsigned count=0;
        for(unsigned n=0;n<12u;++n)if(pcs&(1u<<n))++count;
        if(count>7u)continue;
        unsigned at=0;for(unsigned n=0;n<12u;++n)if(pcs&(1u<<n)){notes[at]=(int16_t)(36+n+12*(at%4u));++at;}
        uint8_t mask=(uint8_t)((1u<<count)-1u);
        CHECK(thy_voicing_fold(notes,mask,out)==THY_OK);
        int low=128,high=-1;unsigned actual=0;
        for(unsigned n=0;n<count;++n){CHECK(out[n]%12==notes[n]%12);if(out[n]<low)low=out[n];if(out[n]>high)high=out[n];}
        CHECK(high-low<12);
        CHECK(thy_voicing_guitar(notes,mask,out)==THY_OK);
        for(unsigned n=0;n<count;++n){CHECK(out[n]%12==notes[n]%12);actual|=1u<<(out[n]%12);if(count<=6u)CHECK(out[n]>=40 && out[n]<=75);}
        CHECK(actual==pcs);
        if(count==7u)CHECK(!memcmp(out,notes,sizeof notes));
        for(unsigned n=count;n<7u;++n)CHECK(out[n]==notes[n]);
        memcpy(alias,notes,sizeof notes);CHECK(thy_voicing_guitar(alias,mask,alias)==THY_OK);CHECK(!memcmp(alias,out,sizeof out));
    }
    const int16_t edge[7]={0,127,999,-1,0,0,0};int16_t out[7],before[7];
    CHECK(thy_voicing_fold(edge,3u,out)==THY_OK && out[0]==0 && out[1]==7 && out[2]==999);
    const int16_t top[7]={127,126,125,0,0,0,0};
    CHECK(thy_voicing_fold(top,7u,out)==THY_OK && out[0]==127 && out[1]==126 && out[2]==125);
    memcpy(before,out,sizeof out);
    CHECK(thy_voicing_fold(edge,7u,out)==THY_INVALID_ARGUMENT && !memcmp(out,before,sizeof out));
    CHECK(thy_voicing_guitar(edge,7u,out)==THY_INVALID_ARGUMENT && !memcmp(out,before,sizeof out));
    CHECK(thy_voicing_fold(edge,128u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_guitar(edge,128u,out)==THY_INVALID_ARGUMENT);
    return 1;
}

static int constrained_register_root_and_owner_boundaries(void)
{
    const int16_t prior[7]={60,64,67,999,-999,0,0};
    const int16_t next[7]={65,69,72,999,-999,65,0};
    int16_t out[7],original[7],old[7],alias[7];
    memcpy(original,next,sizeof next);memcpy(old,prior,sizeof prior);
    CHECK(thy_voicing_lead_constrained(next,39u,prior,7u,60,71,255u,0u,out)==THY_OK);
    CHECK(out[0]==65 && out[1]==69 && out[2]==60 && out[5]==65);
    CHECK(out[3]==999 && out[4]==-999 && out[6]==0);
    CHECK(thy_voicing_lead_constrained(next,39u,prior,7u,60,83,5u,1u,out)==THY_OK);
    CHECK(out[0]==65 && out[1]==69 && out[2]==72 && out[5]==65);
    memcpy(alias,next,sizeof alias);
    CHECK(thy_voicing_lead_constrained(alias,39u,prior,7u,60,83,5u,1u,alias)==THY_OK);
    CHECK(!memcmp(alias,out,sizeof out));
    memcpy(alias,prior,sizeof alias);
    CHECK(thy_voicing_lead_constrained(next,39u,alias,7u,60,83,5u,1u,alias)==THY_OK);
    CHECK(!memcmp(alias,out,sizeof out));
    CHECK(!memcmp(next,original,sizeof original) && !memcmp(prior,old,sizeof old));

    const int16_t low[7]={12,16,19,901,902,903,904};
    const int16_t bottom[7]={0,4,7,0,0,0,0};
    CHECK(thy_voicing_lead_constrained(low,7u,bottom,7u,0,7,0u,1u,out)==THY_OK);
    CHECK(out[0]==0 && out[1]==4 && out[2]==7 && out[3]==901);
    CHECK(thy_voicing_lead_constrained(bottom,7u,prior,0u,120,127,0u,1u,out)==THY_OK);
    CHECK(out[0]==120 && out[1]==124 && out[2]==127);
    CHECK(thy_voicing_lead_constrained(low,7u,prior,0u,60,71,0u,0u,out)==THY_OK);
    CHECK(out[0]==60 && out[1]==64 && out[2]==67);
    const int16_t seven[7]={60,62,64,65,67,69,71};
    CHECK(thy_voicing_lead_constrained(seven,127u,seven,127u,60,71,0u,1u,out)==THY_OK);
    CHECK(!memcmp(out,seven,sizeof out));

    static thy_core held;
    thy_core_init(&held);
    CHECK(thy_core_press(&held,0u,prior,3u,0,0,THY_ROUTE_DIN,96u)==THY_OK);
    thy_pad_owner owner=held.owners[0];
    unsigned voices=held.sounding_count,queued=held.queue_count;
    CHECK(thy_voicing_lead_constrained(next,39u,prior,7u,48,83,5u,1u,out)==THY_OK);
    CHECK(!memcmp(&owner,&held.owners[0],sizeof owner));
    CHECK(held.sounding_count==voices && held.queue_count==queued && held.owners[0].held);
    return 1;
}

static int constrained_failures_are_atomic_and_rootless_stays_rootless(void)
{
    const int16_t chord[7]={60,64,67,999,-999,0,0};
    const int16_t seven[7]={60,62,64,65,67,69,71};
    const int16_t sentinel[7]={901,902,903,904,905,906,907};
    int16_t out[7],alias[7],corrupt[7];memcpy(out,sentinel,sizeof out);
    CHECK(thy_voicing_lead_constrained(chord,7u,chord,7u,60,71,5u,1u,out)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(out,sentinel,sizeof out)); /* F is absent; never insert it. */
    CHECK(thy_voicing_lead_constrained(seven,127u,seven,127u,60,69,0u,0u,out)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(out,sentinel,sizeof out)); /* No legal B, no silent omission. */
    CHECK(thy_voicing_lead_constrained(chord,7u,chord,7u,60,60,0u,1u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_lead_constrained(chord,7u,chord,7u,-1,127,0u,0u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_lead_constrained(chord,7u,chord,7u,0,128,0u,0u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_lead_constrained(chord,7u,chord,7u,71,60,0u,0u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_lead_constrained(chord,7u,chord,7u,0,127,12u,1u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_lead_constrained(chord,7u,chord,7u,0,127,0u,2u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_lead_constrained(chord,128u,chord,7u,0,127,0u,0u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_lead_constrained(chord,7u,chord,128u,0,127,0u,0u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_lead_constrained(NULL,7u,chord,7u,0,127,0u,0u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_lead_constrained(chord,7u,NULL,0u,0,127,0u,0u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_lead_constrained(chord,7u,chord,7u,0,127,0u,0u,NULL)==THY_INVALID_ARGUMENT);
    memcpy(corrupt,chord,sizeof corrupt);corrupt[0]=-1;
    CHECK(thy_voicing_lead_constrained(corrupt,7u,chord,7u,0,127,0u,0u,out)==THY_INVALID_ARGUMENT);
    CHECK(thy_voicing_lead_constrained(chord,7u,corrupt,7u,0,127,0u,0u,out)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(out,sentinel,sizeof out));
    memcpy(alias,chord,sizeof alias);
    CHECK(thy_voicing_lead_constrained(alias,7u,chord,7u,60,60,0u,1u,alias)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(alias,chord,sizeof alias));
    CHECK(thy_voicing_lead_constrained(corrupt,0u,chord,0u,0,127,0u,1u,out)==THY_OK);
    CHECK(!memcmp(out,corrupt,sizeof out)); /* Empty input retains inactive data. */
    return 1;
}

static int constrained_two_voice_independent_oracle_and_legacy_equivalence(void)
{
    const int16_t previous[7]={36,79,0,0,0,0,0};
    static const int intervals[]={1,4,7,11};
    int16_t out[7],old[7];
    for(int root=0;root<128;root+=17)
        for(unsigned interval=0u;interval<4u;++interval) {
            int16_t input[7]={(int16_t)root,(int16_t)((root+intervals[interval])%128),
                (int16_t)root,999,-999,0,0};
            CHECK(thy_voicing_lead(input,7u,previous,3u,old)==THY_OK);
            CHECK(thy_voicing_lead_constrained(input,7u,previous,3u,0,127,255u,0u,out)==THY_OK);
            CHECK(!memcmp(out,old,sizeof out));
            for(int low=0;low<128;low+=12)for(unsigned root_bass=0u;root_bass<2u;++root_bass) {
                int high=low+24<128?low+24:127;
                int16_t expected[7];for(unsigned n=0u;n<7u;++n)expected[n]=(int16_t)(901u+n);
                int reference_result=centered_pair_reference(input,previous,low,high,
                    (unsigned)(input[0]%12),root_bass,expected);
                for(unsigned n=0u;n<7u;++n)out[n]=(int16_t)(901u+n);
                int result=thy_voicing_lead_constrained(input,7u,previous,3u,
                    (int16_t)low,(int16_t)high,(uint8_t)(input[0]%12),(uint8_t)root_bass,out);
                CHECK(result==reference_result);
                CHECK(!memcmp(out,expected,sizeof out));
                if(result!=THY_OK) {
                    for(unsigned n=0u;n<7u;++n)CHECK(out[n]==(int16_t)(901u+n));
                } else {
                    int lo=out[0]<out[1]?out[0]:out[1],hi=out[0]>out[1]?out[0]:out[1];
                    CHECK(lo>=low && hi<=high && out[0]==out[2]);
                    CHECK(!root_bass || lo%12==input[0]%12);
                    CHECK(out[0]%12==input[0]%12 && out[1]%12==input[1]%12);
                    for(unsigned n=3u;n<7u;++n)CHECK(out[n]==input[n]);
                }
            }
        }
    return 1;
}

static int repeated_progressions_remain_in_the_authored_octave_register(void)
{
    static const int offsets[7][3]={{0,4,7},{2,5,9},{4,7,11},{5,9,12},{7,11,14},{9,12,16},{11,14,17}};
    static const int anchors[]={0,24,60,84,108};
    for(unsigned chromatic=0u;chromatic<2u;++chromatic)
        for(unsigned descending=0u;descending<2u;++descending)
            for(unsigned octave=0u;octave<sizeof anchors/sizeof anchors[0];++octave) {
                int16_t previous[7]={0},out[7],other[7];uint8_t old_mask=0;
                unsigned steps=chromatic?12u:7u;int base=anchors[octave];
                for(unsigned cycle=0u;cycle<64u;++cycle)for(unsigned step=0u;step<steps;++step) {
                    unsigned chord=descending?(steps-step)%steps:step;
                    int16_t source[7]={0,0,0,-999,0,999,-123};
                    for(unsigned n=0u;n<3u;++n)source[n]=(int16_t)(base+(chromatic?(int)chord+(n==0u?0:n==1u?4:7):offsets[chord][n]));
                    source[4]=source[0];
                    CHECK(thy_voicing_lead(source,23u,previous,old_mask,out)==THY_OK);
                    CHECK(thy_voicing_lead_constrained(source,23u,previous,old_mask,0,127,255u,0u,other)==THY_OK);
                    CHECK(!memcmp(out,other,sizeof out));
                    CHECK(abs(out[0]+out[1]+out[2]-source[0]-source[1]-source[2])<=18);
                    for(unsigned n=0u;n<3u;++n)CHECK(out[n]>=0 && out[n]<=127 && out[n]%12==source[n]%12);
                    CHECK(out[4]==out[0] && out[3]==-999 && out[5]==999 && out[6]==-123);
                    if(chord==0u)CHECK(out[0]==base && out[1]==base+4 && out[2]==base+7);
                    memcpy(previous,out,sizeof out);old_mask=23u;
                }
            }
    return 1;
}

int main(void)
{
    int(*tests[])(void)={constrained_register_root_and_owner_boundaries,
        constrained_failures_are_atomic_and_rootless_stays_rootless,
        constrained_two_voice_independent_oracle_and_legacy_equivalence,
        repeated_progressions_remain_in_the_authored_octave_register,
        register_modes_preserve_all_pitch_sets,automatic_leading,basic_drop_and_protected_bass,
        duplicates_holes_and_inactive_literals,permutations_use_pitch_rank_not_slot_index,
        exhaustive_duplicate_vectors_masks_and_drop_bits,every_midi_bass_and_boundary_combination,
        invalid_arguments_and_underflow_are_atomic};
    for(unsigned n=0u;n<sizeof tests/sizeof tests[0];++n)if(!tests[n]())return 1;
    printf("Voicing: %u scenarios passed (%u assertions)\n",
        (unsigned)(sizeof tests/sizeof tests[0]),assertions);
    return 0;
}
