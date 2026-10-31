#include "thy_core.h"

_Static_assert(THY_PAD_COUNT + THY_GENERATED_OWNER_COUNT <= UINT8_MAX,
               "voice references must fit uint8_t");
_Static_assert(THY_KEY_COUNT <= UINT16_MAX,
               "packed sustain keys must fit uint16_t");
_Static_assert(THY_OWNER_KEYS <= UINT8_MAX,
               "expanded owner counts must fit uint8_t");
_Static_assert(sizeof(thy_pad_owner) == 140u,
               "captured pad routes must retain the bounded 140-byte layout");

static uint8_t owner_routing(uint8_t destinations, uint8_t note_major)
{
    uint8_t count = (uint8_t)((destinations & 1u) +
        ((destinations >> 1u) & 1u) + ((destinations >> 2u) & 1u));
    return (uint8_t)(destinations | (note_major ? 8u : 0u) | (count << 4u));
}

/* Mutation keys originate from validated routes or bounded ledger scans. */
static void deferred_set(thy_core *core, uint16_t key)
{
    core->deferred_bits[key / 8u] |= (uint8_t)(1u << (key & 7u));
}

static void deferred_clear(thy_core *core, uint16_t key)
{
    core->deferred_bits[key / 8u] &= (uint8_t)~(uint8_t)(1u << (key & 7u));
}

static int route_valid(uint8_t destinations, uint8_t channel)
{
    return channel < 16u && destinations != 0u &&
           (destinations & (uint8_t)~THY_ROUTE_ALL) == 0u;
}

static uint16_t obligation_count(const thy_core *core)
{
    return (uint16_t)(core->sounding_count + core->sustain_count +
                      2u * core->pending_keys);
}

static int room_for(const thy_core *core, uint16_t additional)
{
    return (uint32_t)core->queue_count + obligation_count(core) +
           additional <= THY_QUEUE_CAPACITY;
}

/* All callers have already reserved room; a queue write cannot fail. */
static void append(thy_core *core, uint8_t destination, uint8_t status,
                    uint8_t data1, uint8_t data2)
{
    uint16_t tail = (uint16_t)(core->queue_head + core->queue_count);
    if (tail >= THY_QUEUE_CAPACITY) {
        tail = (uint16_t)(tail - THY_QUEUE_CAPACITY);
    }
    core->queue[tail].destination = destination;
    core->queue[tail].status = status;
    core->queue[tail].data1 = data1;
    core->queue[tail].data2 = data2;
    ++core->queue_count;
}

static void append_note(thy_core *core, uint16_t key, uint8_t kind,
                         uint8_t value)
{
    uint16_t route = (uint16_t)(key / 128u);
    append(core, (uint8_t)(route / 16u),
           (uint8_t)(kind | (route % 16u)), (uint8_t)(key % 128u), value);
}

static void activate_key(thy_core *core, uint16_t key, uint8_t velocity)
{
    if (core->refs[key] == 0u && !thy_core_deferred(core, key)) {
        append_note(core, key, 0x90u, velocity);
        ++core->sounding_count;
    }
    deferred_clear(core, key);
    ++core->refs[key];
}

static void release_key(thy_core *core, uint16_t key)
{
    --core->refs[key];
    if (core->refs[key] == 0u) {
        if (core->sustain[key / 128u] != 0u) {
            deferred_set(core, key);
        } else {
            append_note(core, key, 0x80u, 0u);
            --core->sounding_count;
        }
    }
}

void thy_core_init(thy_core *core)
{
    size_t i;
    uint8_t *bytes;
    if (core == NULL) {
        return;
    }
    bytes = (uint8_t *)core;
    for (i = 0u; i < sizeof(*core); ++i) {
        bytes[i] = 0u;
    }
}

uint16_t thy_core_key(uint8_t destination, uint8_t channel, uint8_t pitch)
{
    if (destination >= THY_DEST_COUNT || channel >= 16u || pitch >= 128u) {
        return UINT16_MAX;
    }
    return (uint16_t)(((uint16_t)destination * 16u + channel) * 128u + pitch);
}

