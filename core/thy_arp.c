/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_arp.h"

static uint32_t add_count(uint32_t value, uint64_t amount)
{
    return amount > UINT32_MAX - value ? UINT32_MAX : value + (uint32_t)amount;
}
static int has(const uint64_t bits[2], unsigned note)
{
    return (bits[note / 64u] & (UINT64_C(1) << (note % 64u))) != 0u;
}
static void put(uint64_t bits[2], unsigned note)
{
    bits[note / 64u] |= UINT64_C(1) << (note % 64u);
}
static int nonempty(const uint64_t bits[2]) { return bits[0] != 0u || bits[1] != 0u; }

void thy_arp_default_config(thy_arp_config *config)
{
    if (!config) return;
    *config = (thy_arp_config){.rate_ticks = 240u,
        .scale = {0u,2u,4u,5u,7u,9u,11u}, .scale_count = 7u,
        .source_zones = 63u, .gate_percent = 50u, .step_mask = 255u,
        .channel = 2u, .destinations = THY_ROUTE_USB0 | THY_ROUTE_DIN};
}

int thy_arp_config_valid(const thy_arp_config *config)
{
    if (!config || config->rate_ticks < 40u || config->rate_ticks > 3840u ||
        config->enabled > 1u || config->type > THY_ARP_CHORD ||
        config->grouping > THY_ARP_EACH || config->source_zones > 63u ||
        config->layer > 1u || config->start > THY_ARP_GRID ||
        config->restart_on_change > 1u || config->gate_percent < 1u ||
        config->gate_percent > 100u || config->swing_percent > 50u ||
        config->pattern > THY_ARP_AS_PLAYED || config->channel > 15u ||
        config->destinations == 0u || config->destinations > 7u ||
        config->scale_count < 1u || config->scale_count > 12u ||
        config->root > 11u || config->scale[0] != 0u) return 0;
    for (unsigned n = 0u; n < config->scale_count; ++n)
        if (config->scale[n] > 11u ||
            (n && config->scale[n] <= config->scale[n - 1u])) return 0;
    for (unsigned n = 0u; n < 8u; ++n)
        if (config->offsets[n] < -24 || config->offsets[n] > 24) return 0;
    return 1;
}

void thy_arp_init(thy_arp *arp, uint32_t source)
{
    if (!arp) return;
    *arp = (thy_arp){0};
    thy_arp_default_config(&arp->config);
    arp->source = source;
}

static void release_output(thy_arp *arp, thy_core *core, unsigned slot)
{
    if (!arp->outputs[slot].handle) return;
    (void)thy_core_generated_release(core, arp->outputs[slot].handle);
    arp->outputs[slot].handle = 0u;
    --arp->output_count;
}
static void release_phrase(thy_arp *arp, thy_core *core, unsigned phrase)
{
    for (unsigned n = 0u; n < THY_GENERATED_OWNER_COUNT; ++n)
        if (arp->outputs[n].handle && arp->outputs[n].phrase == phrase)
            release_output(arp, core, n);
}

void thy_arp_clear(thy_arp *arp, thy_core *core)
{
    if (!arp || !core) return;
    for (unsigned n = 0u; n < THY_GENERATED_OWNER_COUNT; ++n)
        release_output(arp, core, n);
    for (unsigned n = 0u; n < THY_ARP_INPUTS; ++n)
        arp->inputs[n] = (thy_arp_input){0};
    for (unsigned n = 0u; n < THY_ARP_PHRASES; ++n)
        arp->phrases[n] = (thy_arp_phrase){0};
    arp->held_count = arp->phrase_count = 0u;
}
void thy_arp_reset_runtime(thy_arp *arp, thy_core *core) { thy_arp_clear(arp, core); }

static int prepare(thy_arp *arp, thy_core *core, uint64_t tick, uint32_t run, int ready)
{
    if (!arp || !core || !arp->source) return THY_INVALID_ARGUMENT;
    if (tick < arp->last_tick) return THY_ARP_TIME_BACKWARD;
    if (run && arp->run && run < arp->run) return THY_ARP_STALE_RUN;
    if (run != arp->run || !ready || !run) thy_arp_clear(arp, core);
    arp->run = run;
    arp->last_tick = tick;
    return THY_OK;
}

static unsigned selected_chord(const thy_arp *arp)
{
    unsigned selected = THY_ARP_POOL;
    uint64_t order = 0u;
    for (unsigned n = 0u; n < THY_ARP_INPUTS; ++n)
        if (arp->inputs[n].held && arp->inputs[n].role &&
            nonempty(arp->inputs[n].pitches) && arp->inputs[n].order > order) {
            selected = n;
            order = arp->inputs[n].order;
        }
    return selected;
}

