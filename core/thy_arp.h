/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_ARP_H
#define THY_ARP_H
#include "thy_core.h"
#include "thy_clock.h"

#define THY_ARP_INPUTS 96u
#define THY_ARP_PHRASES 97u
#define THY_ARP_POOL 96u
enum thy_arp_type { THY_ARP_NOTES, THY_ARP_CHORD };
enum thy_arp_grouping { THY_ARP_HYBRID, THY_ARP_COMBINED, THY_ARP_EACH };
enum thy_arp_start { THY_ARP_IMMEDIATE, THY_ARP_GRID };
enum thy_arp_pattern {
    THY_ARP_UP, THY_ARP_DOWN, THY_ARP_UP_DOWN, THY_ARP_DOWN_UP, THY_ARP_AS_PLAYED
};
enum thy_arp_result { THY_ARP_TIME_BACKWARD = 5, THY_ARP_STALE_RUN = 6 };
typedef struct thy_arp_config {
    uint32_t rate_ticks; /* 40..3840 ticks, quarter=960. */
    int8_t offsets[8];   /* Chord type: signed scale-degree offsets, -24..24. */
    uint8_t scale[12];   /* Starts at zero, strictly increasing within 0..11. */
    uint8_t scale_count, root;
    uint8_t enabled, type, grouping, source_zones, layer, start;
    uint8_t restart_on_change, gate_percent, swing_percent, step_mask, pattern;
    uint8_t channel, destinations;
} thy_arp_config;
typedef struct thy_arp_input {
    uint64_t pitches[2];
    uint64_t order;
    uint8_t held, role, velocity;
} thy_arp_input;
typedef struct thy_arp_phrase {
    uint64_t pitches[2];
    uint64_t anchor;
    uint64_t ordinal; /* Next step ordinal; rests also consume a step. */
    uint8_t active, velocity;
} thy_arp_phrase;
typedef struct thy_arp_output {
    uint64_t off_tick;
    thy_generated_handle handle;
    uint8_t phrase;
} thy_arp_output;
typedef struct thy_arp {
    thy_arp_config config;
    thy_arp_input inputs[THY_ARP_INPUTS];
    thy_arp_phrase phrases[THY_ARP_PHRASES];
    thy_arp_output outputs[THY_GENERATED_OWNER_COUNT];
    uint64_t last_tick, press_order;
    uint32_t source, run, attacks, skipped, failures;
    uint16_t held_count, phrase_count, output_count;
    uint8_t last_error;
} thy_arp;

void thy_arp_default_config(thy_arp_config *config);
int thy_arp_config_valid(const thy_arp_config *config);
void thy_arp_init(thy_arp *arp, uint32_t source);
int thy_arp_configure(thy_arp *arp, thy_core *core, const thy_arp_config *config,
    uint64_t tick, uint32_t run, int ready);
int thy_arp_press(thy_arp *arp, thy_core *core, uint8_t input_id, uint8_t role,
    const int16_t *notes, size_t count, uint8_t velocity,
    uint64_t tick, uint32_t run, int ready);
int thy_arp_release(thy_arp *arp, thy_core *core, uint8_t input_id,
    uint64_t tick, uint32_t run, int ready);
int thy_arp_advance(thy_arp *arp, thy_core *core,
    uint64_t tick, uint32_t run, int ready);
/* Input IDs are stable caller identities (electrical contacts in the instrument),
 * not logical positions. Caller filters new presses using source_zones against
 * its current mapping; this module cannot infer a logical zone from input_id.
 * Source-zone/Layer edits affect future admission, never adopt held inputs.
 * Current gates retain captured routes; output edits affect future attacks.
 * Both clear forms forget admitted inputs and release only handles acquired.
 * Root owns dry Layer notes. Loss/new generation does not re-adopt held pads.
 * Live contributions preserve each surviving phrase's anchor/ordinal.
 * A remaining single-note pool inherits its last chord target's phase when
 * Continue is selected. Newly pressed chord phrases retain normal start policy.
 * AsPlayed orders contributions by pad press then ascending pitches in each pad.
 * Gate releases are immediate when a phrase disappears; surviving contributions
 * finish the existing gate. No Latch/release-tail/Track-to-Arp processor yet. */
void thy_arp_clear(thy_arp *arp, thy_core *core);
void thy_arp_reset_runtime(thy_arp *arp, thy_core *core);
#endif
