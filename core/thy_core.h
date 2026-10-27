#ifndef THY_CORE_H
#define THY_CORE_H

#include <stddef.h>
#include <stdint.h>
#include "thy_midi.h"

#define THY_PAD_COUNT 96u
#define THY_GENERATED_OWNER_COUNT 128u
#define THY_MAX_NOTES 64u
#define THY_OWNER_KEYS (THY_MAX_NOTES * THY_DEST_COUNT)
#define THY_KEY_COUNT (THY_DEST_COUNT * 16u * 128u)
#define THY_DEFERRED_BYTES ((THY_KEY_COUNT + 7u) / 8u)
#define THY_QUEUE_CAPACITY 1024u
#define THY_ROUTE_USB0 1u
#define THY_ROUTE_USB1 2u
#define THY_ROUTE_DIN 4u
#define THY_ROUTE_ALL 7u

enum thy_result {
    THY_OK = 0,
    THY_INVALID_ARGUMENT = 1,
    THY_QUEUE_FULL = 2,
    THY_OWNER_LIMIT = 3,
    THY_HANDLE_EXHAUSTED = 4
};

enum thy_strum_order {
    THY_STRUM_SERIALIZED = 0,
    THY_STRUM_UP = 1,
    THY_STRUM_DOWN = 2,
    THY_STRUM_ALTERNATING = 3
};

/* Shaping acts on the original serialized note list before deduplication:
 * excluded_slots omits original serialized slots (bit 0 is the first). Spread
 * 0/I/II/III lowers its FIRST entry by 0/12/24/36 semitones; inversion
 * raises the first N entries by 12. Octave shift then applies to every entry.
 * bass_octave shifts the lowest enabled pitch after spread/inversion, before
 * upper stacks append copies at +12, +24, +36. All legacy final pitches must
 * fit MIDI. A nonzero octave_layers replaces those upper stacks: bits0..6
 * select complete transformed chord copies at -3..+3 octaves. Out-of-range
 * layer copies are omitted, never clamped; the resulting list is deduplicated.
 *
 * Strum uses the following scheduling policy:
 * serialized order is the default; up/down sort pitches ascending/descending;
 * alternating chooses ascending then descending on successive successful,
 * nonempty shaped presses, globally across pads. Intervals are 0..1000 ms.
 */
typedef struct thy_shape {
    uint64_t excluded_slots;
    uint8_t spread;
    uint8_t inversion;
    int8_t octave_shift;
    uint8_t upper_octaves;
    uint8_t strum_order;
    int8_t bass_octave;
    uint16_t strum_ms;
    uint8_t octave_layers; /* 0 legacy; seven independent -3..+3 octave bits. */
} thy_shape;

typedef struct thy_pad_owner {
    /* One immutable channel*128+pitch tuple per note. Every note shares the
     * captured destination mask; full-key expansion retains its original order. */
    uint16_t notes[THY_MAX_NOTES];
    uint8_t count;
    uint8_t held;
    uint8_t activated;
    uint8_t stride;
    uint8_t velocity;
    uint8_t routing; /* mask bits0..2; note-major bit3; destination count bits4..5. */
    uint16_t interval;
    uint32_t next_due;
} thy_pad_owner;

/* count/activated/stride still count full destination/channel/pitch keys,
 * including up to 192 keys for 64 notes. Inspection never reads future route
 * configuration. NULL/invalid indices return UINT16_MAX, including silent pads.
 */
static inline uint16_t thy_pad_owner_key(const thy_pad_owner *owner, uint8_t index)
{
    if (owner == NULL || index >= owner->count) return UINT16_MAX;
    unsigned destinations = (owner->routing >> 4u) & 3u;
    if (destinations == 0u) return UINT16_MAX;
    unsigned note, ordinal;
    if (owner->routing & 8u) {
        note = index / destinations;
        ordinal = index % destinations;
    } else {
        unsigned notes = owner->count / destinations;
        if (notes == 0u) return UINT16_MAX;
        note = index % notes;
        ordinal = index / notes;
    }
    if (note >= THY_MAX_NOTES) return UINT16_MAX;
    for (unsigned destination = 0u; destination < THY_DEST_COUNT; ++destination) {
        if (!(owner->routing & (1u << destination))) continue;
        if (ordinal == 0u)
            return (uint16_t)(destination * 16u * 128u + owner->notes[note]);
        --ordinal;
    }
    return UINT16_MAX;
}

