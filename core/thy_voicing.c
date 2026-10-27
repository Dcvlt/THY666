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