int thy_core_press(thy_core *core, uint8_t pad, const int16_t *notes,
                   size_t count, int16_t transpose, uint8_t channel,
                   uint8_t destinations, uint8_t velocity)
{
    uint8_t pitches[THY_MAX_NOTES];
    uint16_t keys[THY_OWNER_KEYS];
    size_t pitch_count = 0u;
    uint16_t key_count = 0u;
    uint16_t new_voices = 0u;
    size_t i;
    uint8_t destination;
    thy_pad_owner *owner;
    if (core == NULL || pad >= THY_PAD_COUNT) {
        return THY_INVALID_ARGUMENT;
    }
    owner = &core->owners[pad];
    if (owner->held != 0u) {
        return THY_OK;
    }
    if (count > THY_MAX_NOTES || (count != 0u && notes == NULL) ||
        !route_valid(destinations, channel) || velocity == 0u || velocity > 127u) {
        return THY_INVALID_ARGUMENT;
    }
    /* Validate every supplied pitch before making any state change. */
    for (i = 0u; i < count; ++i) {
        int32_t pitch = (int32_t)notes[i] + transpose;
        size_t j;
        if (pitch < 0 || pitch > 127) {
            return THY_INVALID_ARGUMENT;
        }
        for (j = 0u; j < pitch_count; ++j) {
            if (pitches[j] == (uint8_t)pitch) {
                break;
            }
        }
        if (j == pitch_count) {
            pitches[pitch_count++] = (uint8_t)pitch;
        }
    }
    for (destination = 0u; destination < THY_DEST_COUNT; ++destination) {
        if ((destinations & (uint8_t)(1u << destination)) != 0u) {
            for (i = 0u; i < pitch_count; ++i) {
                uint16_t key = thy_core_key(destination, channel, pitches[i]);
                keys[key_count++] = key;
                if (core->refs[key] == 0u && !thy_core_deferred(core, key)) {
                    ++new_voices;
                }
            }
        }
    }
    if (!room_for(core, (uint16_t)(2u * new_voices))) {
        return THY_QUEUE_FULL;
    }
    owner->held = 1u;
    owner->count = (uint8_t)key_count;
    owner->activated = (uint8_t)key_count;
    owner->stride = 0u;
    owner->interval = 0u;
    owner->velocity = velocity;
    owner->routing = owner_routing(destinations, 0u);
    for (i = 0u; i < pitch_count; ++i)
        owner->notes[i] = (uint16_t)(keys[i] & 2047u);
    for (i = 0u; i < key_count; ++i) {
        uint16_t key = keys[i];
        activate_key(core, key, velocity);
    }
    return THY_OK;
}

int thy_core_press_assigned(thy_core *core, uint8_t pad, const int16_t *notes,
                            const uint8_t *channels, size_t count,
                            uint8_t destinations, uint8_t velocity,
                            uint16_t strum_ms)
{
    uint16_t keys[THY_OWNER_KEYS];
    uint16_t key_count = 0u, new_voices = 0u;
    uint8_t stride = 0u;
    size_t i;
    uint8_t destination;
    thy_pad_owner *owner;
    if (core == NULL || pad >= THY_PAD_COUNT) return THY_INVALID_ARGUMENT;
    owner = &core->owners[pad];
    if (owner->held != 0u) return THY_OK;
    if (count > THY_MAX_NOTES || (count && (notes == NULL || channels == NULL)) ||
        !route_valid(destinations, 0u) || !velocity || velocity > 127u ||
        strum_ms > 1000u) return THY_INVALID_ARGUMENT;
    for (i = 0u; i < count; ++i) {
        size_t j;
        if (notes[i] < 0 || notes[i] > 127 || channels[i] >= 16u)
            return THY_INVALID_ARGUMENT;
        for (j = 0u; j < i; ++j)
            if (notes[j] == notes[i] && channels[j] == channels[i])
                return THY_INVALID_ARGUMENT;
        for (destination = 0u; destination < THY_DEST_COUNT; ++destination) {
            if (!(destinations & (uint8_t)(1u << destination))) continue;
            uint16_t key = thy_core_key(destination, channels[i], (uint8_t)notes[i]);
            keys[key_count++] = key;
            if (i == 0u) ++stride;
            if ((!strum_ms || i == 0u) && !core->refs[key] && !thy_core_deferred(core, key))
                ++new_voices;
        }
    }
    uint16_t activated = strum_ms ? stride : key_count;
    uint16_t pending = (uint16_t)(key_count - activated);
    if (!room_for(core, (uint16_t)(2u * (new_voices + pending))))
        return THY_QUEUE_FULL;
    owner->held = 1u;
    owner->count = (uint8_t)key_count;
    owner->activated = (uint8_t)activated;
    owner->stride = pending ? stride : 0u;
    owner->interval = pending ? strum_ms : 0u;
    owner->velocity = velocity;
    owner->routing = owner_routing(destinations, 1u);
    if (pending) {
        owner->next_due = core->now_ms + strum_ms;
        core->pending_keys = (uint16_t)(core->pending_keys + pending);
        core->clock_initialized = 1u;
    }
    for (i = 0u; i < count; ++i)
        owner->notes[i] = (uint16_t)(keys[i * stride] & 2047u);
    for (i = 0u; i < activated; ++i) activate_key(core, keys[i], velocity);
    return THY_OK;
}

