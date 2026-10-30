/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_mpe.h"

_Static_assert(sizeof(thy_mpe) <= 1024u, "MPE workspace must stay compact");

static int route_valid(uint8_t destinations)
{
    return destinations != 0u && (destinations & (uint8_t)~THY_ROUTE_ALL) == 0u;
}

static uint8_t member_channel(const thy_mpe_config *config, uint8_t index)
{
    return config->upper_zone ? (uint8_t)(14u - index) : (uint8_t)(1u + index);
}

void thy_mpe_defaults(thy_mpe_config *config)
{
    if (config == NULL) return;
    config->enabled = 0u;
    config->upper_zone = 0u;
    config->members = 15u;
    config->bend_range = 48u;
    config->manager_bend_range = 2u;
    config->glide = 0u;
    config->pad_window = 1u;
    config->semitone_window = 2u;
    config->pressure_output[0] = config->pressure_output[1] = THY_MPE_PRESSURE;
}

int thy_mpe_config_valid(const thy_mpe_config *config)
{
    return config != NULL && config->enabled <= 1u && config->upper_zone <= 1u &&
        config->members >= 1u && config->members <= THY_MPE_MEMBERS &&
        config->bend_range >= 1u && config->bend_range <= 96u &&
        config->manager_bend_range >= 1u && config->manager_bend_range <= 96u &&
        config->glide <= 1u && config->pad_window >= 1u && config->pad_window <= 11u &&
        config->semitone_window >= 1u && config->semitone_window <= 24u &&
        config->pressure_output[0] <= THY_MPE_CC1 &&
        config->pressure_output[1] <= THY_MPE_CC1;
}

uint8_t thy_mpe_manager(const thy_mpe_config *config)
{
    return config != NULL && config->upper_zone ? 15u : 0u;
}

int thy_mpe_channel_in_zone(const thy_mpe_config *config, uint8_t channel)
{
    if (!thy_mpe_config_valid(config) || channel >= 16u) return 0;
    return config->upper_zone ? channel >= (uint8_t)(15u - config->members) && channel < 15u :
        channel >= 1u && channel <= config->members;
}

void thy_mpe_init(thy_mpe *mpe, thy_core *core)
{
    size_t i;
    uint8_t *bytes;
    if (mpe == NULL) return;
    bytes = (uint8_t *)mpe;
    for (i = 0u; i < sizeof(*mpe); ++i) bytes[i] = 0u;
    mpe->core = core;
    thy_mpe_defaults(&mpe->config);
}

uint8_t thy_mpe_active(const thy_mpe *mpe)
{
    uint8_t result = 0u, i;
    if (mpe == NULL) return 0u;
    for (i = 0u; i < THY_MPE_MEMBERS; ++i)
        if (mpe->voices[i].state != THY_MPE_FREE) ++result;
    return result;
}

int thy_mpe_quiet(const thy_mpe *mpe)
{
    uint8_t i;
    if (mpe == NULL || thy_mpe_active(mpe) != 0u) return 0;
    for (i = 0u; i < THY_PAD_COUNT; ++i)
        if (mpe->consumed[i] != 0u || (mpe->owned[i / 8u] & (1u << (i % 8u)))) return 0;
    return 1;
}

static int core_quiet(const thy_core *core)
{
    uint8_t pad;
    if (core == NULL || core->sounding_count != 0u || core->pending_keys != 0u ||
        core->generated_count != 0u || core->sustain_count != 0u) return 0;
    for (pad = 0u; pad < THY_PAD_COUNT; ++pad) if (core->owners[pad].held) return 0;
    return 1;
}

int thy_mpe_configure(thy_mpe *mpe, const thy_mpe_config *config)
{
    if (mpe == NULL || mpe->core == NULL || !thy_mpe_config_valid(config))
        return THY_INVALID_ARGUMENT;
    if (!thy_mpe_quiet(mpe) || !core_quiet(mpe->core)) return THY_OWNER_LIMIT;
    mpe->config = *config;
    return THY_OK;
}

/* A member lease must cover active, sustained and future strum keys. This is
 * bounded by the existing ledger; no shadow reference counters can diverge. */
