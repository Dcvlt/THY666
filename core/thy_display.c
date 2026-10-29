/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_display.h"

#define DISPLAY_LEGACY 1u
#define DISPLAY_DIRTY 2u
#define SCENE_NONE 0u
#define SCENE_OFF 1u
#define SCENE_UPLOAD 2u
#define SCENE_ON 3u

/* Freestanding MCU/WebAssembly builds have no C library headers or runtime. */
static void copy_bytes(uint8_t *destination, const uint8_t *source, unsigned count)
{
    for (unsigned byte = 0; byte < count; ++byte) destination[byte] = source[byte];
}

static int bytes_differ(const uint8_t *first, const uint8_t *second, unsigned count)
{
    for (unsigned byte = 0; byte < count; ++byte)
        if (first[byte] != second[byte]) return 1;
    return 0;
}

static unsigned bitmap_get(const uint8_t *bitmap, unsigned offset)
{
    return (bitmap[offset >> 3u] >> (offset & 7u)) & 1u;
}

static void bitmap_set(uint8_t *bitmap, unsigned offset, unsigned value)
{
    uint8_t bit = (uint8_t)(1u << (offset & 7u));
    if (value) bitmap[offset >> 3u] |= bit;
    else bitmap[offset >> 3u] &= (uint8_t)~bit;
}

static void refresh_dirty(thy_display *display, unsigned offset)
{
    unsigned before = bitmap_get(display->dirty, offset);
    unsigned after = !bitmap_get(display->valid, offset) ||
        display->target[offset] != display->shadow[offset];
    bitmap_set(display->dirty, offset, after);
    if (after && !before) ++display->report.dirty_bytes;
    else if (before && !after) --display->report.dirty_bytes;
}

static void note_render(thy_display *display, uint32_t now_ms)
{
    if (display->started) {
        display->report.last_render_gap_ms = now_ms - display->report.last_started_ms;
        if (display->report.last_render_gap_ms > display->report.max_render_gap_ms)
            display->report.max_render_gap_ms = display->report.last_render_gap_ms;
    }
    display->started = 1u;
    display->report.last_started_ms = now_ms;
    display->next_due_ms = now_ms + (display->mode == DISPLAY_LEGACY ?
        THY_DISPLAY_LEGACY_PERIOD_MS : THY_DISPLAY_PERIOD_MS);
}

static void finish_if_clean(thy_display *display, uint32_t now_ms)
{
    if (!display->report.active || display->prepared || display->report.dirty_bytes)
        return;
    if (display->scene_phase != SCENE_NONE) {
        if (display->scene_phase == SCENE_UPLOAD && display->report.initial_complete)
            display->scene_phase = SCENE_ON;
        return; /* A coherent scene completes only after its AF commits. */
    }
    display->report.active = 0u;
    ++display->report.frames;
    display->report.last_completed_ms = now_ms;
    display->report.last_duration_ms = now_ms - display->target_submitted_ms;
    if (display->report.last_duration_ms > display->report.max_duration_ms)
        display->report.max_duration_ms = display->report.last_duration_ms;
}

void thy_display_init(thy_display *display)
{
    if (display != 0) *display = (thy_display){0};
}

int thy_display_due(const thy_display *display, uint32_t now_ms)
{
    return display != 0 &&
        (display->mode == DISPLAY_DIRTY || !display->report.active) &&
        !display->report.faulted && !display->report.scene_active &&
        (!display->started || (int32_t)(now_ms - display->next_due_ms) >= 0);
}

void thy_display_enable_scene_control(thy_display *display, int enabled)
{
    if (display == 0 || display->started || display->prepared ||
        display->mode == DISPLAY_LEGACY) return;
    display->scene_control = (uint8_t)!!enabled;
    display->report.panel_visible = 0u; /* Bringup owns the initial AE. */
}

int thy_display_scene_active(const thy_display *display)
{
    return display != 0 && display->report.scene_active;
}

int thy_display_panel_visible(const thy_display *display)
{
    return display != 0 &&
        (!display->scene_control || display->report.panel_visible);
}