int thy_core_preview(const thy_core *core, const int16_t *notes, size_t count,
                     int16_t transpose, const thy_shape *shape,
                     int16_t out[THY_MAX_NOTES], size_t *out_count)
{
    int16_t result[THY_MAX_NOTES];
    size_t result_count = 0u;
    size_t enabled_count = 0u;
    int32_t bass_pitch = INT32_MAX;
    size_t i;
    uint8_t layer;
    uint8_t order;
    if (core == NULL || shape == NULL || out == NULL || out_count == NULL ||
        count > THY_MAX_NOTES || (count != 0u && notes == NULL) ||
        shape->spread > 3u || shape->inversion > count ||
        shape->octave_shift < -10 || shape->octave_shift > 10 ||
        shape->bass_octave < -3 || shape->bass_octave > 3 ||
        shape->upper_octaves > 3u || shape->octave_layers > 0x7fu ||
        shape->strum_order > THY_STRUM_ALTERNATING ||
        shape->strum_ms > 1000u) {
        return THY_INVALID_ARGUMENT;
    }
    for (i = 0u; i < count; ++i) {
        if ((shape->excluded_slots & (UINT64_C(1) << i)) == 0u) {
            ++enabled_count;
            int32_t pitch = (int32_t)notes[i] + transpose +
                (int32_t)shape->octave_shift * 12;
            if (i == 0u) pitch -= (int32_t)shape->spread * 12;
            if (i < shape->inversion) pitch += 12;
            if (pitch < bass_pitch) bass_pitch = pitch;
        }
    }
    if (shape->octave_layers == 0u &&
        enabled_count * (1u + shape->upper_octaves) > THY_MAX_NOTES) {
        return THY_INVALID_ARGUMENT;
    }
    uint8_t layer_count = shape->octave_layers != 0u ? 7u :
                          (uint8_t)(shape->upper_octaves + 1u);
    for (layer = 0u; layer < layer_count; ++layer) {
        if (shape->octave_layers != 0u &&
            !(shape->octave_layers & (1u << layer))) continue;
        int octave = shape->octave_layers != 0u ? (int)layer - 3 : (int)layer;
        for (i = 0u; i < count; ++i) {
            int32_t pitch = (int32_t)notes[i] + transpose +
                            (int32_t)shape->octave_shift * 12;
            size_t j;
            if ((shape->excluded_slots & (UINT64_C(1) << i)) != 0u) {
                continue;
            }
            if (i == 0u) {
                pitch -= (int32_t)shape->spread * 12;
            }
            if (i < shape->inversion) {
                pitch += 12;
            }
            if (pitch == bass_pitch) pitch += (int32_t)shape->bass_octave * 12;
            pitch += octave * 12;
            if (pitch < 0 || pitch > 127) {
                if (shape->octave_layers != 0u) continue;
                return THY_INVALID_ARGUMENT;
            }
            for (j = 0u; j < result_count; ++j) {
                if (result[j] == (int16_t)pitch) {
                    break;
                }
            }
            if (j == result_count) {
                if (result_count == THY_MAX_NOTES) return THY_INVALID_ARGUMENT;
                result[result_count++] = (int16_t)pitch;
            }
        }
    }
    order = shape->strum_order;
    if (order == THY_STRUM_ALTERNATING) {
        order = core->alternating_down != 0u ? THY_STRUM_DOWN : THY_STRUM_UP;
    }
    if (order == THY_STRUM_UP || order == THY_STRUM_DOWN) {
        /* Stable insertion sort, bounded by 64 notes. */
        for (i = 1u; i < result_count; ++i) {
            int16_t value = result[i];
            size_t j = i;
            while (j != 0u && (order == THY_STRUM_UP ? result[j - 1u] > value :
                                                        result[j - 1u] < value)) {
                result[j] = result[j - 1u];
                --j;
            }
            result[j] = value;
        }
    }
    for (i = 0u; i < result_count; ++i) {
        out[i] = result[i];
    }
    *out_count = result_count;
    return THY_OK;
}