static int channel_busy(const thy_core *core, uint8_t channel, uint8_t destinations)
{
    uint8_t destination, pad;
    for (destination = 0u; destination < THY_DEST_COUNT; ++destination) {
        uint16_t first;
        uint16_t pitch;
        if (!(destinations & (1u << destination))) continue;
        first = thy_core_key(destination, channel, 0u);
        for (pitch = 0u; pitch < 128u; ++pitch)
            if (core->refs[first + pitch] || thy_core_deferred(core, (uint16_t)(first + pitch))) return 1;
    }
    if (core->pending_keys == 0u) return 0;
    for (pad = 0u; pad < THY_PAD_COUNT; ++pad) {
        const thy_pad_owner *owner = &core->owners[pad];
        uint8_t i;
        for (i = owner->activated; i < owner->count; ++i) {
            uint16_t route = (uint16_t)(thy_pad_owner_key(owner, (uint8_t)(i)) / 128u);
            if (route % 16u == channel && (destinations & (1u << (route / 16u)))) return 1;
        }
    }
    return 0;
}

static size_t controls(thy_midi_event events[9], uint8_t destinations,
    uint8_t channel, int pressure, uint16_t bend, int send_bend, int timbre,
    uint8_t output)
{
    uint8_t destination;
    size_t count = 0u;
    for (destination = 0u; destination < THY_DEST_COUNT; ++destination) {
        if (!(destinations & (1u << destination))) continue;
        if (pressure >= 0) {
            events[count++] = output == THY_MPE_PRESSURE ?
                (thy_midi_event){destination, (uint8_t)(0xd0u | channel), (uint8_t)pressure, 0u} :
                (thy_midi_event){destination, (uint8_t)(0xb0u | channel),
                    output == THY_MPE_CC74 ? 74u : 1u, (uint8_t)pressure};
        }
        if (send_bend) {
            events[count++] = (thy_midi_event){destination, (uint8_t)(0xe0u | channel),
                (uint8_t)(bend & 127u), (uint8_t)(bend >> 7u)};
        }
        if (timbre >= 0) {
            events[count++] = output == THY_MPE_PRESSURE ?
                (thy_midi_event){destination, (uint8_t)(0xb0u | channel), 74u, (uint8_t)timbre} :
                (thy_midi_event){destination, (uint8_t)(0xd0u | channel), (uint8_t)timbre, 0u};
        }
    }
    return count;
}

static int retire_one(thy_mpe *mpe, thy_mpe_voice *voice)
{
    thy_midi_event events[9];
    int result;
    if (voice->state != THY_MPE_RETIRING ||
        channel_busy(mpe->core, voice->channel, voice->destinations)) return THY_OK;
    result = thy_core_emit_batch(mpe->core, events,
        controls(events, voice->destinations, voice->channel,
            voice->pressure_output == THY_MPE_CC74 ? 64 : 0, 8192u, 1,
            voice->pressure_output == THY_MPE_PRESSURE ? 64 : 0, voice->pressure_output));
    if (result == THY_OK) voice->state = THY_MPE_FREE;
    return result;
}

static void reap_external_releases(thy_mpe *mpe)
{
    uint8_t i;
    for (i = 0u; i < THY_PAD_COUNT; ++i)
        if (!mpe->core->owners[i].held) mpe->owned[i / 8u] &= (uint8_t)~(1u << (i % 8u));
    for (i = 0u; i < THY_MPE_MEMBERS; ++i) {
        thy_mpe_voice *voice = &mpe->voices[i];
        if (voice->state == THY_MPE_HELD && !mpe->core->owners[voice->pad].held)
            voice->state = THY_MPE_RETIRING;
    }
}

static int retire_ready(thy_mpe *mpe)
{
    uint8_t i;
    reap_external_releases(mpe);
    for (i = 0u; i < THY_MPE_MEMBERS; ++i) {
        int result = retire_one(mpe, &mpe->voices[i]);
        if (result != THY_OK) return result;
    }
    return THY_OK;
}

