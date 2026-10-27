/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_EXPRESSION_PROFILE_H
#define THY_EXPRESSION_PROFILE_H
#include <stdint.h>
/* Two logical zones derived from one sensor, not independent physical axes. */
typedef struct thy_expression_profile {
    uint8_t enabled, split, pressure_curve, aftertouch_curve;
} thy_expression_profile;
void thy_expression_profile_defaults(thy_expression_profile *);
int thy_expression_profile_valid(const thy_expression_profile *);
/* floor/full are calibrated sensor endpoints. Pressure reaches127 at split
 * and remains there; aftertouch rises0..127 above split. Disabled profiles
 * preserve the legacy curve and return the same value for both outputs.
 * Validation is atomic; output parameters remain untouched on error. */
int thy_expression_profile_axes(const thy_expression_profile *,uint16_t raw,
    uint16_t floor,uint16_t full,uint8_t legacy_curve,
    uint8_t *pressure,uint8_t *aftertouch);
#endif