int thy_core_press_shaped(thy_core *core, uint8_t pad, const int16_t *notes,
                          size_t count, int16_t transpose, uint8_t channel,
                          uint8_t destinations, uint8_t velocity,
                          const thy_shape *shape)
{
    int16_t pitches[THY_MAX_NOTES];
    uint16_t keys[THY_OWNER_KEYS];
    size_t pitch_count;
    size_t i;
    uint16_t key_count = 0u;
    uint16_t first_new = 0u;
    uint8_t stride = 0u;
    uint8_t destination;
    int result;
    thy_pad_owner *owner;
    if (core == NULL || pad >= THY_PAD_COUNT) {
        return THY_INVALID_ARGUMENT;
    }
    if (core->owners[pad].held != 0u) {
        return THY_OK;
    }
    if (!route_valid(destinations, channel) || velocity == 0u || velocity > 127u) {
        return THY_INVALID_ARGUMENT;
    }
    result = thy_core_preview(core, notes, count, transpose, shape, pitches, &pitch_count);
    if (result != THY_OK) {
        return result;
    }
    if (shape->strum_ms == 0u || pitch_count <= 1u) {
        result = thy_core_press(core, pad, pitches, pitch_count, 0, channel,
                                destinations, velocity);
        if (result == THY_OK && pitch_count != 0u &&
            shape->strum_order == THY_STRUM_ALTERNATING) {
            core->alternating_down ^= 1u;
        }
        return result;
    }
    /* Note-major layout keeps all destinations of each attack in a contiguous
     * activated prefix; no per-key activation flags are necessary. */
    for (i = 0u; i < pitch_count; ++i) {
        for (destination = 0u; destination < THY_DEST_COUNT; ++destination) {
            if ((destinations & (uint8_t)(1u << destination)) != 0u) {
                uint16_t key = thy_core_key(destination, channel, (uint8_t)pitches[i]);
                keys[key_count++] = key;
                if (i == 0u) {
                    ++stride;
                    if (core->refs[key] == 0u && !thy_core_deferred(core, key)) {
                        ++first_new;
                    }
                }
            }
        }
    }
    if (!room_for(core, (uint16_t)(2u * (first_new + key_count - stride)))) {
        return THY_QUEUE_FULL;
    }
    owner = &core->owners[pad];
    owner->held = 1u;
    owner->count = (uint8_t)key_count;
    owner->activated = stride;
    owner->stride = stride;
    owner->velocity = velocity;
    owner->interval = shape->strum_ms;
    owner->next_due = core->now_ms + shape->strum_ms;
    owner->routing = owner_routing(destinations, 1u);
    for (i = 0u; i < pitch_count; ++i)
        owner->notes[i] = (uint16_t)(keys[i * stride] & 2047u);
    core->pending_keys = (uint16_t)(core->pending_keys + key_count - stride);
    core->clock_initialized = 1u;
    for (i = 0u; i < stride; ++i) {
        activate_key(core, keys[i], velocity);
    }
    if (shape->strum_order == THY_STRUM_ALTERNATING) {
        core->alternating_down ^= 1u;
    }
    return THY_OK;
}

int thy_core_advance(thy_core *core, uint32_t now_ms)
{
    if (core == NULL) {
        return THY_INVALID_ARGUMENT;
    }
    if (core->clock_initialized != 0u &&
        now_ms - core->now_ms >= UINT32_C(0x80000000)) {
        return THY_INVALID_ARGUMENT;
    }
    core->clock_initialized = 1u;
    core->now_ms = now_ms;
    while (core->pending_keys != 0u) {
        uint8_t selected = THY_PAD_COUNT;
        uint32_t oldest_age = 0u;
        uint8_t pad;
        thy_pad_owner *owner;
        uint8_t i;
        for (pad = 0u; pad < THY_PAD_COUNT; ++pad) {
            const thy_pad_owner *candidate = &core->owners[pad];
            uint32_t age;
            if (candidate->held == 0u || candidate->activated == candidate->count) {
                continue;
            }
            age = now_ms - candidate->next_due;
            if (age < UINT32_C(0x80000000) &&
                (selected == THY_PAD_COUNT || age > oldest_age)) {
                selected = pad;
                oldest_age = age;
            }
        }
        if (selected == THY_PAD_COUNT) {
            break;
        }
        owner = &core->owners[selected];
        for (i = 0u; i < owner->stride; ++i) {
            uint16_t key = thy_pad_owner_key(owner, owner->activated++);
            --core->pending_keys;
            activate_key(core, key, owner->velocity);
        }
        owner->next_due += owner->interval;
    }
    return THY_OK;
}

