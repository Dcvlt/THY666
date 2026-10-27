/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_VOICING_H
#define THY_VOICING_H
#include "thy_core.h"

#define THY_VOICING_SLOTS 7u
#define THY_VOICING_DROP_SECOND 1u
#define THY_VOICING_DROP_THIRD 2u
#define THY_VOICING_DROP_FOURTH 4u

/* Future-press slot substitution, independent of musical ownership/state.
 * Rank the UNIQUE enabled original pitches in ascending order. Bits0/1/2 of
 * drop_mask lower ranks1/2/3 by one octave, protecting the lowest unique pitch
 * (rank0). Every enabled original slot matching a selected pitch is lowered;
 * duplicate slots, ordering, holes and all inactive values remain intact.
 * Unavailable higher ranks are no-ops. The original validity mask is unchanged.
 * Reject NULL, mask bit7, unknown drop bits, enabled pitches outside MIDI0..127,
 * or a selected enabled pitch below12. Every rejection leaves output untouched.
 * Exact in-place operation is supported; no state, heap, MIDI or device I/O. */
int thy_voicing_drop(const int16_t notes[THY_VOICING_SLOTS],uint8_t valid_mask,
    uint8_t drop_mask,int16_t out[THY_VOICING_SLOTS]);

#endif