uint16_t thy_mpe_glide_bend(uint8_t source_pressure, uint8_t target_pressure,
    int8_t interval, uint8_t bend_range)
{
    int32_t offset;
    uint16_t sum = (uint16_t)source_pressure + target_pressure;
    if (source_pressure > 127u || target_pressure > 127u || bend_range == 0u ||
        sum == 0u) return 8192u;
    /* Negative endpoint has 8192 steps, positive has 8191. Integer rounding is
     * symmetric enough for the 7-bit pressure sensor; both endpoints are exact. */
    offset = (int32_t)interval * (interval < 0 ? 8192 : 8191) * target_pressure /
        ((int32_t)sum * bend_range);
    if (offset < -8192) offset = -8192;
    if (offset > 8191) offset = 8191;
    return (uint16_t)(8192 + offset);
}

static void update_glide(thy_mpe *mpe, uint8_t source)
{
    uint8_t i;
    for (i = 0u; i < THY_MPE_MEMBERS; ++i) {
        thy_mpe_voice *voice = &mpe->voices[i];
        if (voice->state == THY_MPE_HELD && voice->pad == source)
            voice->bend = voice->modifier ? thy_mpe_glide_bend(voice->pressure,
                voice->modifier_pressure, voice->interval, mpe->config.bend_range) : 8192u;
    }
}

static int try_modifier(thy_mpe *mpe, uint8_t pad, uint8_t logical, uint8_t reference)
{
    thy_mpe_voice *selected = NULL;
    uint8_t i;
    if (!mpe->config.glide) return 0;
    for (i = 0u; i < THY_MPE_MEMBERS; ++i) {
        thy_mpe_voice *voice = &mpe->voices[i];
        int distance, interval;
        if (voice->state != THY_MPE_HELD || voice->modifier ||
            !mpe->core->owners[voice->pad].held || voice->logical / 48u != logical / 48u ||
            voice->logical % 4u != logical % 4u) continue;
        distance = (int)(logical % 48u) / 4 - (int)(voice->logical % 48u) / 4;
        interval = (int)reference - voice->reference;
        if (distance < 0) distance = -distance;
        if (interval < 0) interval = -interval;
        if (distance == 0 || distance > mpe->config.pad_window || interval == 0 ||
            interval > mpe->config.semitone_window || interval > mpe->config.bend_range) continue;
        if (selected == NULL || voice->started > selected->started) selected = voice;
    }
    if (selected == NULL) return 0;
    mpe->consumed[pad] = (uint8_t)(selected->pad + 1u);
    for (i = 0u; i < THY_MPE_MEMBERS; ++i) {
        thy_mpe_voice *voice = &mpe->voices[i];
        if (voice->state == THY_MPE_HELD && voice->pad == selected->pad) {
            voice->modifier = (uint8_t)(pad + 1u);
            voice->modifier_pressure = 0u;
            voice->interval = (int8_t)((int)reference - voice->reference);
        }
    }
    update_glide(mpe, selected->pad);
    return 1;
}

int thy_mpe_press(thy_mpe *mpe, uint8_t pad, uint8_t logical,
    uint8_t reference, const int16_t *notes, size_t count,
    uint8_t destinations, uint8_t velocity, uint16_t strum_ms,
    int *out_consumed)
{
    return thy_mpe_press_ex(mpe, pad, logical, reference, notes, count,
        destinations, velocity, strum_ms, out_consumed,
        mpe && logical < THY_PAD_COUNT && mpe->config.pressure_output[logical / 48u] != THY_MPE_PRESSURE ? 0u : 64u);
}