int thy_core_melody_pitch(int layout, int16_t anchor, const int16_t *degrees,
                          size_t count, uint8_t column, uint8_t row,
                          int16_t *out)
{
    int32_t pitch;
    if (out == NULL || layout < 1 || layout > 3 || anchor < 0 || anchor > 127 ||
        column >= 12u || row >= 4u) {
        return THY_INVALID_ARGUMENT;
    }
    if (layout == 1) {
        pitch = (int32_t)anchor + column - 12 * (int32_t)row;
    } else {
        size_t i;
        size_t index;
        if (degrees == NULL || count == 0u || count > 12u || degrees[0] != 0) {
            return THY_INVALID_ARGUMENT;
        }
        for (i = 0u; i < count; ++i) {
            if (degrees[i] < 0 || degrees[i] > 11 ||
                (i != 0u && degrees[i] <= degrees[i - 1u])) {
                return THY_INVALID_ARGUMENT;
            }
        }
        index = (size_t)column * (layout == 2 ? 2u : 3u);
        pitch = (int32_t)anchor + degrees[index % count] +
                12 * (int32_t)(index / count) - 12 * (int32_t)row;
    }
    if (pitch < 0 || pitch > 127) {
        return THY_INVALID_ARGUMENT;
    }
    *out = (int16_t)pitch;
    return THY_OK;
}

int thy_core_release(thy_core *core, uint8_t pad)
{
    size_t i;
    thy_pad_owner *owner;
    if (core == NULL || pad >= THY_PAD_COUNT) {
        return THY_INVALID_ARGUMENT;
    }
    owner = &core->owners[pad];
    if (owner->held == 0u) {
        return THY_OK;
    }
    core->pending_keys = (uint16_t)(core->pending_keys - owner->count + owner->activated);
    for (i = 0u; i < owner->activated; ++i) {
        release_key(core, thy_pad_owner_key(owner, (uint8_t)i));
    }
    owner->held = 0u;
    owner->count = 0u;
    owner->activated = 0u;
    owner->interval = 0u;
    return THY_OK;
}

void thy_core_set_generated_guard(thy_core *core,thy_core_generated_guard_fn guard,
    void *context)
{
    if(!core)return;
    core->generated_guard=guard;
    core->generated_guard_context=guard?context:NULL;
}

int thy_core_prepare_generated(thy_core *core,uint8_t channel,uint8_t destinations)
{
    if(!core || !route_valid(destinations,channel))return THY_INVALID_ARGUMENT;
    if(!core->generated_guard)return THY_OK;
    int result=core->generated_guard(core->generated_guard_context,channel,destinations);
    return result>=THY_OK && result<=THY_HANDLE_EXHAUSTED?result:THY_INVALID_ARGUMENT;
}

int thy_core_generated_acquire(thy_core *core, uint32_t source, uint32_t run,
                               uint8_t pitch, uint8_t channel,
                               uint8_t destinations, uint8_t velocity,
                               thy_generated_handle *out_handle)
{
    uint16_t slot;
    uint16_t keys[THY_DEST_COUNT];
    uint8_t count = 0u;
    uint8_t new_voices = 0u;
    uint8_t destination;
    thy_generated_owner *owner;
    if (core == NULL || out_handle == NULL || source == 0u || run == 0u ||
        pitch > 127u || !route_valid(destinations, channel) ||
        velocity == 0u || velocity > 127u) {
        return THY_INVALID_ARGUMENT;
    }
    for (slot = 0u; slot < THY_GENERATED_OWNER_COUNT; ++slot) {
        if (core->generated[slot].handle == 0u) break;
    }
    if (slot == THY_GENERATED_OWNER_COUNT) return THY_OWNER_LIMIT;
    if (core->generated_last_handle == UINT32_MAX) return THY_HANDLE_EXHAUSTED;
    for (destination = 0u; destination < THY_DEST_COUNT; ++destination) {
        if ((destinations & (uint8_t)(1u << destination)) != 0u) {
            uint16_t key = thy_core_key(destination, channel, pitch);
            keys[count++] = key;
            if (core->refs[key] == 0u && !thy_core_deferred(core, key)) ++new_voices;
        }
    }
    int guard=thy_core_prepare_generated(core,channel,destinations);
    if(guard!=THY_OK)return guard;
    if (!room_for(core, (uint16_t)(2u * new_voices))) return THY_QUEUE_FULL;
    owner = &core->generated[slot];
    owner->handle = ++core->generated_last_handle;
    owner->source = source;
    owner->run = run;
    owner->count = count;
    owner->velocity = velocity;
    for (destination = 0u; destination < count; ++destination) {
        owner->keys[destination] = keys[destination];
        activate_key(core, keys[destination], velocity);
    }
    ++core->generated_count;
    *out_handle = owner->handle;
    return THY_OK;
}