static int submit_target(thy_display *display,
    const uint8_t frame[THY_DISPLAY_FRAME_BYTES], uint32_t now_ms)
{
    unsigned changed = !display->started ||
        bytes_differ(display->target, frame, THY_DISPLAY_FRAME_BYTES);
    unsigned was_active = display->report.active && display->started;
    display->mode = DISPLAY_DIRTY;
    note_render(display, now_ms);
    ++display->report.submitted;
    if (changed) {
        if (was_active) ++display->report.coalesced;
        ++display->report.generation;
        display->target_submitted_ms = now_ms;
        copy_bytes(display->target, frame, THY_DISPLAY_FRAME_BYTES);
    } else ++display->report.unchanged;
    /* The shadow is what successfully reached the panel, not the last target.
     * A newer target may cancel dirty bytes, or make an in-flight old chunk
     * dirty again after it commits. Both cases are resolved against shadow. */
    for (unsigned offset = 0; offset < THY_DISPLAY_FRAME_BYTES; ++offset)
        refresh_dirty(display, offset);
    if (display->report.dirty_bytes || display->prepared)
        display->report.active = 1u;
    finish_if_clean(display, now_ms);
    return (int)changed;
}

int thy_display_submit(thy_display *display,
    const uint8_t frame[THY_DISPLAY_FRAME_BYTES], uint32_t now_ms)
{
    if (display == 0 || frame == 0 || display->report.faulted ||
        display->mode == DISPLAY_LEGACY) return 0;
    if (display->report.scene_active) {
        ++display->report.scene_deferred;
        return 0;
    }
    if (display->scene_control && !display->started) {
        display->scene_phase = SCENE_UPLOAD;
        display->report.scene_active = display->report.active = 1u;
    }
    return submit_target(display, frame, now_ms);
}

int thy_display_submit_scene(thy_display *display,
    const uint8_t frame[THY_DISPLAY_FRAME_BYTES], uint32_t now_ms)
{
    if (display == 0 || frame == 0 || display->report.faulted ||
        display->mode == DISPLAY_LEGACY) return 0;
    if (!display->scene_control) return thy_display_submit(display, frame, now_ms);
    ++display->report.scene_requests;
    if (display->prepared && display->transfer.kind == THY_DISPLAY_TRANSFER_ON) {
        /* AF is immutable and may already be on the wire. Its complete image
         * must remain pinned until commit; only then may AE hide a replacement. */
        unsigned changed = bytes_differ(display->target, frame, THY_DISPLAY_FRAME_BYTES);
        if (display->queued_scene) ++display->report.scene_coalesced;
        display->queued_scene = (uint8_t)changed;
        if (changed) {
            copy_bytes(display->queued_target, frame, THY_DISPLAY_FRAME_BYTES);
            display->queued_submitted_ms = now_ms;
        }
        return (int)changed;
    }
    unsigned changed = !display->started ||
        bytes_differ(display->target, frame, THY_DISPLAY_FRAME_BYTES);
    if (!changed) return 0;
    if (display->report.scene_active) ++display->report.scene_coalesced;
    display->report.scene_active = display->report.active = 1u;
    display->scene_phase = display->report.panel_visible ? SCENE_OFF : SCENE_UPLOAD;
    return submit_target(display, frame, now_ms);
}

/* Compact group indexes let each search inspect its own pixels only. Sparse
 * changes favor the note band 3:1. Dense images use 3:5, proportional to the
 * three note pages and five other pages, so full-screen slides cannot starve
 * headers or footers. Each group's cursor rotates under constant churn. */
static unsigned group_offset(unsigned index, unsigned priority)
{
    /* Five-note layouts also occupy pages 1 and 5. Within the regular
     * share, service those before the title; group sizes and fairness stay
     * unchanged, including under dense full-screen churn. */
    static const uint8_t other_pages[5] = {1u, 5u, 0u, 6u, 7u};
    unsigned page = priority ? 2u + index / 128u : other_pages[index / 128u];
    return page * 128u + index % 128u;
}

