/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "thy_chord_layout.h"

static unsigned assertions;
#define CHECK(x) do{++assertions;if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static const int16_t major[7]={0,2,4,5,7,9,11};

static int chromatic_quality_rows(void)
{
    static const int16_t expected[7][6]={
        {0,4,7,11,14,17},{0,3,7,10,14,17},{0,4,7,10,14,17},
        {0,3,6,9,14,17},{0,4,8,11,14,18},{0,2,7,10,17,21},
        {0,5,7,10,14,21}
    };
    for(unsigned root=0u;root<12u;++root)
        for(unsigned q=0u;q<THY_CHORD_QUALITY_COUNT;++q)
            for(unsigned p=0u;p<48u;++p) {
                thy_chord_cell cell;
                CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_CHROMATIC,(uint8_t)q,
                    (uint8_t)p,(int16_t)(48+root),major,7u,NULL,&cell)==THY_OK);
                unsigned count=3u+p%4u;
                CHECK(cell.root==(int16_t)(48+root+p/4u));
                CHECK(cell.degree==THY_CHORD_NO_DEGREE);
                CHECK(cell.valid_mask==(1u<<count)-1u);
                for(unsigned slot=0u;slot<7u;++slot)
                    CHECK(cell.notes[slot]==(slot<count?cell.root+expected[q][slot]:0));
            }
    return 1;
}

static int all_scale_sizes_and_roots(void)
{
    /* Every allowed degree count, in all twelve roots, independent of one
     * seven-note major fixture. Sparse masks exercise nonconsecutive degrees. */
    for(unsigned count=1u;count<=12u;++count) {
        int16_t degrees[12];
        for(unsigned n=0u;n<count;++n)degrees[n]=(int16_t)(n*12u/count);
        for(unsigned root=0u;root<12u;++root)
            for(unsigned p=0u;p<48u;++p) {
                thy_chord_cell cell;
                int anchor=(int)(root+24u),column=(int)(p/4u);
                CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_DIATONIC,0u,
                    (uint8_t)p,(int16_t)anchor,degrees,(uint8_t)count,NULL,&cell)==THY_OK);
                CHECK(cell.root==anchor+degrees[(unsigned)column%count]+12*(column/(int)count));
                CHECK(cell.degree==(unsigned)column%count);
                unsigned mask=0u,tones=3u+p%4u;
                for(unsigned n=0u;n<tones;++n) {
                    unsigned degree=(unsigned)column+n*2u;
                    int expected=anchor+degrees[degree%count]+12*(int)(degree/count);
                    CHECK(cell.notes[n]==expected);
                    if(expected<=127)mask|=1u<<n;
                }
                CHECK(cell.valid_mask==mask);
            }
    }
    return 1;
}

static int all_degree_sets_are_validated_without_reinterpretation(void)
{
    for(unsigned set=1u;set<4096u;set+=2u) {
        int16_t degrees[12];uint8_t count=0;
        for(unsigned n=0u;n<12u;++n)if(set&(1u<<n))degrees[count++]=(int16_t)n;
        for(unsigned root=0u;root<12u;++root) {
            thy_chord_cell cell;
            CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_DIATONIC,0u,47u,
                (int16_t)(48u+root),degrees,count,NULL,&cell)==THY_OK);
            CHECK(cell.root==48+(int)root+degrees[11u%count]+12*(int)(11u/count));
            CHECK(cell.degree==11u%count);
        }
    }
    return 1;
}

static int six_default_and_holey_custom_templates(void)
{
    thy_chord_template bank[6];
    for(unsigned n=0u;n<6u;++n)CHECK(thy_chord_default_template((uint8_t)n,&bank[n])==THY_OK);
    for(unsigned p=0u;p<48u;++p) {
        thy_chord_cell cell;
        unsigned degree=p%4u*2u+p/4u/6u,index=p/4u%6u;
        CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_SIX_DEFAULT,0u,(uint8_t)p,
            48,major,7u,NULL,&cell)==THY_OK);
        CHECK(cell.root==48+major[degree%7u]+12*(int)(degree/7u));
        CHECK(cell.degree==degree%7u && cell.valid_mask==bank[index].valid_mask);
        for(unsigned n=0u;n<7u;++n)CHECK(cell.notes[n]==cell.root+bank[index].intervals[n]);
    }
    bank[0]=(thy_chord_template){{0,-12,4,127,7,-127,12},0x55u};
    thy_chord_cell cell;
    CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_SIX_USER,0u,0u,0,major,7u,bank,&cell)==THY_OK);
    CHECK(cell.root==0 && cell.degree==0 && cell.valid_mask==0x55u);
    CHECK(cell.notes[0]==0 && cell.notes[1]==-12 && cell.notes[2]==4 &&
        cell.notes[3]==127 && cell.notes[4]==7 && cell.notes[5]==-127 && cell.notes[6]==12);
    bank[0].valid_mask=0u;
    CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_SIX_USER,0u,0u,60,major,7u,bank,&cell)==THY_OK);
    CHECK(cell.valid_mask==0 && cell.notes[0]==60 && cell.notes[3]==187);
    return 1;
}