static void release_generated_owner(thy_core *core, thy_generated_owner *owner)
{
    uint8_t i;
    for (i = 0u; i < owner->count; ++i) release_key(core, owner->keys[i]);
    owner->handle = 0u;
    owner->source = 0u;
    owner->run = 0u;
    owner->count = 0u;
    --core->generated_count;
}

int thy_core_generated_release(thy_core *core, thy_generated_handle handle)
{
    uint16_t slot;
    if (core == NULL || handle == 0u) return THY_INVALID_ARGUMENT;
    for (slot = 0u; slot < THY_GENERATED_OWNER_COUNT; ++slot) {
        if (core->generated[slot].handle == handle) {
            release_generated_owner(core, &core->generated[slot]);
            break;
        }
    }
    return THY_OK;
}

int thy_core_generated_release_source(thy_core *core, uint32_t source,
                                      uint32_t run)
{
    uint16_t slot;
    if (core == NULL || source == 0u || run == 0u) return THY_INVALID_ARGUMENT;
    for (slot = 0u; slot < THY_GENERATED_OWNER_COUNT; ++slot) {
        thy_generated_owner *owner = &core->generated[slot];
        if (owner->handle != 0u && owner->source == source && owner->run == run) {
            release_generated_owner(core, owner);
        }
    }
    return THY_OK;
}

int thy_core_generated_release_all(thy_core *core)
{
    uint16_t slot;
    if (core == NULL) return THY_INVALID_ARGUMENT;
    for (slot = 0u; slot < THY_GENERATED_OWNER_COUNT; ++slot) {
        if (core->generated[slot].handle != 0u) {
            release_generated_owner(core, &core->generated[slot]);
        }
    }
    return THY_OK;
}

uint16_t thy_core_generated_count(const thy_core *core)
{
    return core != NULL ? core->generated_count : 0u;
}

uint16_t thy_core_generated_source_count(const thy_core *core, uint32_t source,
                                         uint32_t run)
{
    uint16_t slot;
    uint16_t count = 0u;
    if (core == NULL || source == 0u || run == 0u) return 0u;
    for (slot = 0u; slot < THY_GENERATED_OWNER_COUNT; ++slot) {
        const thy_generated_owner *owner = &core->generated[slot];
        if (owner->handle != 0u && owner->source == source && owner->run == run) {
            ++count;
        }
    }
    return count;
}

const thy_generated_owner *thy_core_generated_find(const thy_core *core,
                                                   thy_generated_handle handle)
{
    uint16_t slot;
    if (core == NULL || handle == 0u) return NULL;
    for (slot = 0u; slot < THY_GENERATED_OWNER_COUNT; ++slot) {
        if (core->generated[slot].handle == handle) return &core->generated[slot];
    }
    return NULL;
}

const thy_generated_owner *thy_core_generated_at(const thy_core *core,
                                                 uint16_t slot)
{
    return core != NULL && slot < THY_GENERATED_OWNER_COUNT &&
           core->generated[slot].handle != 0u ? &core->generated[slot] : NULL;
}

int thy_core_pressure(thy_core *core, uint8_t pad, uint8_t pressure)
{
    size_t i;
    const thy_pad_owner *owner;
    if (core == NULL || pad >= THY_PAD_COUNT || pressure > 127u) {
        return THY_INVALID_ARGUMENT;
    }
    owner = &core->owners[pad];
    if (owner->held == 0u) {
        return THY_OK;
    }
    if (!room_for(core, owner->activated)) {
        return THY_QUEUE_FULL;
    }
    for (i = 0u; i < owner->activated; ++i) {
        append_note(core, thy_pad_owner_key(owner, (uint8_t)i), 0xa0u, pressure);
    }
    return THY_OK;
}