/* An opaque voice identity, never a pool index. Zero is not a valid handle. */
typedef uint32_t thy_generated_handle;

/* One generated owner represents one pitch on up to three destinations.
 * source identifies an Arp/track/context; run distinguishes its playback runs.
 * Both are supplied by the caller and must be nonzero. Keys are immutable.
 */
typedef struct thy_generated_owner {
    thy_generated_handle handle;
    uint32_t source;
    uint32_t run;
    uint16_t keys[THY_DEST_COUNT];
    uint8_t count, velocity;
} thy_generated_owner;

/* Optional generated-attack cleanup seam. It may append owned controller
 * cleanup and update its own sent caches, but must not mutate note ownership.
 * Successful preparation must be idempotent until contributors/config change:
 * chord emitters preflight it once, then each individual acquire checks again.
 * Return a thy_result value. The callback must not recursively acquire notes.
 */
typedef int (*thy_core_generated_guard_fn)(void *context,uint8_t channel,
    uint8_t destinations);

/* Caller-owned deterministic single-thread state. No allocation, operating
 * system, libc calls, clock, hardware I/O or implicit global configuration.
 * Serialize calls in an eventual interrupt/main-loop integration.
 *
 * Key = ((destination * 16 + channel) * 128 + pitch). refs <= 224:
 * at most 96 physical owners plus 128 single-pitch generated owners.
 * A voice sounds while owned or awaiting release under sustain. A held pad
 * snapshots its keys, so later chord/channel/route changes cannot change its
 * release. The first owner starts a voice; the last owner releases it.
 *
 * Capacity invariant: queued + sounding + enabled sustain channels +
 * 2 * pending strum keys <= 1024. Future keys reserve a worst-case NoteOn and
 * NoteOff each; pending keys acquire no refs until their scheduled attack.
 * The other counts reserve future NoteOff and CC64=0 events. A press or
 * control operation is all-or-nothing. Releases, sustain transitions through
 * thy_core_sustain, and panic cannot drop an event because the queue is full.
 */
typedef struct thy_core {
    thy_pad_owner owners[THY_PAD_COUNT];
    thy_generated_owner generated[THY_GENERATED_OWNER_COUNT];
    uint32_t generated_last_handle;
    uint16_t generated_count;
    uint8_t refs[THY_KEY_COUNT];
    uint8_t deferred_bits[THY_DEFERRED_BYTES];
    uint8_t sustain[THY_DEST_COUNT * 16u];
    thy_midi_event queue[THY_QUEUE_CAPACITY];
    uint16_t queue_head;
    uint16_t queue_count;
    uint16_t sounding_count;
    uint8_t sustain_count;
    uint16_t pending_keys;
    uint32_t now_ms;
    uint8_t clock_initialized;
    uint8_t alternating_down;
    thy_core_generated_guard_fn generated_guard;
    void *generated_guard_context;
} thy_core;

/* One boolean per destination/channel/pitch: the last owner has released but
 * sustain still holds its NoteOff. Only the core mutates this packed ledger.
 * Invalid inspection keys and NULL return zero, like thy_core_refcount.
 */
static inline uint8_t thy_core_deferred(const thy_core *core, uint16_t key)
{
    return core != NULL && key < THY_KEY_COUNT ?
        (uint8_t)((core->deferred_bits[key / 8u] >> (key & 7u)) & 1u) : 0u;
}

void thy_core_init(thy_core *core);
/* Notes are absolute MIDI integers; signed transpose is applied in int32_t.
 * Every final pitch must be 0..127, including note zero. Duplicate pitches
 * collapse within this pad. Empty lists are valid silent pad owners. Velocity
 * is 1..127, channel 0..15, destinations a nonempty subset of route mask 7.
 * Re-pressing an already held pad is an idempotent no-op even if config changes.
 */
int thy_core_press(thy_core *core, uint8_t pad, const int16_t *notes,
                   size_t count, int16_t transpose, uint8_t channel,
                   uint8_t destinations, uint8_t velocity);
