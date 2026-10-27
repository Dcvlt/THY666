/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "control_lights.h"
#include "../diagnostic/board.h"
#include "../diagnostic/center.h"

static thy_control_lights_status status;
static uint16_t accepted[DIAG_CENTER_CHANNELS], queued[DIAG_CENTER_CHANNELS];
static uint32_t observed_frames, queued_after, last_request, retry_after;
static uint8_t accepted_valid, request_valid, retry_pending;

void thy_instrument_control_lights_init(void)
{
    status = (thy_control_lights_status){0};
    observed_frames = queued_after = last_request = retry_after = 0u;
    accepted_valid = request_valid = retry_pending = 0u;
    for (unsigned n = 0u; n < DIAG_CENTER_CHANNELS; ++n) accepted[n] = queued[n] = 0u;
}

const thy_control_lights_status *thy_instrument_control_lights_report(void) { return &status; }

static void retry(int result, uint32_t now)
{
    status.last_result = result;
    ++status.errors;
    status.waiting = 0u;
    accepted_valid = 0u;
    retry_after = now + THY_CONTROL_LIGHT_RETRY_MS;
    retry_pending = 1u;
}

void thy_instrument_control_lights_task(const thy_engine *engine, uint32_t now)
{
    if (engine == NULL || engine->instrument == NULL) return;
    const diag_center_report_t *driver = board_center_report();
    if (status.waiting) {
        if (driver->state == DIAG_CENTER_QUEUED) return;
        if (driver->state == DIAG_CENTER_READY && driver->mode == DIAG_CENTER_LEVELS &&
            driver->last_result == DIAG_SUCCESS && driver->frames != queued_after) {
            for (unsigned n = 0u; n < DIAG_CENTER_CHANNELS; ++n) accepted[n] = queued[n];
            accepted_valid = 1u;
            status.waiting = 0u;
            status.updated_ms = driver->updated_ms;
            status.last_result = DIAG_SUCCESS;
            ++status.updates;
        } else {
            retry(driver->last_result == DIAG_SUCCESS ? DIAG_BUSY : driver->last_result, now);
        }
        observed_frames = driver->frames;
    } else if (driver->frames != observed_frames) {
        /* An explicit diagnostic command can temporarily replace the frame.
         * Reassert the current shared UI state once that transfer finishes. */
        observed_frames = driver->frames;
        accepted_valid = 0u;
    }
    if (driver->state == DIAG_CENTER_QUEUED) return;
    if (retry_pending) {
        if ((int32_t)(now - retry_after) < 0) return;
        retry_pending = 0u;
    }
    /* A valid nonzero brightness always has function-button legend levels.
     * It therefore cannot produce an immediate all-OFF frame. Skip expensive
     * musical availability evaluation during the same coalescing window that
     * already postpones transmission. First/OFF/diagnostic/retry paths retain
     * their existing handling above and below. */
    if (request_valid && engine != NULL && engine->instrument != NULL &&
        engine->instrument->center_brightness > 0u &&
        engine->instrument->center_brightness <= 8u &&
        (uint32_t)(now - last_request) < THY_CONTROL_LIGHT_INTERVAL_MS) return;
    uint16_t logical[THY_INSTRUMENT_CENTER_LIGHTS], desired[DIAG_CENTER_CHANNELS] = {0};
    thy_engine_center_levels(engine, logical);
    for (unsigned n = 0u; n < THY_INSTRUMENT_CENTER_LIGHTS; ++n) {
        int channel = diag_center_logical_channel(n < 5u ? 0u : 1u, n < 5u ? n : n - 5u);
        if (channel < 5 || channel >= (int)DIAG_CENTER_CHANNELS) { retry(DIAG_INVALID, now); return; }
        desired[channel] = logical[n];
    }
    int changed = !accepted_valid;
    int any = 0;
    for (unsigned n = 0u; n < DIAG_CENTER_CHANNELS; ++n) {
        if (desired[n] != accepted[n]) changed = 1;
        any |= desired[n] != 0u;
    }
    if (!changed) return;
    /* Immediate first/OFF frames; other edits coalesce at most once per16ms. */
    if (request_valid && any && (uint32_t)(now - last_request) < THY_CONTROL_LIGHT_INTERVAL_MS) return;
    int result = board_center_request_levels(desired);
    status.last_result = result;
    if (result == DIAG_BUSY) { ++status.busy; return; }
    if (result != DIAG_SUCCESS) { retry(result, now); return; }
    for (unsigned n = 0u; n < DIAG_CENTER_CHANNELS; ++n) queued[n] = desired[n];
    queued_after = driver->frames;
    last_request = now;
    request_valid = 1u;
    status.waiting = 1u;
}