int thy_mpe_press_ex(thy_mpe *mpe, uint8_t pad, uint8_t logical,
    uint8_t reference, const int16_t *notes, size_t count,
    uint8_t destinations, uint8_t velocity, uint16_t strum_ms,
    int *out_consumed, uint8_t initial_timbre)
{
    uint8_t slots[THY_MPE_MEMBERS], channels[THY_MPE_MEMBERS];
    thy_midi_event events[THY_MPE_MEMBERS * THY_DEST_COUNT * 3u];
    size_t i, j, event_count = 0u, found = 0u;
    uint16_t previous_queue;
    int result;
    if (mpe == NULL || mpe->core == NULL || !thy_mpe_config_valid(&mpe->config) ||
        !mpe->config.enabled || pad >= THY_PAD_COUNT || logical >= THY_PAD_COUNT ||
        reference > 127u || count > THY_MAX_NOTES || (count && notes == NULL) ||
        !route_valid(destinations) || velocity == 0u || velocity > 127u ||
        strum_ms > 1000u || initial_timbre > 127u) return THY_INVALID_ARGUMENT;
    if (mpe->consumed[pad]) {
        if (out_consumed != NULL) *out_consumed = 1;
        return THY_OK;
    }
    for (i = 0u; i < count; ++i) {
        if (notes[i] < 0 || notes[i] > 127) return THY_INVALID_ARGUMENT;
        for (j = 0u; j < i; ++j) if (notes[j] == notes[i]) return THY_INVALID_ARGUMENT;
    }
    if (mpe->core->owners[pad].held) {
        for (i = 0u; i < THY_MPE_MEMBERS; ++i) {
            if (mpe->voices[i].state == THY_MPE_HELD && mpe->voices[i].pad == pad) {
                if (out_consumed != NULL) *out_consumed = 0;
                return THY_OK;
            }
        }
        /* A silent owner from this router is also idempotent. */
        if (!(mpe->owned[pad / 8u] & (1u << (pad % 8u)))) return THY_OWNER_LIMIT;
        if (out_consumed != NULL) *out_consumed = 0;
        return THY_OK;
    }
    if (count && try_modifier(mpe, pad, logical, reference)) {
        if (out_consumed != NULL) *out_consumed = 1;
        return THY_OK;
    }
    if (count > mpe->config.members) return THY_OWNER_LIMIT;
    if (count && mpe->started == UINT32_MAX) return THY_HANDLE_EXHAUSTED;
    (void)retire_ready(mpe);
    for (i = 0u; i < mpe->config.members && found < count; ++i) {
        uint8_t channel = member_channel(&mpe->config, (uint8_t)i);
        if (mpe->voices[i].state != THY_MPE_FREE ||
            channel_busy(mpe->core, channel, destinations)) continue;
        slots[found] = (uint8_t)i;
        channels[found++] = channel;
    }
    if (found != count) return THY_OWNER_LIMIT;
    for (i = 0u; i < count; ++i) {
        event_count += controls(events + event_count, destinations, channels[i], 0, 8192u, 1,
            initial_timbre, mpe->config.pressure_output[logical / 48u]);
    }
    /* Only queue bytes are touched by initialization. Restore its logical tail
     * if the complete reserved attack cannot fit; no prefix reaches a sink. */
    previous_queue = mpe->core->queue_count;
    result = thy_core_emit_batch(mpe->core, events, event_count);
    if (result == THY_OK) result = thy_core_press_assigned(mpe->core, pad, notes,
        channels, count, destinations, velocity, strum_ms);
    if (result != THY_OK) {
        mpe->core->queue_count = previous_queue;
        return result;
    }
    if (count) ++mpe->started;
    mpe->owned[pad / 8u] |= (uint8_t)(1u << (pad % 8u));
    for (i = 0u; i < count; ++i) {
        thy_mpe_voice *voice = &mpe->voices[slots[i]];
        *voice = (thy_mpe_voice){0};
        voice->started = mpe->started;
        voice->bend = voice->sent_bend = 8192u;
        voice->timbre = voice->sent_timbre = initial_timbre;
        voice->pad = pad;
        voice->logical = logical;
        voice->reference = reference;
        voice->pitch = (uint8_t)notes[i];
        voice->channel = channels[i];
        voice->destinations = destinations;
        voice->pressure_output = mpe->config.pressure_output[logical / 48u];
        voice->state = THY_MPE_HELD;
    }
    if (out_consumed != NULL) *out_consumed = 0;
    return THY_OK;
}

int thy_mpe_release(thy_mpe *mpe, uint8_t pad)
{
    uint8_t i;
    int result;
    if (mpe == NULL || mpe->core == NULL || pad >= THY_PAD_COUNT) return THY_INVALID_ARGUMENT;
    if (mpe->consumed[pad]) {
        uint8_t source = (uint8_t)(mpe->consumed[pad] - 1u);
        mpe->consumed[pad] = 0u;
        for (i = 0u; i < THY_MPE_MEMBERS; ++i) {
            thy_mpe_voice *voice = &mpe->voices[i];
            if (voice->state == THY_MPE_HELD && voice->pad == source && voice->modifier == pad + 1u) {
                voice->modifier = 0u;
                voice->modifier_pressure = 0u;
                voice->interval = 0;
                voice->bend = 8192u;
            }
        }
        return THY_OK;
    }
    if (!(mpe->owned[pad / 8u] & (1u << (pad % 8u)))) return THY_OK;
    result = thy_core_release(mpe->core, pad);
    if (result != THY_OK) return result;
    mpe->owned[pad / 8u] &= (uint8_t)~(1u << (pad % 8u));
    for (i = 0u; i < THY_MPE_MEMBERS; ++i)
        if (mpe->voices[i].state == THY_MPE_HELD && mpe->voices[i].pad == pad)
            mpe->voices[i].state = THY_MPE_RETIRING;
    (void)retire_ready(mpe);
    return THY_OK;
}

