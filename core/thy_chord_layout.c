/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_chord_layout.h"

static int scale_valid(const int16_t *degrees,uint8_t count)
{
    if(!degrees || !count || count>12u || degrees[0]!=0)return 0;
    for(unsigned n=0u;n<count;++n)
        if(degrees[n]<0 || degrees[n]>11 || (n && degrees[n]<=degrees[n-1u]))return 0;
    return 1;
}

int thy_chord_template_valid(const thy_chord_template *value)
{
    if(!value || value->valid_mask>127u)return 0;
    for(unsigned n=0u;n<THY_CHORD_SLOTS;++n)
        if(value->intervals[n]<-127 || value->intervals[n]>127)return 0;
    return 1;
}

int thy_chord_default_template(uint8_t index,thy_chord_template *out)
{
    static const thy_chord_template values[THY_CHORD_SIX_TEMPLATES]={
        {{0,4,7,0,0,0,0},7u},
        {{0,3,7,0,0,0,0},7u},
        {{0,4,7,10,0,0,0},15u},
        {{0,4,7,11,0,0,0},15u},
        {{0,3,7,10,0,0,0},15u},
        {{0,3,6,0,0,0,0},7u}
    };
    if(!out || index>=THY_CHORD_SIX_TEMPLATES)return THY_INVALID_ARGUMENT;
    *out=values[index];return THY_OK;
}

static int16_t scale_pitch(int16_t anchor,const int16_t *degrees,uint8_t count,
    unsigned degree)
{
    return (int16_t)(anchor+degrees[degree%count]+12*(int)(degree/count));
}

static void tone(thy_chord_cell *cell,unsigned slot,int16_t pitch,int enabled)
{
    cell->notes[slot]=pitch;
    if(enabled && pitch>=0 && pitch<=127)cell->valid_mask|=(uint8_t)(1u<<slot);
}
int thy_chord_six_resolve(uint8_t logical,int16_t anchor,const int16_t *degrees,
    uint8_t count,const thy_chord_template *value,thy_chord_cell *out)
{
    if(!out||logical>=48u||anchor<0||anchor>127||!scale_valid(degrees,count)||
       !thy_chord_template_valid(value))return THY_INVALID_ARGUMENT;
    unsigned degree=(logical%4u)*2u+logical/24u;thy_chord_cell cell={0};
    cell.root=scale_pitch(anchor,degrees,count,degree);cell.degree=(uint8_t)(degree%count);
    for(unsigned n=0;n<7u;++n)tone(&cell,n,(int16_t)(cell.root+value->intervals[n]),!!(value->valid_mask&(1u<<n)));
    *out=cell;return THY_OK;
}

int thy_chord_layout_resolve(uint8_t layout,uint8_t quality,uint8_t logical,
    int16_t anchor,const int16_t *degrees,uint8_t degree_count,
    const thy_chord_template six[THY_CHORD_SIX_TEMPLATES],thy_chord_cell *out)
{
    /* Sus qualities retain their suspended identity: their higher rows add
     * seventh/eleventh/thirteenth or seventh/ninth/thirteenth, respectively.
     * The table is an explicit THY666 policy, not a legacy reconstruction. */
    static const int16_t intervals[THY_CHORD_QUALITY_COUNT][6]={
        {0,4,7,11,14,17},
        {0,3,7,10,14,17},
        {0,4,7,10,14,17},
        {0,3,6,9,14,17},
        {0,4,8,11,14,18},
        {0,2,7,10,17,21},
        {0,5,7,10,14,21}
    };
    if(!out || layout<=THY_CHORD_LAYOUT_CLASSIC || layout>=THY_CHORD_LAYOUT_COUNT ||
        quality>=THY_CHORD_QUALITY_COUNT || logical>=48u || anchor<0 || anchor>127 ||
        !scale_valid(degrees,degree_count))return THY_INVALID_ARGUMENT;
    if(layout==THY_CHORD_LAYOUT_SIX_USER) {
        if(!six)return THY_INVALID_ARGUMENT;
        for(unsigned n=0u;n<THY_CHORD_SIX_TEMPLATES;++n)
            if(!thy_chord_template_valid(&six[n]))return THY_INVALID_ARGUMENT;
    }
    unsigned column=logical/4u,row=logical%4u;
    thy_chord_cell result={0};
    if(layout==THY_CHORD_LAYOUT_CHROMATIC) {
        result.root=(int16_t)(anchor+(int)column);
        result.degree=THY_CHORD_NO_DEGREE;
        for(unsigned slot=0u;slot<3u+row;++slot)
            tone(&result,slot,(int16_t)(result.root+intervals[quality][slot]),1);
    } else if(layout==THY_CHORD_LAYOUT_DIATONIC) {
        result.root=scale_pitch(anchor,degrees,degree_count,column);
        result.degree=(uint8_t)(column%degree_count);
        for(unsigned slot=0u;slot<3u+row;++slot)
            tone(&result,slot,scale_pitch(anchor,degrees,degree_count,column+slot*2u),1);
    } else {
        unsigned degree=row*2u+column/6u;
        result.root=scale_pitch(anchor,degrees,degree_count,degree);
        result.degree=(uint8_t)(degree%degree_count);
        thy_chord_template value;
        if(layout==THY_CHORD_LAYOUT_SIX_DEFAULT)
            (void)thy_chord_default_template((uint8_t)(column%6u),&value);
        else value=six[column%6u];
        for(unsigned slot=0u;slot<THY_CHORD_SLOTS;++slot)
            tone(&result,slot,(int16_t)(result.root+value.intervals[slot]),
                !!(value.valid_mask&(1u<<slot)));
    }
    *out=result;return THY_OK;
}

int thy_chord_piano_resolve(uint8_t logical,int16_t anchor,int16_t *pitch,
    uint8_t *active)
{
    if(!pitch || !active || logical>=48u || anchor<0 || anchor>127)return THY_INVALID_ARGUMENT;
    unsigned column=logical/4u,row=logical%4u;
    int16_t result=0;uint8_t enabled=0;
    /* One semitone per column. Each octave is a complementary pair: black
     * keys above, white keys below. A gap never acquires a musical owner. */
    unsigned black=(UINT32_C(0x54a)>>column)&1u;
    if(black!=(row&1u)) {
        result=(int16_t)((anchor/12)*12+(int)column-(row>=2u?12:0));
        enabled=(uint8_t)(result>=0 && result<=127);
    }
    *pitch=result;*active=enabled;return THY_OK;
}