static int set_sustain(thy_core *core, uint8_t destinations, uint8_t channel,
                       uint8_t value, int repeat)
{
    uint8_t destination;
    uint8_t enabled = (uint8_t)(value >= 64u);
    uint16_t events = 0u;
    uint16_t newly_enabled = 0u;
    uint16_t disabled = 0u;
    /* Deferred NoteOffs cancel their existing reservation one-for-one. */
    for (destination = 0u; destination < THY_DEST_COUNT; ++destination) {
        if ((destinations & (uint8_t)(1u << destination)) != 0u) {
            uint16_t route = (uint16_t)(destination * 16u + channel);
            uint8_t prior = core->sustain[route];
            if (prior != enabled || repeat != 0) {
                ++events;
            }
            if (prior == 0u && enabled != 0u) {
                ++newly_enabled;
            } else if (prior != 0u && enabled == 0u) {
                ++disabled;
            }
        }
    }
    if ((uint32_t)core->queue_count + obligation_count(core) + events +
        newly_enabled - disabled > THY_QUEUE_CAPACITY) {
        return THY_QUEUE_FULL;
    }
    for (destination = 0u; destination < THY_DEST_COUNT; ++destination) {
        if ((destinations & (uint8_t)(1u << destination)) != 0u) {
            uint16_t route = (uint16_t)(destination * 16u + channel);
            uint8_t prior = core->sustain[route];
            if (prior != enabled || repeat != 0) {
                append(core, destination, (uint8_t)(0xb0u | channel), 64u, value);
            }
            if (prior == enabled) {
                continue;
            }
            core->sustain[route] = enabled;
            if (enabled != 0u) {
                ++core->sustain_count;
            } else {
                uint16_t pitch;
                --core->sustain_count;
                for (pitch = 0u; pitch < 128u; ++pitch) {
                    uint16_t key = (uint16_t)(route * 128u + pitch);
                    if (thy_core_deferred(core, key)) {
                        append_note(core, key, 0x80u, 0u);
                        deferred_clear(core, key);
                        --core->sounding_count;
                    }
                }
            }
        }
    }
    return THY_OK;
}

int thy_core_sustain(thy_core *core, uint8_t destinations, uint8_t channel,
                     int enabled)
{
    if (core == NULL || !route_valid(destinations, channel)) {
        return THY_INVALID_ARGUMENT;
    }
    return set_sustain(core, destinations, channel,
                        (uint8_t)(enabled != 0 ? 127u : 0u), 0);
}

static int emit_control(thy_core *core, uint8_t destinations, uint8_t channel,
                         uint8_t kind, uint8_t data1, uint8_t data2)
{
    uint8_t destination;
    uint16_t events = 0u;
    for (destination = 0u; destination < THY_DEST_COUNT; ++destination) {
        if ((destinations & (uint8_t)(1u << destination)) != 0u) {
            ++events;
        }
    }
    if (!room_for(core, events)) {
        return THY_QUEUE_FULL;
    }
    for (destination = 0u; destination < THY_DEST_COUNT; ++destination) {
        if ((destinations & (uint8_t)(1u << destination)) != 0u) {
            append(core, destination, (uint8_t)(kind | channel), data1, data2);
        }
    }
    return THY_OK;
}

int thy_core_emit_batch(thy_core *core, const thy_midi_event *events, size_t count)
{
    size_t i;
    if (core == NULL || count > THY_QUEUE_CAPACITY || (count && events == NULL))
        return THY_INVALID_ARGUMENT;
    for (i = 0u; i < count; ++i) {
        const thy_midi_event *event = &events[i];
        uint8_t kind = event->status & 0xf0u;
        if (event->destination >= THY_DEST_COUNT || event->data1 > 127u ||
            event->data2 > 127u ||
            (kind != 0xb0u && kind != 0xd0u && kind != 0xe0u) ||
            (kind == 0xb0u && event->data1 == 64u) ||
            (kind == 0xd0u && event->data2 != 0u))
            return THY_INVALID_ARGUMENT;
    }
    if (!room_for(core, (uint16_t)count)) return THY_QUEUE_FULL;
    for (i = 0u; i < count; ++i)
        append(core, events[i].destination, events[i].status,
            events[i].data1, events[i].data2);
    return THY_OK;
}

int thy_core_channel_pressure(thy_core *core, uint8_t destinations,
                               uint8_t channel, uint8_t pressure)
{
    if (core == NULL || !route_valid(destinations, channel) || pressure > 127u)
        return THY_INVALID_ARGUMENT;
    return emit_control(core, destinations, channel, 0xd0u, pressure, 0u);
}

int thy_core_cc(thy_core *core, uint8_t destinations, uint8_t channel,
                uint8_t controller, uint8_t value)
{
    if (core == NULL || !route_valid(destinations, channel) ||
        controller > 127u || value > 127u) {
        return THY_INVALID_ARGUMENT;
    }
    if (controller == 64u) {
        return set_sustain(core, destinations, channel, value, 1);
    }
    return emit_control(core, destinations, channel, 0xb0u, controller, value);
}