int thy_mpe_pressure(thy_mpe *mpe, uint8_t pad, uint8_t pressure)
{
    uint8_t i;
    if (mpe == NULL || mpe->core == NULL || pad >= THY_PAD_COUNT || pressure > 127u)
        return THY_INVALID_ARGUMENT;
    if (mpe->consumed[pad]) {
        uint8_t source = (uint8_t)(mpe->consumed[pad] - 1u);
        for (i = 0u; i < THY_MPE_MEMBERS; ++i)
            if (mpe->voices[i].state == THY_MPE_HELD && mpe->voices[i].pad == source &&
                mpe->voices[i].modifier == pad + 1u) mpe->voices[i].modifier_pressure = pressure;
        update_glide(mpe, source);
    } else {
        for (i = 0u; i < THY_MPE_MEMBERS; ++i)
            if (mpe->voices[i].state == THY_MPE_HELD && mpe->voices[i].pad == pad)
                mpe->voices[i].pressure = pressure;
        if (mpe->config.glide) update_glide(mpe, pad);
    }
    return THY_OK;
}

int thy_mpe_bend(thy_mpe *mpe, uint8_t pad, int32_t value)
{
    uint8_t i;
    if (mpe == NULL || mpe->core == NULL || pad >= THY_PAD_COUNT) return THY_INVALID_ARGUMENT;
    if (value < 0) value = 0;
    if (value > 16383) value = 16383;
    for (i = 0u; i < THY_MPE_MEMBERS; ++i)
        if (mpe->voices[i].state == THY_MPE_HELD && mpe->voices[i].pad == pad)
            mpe->voices[i].bend = (uint16_t)value;
    return THY_OK;
}

int thy_mpe_timbre(thy_mpe *mpe, uint8_t pad, uint8_t value)
{
    uint8_t i;
    if (mpe == NULL || mpe->core == NULL || pad >= THY_PAD_COUNT || value > 127u)
        return THY_INVALID_ARGUMENT;
    if (mpe->consumed[pad]) return THY_OK;
    for (i = 0u; i < THY_MPE_MEMBERS; ++i)
        if (mpe->voices[i].state == THY_MPE_HELD && mpe->voices[i].pad == pad)
            mpe->voices[i].timbre = value;
    return THY_OK;
}

int thy_mpe_service(thy_mpe *mpe)
{
    uint8_t scanned, emitted = 0u;
    if (mpe == NULL || mpe->core == NULL) return THY_INVALID_ARGUMENT;
    reap_external_releases(mpe);
    for (scanned = 0u; scanned < THY_MPE_MEMBERS && emitted < 2u; ++scanned) {
        thy_mpe_voice *voice = &mpe->voices[mpe->service_cursor];
        int result = THY_OK;
        mpe->service_cursor = (uint8_t)((mpe->service_cursor + 1u) % THY_MPE_MEMBERS);
        if (voice->state == THY_MPE_RETIRING) {
            if (!channel_busy(mpe->core, voice->channel, voice->destinations)) {
                result = retire_one(mpe, voice);
                if (result == THY_OK) ++emitted;
            }
        } else if (voice->state == THY_MPE_HELD &&
            (voice->pressure != voice->sent_pressure || voice->bend != voice->sent_bend ||
             voice->timbre != voice->sent_timbre)) {
            thy_midi_event events[9];
            size_t count = controls(events, voice->destinations, voice->channel,
                voice->pressure != voice->sent_pressure ? voice->pressure : -1,
                voice->bend, voice->bend != voice->sent_bend,
                voice->timbre != voice->sent_timbre ? voice->timbre : -1,
                voice->pressure_output);
            result = thy_core_emit_batch(mpe->core, events, count);
            if (result == THY_OK) {
                voice->sent_pressure = voice->pressure;
                voice->sent_bend = voice->bend;
                voice->sent_timbre = voice->timbre;
                ++emitted;
            }
        }
        if (result != THY_OK) return result;
    }
    return THY_OK;
}