static int midi_boundaries_omit_without_clamping(void)
{
    thy_chord_cell cell;
    CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_CHROMATIC,0u,0u,0,major,7u,NULL,&cell)==THY_OK);
    CHECK(cell.root==0 && cell.valid_mask==7u && cell.notes[0]==0);
    CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_CHROMATIC,0u,3u,127,major,7u,NULL,&cell)==THY_OK);
    CHECK(cell.root==127 && cell.valid_mask==1u && cell.notes[1]==131 && cell.notes[5]==144);
    CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_CHROMATIC,0u,47u,127,major,7u,NULL,&cell)==THY_OK);
    CHECK(cell.root==138 && !cell.valid_mask && cell.notes[0]==138);
    const int16_t only_root[]={0};
    CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_DIATONIC,0u,47u,127,only_root,1u,NULL,&cell)==THY_OK);
    CHECK(cell.root==259 && !cell.valid_mask && cell.notes[5]==379);
    thy_chord_template bank[6];
    for(unsigned n=0u;n<6u;++n)bank[n]=(thy_chord_template){{-127,0,127,0,0,0,0},7u};
    CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_SIX_USER,0u,0u,0,major,7u,bank,&cell)==THY_OK);
    CHECK(cell.notes[0]==-127 && cell.notes[1]==0 && cell.notes[2]==127 && cell.valid_mask==6u);
    return 1;
}

static int piano_geometry_and_octaves(void)
{
    for(unsigned anchor=60u;anchor<72u;++anchor) {
        unsigned active_count=0u;
        for(unsigned p=0u;p<48u;++p) {
            int16_t pitch=-300;uint8_t active=99u;
            unsigned col=p/4u,row=p%4u;
            int accidental=col==1u || col==3u || col==6u || col==8u || col==10u;
            int gap=accidental==(int)(row&1u);
            CHECK(thy_chord_piano_resolve((uint8_t)p,(int16_t)anchor,&pitch,&active)==THY_OK);
            CHECK(active==!gap);
            CHECK(pitch==(gap?0:60+(int)col-(row>=2u?12:0)));
            active_count+=active;
        }
        CHECK(active_count==24u);
    }
    int16_t pitch;uint8_t active;
    CHECK(thy_chord_piano_resolve(1u,0,&pitch,&active)==THY_OK && pitch==0 && active);
    CHECK(thy_chord_piano_resolve(3u,0,&pitch,&active)==THY_OK && pitch==-12 && !active);
    CHECK(thy_chord_piano_resolve(31u,0,&pitch,&active)==THY_OK && pitch==-5 && !active);
    CHECK(thy_chord_piano_resolve(45u,127,&pitch,&active)==THY_OK && pitch==131 && !active);
    CHECK(thy_chord_piano_resolve(5u,60,&pitch,&active)==THY_OK && pitch==0 && !active);
    CHECK(thy_chord_piano_resolve(4u,60,&pitch,&active)==THY_OK && pitch==61 && active);
    return 1;
}