int thy_core_bend(thy_core *core, uint8_t destinations, uint8_t channel,
                  int32_t value)
{
    if (core == NULL || !route_valid(destinations, channel)) {
        return THY_INVALID_ARGUMENT;
    }
    if (value < 0) {
        value = 0;
    } else if (value > 16383) {
        value = 16383;
    }
    return emit_control(core, destinations, channel, 0xe0u,
                         (uint8_t)(value & 127), (uint8_t)(value >> 7));
}

void thy_core_panic(thy_core *core)
{
    uint16_t route;
    uint16_t key;
    uint8_t pad;
    uint16_t slot;
    if (core == NULL) {
        return;
    }
    core->pending_keys = 0u;
    for (route = 0u; route < THY_DEST_COUNT * 16u; ++route) {
        if (core->sustain[route] != 0u) {
            append(core, (uint8_t)(route / 16u),
                   (uint8_t)(0xb0u | (route % 16u)), 64u, 0u);
            core->sustain[route] = 0u;
            --core->sustain_count;
        }
    }
    for (key = 0u; key < THY_KEY_COUNT; ++key) {
        if (core->refs[key] != 0u || thy_core_deferred(core, key)) {
            append_note(core, key, 0x80u, 0u);
            core->refs[key] = 0u;
            deferred_clear(core, key);
            --core->sounding_count;
        }
    }
    for (pad = 0u; pad < THY_PAD_COUNT; ++pad) {
        core->owners[pad].held = 0u;
        core->owners[pad].count = 0u;
        core->owners[pad].activated = 0u;
        core->owners[pad].interval = 0u;
    }
    for (slot = 0u; slot < THY_GENERATED_OWNER_COUNT; ++slot) {
        core->generated[slot].handle = 0u;
        core->generated[slot].source = 0u;
        core->generated[slot].run = 0u;
        core->generated[slot].count = 0u;
    }
    core->generated_count = 0u;
}

int thy_core_poll(thy_core *core, thy_midi_event *event)
{
    if (core == NULL || event == NULL || core->queue_count == 0u) {
        return 0;
    }
    *event = core->queue[core->queue_head];
    ++core->queue_head;
    if (core->queue_head == THY_QUEUE_CAPACITY) {
        core->queue_head = 0u;
    }
    --core->queue_count;
    return 1;
}

uint8_t thy_core_refcount(const thy_core *core, uint16_t key)
{
    return core != NULL && key < THY_KEY_COUNT ? core->refs[key] : 0u;
}

int thy_core_pad_held(const thy_core *core, uint8_t pad)
{
    return core != NULL && pad < THY_PAD_COUNT && core->owners[pad].held != 0u;
}

uint16_t thy_core_owner_key(const thy_core *core, uint8_t pad, uint8_t index)
{
    if (core == NULL || pad >= THY_PAD_COUNT || index >= core->owners[pad].count) {
        return UINT16_MAX;
    }
    return thy_pad_owner_key(&core->owners[pad], index);
}

uint16_t thy_core_queued(const thy_core *core)
{
    return core != NULL ? core->queue_count : 0u;
}

uint16_t thy_core_sounding(const thy_core *core)
{
    return core != NULL ? core->sounding_count : 0u;
}

/* Byte assembly is alias/alignment safe in portable C. The target compiler
 * recognizes it as a word load; only nonzero-ness matters, not byte order. */
static uint32_t reference_word(const uint8_t *refs)
{
    return (uint32_t)refs[0] | ((uint32_t)refs[1] << 8u) |
        ((uint32_t)refs[2] << 16u) | ((uint32_t)refs[3] << 24u);
}

void thy_core_sounding_pitches(const thy_core *core, uint8_t pitches[128])
{
    if (pitches == NULL) return;
    for (unsigned pitch = 0u; pitch < 128u; ++pitch) pitches[pitch] = 0u;
    if (core == NULL || core->sounding_count == 0u) return;

    _Static_assert(THY_KEY_COUNT % 8u == 0u && 128u % 8u == 0u,
                   "pitch blocks must align with deferred bytes and channels");
    for (unsigned key = 0u; key < THY_KEY_COUNT; key += 8u) {
        uint8_t deferred = core->deferred_bits[key / 8u];
        if (!(reference_word(core->refs + key) |
              reference_word(core->refs + key + 4u) | deferred)) continue;
        for (unsigned bit = 0u; bit < 8u; ++bit)
            if (core->refs[key + bit] || (deferred & (1u << bit)))
                pitches[(key % 128u) + bit] = 1u;
    }
}

uint16_t thy_core_reserved(const thy_core *core)
{
    return core != NULL ? obligation_count(core) : 0u;
}

uint16_t thy_core_pending(const thy_core *core)
{
    return core != NULL ? core->pending_keys : 0u;
}