void thy_mpe_release_all(thy_mpe *mpe)
{
    uint8_t i;
    if (mpe == NULL || mpe->core == NULL) return;
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        mpe->consumed[i] = 0u;
        if (mpe->owned[i / 8u] & (1u << (i % 8u))) (void)thy_core_release(mpe->core, i);
    }
    for (i = 0u; i < THY_PAD_COUNT / 8u; ++i) mpe->owned[i] = 0u;
    for (i = 0u; i < THY_MPE_MEMBERS; ++i) {
        thy_mpe_voice *voice = &mpe->voices[i];
        if (voice->state == THY_MPE_HELD) {
            (void)thy_core_release(mpe->core, voice->pad);
            voice->state = THY_MPE_RETIRING;
        }
    }
    (void)retire_ready(mpe);
}

static int rpn(thy_core *core, uint8_t destinations, uint8_t channel,
    uint8_t number, uint8_t value)
{
    const uint8_t controllers[6] = {101u, 100u, 6u, 38u, 101u, 100u};
    uint8_t values[6] = {0u, number, value, 0u, 127u, 127u};
    thy_midi_event events[6u * THY_DEST_COUNT];
    uint8_t destination, i;
    size_t count = 0u;
    for (destination = 0u; destination < THY_DEST_COUNT; ++destination) {
        if (!(destinations & (1u << destination))) continue;
        for (i = 0u; i < 6u; ++i) events[count++] = (thy_midi_event){destination,
            (uint8_t)(0xb0u | channel), controllers[i], values[i]};
    }
    return thy_core_emit_batch(core, events, count);
}

int thy_mpe_send_configuration(thy_mpe *mpe, uint8_t destinations)
{
    uint8_t i, destination_count = 0u;
    uint16_t previous_queue;
    int result;
    if (mpe == NULL || mpe->core == NULL || !thy_mpe_config_valid(&mpe->config) ||
        !mpe->config.enabled || !route_valid(destinations)) return THY_INVALID_ARGUMENT;
    if (!thy_mpe_quiet(mpe) || !core_quiet(mpe->core)) return THY_OWNER_LIMIT;
    for (i = 0u; i < THY_DEST_COUNT; ++i) if (destinations & (1u << i)) ++destination_count;
    if ((uint32_t)mpe->core->queue_count + thy_core_reserved(mpe->core) +
        (uint32_t)(mpe->config.members + 3u) * 6u * destination_count > THY_QUEUE_CAPACITY)
        return THY_QUEUE_FULL;
    previous_queue = mpe->core->queue_count;
    result = rpn(mpe->core, destinations, mpe->config.upper_zone ? 0u : 15u, 6u, 0u);
    if (result == THY_OK) result = rpn(mpe->core, destinations, thy_mpe_manager(&mpe->config),
        6u, mpe->config.members);
    if (result == THY_OK) result = rpn(mpe->core, destinations, thy_mpe_manager(&mpe->config),
        0u, mpe->config.manager_bend_range);
    for (i = 0u; result == THY_OK && i < mpe->config.members; ++i)
        result = rpn(mpe->core, destinations, member_channel(&mpe->config, i), 0u,
            mpe->config.bend_range);
    if (result != THY_OK) mpe->core->queue_count = previous_queue;
    return result;
}

int thy_mpe_consumed(const thy_mpe *mpe, uint8_t pad)
{
    return mpe != NULL && pad < THY_PAD_COUNT && mpe->consumed[pad] != 0u;
}

const thy_mpe_voice *thy_mpe_voice_at(const thy_mpe *mpe, uint8_t slot)
{
    if (mpe == NULL || slot >= THY_MPE_MEMBERS || mpe->voices[slot].state == THY_MPE_FREE)
        return NULL;
    return &mpe->voices[slot];
}
