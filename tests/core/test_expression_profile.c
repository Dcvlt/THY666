/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "thy_expression_profile.h"
static unsigned assertions;
#define CHECK(x) do{++assertions;if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static int stages_and_atomicity(void)
{
    thy_expression_profile p;thy_expression_profile_defaults(&p);
    CHECK(!p.enabled&&p.split==70&&thy_expression_profile_valid(&p));
    uint8_t pressure=91,at=92;
    CHECK(!thy_expression_profile_axes(&p,600,100,1100,0,&pressure,&at));CHECK(pressure==63&&at==63);
    p.enabled=1;p.split=50;
    const uint16_t raw[]={0,100,350,600,850,1100,4095};
    const uint8_t expected_pressure[]={0,0,63,127,127,127,127},expected_at[]={0,0,0,0,63,127,127};
    for(unsigned n=0;n<7;++n){CHECK(!thy_expression_profile_axes(&p,raw[n],100,1100,0,&pressure,&at));CHECK(pressure==expected_pressure[n]&&at==expected_at[n]);}
    pressure=91;at=92;
    CHECK(thy_expression_profile_axes(&p,100,100,101,0,&pressure,&at));CHECK(pressure==91&&at==92);
    p.split=9;CHECK(!thy_expression_profile_valid(&p));CHECK(thy_expression_profile_axes(&p,600,100,1100,0,&pressure,&at));CHECK(pressure==91&&at==92);
    return 1;
}
static int monotone_independent_curves(void)
{
    for(unsigned split=10;split<=90;split+=10)for(unsigned pc=0;pc<3;++pc)for(unsigned ac=0;ac<3;++ac){
        thy_expression_profile p={1,(uint8_t)split,(uint8_t)pc,(uint8_t)ac};
        unsigned lastp=0,lasta=0;uint8_t pressure=0,at=0;
        uint16_t boundary=(uint16_t)(200+2800u*split/100u);
        for(unsigned raw=0;raw<=4095;++raw){CHECK(!thy_expression_profile_axes(&p,(uint16_t)raw,200,3000,2,&pressure,&at));
            CHECK(pressure>=lastp&&at>=lasta);CHECK(raw>boundary?pressure==127:at==0);lastp=pressure;lasta=at;}
        CHECK(pressure==127&&at==127);
    }
    return 1;
}
static int legacy_preservation(void)
{
    thy_expression_profile p={0,10,2,2};
    for(unsigned curve=0;curve<3;++curve)for(unsigned raw=0;raw<=4095;++raw){
        uint32_t n=raw<=700?0:raw>=3700?127:(raw-700)*127u/3000u;
        if(curve==1)n=127-(127-n)*(127-n)/127;else if(curve==2)n=n*n/127;
        uint8_t primary=88,at=99;CHECK(!thy_expression_profile_axes(&p,(uint16_t)raw,700,3700,(uint8_t)curve,&primary,&at));
        CHECK(primary==n&&at==n);
    }
    return 1;
}
static int bounded_tiny_ranges(void)
{
    for(unsigned width=2;width<=24;++width)for(unsigned split=10;split<=90;split+=10){
        thy_expression_profile p={1,(uint8_t)split,0,0};uint8_t primary=0,at=0;
        CHECK(!thy_expression_profile_axes(&p,1000,1000,(uint16_t)(1000+width),0,&primary,&at));CHECK(!primary&&!at);
        CHECK(!thy_expression_profile_axes(&p,(uint16_t)(1000+width),1000,(uint16_t)(1000+width),0,&primary,&at));CHECK(primary==127&&at==127);
    }
    return 1;
}
int main(void)
{if(!stages_and_atomicity()||!monotone_independent_curves()||!legacy_preservation()||!bounded_tiny_ranges())return 1;
printf("Expression profiles: 4 scenarios passed (%u assertions)\n",assertions);return 0;}
