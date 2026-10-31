/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include "thy_crc32.h"

static unsigned checks;
#define CHECK(value) do {++checks;if(!(value)){ \
    fprintf(stderr,"CRC32 check failed at line %u: %s\n",(unsigned)__LINE__,#value); \
    abort();}} while(0)
/* Independent bit-at-a-time oracle. Keep the polynomial arithmetic here,
 * rather than copying the production table or testing it against itself. */
static uint32_t reference(uint32_t state,const uint8_t *bytes,size_t count)
{
    for(size_t n=0;n<count;++n){state^=bytes[n];
        for(unsigned bit=0;bit<8u;++bit)
            state=(state>>1)^((state&1u)?UINT32_C(0xedb88320):0u);
    }
    return state;
}
static uint32_t random_word(uint32_t *state)
{*state=*state*UINT32_C(1664525)+UINT32_C(1013904223);return *state;}

int main(void)
{
    static uint8_t data[13920];
    CHECK(thy_crc32(NULL,0u)==0u);
    CHECK(thy_crc32_update(UINT32_C(0x12345678),NULL,0u)==UINT32_C(0x12345678));
    CHECK(thy_crc32((const uint8_t *)"123456789",9u)==UINT32_C(0xcbf43926));
    for(unsigned n=0;n<256u;++n)data[n]=(uint8_t)n;
    CHECK(thy_crc32(data,256u)==UINT32_C(0x29058c73));
    for(unsigned byte=0;byte<256u;++byte){
        uint8_t value=(uint8_t)byte;
        for(unsigned seed=0;seed<256u;++seed){
            uint32_t initial=UINT32_C(0x01010101)*seed;
            CHECK(thy_crc32_update(initial,&value,1u)==reference(initial,&value,1u));
        }
    }
    uint32_t rng=UINT32_C(0x31415926);
    const size_t lengths[]={0u,1u,2u,7u,31u,32u,63u,64u,127u,128u,
        256u,800u,1024u,1672u,1680u,8812u,13854u,13918u};
    for(unsigned round=0;round<8u;++round){
        for(size_t n=0;n<sizeof data;++n)data[n]=(uint8_t)(random_word(&rng)>>24);
        for(unsigned offset=0;offset<2u;++offset){
            const uint8_t *bytes=data+offset;
            for(unsigned i=0;i<sizeof lengths/sizeof lengths[0];++i){
                size_t count=lengths[i];uint32_t initial=random_word(&rng);
                uint32_t expected=reference(initial,bytes,count);
                CHECK(thy_crc32_update(initial,bytes,count)==expected);
                CHECK(thy_crc32(bytes,count)==~reference(UINT32_MAX,bytes,count));
                for(size_t stride=1u;stride<=128u;stride*=2u){
                    uint32_t actual=initial;
                    for(size_t at=0;at<count;){size_t part=count-at;if(part>stride)part=stride;
                        actual=thy_crc32_update(actual,bytes+at,part);at+=part;}
                    CHECK(actual==expected);
                }
                size_t split=count?(size_t)(random_word(&rng)%(count+1u)):0u;
                CHECK(thy_crc32_update(thy_crc32_update(initial,bytes,split),bytes+split,count-split)==expected);
            }
        }
    }
    puts("Shared CRC32: known vectors, all single bytes/states, maximum payloads, alignment and streaming boundaries passed");
    printf("CRC32 checks: %u\n",checks);
    return 0;
}
