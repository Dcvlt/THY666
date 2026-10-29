/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_HARMONY_WORKSPACE_H
#define THY_HARMONY_WORKSPACE_H
#include "thy_harmony.h"

#define THY_HARMONY_WORKSPACE_CHOICES 9u
enum thy_harmony_page { THY_HARMONY_CHORDS, THY_HARMONY_SCALES, THY_HARMONY_KEY };
enum thy_harmony_pad_role { THY_HARMONY_OTHER, THY_HARMONY_CURRENT,
    THY_HARMONY_SELECTED, THY_HARMONY_SUGGESTED };
enum thy_harmony_reason { THY_HARMONY_COMMON_TONES, THY_HARMONY_PATTERN_NEXT,
    THY_HARMONY_IN_KEY, THY_HARMONY_BORROWED, THY_HARMONY_EXTENSION,
    THY_HARMONY_RESOLVE, THY_HARMONY_SECONDARY, THY_HARMONY_PARALLEL };
enum thy_harmony_workspace_action {
    THY_HARMONY_ACTIVATE, THY_HARMONY_PAGE, THY_HARMONY_CURSOR,
    THY_HARMONY_TARGET_ROOT, THY_HARMONY_TARGET_FAMILY,
    THY_HARMONY_APPLY_KEY, THY_HARMONY_CLEAR_INPUT, THY_HARMONY_CAPTURE_SELECTED,
    THY_HARMONY_CAPTURE_HELD, THY_HARMONY_PIN_INPUT, THY_HARMONY_COMPARE_SCALE
};
typedef struct thy_harmony_choice {
    uint64_t pads; /* Chord-role pad bits; physical side follows cluster swap. */
    uint16_t mask;
    uint8_t root, shared, degree, reason, pad;
} thy_harmony_choice;
typedef struct thy_harmony_workspace {
    thy_harmony_choice choices[THY_HARMONY_WORKSPACE_CHOICES];
    thy_scale_match scales[THY_GUIDANCE_LIMIT];
    thy_modulation comparison;
    uint16_t pad_masks[48];
    uint16_t pending_masks[48];
    uint8_t pending_roots[48];
    uint8_t pad_roots[48], pad_roles[48];
    uint32_t revision, changed_ms, layout_signature;
    uint16_t key_mask, input_mask, target_mask, scale_total, base_mask, right_mask;
    uint16_t ranking_mask; /* Last attack/capture; releases clear live labels, not this gesture. */
    uint16_t right_held[48]; /* Accepted physical-note-role inputs, before generators. */
    uint64_t chord_held;
    uint8_t source_root, source_family, current_root, current_pad;
    uint8_t target_root, target_family, target_catalog, can_apply;
    uint8_t page, cursor, count, scale_count, cache_cursor, cache_ready;
    uint8_t pinned, dirty, discovery_dirty, selected_suggestion;
    /* Persisted preferences; all analysis/capture/selection above is transient. */
    uint8_t pattern, highlights, motion;
    uint8_t view, control_bank, octave_target, right_mode, analysis;
    uint8_t bass_root, low_note, high_note;
    uint8_t preview_scale;
    uint8_t direction, pad_map, hint_contrast;
    uint8_t mapping_preview, view_picker;
    uint8_t outside, stay_in_key, lock_map, palette;
    uint16_t history[4];
    uint8_t history_roots[4], history_count;
    uint8_t job_root, job_active, job_palette, context_root;
    uint32_t melody_since;
    uint8_t melody_accepted;
    int16_t pending_scores[48];
    uint8_t pending_reasons[48];
} thy_harmony_workspace;
struct thy_engine;
void thy_engine_harmony_reset(struct thy_engine *);
void thy_engine_harmony_tick(struct thy_engine *);
void thy_engine_harmony_input(struct thy_engine *,uint8_t logical,
    const int16_t *notes,size_t count);
void thy_engine_harmony_release(struct thy_engine *,uint8_t logical);
int thy_engine_harmony_control(struct thy_engine *,uint8_t bank,uint8_t index,int pressed);
int thy_engine_harmony_melody_plan(void *,uint8_t logical,int16_t *note);
int thy_engine_harmony_chord_plan(void *,uint8_t logical,int16_t notes[7],uint8_t *mask);
int thy_engine_harmony_slot(uint8_t logical);
void thy_engine_harmony_invalidate(struct thy_engine *);
int thy_engine_harmony_voicing_plan(void *,uint8_t logical,const int16_t notes[7],
    uint8_t valid_mask,int16_t out[7]);
int thy_engine_harmony_action(struct thy_engine *,uint8_t action,int32_t value);
int thy_engine_harmony_navigate(struct thy_engine *,uint8_t action);
int thy_engine_harmony_active(const struct thy_engine *);
uint8_t thy_engine_harmony_pad_role(const struct thy_engine *,uint8_t logical);
void thy_engine_harmony_label(uint16_t mask,uint8_t root,char out[32]);
void thy_engine_harmony_melody_label(const struct thy_engine *,char out[32]);
void thy_engine_harmony_key_label(const struct thy_engine *,int target,char out[32]);
void thy_engine_harmony_reason(const struct thy_engine *,unsigned choice,char out[32]);
#endif
