/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "thy_voicing.h"

static unsigned assertions;
#define CHECK(x) do{++assertions;if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)

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

int main(void)
{
    int(*tests[])(void)={basic_drop_and_protected_bass,
        duplicates_holes_and_inactive_literals,permutations_use_pitch_rank_not_slot_index,
        exhaustive_duplicate_vectors_masks_and_drop_bits,every_midi_bass_and_boundary_combination,
        invalid_arguments_and_underflow_are_atomic};
    for(unsigned n=0u;n<sizeof tests/sizeof tests[0];++n)if(!tests[n]())return 1;
    printf("Voicing: %u scenarios passed (%u assertions)\n",
        (unsigned)(sizeof tests/sizeof tests[0]),assertions);
    return 0;
}