/* Already resolved absolute pitches, each with its own zero-based channel.
 * Input order is preserved; duplicate (channel, pitch) pairs are rejected.
 * Equal pitches on different channels are distinct voices. count is 0..64;
 * empty arrays may be NULL and create a silent held owner. Destinations and
 * velocity have the same rules as press. strum_ms is 0..1000: zero attacks
 * all voices now, otherwise the first voice attacks now and subsequent voices
 * at that interval. Every voice's destinations form one note-major group.
 * The complete press, including pending attacks, is reserved atomically.
 * Caller arrays may change afterward; release uses captured keys. An already
 * held pad is an idempotent no-op, as with press.
 */
int thy_core_press_assigned(thy_core *core, uint8_t pad, const int16_t *notes,
                            const uint8_t *channels, size_t count,
                            uint8_t destinations, uint8_t velocity,
                            uint16_t strum_ms);
/* Preview predicts a future shaped press, including the NEXT alternating
 * direction, without changing state. Outputs remain untouched on error. The
 * held-pad inspector should read its immutable owner keys instead. count may
 * be zero; inversion may not exceed original count; output is deduplicated and
 * <= 64. Spread/inversion never move onto another slot when one is excluded.
 */
int thy_core_preview(const thy_core *core, const int16_t *notes, size_t count,
                     int16_t transpose, const thy_shape *shape,
                     int16_t out[THY_MAX_NOTES], size_t *out_count);
/* First unique pitch attacks immediately; the remainder attack one pitch per
 * interval across all requested destinations. Shape/channel/routes/velocity
 * are captured once. Release cancels every pending attack before releasing
 * activated keys, so a quick tap cannot create a later ghost note.
 */
int thy_core_press_shaped(thy_core *core, uint8_t pad, const int16_t *notes,
                          size_t count, int16_t transpose, uint8_t channel,
                          uint8_t destinations, uint8_t velocity,
                          const thy_shape *shape);
/* Caller supplies a monotonic uint32 millisecond clock modulo 2^32. The first
 * advance establishes the epoch unless a strum already began at initial time
 * zero. A backward jump or forward gap >= 2^31 is rejected without mutation.
 * Overdue attacks replay in deadline order (pad ID breaks ties), bounded by
 * the at-most-512 reserved pending keys. No free-running timer is assumed.
 */
int thy_core_advance(thy_core *core, uint32_t now_ms);
/* Proposed replacement melody layouts for the full 12-column/4-row grid:
 * 1 chromatic: anchor + column - 12*row;
 * 2 scale thirds / 3 scale fourths: advance 2/3 scale degrees per column, wrap
 * by octaves, then subtract 12*row. Degree lists must start at zero, strictly
 * increase within 0..11, and contain 1..12 entries. Chromatic ignores degrees.
 * Layout 0 remains the caller's original archived notes, not a generated mode.
 * Anchor and result must fit MIDI. Output stays untouched on error. This is
 * explicit voicing policy for the shared musical engine.
 */
int thy_core_melody_pitch(int layout, int16_t anchor, const int16_t *degrees,
                          size_t count, uint8_t column, uint8_t row,
                          int16_t *out);
int thy_core_release(thy_core *core, uint8_t pad);
int thy_core_pressure(thy_core *core, uint8_t pad, uint8_t pressure);
void thy_core_set_generated_guard(thy_core *,thy_core_generated_guard_fn,void *context);
/* Validate a generated route and publish mandatory pre-attack cleanup. A NULL
 * guard preserves legacy behavior. A successful cleanup is independent of a
 * later note admission: it is not rolled back if queue admission fails.
 * Batch emitters call this BEFORE their aggregate queue-capacity check.
 */
int thy_core_prepare_generated(thy_core *,uint8_t channel,uint8_t destinations);
/* Acquires one generated pitch using the SAME voice/reference ledger as pads.
 * A chord is several acquisitions; the caller must reserve/rollback a complete
 * chord if it requires group atomicity. Note ownership is atomic, including
 * pool/queue exhaustion; out_handle remains untouched on error. An optional
 * guard can independently publish required controller cleanup before a later
 * queue rejection. Such cleanup remains committed even when no note starts.
 *
 * Handles strictly increase within an initialized core lifetime, including
 * across panic and pool reuse. UINT32_MAX is the final usable handle; future
 * acquisitions fail with THY_HANDLE_EXHAUSTED instead of wrapping. A full pool
 * returns THY_OWNER_LIMIT. Source/run IDs are caller-owned: use a new nonzero
 * run for each source restart and discard old acquisition callbacks. Exact
 * source/run cleanup safely ignores owners from other runs, even if slots are
 * reused. Reinitialization starts a new lifetime: discard all old callbacks.
 */
