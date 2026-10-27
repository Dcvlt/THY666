/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_INSTRUMENT_LED_H
#define THY_INSTRUMENT_LED_H

#include "../diagnostic/led.h"
#include "../../core/thy_lighting.h"

#define THY_INSTRUMENT_LIGHT_INTERVAL_MS UINT32_C(20)
#define THY_INSTRUMENT_LIGHT_RENEW_MS UINT32_C(5000)

typedef struct thy_instrument_lights_status {
    /* Latest accepted electrical-pad selection, not an optical measurement. */
    uint32_t selected[3];
    uint32_t updates, errors, faulted;
    int32_t last_result;
} thy_instrument_lights_status;

/* RAM-only initialization; board_led_init() remains the driver's owner. */
void thy_instrument_lights_init(void);
typedef int (*thy_instrument_lights_override_fn)(void *,uint32_t,uint32_t colors[96]);
void thy_instrument_lights_set_override(thy_instrument_lights_override_fn,void *context);
/* Call after input/transport readiness and panic processing. Shared note
 * lighting supplies idle/held colors; logical->electrical remapping occurs
 * only here. Ordered LED components are retained without asserting RGB
 * wiring. Brightness never exceeds the tested8/255 per component. Disable
 * input on acquisition faults. board_led_task() still owns DMA progress. */
void thy_instrument_lights_task(const thy_instrument *instrument, uint32_t now);
const thy_instrument_lights_status *thy_instrument_lights_report(void);
/* Copy the existing last accepted publication in logical RGB order,0..8.
 * No rendering, DMA wait or new buffer; not an optical completion claim. */
int thy_instrument_lights_copy_rgb(const thy_instrument *,uint8_t rgb[288]);

#endif
