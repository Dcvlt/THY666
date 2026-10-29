/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_RESOURCES_H
#define THY_RESOURCES_H
#include <stdint.h>
/* Read-only target measurements. Zero valid means no hardware provider.
 * CPU active includes polling and interrupt time, not useful-work load.
 * Stack is an observed watermark, never a proof of worst-case safety. */
typedef struct thy_resources {
    uint32_t flash_used, flash_total, ram_used, ram_total, ccm_used, ccm_total;
    uint32_t stack_used, stack_total, loop_us, loop_peak_us, sampled_ms;
    uint8_t valid, timing_valid, stack_valid, cpu_active;
} thy_resources;
typedef struct thy_resource_clock {
    uint32_t cycles, ms, previous, count, peak;
    uint8_t initialized;
} thy_resource_clock;
static inline void thy_resources_tick(thy_resources *r, thy_resource_clock *c,
    uint32_t cycles, uint32_t ms, uint32_t hz)
{
    if(hz<1000000u)return;
    if(!c->initialized){c->cycles=c->previous=cycles;c->ms=ms;c->initialized=1u;return;}
    uint32_t delta=cycles-c->previous;c->previous=cycles;
    if(delta>c->peak)c->peak=delta;
    ++c->count;
    uint32_t elapsed=ms-c->ms;
    if(elapsed<1000u)return;
    /* CYCCNT is modulo 32 bits: reject windows long enough to wrap twice. */
    if(elapsed<=5000u && hz<=200000000u){
        uint32_t spent=cycles-c->cycles;
        uint64_t expected=(uint64_t)hz*elapsed/1000u;
        uint64_t percent=expected?((uint64_t)spent*100u+expected/2u)/expected:0u;
        r->cpu_active=(uint8_t)(percent>100u?100u:percent);
        r->loop_us=spent/c->count/(hz/1000000u);
        r->loop_peak_us=c->peak/(hz/1000000u);r->timing_valid=1u;
    }else r->timing_valid=0u;
    r->sampled_ms=ms;c->cycles=cycles;c->ms=ms;c->count=0u;c->peak=0u;
}
#endif