static unsigned input_phrase(const thy_arp *arp, unsigned input, unsigned chord)
{
    if (arp->config.grouping == THY_ARP_COMBINED) return THY_ARP_POOL;
    if (arp->config.grouping == THY_ARP_EACH || arp->inputs[input].role) return input;
    return chord;
}

static void anchor_phrase(thy_arp *arp, thy_arp_phrase *phrase, uint64_t tick)
{
    uint32_t rate = arp->config.rate_ticks;
    uint64_t wait = arp->config.start == THY_ARP_GRID && tick % rate ? rate - tick % rate : 0u;
    phrase->anchor = UINT64_MAX - tick < wait ? UINT64_MAX : tick + wait;
    phrase->ordinal = 0u;
}

static void rebuild(thy_arp *arp, thy_core *core, uint64_t tick)
{
    uint64_t bits[THY_ARP_PHRASES][2] = {{0}};
    uint8_t velocity[THY_ARP_PHRASES] = {0};
    unsigned chord = selected_chord(arp);
    for (unsigned n = 0u; n < THY_ARP_INPUTS; ++n) {
        const thy_arp_input *input = &arp->inputs[n];
        if (!input->held) continue;
        unsigned phrase = input_phrase(arp, n, chord);
        bits[phrase][0] |= input->pitches[0];
        bits[phrase][1] |= input->pitches[1];
        if (input->velocity > velocity[phrase]) velocity[phrase] = input->velocity;
    }
    arp->phrase_count = 0u;
    for (unsigned n = 0u; n < THY_ARP_PHRASES; ++n) {
        thy_arp_phrase *phrase = &arp->phrases[n];
        int active = nonempty(bits[n]);
        int changed = phrase->pitches[0] != bits[n][0] || phrase->pitches[1] != bits[n][1];
        if (!active) {
            if (phrase->active) release_phrase(arp, core, n);
            *phrase = (thy_arp_phrase){0};
            continue;
        }
        if (!phrase->active || (changed && arp->config.restart_on_change)) {
            if (phrase->active) release_phrase(arp, core, n);
            anchor_phrase(arp, phrase, tick);
        }
        phrase->pitches[0] = bits[n][0];
        phrase->pitches[1] = bits[n][1];
        phrase->active = 1u;
        phrase->velocity = velocity[n];
        ++arp->phrase_count;
    }
}

static unsigned pattern_index(uint64_t ordinal, unsigned count, uint8_t pattern)
{
    if (count < 2u) return 0u;
    if (pattern == THY_ARP_UP_DOWN || pattern == THY_ARP_DOWN_UP) {
        unsigned index = (unsigned)(ordinal % (2u * (count - 1u)));
        if (index >= count) index = 2u * (count - 1u) - index;
        return pattern == THY_ARP_DOWN_UP ? count - 1u - index : index;
    }
    unsigned index = (unsigned)(ordinal % count);
    return pattern == THY_ARP_DOWN ? count - 1u - index : index;
}

static unsigned ordered_pitches(const thy_arp *arp, unsigned phrase, uint8_t notes[128])
{
    unsigned count = 0u;
    if (arp->config.pattern != THY_ARP_AS_PLAYED) {
        for (unsigned n = 0u; n < 128u; ++n)
            if (has(arp->phrases[phrase].pitches, n)) notes[count++] = (uint8_t)n;
        return count;
    }
    uint64_t seen[2] = {0u, 0u};
    uint64_t previous = 0u;
    unsigned chord = selected_chord(arp);
    for (unsigned pass = 0u; pass < THY_ARP_INPUTS; ++pass) {
        unsigned next = THY_ARP_INPUTS;
        uint64_t order = UINT64_MAX;
        for (unsigned n = 0u; n < THY_ARP_INPUTS; ++n)
            if (arp->inputs[n].held && input_phrase(arp, n, chord) == phrase &&
                arp->inputs[n].order > previous &&
                (next == THY_ARP_INPUTS || arp->inputs[n].order < order)) {
                next = n;
                order = arp->inputs[n].order;
            }
        if (next == THY_ARP_INPUTS) break;
        previous = order;
        for (unsigned note = 0u; note < 128u; ++note)
            if (has(arp->inputs[next].pitches, note) && !has(seen, note)) {
                put(seen, note);
                notes[count++] = (uint8_t)note;
            }
    }
    return count;
}