int thy_core_generated_acquire(thy_core *core, uint32_t source, uint32_t run,
                               uint8_t pitch, uint8_t channel,
                               uint8_t destinations, uint8_t velocity,
                               thy_generated_handle *out_handle);
/* Valid stale handles and already-cleaned source/run pairs are idempotent.
 * All release forms retain each voice's original route and honor sustain:
 * the final owner can defer its NoteOff until pedal-up, without clearing the
 * pedal or disturbing pad/other-source owners. Queue room is reserved at
 * acquisition, so these operations cannot fail because the queue is full.
 */
int thy_core_generated_release(thy_core *core, thy_generated_handle handle);
int thy_core_generated_release_source(thy_core *core, uint32_t source,
                                      uint32_t run);
int thy_core_generated_release_all(thy_core *core);
uint16_t thy_core_generated_count(const thy_core *core);
uint16_t thy_core_generated_source_count(const thy_core *core, uint32_t source,
                                         uint32_t run);
/* Inspection is read-only; pointers expire on the next core mutation. */
const thy_generated_owner *thy_core_generated_find(const thy_core *core,
                                                   thy_generated_handle handle);
const thy_generated_owner *thy_core_generated_at(const thy_core *core,
                                                 uint16_t slot);
/* Canonical sustain level 127/0, emits only when the state changes. Disabling
 * an enabled destination/channel has a reserved CC64=0 and all deferred offs.
 */
int thy_core_sustain(thy_core *core, uint8_t destinations, uint8_t channel,
                     int enabled);
/* Generic CC. CC64 also updates internal sustain. Repeated CC64 levels emit
 * an event, so use thy_core_sustain for guaranteed/idempotent pedal transitions.
 * CC123 is a real control message, not a substitute for ownership-aware panic.
 */
int thy_core_cc(thy_core *core, uint8_t destinations, uint8_t channel,
                uint8_t controller, uint8_t value);
/* Atomically appends an ordered batch of channel CC, pressure or bend events
 * (B0/D0/E0). Destinations are indices, data bytes are 0..127, and D0's unused
 * data2 must be zero. CC64 is rejected: use the sustain API so ownership and
 * reserved cleanup remain correct. Empty batches may use NULL. Invalid input
 * or insufficient unreserved queue capacity leaves the entire core untouched.
 * This reserves no future controller reset: callers must arrange cleanup.
 */
int thy_core_emit_batch(thy_core *core, const thy_midi_event *events, size_t count);
int thy_core_channel_pressure(thy_core *core, uint8_t destinations,
                               uint8_t channel, uint8_t pressure);
/* Signed input is saturated to the full MIDI 14-bit range, 0..16383. */
int thy_core_bend(thy_core *core, uint8_t destinations, uint8_t channel,
                  int32_t value);
/* Appends sustain-off then every owned/deferred NoteOff; prior queued events
 * retain order. Clears ownership and sustain. Safe to repeat with a full queue.
 */
void thy_core_panic(thy_core *core);
/* Returns 1 and removes one event, or 0 for an empty queue/invalid arguments. */
int thy_core_poll(thy_core *core, thy_midi_event *event);

uint16_t thy_core_key(uint8_t destination, uint8_t channel, uint8_t pitch);
uint8_t thy_core_refcount(const thy_core *core, uint16_t key);
int thy_core_pad_held(const thy_core *core, uint8_t pad);
uint16_t thy_core_owner_key(const thy_core *core, uint8_t pad, uint8_t index);
uint16_t thy_core_queued(const thy_core *core);
uint16_t thy_core_sounding(const thy_core *core);
uint16_t thy_core_reserved(const thy_core *core);
/* Pending destination/channel/pitch keys, distinct from activated voices. */
uint16_t thy_core_pending(const thy_core *core);

#endif
