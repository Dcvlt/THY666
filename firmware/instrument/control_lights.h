/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_INSTRUMENT_CONTROL_LIGHTS_H
#define THY_INSTRUMENT_CONTROL_LIGHTS_H

#include "thy_engine.h"

#define THY_CONTROL_LIGHT_INTERVAL_MS 16u
#define THY_CONTROL_LIGHT_RETRY_MS 250u

typedef struct thy_control_lights_status {
    uint32_t updates, errors, busy, updated_ms;
    int32_t last_result;
    uint8_t waiting;
} thy_control_lights_status;

/* No hardware access at init. task coalesces shared logical levels and only
 * queues a changed finite center frame. Busy frames and clock failures retry;
 * neither changes pad acquisition or musical ownership. */
void thy_instrument_control_lights_init(void);
void thy_instrument_control_lights_task(const thy_engine *engine, uint32_t now);
const thy_control_lights_status *thy_instrument_control_lights_report(void);

#endif
