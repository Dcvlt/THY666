/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_RESTORE_PROGRESS_H
#define THY_RESTORE_PROGRESS_H

#include <stdint.h>

enum thy_restore_phase {
    THY_RESTORE_IDLE = 0,
    THY_RESTORE_RECEIVING,
    THY_RESTORE_SAVING,
    THY_RESTORE_CHECKING,
    THY_RESTORE_SUCCESS,
    THY_RESTORE_ERROR
};

/* Read-only presentation of the actual restore service. The service owns
 * lifetime, input gating and counters. A zero total is indeterminate;
 * presentation must never turn elapsed time into apparent progress. */
typedef struct thy_restore_progress {
    uint8_t phase, active, pads_locked, device_verified;
    uint32_t completed, total;
} thy_restore_progress;

#endif
