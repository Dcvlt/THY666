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

/* Automatic octave placement around the authored source register. Center a
 * temporary previous-voice reference by whole octaves near the current source
 * centroid, then minimize motion across cyclic source orders. Ties favor the
 * authored register, then lower rotation/pitches. Place the resulting chord
 * in its nearest legal whole-octave register to that authored centroid.
 * This prevents repeated progressions walking toward MIDI0/127; it can make
 * an octave correction at a register boundary instead of preserving that walk.
 * Unequal counts map ranks proportionally; a single new voice uses the lower
 * median previous voice. Preserve pitch classes, duplicate slots, holes and
 * inactive literals. No previous voices means identity. MIDI0..127 only.
 * Both inputs may alias output. Invalid input leaves output untouched. */
int thy_voicing_lead(const int16_t notes[7],uint8_t mask,
    const int16_t previous[7],uint8_t previous_mask,int16_t out[7]);

/* Constrained placement with the same centered register policy.
 * Inclusive low/high must fit MIDI0..127. The same bounded cyclic-order search
 * minimizes centered-reference motion, then authored displacement; without previous voices the
 * authored source is the reference. Every enabled source voice/pitch class,
 * duplicate slot, hole and inactive literal is retained.
 * root_bass=1 requires root_pc0..11 to be present in the source, and makes that
 * pitch class the lowest enabled voice. No missing root is invented. root_pc
 * is ignored when root_bass=0. Empty input is identity.
 * Impossible constraints or invalid enabled MIDI/masks/pointers leave out
 * untouched and return THY_INVALID_ARGUMENT, permitting an explicit caller
 * fallback. Exact aliasing with either input is supported. No owner, heap,
 * mutable global storage, MIDI or clock state is accessed. */
int thy_voicing_lead_constrained(const int16_t notes[7],uint8_t mask,
    const int16_t previous[7],uint8_t previous_mask,int16_t low,int16_t high,
    uint8_t root_pc,uint8_t root_bass,int16_t out[7]);

/* Opt-in compact register: fit enabled notes into the octave beginning at the
 * lowest source pitch (capped at116 to fit MIDI127). Preserve slots and holes. */
int thy_voicing_fold(const int16_t notes[7],uint8_t mask,int16_t out[7]);
/* Standard E2 A2 D3 G3 B3 E4 guitar register, one distinct pitch class per
 * string, frets0..11. Minimize total fret then register. Unused strings mute;
 * duplicate pitch classes map together. Seven distinct classes pass unchanged
 * rather than losing a tone. No ergonomic fingering guarantee. Both functions
 * accept exact in-place use; invalid enabled MIDI/mask/pointers leave out intact. */
int thy_voicing_guitar(const int16_t notes[7],uint8_t mask,int16_t out[7]);
#endif
