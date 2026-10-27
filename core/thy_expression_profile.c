/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_expression_profile.h"
#include <stddef.h>
void thy_expression_profile_defaults(thy_expression_profile *p)
{if(p)*p=(thy_expression_profile){0u,70u,0u,0u};}
int thy_expression_profile_valid(const thy_expression_profile *p)
{return p && p->enabled<=1u && p->split>=10u && p->split<=90u && p->pressure_curve<=2u && p->aftertouch_curve<=2u;}
static uint8_t curve(uint32_t n,uint8_t c)
{
    if(c==1u)n=127u-(127u-n)*(127u-n)/127u;
    else if(c==2u)n=n*n/127u;
    return (uint8_t)n;
}
static uint8_t normalized(uint16_t raw,uint16_t low,uint16_t high,uint8_t c)
{return curve(raw<=low?0u:raw>=high?127u:((uint32_t)raw-low)*127u/(high-low),c);}
int thy_expression_profile_axes(const thy_expression_profile *p,uint16_t raw,
    uint16_t floor,uint16_t full,uint8_t legacy,uint8_t *pressure,uint8_t *at)
{
    if(!thy_expression_profile_valid(p)||!pressure||!at||floor>=full||full>4095u||raw>4095u||legacy>2u)return 1;
    if(!p->enabled){*pressure=*at=normalized(raw,floor,full,legacy);return 0;}
    /* A one-count calibrated span cannot contain two nonempty zones. */
    if(full-floor<2u)return 1;
    uint16_t split=(uint16_t)(floor+((uint32_t)(full-floor)*p->split+50u)/100u);
    if(split<=floor)split=(uint16_t)(floor+1u);
    if(split>=full)split=(uint16_t)(full-1u);
    uint8_t primary=normalized(raw,floor,split,p->pressure_curve);
    uint8_t aftertouch=normalized(raw,split,full,p->aftertouch_curve);
    *pressure=primary;*at=aftertouch;return 0;
}
