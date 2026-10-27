/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_DISPLAY_H
#define THY_DISPLAY_H
#include <stdint.h>

/* Instrument transport only. Rendering and wire service have separate clocks:
 * a new image can replace the target while older bytes are still in flight.
 * SPI remains at the previously validated clock. */
#define THY_DISPLAY_PERIOD_MS 16u
#define THY_DISPLAY_LEGACY_PERIOD_MS 42u
#define THY_DISPLAY_CHUNK_BYTES 16u
#define THY_DISPLAY_SPI_HZ 328125u
#define THY_DISPLAY_FRAME_BYTES 1024u
#define THY_DISPLAY_BITMAP_BYTES (THY_DISPLAY_FRAME_BYTES / 8u)
#define THY_DISPLAY_TRANSFER_DATA 0u
#define THY_DISPLAY_TRANSFER_OFF 1u
#define THY_DISPLAY_TRANSFER_ON 2u

typedef struct thy_display_transfer {
    uint32_t generation, submitted_ms;
    uint16_t offset;
    uint8_t count, command[3], data[THY_DISPLAY_CHUNK_BYTES];
    uint8_t kind, command_count;
} thy_display_transfer;

typedef struct thy_display_report {
    uint32_t frames, chunks, command_bytes, data_bytes, busy_deferrals;
    uint32_t last_started_ms, last_completed_ms;
    uint32_t last_duration_ms, max_duration_ms;
    uint32_t last_render_gap_ms, max_render_gap_ms;
    uint32_t submitted, coalesced, unchanged, failed_chunks;
    uint32_t generation, last_generation_age_ms, max_generation_age_ms;
    uint32_t last_render_us, max_render_us, last_chunk_us, max_chunk_us;
    uint32_t scene_requests, scene_coalesced, scene_deferred, scene_completed;
    uint16_t dirty_bytes, valid_bytes;
    uint8_t active, page, column, faulted, initial_complete;
    uint8_t panel_visible, scene_active;
} thy_display_report;

typedef struct thy_display {
    thy_display_report report;
    uint32_t next_due_ms;
    uint32_t target_submitted_ms;
    uint32_t queued_submitted_ms;
    uint16_t priority_cursor, other_cursor;
    uint8_t started, mode, prepared, priority_streak, dense_phase;
    uint8_t scene_control, scene_phase, queued_scene;
    uint8_t target[THY_DISPLAY_FRAME_BYTES];
    uint8_t shadow[THY_DISPLAY_FRAME_BYTES];
    uint8_t valid[THY_DISPLAY_BITMAP_BYTES];
    uint8_t dirty[THY_DISPLAY_BITMAP_BYTES];
    uint8_t queued_target[THY_DISPLAY_FRAME_BYTES];
    thy_display_transfer transfer;
} thy_display;

void thy_display_init(thy_display *display);
int thy_display_due(const thy_display *display, uint32_t now_ms);
/* Optional coherent scene replacement. Enable immediately after init, before
 * any submission. Bringup must already have sent AE (panel off); the initial
 * image remains hidden until all 1024 bytes are valid and the target is clean.
 * When disabled (the default), existing dirty/legacy behavior is unchanged. */
void thy_display_enable_scene_control(thy_display *display, int enabled);
int thy_display_scene_active(const thy_display *display);
int thy_display_panel_visible(const thy_display *display);
/* While enabled, a scene pins a complete target behind AE, then reveals it with
 * AF only after all its bytes commit. Ordinary submit/due pauses until reveal,
 * so a continuously animated Play view cannot prolong the black interval.
 * Explicit newer scenes coalesce while hidden. If AF is already prepared,
 * keep one latest replacement, reveal the completed old scene, then send AE
 * before modifying its replacement. No black framebuffer is transmitted. */
int thy_display_submit_scene(thy_display *display,
    const uint8_t frame[THY_DISPLAY_FRAME_BYTES], uint32_t now_ms);
/* Copies the latest image; this is allowed during a prepared transfer. Returns
 * one for a changed target (including the initial image), zero otherwise.
 * An active coherent handoff ignores this request without modifying its target.
 * Call at most once per due render, rather than once per foreground iteration. */
int thy_display_submit(thy_display *display,
    const uint8_t frame[THY_DISPLAY_FRAME_BYTES], uint32_t now_ms);
/* Each prepared chunk owns its immutable payload until successful commit. DATA
 * carries 3 explicit address bytes and <=16 data bytes. OFF/ON carries one AE/AF
 * command and zero data bytes. Callers must use command_count and kind.
 * The pointer remains stable across submissions, deferrals and failed commits.
 * At most 1024 bitmap positions are examined; no bus I/O or allocation occurs. */
const thy_display_transfer *thy_display_prepare(thy_display *display,
    uint32_t now_ms);
void thy_display_commit(thy_display *display, int success, uint32_t now_ms);
/* Caller renders exactly once before begin, then keeps that image frozen
 * until complete. Legacy frozen-frame API, retained for existing callers.
 * Do not mix begin/chunk with submit/prepare on the same initialized object. */
void thy_display_begin(thy_display *display, uint32_t now_ms);
/* Returns command count: three at page start, zero for subsequent chunks.
 * offset/count identify the next <=16 byte slice of the frozen framebuffer. */
unsigned thy_display_chunk(const thy_display *display, uint8_t command[3],
    uint16_t *offset, uint16_t *count);
void thy_display_complete_chunk(thy_display *display, uint32_t now_ms);
void thy_display_defer(thy_display *display);
void thy_display_fault(thy_display *display);
#endif