static int rejected_arguments_are_atomic(void)
{
    thy_chord_cell before,cell;
    memset(&before,0x5a,sizeof before);cell=before;
    thy_chord_template bank[6];
    for(unsigned n=0u;n<6u;++n)CHECK(thy_chord_default_template((uint8_t)n,&bank[n])==THY_OK);
    CHECK(!thy_chord_template_valid(NULL));
    thy_chord_template original=bank[0];
    CHECK(thy_chord_default_template(6u,&bank[0])==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&original,&bank[0],sizeof original));
    CHECK(thy_chord_default_template(0u,NULL)==THY_INVALID_ARGUMENT);
    for(unsigned layout=0u;layout<7u;++layout) {
        if(layout>0u && layout<5u)continue;
        CHECK(thy_chord_layout_resolve((uint8_t)layout,0u,0u,60,major,7u,NULL,&cell)==THY_INVALID_ARGUMENT);
        CHECK(!memcmp(&before,&cell,sizeof cell));
    }
    CHECK(thy_chord_layout_resolve(1u,7u,0u,60,major,7u,NULL,&cell)==THY_INVALID_ARGUMENT);
    CHECK(thy_chord_layout_resolve(1u,0u,48u,60,major,7u,NULL,&cell)==THY_INVALID_ARGUMENT);
    CHECK(thy_chord_layout_resolve(1u,0u,0u,-1,major,7u,NULL,&cell)==THY_INVALID_ARGUMENT);
    CHECK(thy_chord_layout_resolve(1u,0u,0u,128,major,7u,NULL,&cell)==THY_INVALID_ARGUMENT);
    CHECK(thy_chord_layout_resolve(1u,0u,0u,60,NULL,7u,NULL,&cell)==THY_INVALID_ARGUMENT);
    CHECK(thy_chord_layout_resolve(1u,0u,0u,60,major,0u,NULL,&cell)==THY_INVALID_ARGUMENT);
    CHECK(thy_chord_layout_resolve(1u,0u,0u,60,major,13u,NULL,&cell)==THY_INVALID_ARGUMENT);
    CHECK(thy_chord_layout_resolve(1u,0u,0u,60,major,7u,NULL,NULL)==THY_INVALID_ARGUMENT);
    const int16_t invalid[][3]={{1,2,3},{0,0,2},{0,2,1},{0,-1,2},{0,2,12}};
    for(unsigned n=0u;n<5u;++n) {
        CHECK(thy_chord_layout_resolve(1u,0u,0u,60,invalid[n],3u,NULL,&cell)==THY_INVALID_ARGUMENT);
        CHECK(!memcmp(&before,&cell,sizeof cell));
    }
    CHECK(thy_chord_layout_resolve(4u,0u,0u,60,major,7u,NULL,&cell)==THY_INVALID_ARGUMENT);
    bank[5].intervals[6]=128;
    CHECK(!thy_chord_template_valid(&bank[5]));
    CHECK(thy_chord_layout_resolve(4u,0u,0u,60,major,7u,bank,&cell)==THY_INVALID_ARGUMENT);
    bank[5].intervals[6]=-128;
    CHECK(!thy_chord_template_valid(&bank[5]));
    CHECK(thy_chord_layout_resolve(4u,0u,0u,60,major,7u,bank,&cell)==THY_INVALID_ARGUMENT);
    bank[5].intervals[6]=0;bank[5].valid_mask=128u;
    CHECK(!thy_chord_template_valid(&bank[5]));
    CHECK(thy_chord_layout_resolve(4u,0u,0u,60,major,7u,bank,&cell)==THY_INVALID_ARGUMENT);
    CHECK(!memcmp(&before,&cell,sizeof cell));
    int16_t pitch=91;uint8_t active=92u;
    CHECK(thy_chord_piano_resolve(48u,60,&pitch,&active)==THY_INVALID_ARGUMENT);
    CHECK(thy_chord_piano_resolve(0u,-1,&pitch,&active)==THY_INVALID_ARGUMENT);
    CHECK(thy_chord_piano_resolve(0u,128,&pitch,&active)==THY_INVALID_ARGUMENT);
    CHECK(thy_chord_piano_resolve(0u,60,NULL,&active)==THY_INVALID_ARGUMENT);
    CHECK(thy_chord_piano_resolve(0u,60,&pitch,NULL)==THY_INVALID_ARGUMENT);
    CHECK(pitch==91 && active==92u);
    return 1;
}

int main(void)
{
    int(*tests[])(void)={chromatic_quality_rows,all_scale_sizes_and_roots,
        all_degree_sets_are_validated_without_reinterpretation,
        six_default_and_holey_custom_templates,midi_boundaries_omit_without_clamping,
        piano_geometry_and_octaves,rejected_arguments_are_atomic};
    for(unsigned n=0u;n<sizeof tests/sizeof tests[0];++n)if(!tests[n]())return 1;
    printf("Chord layouts: %u scenarios passed (%u assertions; cell %u bytes, template %u bytes)\n",
        (unsigned)(sizeof tests/sizeof tests[0]),assertions,(unsigned)sizeof(thy_chord_cell),
        (unsigned)sizeof(thy_chord_template));
    return 0;
}
