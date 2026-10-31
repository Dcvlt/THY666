/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_crc32.h"

/* Four polynomial steps per entry: two lookups per byte, one immutable
 * 64-byte table shared by codecs and snapshots, no mutable state or heap. */
static const uint32_t nibble[16]={
    UINT32_C(0x00000000),UINT32_C(0x1db71064),UINT32_C(0x3b6e20c8),UINT32_C(0x26d930ac),
    UINT32_C(0x76dc4190),UINT32_C(0x6b6b51f4),UINT32_C(0x4db26158),UINT32_C(0x5005713c),
    UINT32_C(0xedb88320),UINT32_C(0xf00f9344),UINT32_C(0xd6d6a3e8),UINT32_C(0xcb61b38c),
    UINT32_C(0x9b64c2b0),UINT32_C(0x86d3d2d4),UINT32_C(0xa00ae278),UINT32_C(0xbdbdf21c)
};

uint32_t thy_crc32_update(uint32_t state,const uint8_t *bytes,size_t count)
{
    for(size_t n=0;n<count;++n){
        state^=bytes[n];
        state=(state>>4)^nibble[state&15u];
        state=(state>>4)^nibble[state&15u];
    }
    return state;
}
uint32_t thy_crc32(const uint8_t *bytes,size_t count)
{return ~thy_crc32_update(UINT32_MAX,bytes,count);}