static int find_dirty(thy_display *display, unsigned priority,
    unsigned *found, unsigned *group_index)
{
    unsigned length = priority ? 384u : 640u;
    unsigned cursor = priority ? display->priority_cursor : display->other_cursor;
    for (unsigned scanned = 0; scanned < length; ++scanned) {
        unsigned index = cursor + scanned;
        if (index >= length) index -= length;
        unsigned offset = group_offset(index, priority);
        if (bitmap_get(display->dirty, offset)) {
            *found = offset;
            *group_index = index;
            return 1;
        }
    }
    return 0;
}

const thy_display_transfer *thy_display_prepare(thy_display *display,
    uint32_t now_ms)
{
    if (display == 0 || display->mode != DISPLAY_DIRTY ||
        display->report.faulted || !display->report.active) return 0;
    if (display->prepared) return &display->transfer;
    if (display->scene_phase == SCENE_OFF || display->scene_phase == SCENE_ON) {
        thy_display_transfer *transfer = &display->transfer;
        *transfer = (thy_display_transfer){0};
        transfer->kind = display->scene_phase == SCENE_OFF ?
            THY_DISPLAY_TRANSFER_OFF : THY_DISPLAY_TRANSFER_ON;
        transfer->command[0] = transfer->kind == THY_DISPLAY_TRANSFER_OFF ? 0xaeu : 0xafu;
        transfer->command_count = 1u;
        transfer->generation = display->report.generation;
        transfer->submitted_ms = display->target_submitted_ms;
        display->prepared = 1u;
        return transfer;
    }
    if (!display->report.dirty_bytes) {
        finish_if_clean(display, now_ms);
        return 0;
    }
    unsigned dense = display->report.dirty_bytes > 256u;
    unsigned priority = dense ? display->dense_phase < 3u :
        display->priority_streak < 3u;
    unsigned offset, index;
    if (!find_dirty(display, priority, &offset, &index)) {
        priority = !priority;
        if (!find_dirty(display, priority, &offset, &index)) return 0;
    }
    /* Nearby glyph strokes share one bounded wire slice. Clean bytes between
     * them are harmless: they are copied from the latest target as well. An
     * isolated dirty byte still costs one data byte, not an entire block. */
    unsigned count = 1u;
    for (unsigned candidate = 1u; candidate < THY_DISPLAY_CHUNK_BYTES &&
        (offset + candidate) % 128u; ++candidate)
        if (bitmap_get(display->dirty, offset + candidate)) count = candidate + 1u;
    thy_display_transfer *transfer = &display->transfer;
    transfer->generation = display->report.generation;
    transfer->submitted_ms = display->target_submitted_ms;
    transfer->offset = (uint16_t)offset;
    transfer->count = (uint8_t)count;
    transfer->kind = THY_DISPLAY_TRANSFER_DATA;
    transfer->command_count = 3u;
    transfer->command[0] = (uint8_t)(0xb0u | (offset / 128u));
    transfer->command[1] = (uint8_t)(offset & 0x0fu);
    transfer->command[2] = (uint8_t)(0x10u | ((offset % 128u) >> 4u));
    copy_bytes(transfer->data, display->target + offset, count);
    display->report.page = (uint8_t)(offset / 128u);
    display->report.column = (uint8_t)(offset % 128u);
    if (priority) {
        display->priority_cursor = (uint16_t)((index + count) % 384u);
        if (display->priority_streak < 3u) ++display->priority_streak;
    } else {
        display->other_cursor = (uint16_t)((index + count) % 640u);
        display->priority_streak = 0u;
    }
    if (dense) display->dense_phase = (uint8_t)((display->dense_phase + 1u) % 8u);
    display->prepared = 1u;
    return transfer;
}