static int shifted_pitch(const thy_arp_config *config, unsigned pitch, int offset)
{
    /* Preserve chromatic displacement from the scale degree immediately below
     * the source pitch. Offset zero therefore preserves every original tone. */
    int relative = (int)pitch - config->root;
    int octave = relative / 12;
    int pc = relative % 12;
    if (pc < 0) { pc += 12; --octave; }
    unsigned index = 0u;
    while (index + 1u < config->scale_count && config->scale[index + 1u] <= pc) ++index;
    int accidental = pc - config->scale[index];
    int degree = octave * config->scale_count + (int)index + offset;
    octave = degree / config->scale_count;
    int target = degree % config->scale_count;
    if (target < 0) { target += config->scale_count; --octave; }
    return config->root + octave * 12 + config->scale[target] + accidental;
}

static void failure(thy_arp *arp, int error)
{
    arp->failures = add_count(arp->failures, 1u);
    arp->last_error = (uint8_t)error;
}

static int emit(thy_arp *arp, thy_core *core, unsigned phrase,
    const uint8_t *notes, unsigned count, uint64_t off_tick)
{
    if(!count)return THY_OK;
    if (count > THY_GENERATED_OWNER_COUNT - core->generated_count ||
        count > THY_GENERATED_OWNER_COUNT - arp->output_count) return THY_OWNER_LIMIT;
    if (count > UINT32_MAX - core->generated_last_handle) return THY_HANDLE_EXHAUSTED;
    int prepared=thy_core_prepare_generated(core,arp->config.channel,arp->config.destinations);
    if(prepared!=THY_OK)return prepared;
    unsigned voices = 0u;
    for (unsigned n = 0u; n < count; ++n)
        for (unsigned destination = 0u; destination < THY_DEST_COUNT; ++destination)
            if (arp->config.destinations & (1u << destination)) {
                uint16_t key = thy_core_key((uint8_t)destination, arp->config.channel, notes[n]);
                if (!core->refs[key] && !thy_core_deferred(core, key)) ++voices;
            }
    if ((uint32_t)core->queue_count + thy_core_reserved(core) + 2u * voices > THY_QUEUE_CAPACITY)
        return THY_QUEUE_FULL;
    /* Serialized caller + deduplicated pitches + aggregate admission prove that
     * every following single-pitch acquisition succeeds; no partial rollback or
     * large copy of the voice ledger is needed. */
    unsigned slot = 0u;
    for (unsigned n = 0u; n < count; ++n) {
        while (arp->outputs[slot].handle) ++slot;
        thy_generated_handle handle = 0u;
        int result = thy_core_generated_acquire(core, arp->source, arp->run, notes[n],
            arp->config.channel, arp->config.destinations, arp->phrases[phrase].velocity, &handle);
        if (result != THY_OK) return result;
        arp->outputs[slot] = (thy_arp_output){.off_tick=off_tick,.handle=handle,.phrase=(uint8_t)phrase};
        ++arp->output_count;
    }
    arp->attacks = add_count(arp->attacks, 1u);
    return THY_OK;
}

int thy_arp_advance(thy_arp *arp, thy_core *core, uint64_t tick, uint32_t run, int ready)
{
    int result = prepare(arp, core, tick, run, ready);
    if (result != THY_OK) return result;
    if (!ready || !run || !arp->config.enabled) return THY_OK;
    for (unsigned n = 0u; n < THY_GENERATED_OWNER_COUNT; ++n)
        if (arp->outputs[n].handle &&
            (arp->outputs[n].off_tick <= tick || !thy_core_generated_find(core, arp->outputs[n].handle)))
            release_output(arp, core, n);
    uint32_t rate = arp->config.rate_ticks;
    uint32_t long_step = rate + rate * arp->config.swing_percent / 100u;
    for (unsigned n = 0u; n < THY_ARP_PHRASES; ++n) {
        thy_arp_phrase *phrase = &arp->phrases[n];
        if (!phrase->active || tick < phrase->anchor) continue;
        uint64_t elapsed = tick - phrase->anchor;
        uint64_t pair = elapsed / (2u * rate);
        unsigned odd = elapsed % (2u * rate) >= long_step;
        uint64_t ordinal = 2u * pair + odd;
        if (ordinal < phrase->ordinal) continue;
        arp->skipped = add_count(arp->skipped, ordinal - phrase->ordinal);
        phrase->ordinal = ordinal + 1u;
        uint64_t onset = phrase->anchor + pair * (2u * rate) + (odd ? long_step : 0u);
        uint32_t duration = (odd ? 2u * rate - long_step : long_step) * arp->config.gate_percent / 100u;
        if (duration == 0u) duration = 1u;
        if (!(arp->config.step_mask & (1u << (ordinal % 8u))) ||
            UINT64_MAX - onset < duration || onset + duration <= tick) {
            arp->skipped = add_count(arp->skipped, 1u);
            continue;
        }
        release_phrase(arp, core, n);
        uint8_t notes[128];
        unsigned count = ordered_pitches(arp, n, notes);
        if (count == 0u) continue;
        if (arp->config.type == THY_ARP_NOTES) {
            notes[0] = notes[pattern_index(ordinal, count, arp->config.pattern)];
            count = 1u;
        } else {
            int offset = arp->config.offsets[pattern_index(ordinal, 8u, arp->config.pattern)];
            uint64_t seen[2] = {0u, 0u};
            unsigned target_count = 0u;
            for (unsigned p = 0u; p < count; ++p) {
                int pitch = shifted_pitch(&arp->config, notes[p], offset);
                if (pitch >= 0 && pitch <= 127 && !has(seen, (unsigned)pitch)) {
                    put(seen, (unsigned)pitch);
                    notes[target_count++] = (uint8_t)pitch;
                }
            }
            count = target_count;
        }
        if (count) {
            result = emit(arp, core, n, notes, count, onset + duration);
            if (result != THY_OK) failure(arp, result);
        }
    }
    return THY_OK;
}