void thy_display_commit(thy_display *display, int success, uint32_t now_ms)
{
    if (display == 0 || display->mode != DISPLAY_DIRTY ||
        display->report.faulted || !display->prepared) return;
    if (!success) {
        ++display->report.failed_chunks;
        return;
    }
    const thy_display_transfer *transfer = &display->transfer;
    if (transfer->kind != THY_DISPLAY_TRANSFER_DATA) {
        ++display->report.chunks;
        display->report.command_bytes += transfer->command_count;
        display->prepared = 0u;
        if (transfer->kind == THY_DISPLAY_TRANSFER_OFF) {
            display->report.panel_visible = 0u;
            display->scene_phase = SCENE_UPLOAD;
            finish_if_clean(display, now_ms);
        } else {
            display->report.panel_visible = 1u;
            display->report.scene_active = 0u;
            display->scene_phase = SCENE_NONE;
            display->next_due_ms = now_ms; /* Refresh live Arp state on reveal. */
            ++display->report.scene_completed;
            finish_if_clean(display, now_ms);
            if (display->queued_scene) {
                display->queued_scene = 0u;
                display->report.scene_active = display->report.active = 1u;
                display->scene_phase = SCENE_OFF;
                (void)submit_target(display, display->queued_target,
                    display->queued_submitted_ms);
            }
        }
        return;
    }
    for (unsigned byte = 0; byte < transfer->count; ++byte) {
        unsigned offset = transfer->offset + byte;
        display->shadow[offset] = transfer->data[byte];
        if (!bitmap_get(display->valid, offset)) {
            bitmap_set(display->valid, offset, 1u);
            ++display->report.valid_bytes;
        }
        refresh_dirty(display, offset);
    }
    display->report.initial_complete =
        display->report.valid_bytes == THY_DISPLAY_FRAME_BYTES;
    ++display->report.chunks;
    display->report.command_bytes += transfer->command_count;
    display->report.data_bytes += transfer->count;
    display->report.last_generation_age_ms = now_ms - transfer->submitted_ms;
    if (display->report.last_generation_age_ms > display->report.max_generation_age_ms)
        display->report.max_generation_age_ms = display->report.last_generation_age_ms;
    display->prepared = 0u;
    finish_if_clean(display, now_ms);
}

void thy_display_begin(thy_display *display, uint32_t now_ms)
{
    if (display == 0 || display->mode == DISPLAY_DIRTY ||
        !thy_display_due(display, now_ms)) return;
    display->mode = DISPLAY_LEGACY;
    note_render(display, now_ms);
    display->report.active = 1u;
    display->report.page = display->report.column = 0u;
}

unsigned thy_display_chunk(const thy_display *display, uint8_t command[3],
    uint16_t *offset, uint16_t *count)
{
    if (display == 0 || display->mode != DISPLAY_LEGACY ||
        !display->report.active || display->report.faulted ||
        command == 0 || offset == 0 || count == 0) return 0u;
    *offset = (uint16_t)(display->report.page * 128u + display->report.column);
    *count = THY_DISPLAY_CHUNK_BYTES;
    if (display->report.column != 0u) return 0u;
    command[0] = (uint8_t)(0xb0u | display->report.page);
    command[1] = 0x00u;
    command[2] = 0x10u;
    return 3u;
}

void thy_display_complete_chunk(thy_display *display, uint32_t now_ms)
{
    if (display == 0 || display->mode != DISPLAY_LEGACY ||
        !display->report.active || display->report.faulted) return;
    thy_display_report *report = &display->report;
    ++report->chunks;
    report->data_bytes += THY_DISPLAY_CHUNK_BYTES;
    if (report->column == 0u) report->command_bytes += 3u;
    report->column += THY_DISPLAY_CHUNK_BYTES;
    if (report->column != 128u) return;
    report->column = 0u;
    if (++report->page != 8u) return;
    report->active = 0u;
    ++report->frames;
    report->last_completed_ms = now_ms;
    report->last_duration_ms = now_ms - report->last_started_ms;
    if (report->last_duration_ms > report->max_duration_ms)
        report->max_duration_ms = report->last_duration_ms;
}

void thy_display_defer(thy_display *display)
{
    if (display != 0 && display->report.active) ++display->report.busy_deferrals;
}

void thy_display_fault(thy_display *display)
{
    if (display == 0) return;
    display->report.active = 0u;
    display->report.faulted = 1u;
    display->prepared = 0u;
}