int thy_arp_configure(thy_arp *arp, thy_core *core, const thy_arp_config *config,
    uint64_t tick, uint32_t run, int ready)
{
    if (!thy_arp_config_valid(config)) return THY_INVALID_ARGUMENT;
    int result = prepare(arp, core, tick, run, ready);
    if (result != THY_OK) return result;
    int timing_changed = arp->config.rate_ticks != config->rate_ticks ||
        arp->config.swing_percent != config->swing_percent || arp->config.start != config->start;
    arp->config = *config;
    if (!config->enabled || !ready || !run) { thy_arp_clear(arp, core); return THY_OK; }
    if (timing_changed)
        for (unsigned n = 0u; n < THY_ARP_PHRASES; ++n)
            if (arp->phrases[n].active) {
                release_phrase(arp, core, n);
                anchor_phrase(arp, &arp->phrases[n], tick);
            }
    rebuild(arp, core, tick);
    return thy_arp_advance(arp, core, tick, run, ready);
}

int thy_arp_press(thy_arp *arp, thy_core *core, uint8_t input_id, uint8_t role,
    const int16_t *notes, size_t count, uint8_t velocity,
    uint64_t tick, uint32_t run, int ready)
{
    if (input_id >= THY_ARP_INPUTS || role > 1u || count > THY_MAX_NOTES ||
        (!notes && count) || velocity < 1u || velocity > 127u) return THY_INVALID_ARGUMENT;
    for (size_t n = 0u; n < count; ++n)
        if (notes[n] < 0 || notes[n] > 127) return THY_INVALID_ARGUMENT;
    int result = prepare(arp, core, tick, run, ready);
    if (result != THY_OK) return result;
    if (!arp->config.enabled || !ready || !run || arp->inputs[input_id].held || count == 0u)
        return thy_arp_advance(arp, core, tick, run, ready);
    if (arp->press_order == UINT64_MAX) return THY_OWNER_LIMIT;
    thy_arp_input *input = &arp->inputs[input_id];
    *input = (thy_arp_input){.order = ++arp->press_order, .held = 1u, .role = role, .velocity = velocity};
    for (size_t n = 0u; n < count; ++n) put(input->pitches, (unsigned)notes[n]);
    ++arp->held_count;
    rebuild(arp, core, tick);
    return thy_arp_advance(arp, core, tick, run, ready);
}

int thy_arp_release(thy_arp *arp, thy_core *core, uint8_t input_id,
    uint64_t tick, uint32_t run, int ready)
{
    if (input_id >= THY_ARP_INPUTS) return THY_INVALID_ARGUMENT;
    int result = prepare(arp, core, tick, run, ready);
    if (result != THY_OK) return result;
    if (!arp->inputs[input_id].held) return thy_arp_advance(arp, core, tick, run, ready);
    unsigned previous_chord = selected_chord(arp);
    thy_arp_phrase previous_phase = arp->phrases[previous_chord];
    arp->inputs[input_id] = (thy_arp_input){0};
    --arp->held_count;
    /* A still-held melody pool continues when its final chord target vanishes.
     * The newly played chord case remains a genuinely new independent phrase. */
    if (arp->config.grouping == THY_ARP_HYBRID && !arp->config.restart_on_change &&
        previous_chord == input_id && previous_phase.active &&
        selected_chord(arp) == THY_ARP_POOL && !arp->phrases[THY_ARP_POOL].active)
        arp->phrases[THY_ARP_POOL] = previous_phase;
    rebuild(arp, core, tick);
    return thy_arp_advance(arp, core, tick, run, ready);
}
