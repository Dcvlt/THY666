/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_lighting.h"
#include "thy_harmony_workspace.h"

static const uint32_t spectrum_palette[12] = {
    UINT32_C(0x00ff00), UINT32_C(0x55ff00), UINT32_C(0xffff00),
    UINT32_C(0xff5500), UINT32_C(0xff0000), UINT32_C(0xff0055),
    UINT32_C(0xff00ff), UINT32_C(0x5500ff), UINT32_C(0x0000ff),
    UINT32_C(0x0055ff), UINT32_C(0x00ffff), UINT32_C(0x00ff55)
};

/* Default bank0 at initialized RAM0x20000cf4; default note mapping loaded by
 * 08046cf4 from settings words0x1b0..0x1bb is identity. 0804acb8 rebuilds the
 * active pitch table; 0806961c/080697d8 select it by absolute note modulo12.
 * GRB component order is independently supported by the device photograph. */
static const uint8_t classic_components[12][3] = {
    {0,16,0}, {1,3,0}, {5,11,0}, {2,2,0}, {8,8,0}, {16,0,0},
    {2,0,2}, {0,0,16}, {0,1,4}, {0,4,12}, {0,2,3}, {0,8,8}
};

void thy_lighting_init(thy_lighting *lighting)
{
    if (lighting != NULL) *lighting = (thy_lighting){0};
}

void thy_lighting_defaults(thy_lighting_config *config)
{
    if (config != NULL) *config = (thy_lighting_config){
        .enabled = 1u, .idle_brightness = 3u, .held_brightness = 8u,
        .selected_brightness = 4u, .palette = THY_LIGHTING_CLASSIC,
        .fade_ms = 120u, .chord_color_mode = THY_LIGHTING_CHORD_ROOT,
        .key_contrast = 1u, .key_dim_percent = 45u, .held_feedback = 1u
    };
}

int thy_lighting_config_valid(const thy_lighting_config *config)
{
    return config != NULL && config->enabled <= 1u &&
        config->selected_enabled <= 1u && config->palette <= THY_LIGHTING_SPECTRUM &&
        config->chord_color_mode <= THY_LIGHTING_CHORD_DEGREE &&
        config->key_contrast <= 1u && config->key_dim_percent <= 100u && config->held_style <= THY_LIGHTING_HELD_WHITE &&
        config->held_feedback <= 1u && config->right_illumination <= THY_LIGHTING_RIGHT_IN_KEY &&
        config->fade_ms <= THY_LIGHTING_MAX_FADE_MS;
}

static uint8_t pitch_class(int note)
{
    int pc = note % 12;
    return (uint8_t)(pc < 0 ? pc + 12 : pc);
}

static uint8_t degree_color(uint8_t degree, uint8_t fallback)
{
    static const uint8_t major_degrees[7] = {0u, 2u, 4u, 5u, 7u, 9u, 11u};
    return degree < 7u ? major_degrees[degree] : fallback;
}

int thy_lighting_chord_root(const thy_instrument *instrument, uint8_t logical,
    int16_t *result)
{
    int16_t note;
    if (result == NULL ||
        thy_instrument_chord_root(instrument, logical, &note) != THY_OK)
        return THY_INVALID_ARGUMENT;
    *result = pitch_class(note);
    return THY_OK;
}

static int black_key(uint8_t pc)
{
    return pc == 1u || pc == 3u || pc == 6u || pc == 8u || pc == 10u;
}

uint32_t thy_lighting_note_color(uint8_t midi_note)
{
    return thy_lighting_note_color_palette(midi_note, THY_LIGHTING_CLASSIC);
}

uint32_t thy_lighting_note_color_palette(uint8_t midi_note, uint8_t palette)
{
    if (midi_note > 127u || palette > THY_LIGHTING_SPECTRUM) return 0u;
    unsigned pc = midi_note % 12u;
    if (palette == THY_LIGHTING_SPECTRUM) return spectrum_palette[pc];
    uint8_t peak = 0u;
    for (unsigned c = 0u; c < 3u; ++c)
        if (classic_components[pc][c] > peak) peak = classic_components[pc][c];
    uint32_t result = 0u;
    for (unsigned c = 0u; c < 3u; ++c)
        result = (result << 8u) |
            ((classic_components[pc][c] * 255u + peak / 2u) / peak);
    return result;
}

int thy_lighting_harmony_color(uint32_t color, uint8_t role, uint16_t grb[3])
{
    if (!grb || color > UINT32_C(0xffffff) || role > THY_HARMONY_SUGGESTED)
        return 0;
    /* The retained current chord stays below a physical held highlight (8),
     * so a fresh attack remains visible even on the same current chord. */
    static const uint16_t levels[] = {1792u, 2304u, 4095u, 3328u};
    for (unsigned c = 0u; c < 3u; ++c)
        grb[c] = (uint16_t)((((color >> (16u - c * 8u)) & 255u) *
            levels[role] + 127u) / 255u);
    return 1;
}

static uint32_t brightness(uint32_t rgb, uint8_t level)
{
    uint32_t result = 0u;
    for (unsigned shift = 0u; shift < 24u; shift += 8u)
        result |= ((((rgb >> shift) & 255u) * level + 127u) / 255u) << shift;
    return result;
}

static uint32_t interpolate(uint32_t from, uint32_t target, uint32_t elapsed,
    uint16_t duration)
{
    uint32_t result = 0u;
    if (duration == 0u || elapsed >= duration) return target;
    for (unsigned shift = 0u; shift < 24u; shift += 8u) {
        uint32_t first = (from >> shift) & 255u;
        uint32_t last = (target >> shift) & 255u;
        result |= ((first * (duration - elapsed) + last * elapsed + duration / 2u) /
                   duration) << shift;
    }
    return result;
}
static uint32_t overlay_rgb(const uint16_t components[3])
{
    uint32_t result=0u;
    for(unsigned c=0u;c<3u;++c){unsigned value=(components[c]+255u)/512u;
        if(value>8u)value=8u;result|=(uint32_t)value<<(16u-c*8u);}
    return result;
}

int thy_lighting_render(thy_lighting *lighting, const thy_instrument *instrument,
    const thy_lighting_config *config, uint32_t now_ms,
    uint32_t rgb[THY_PAD_COUNT])
{
    uint8_t electrical_for_logical[THY_PAD_COUNT];
    uint8_t seen[THY_PAD_COUNT] = {0};
    if (lighting == NULL || instrument == NULL || instrument->core == NULL ||
        rgb == NULL || !thy_lighting_config_valid(config) ||
        (lighting->initialized && (uint32_t)(now_ms - lighting->last_ms) >=
         UINT32_C(0x80000000))) return THY_INVALID_ARGUMENT;
    for (unsigned electrical = 0u; electrical < THY_PAD_COUNT; ++electrical) {
        unsigned logical = instrument->mapping[electrical];
        if (logical >= THY_PAD_COUNT || seen[logical]) return THY_INVALID_ARGUMENT;
        seen[logical] = 1u;
        electrical_for_logical[logical] = (uint8_t)electrical;
    }
    for (unsigned logical = 0u; logical < THY_PAD_COUNT; ++logical) {
        unsigned electrical = electrical_for_logical[logical];
        uint8_t role=thy_instrument_role_pad(instrument,(uint8_t)logical);
        if(role>=THY_PAD_COUNT)return THY_INVALID_ARGUMENT;
        const thy_instrument_sensor *sensor = &instrument->sensors[electrical];
        const thy_pad_owner *owner = &instrument->core->owners[electrical];
        /* Musical ownership is canonical in both MCU acquisition and direct
         * browser presses. A browser owner need not synthesize ADC state. */
        int held = !sensor->blocked && owner->held;
        int control = thy_instrument_context_pad(instrument, role);
        int force_off = !config->enabled || !instrument->input_enabled || sensor->blocked;
        uint32_t color = 0u, desired;
        uint8_t color_pc = 0u;
        int sounding_note = 0;
        int16_t notes[THY_MAX_NOTES];
        size_t count = 0u;
        lighting->source_color[logical] = instrument->layout != NULL ?
            instrument->layout[role].color : 0u;
        /* Cached workspace hints already own idle output. Avoid resolving all
         * 48 automatic voicings again just to overwrite their colors below.
         * Held/context feedback still takes its original snapshot path. */
        uint16_t cached_overlay[3];
        if(force_off||(!held&&!control&&instrument->light_overlay&&
            instrument->light_overlay(instrument->light_overlay_context,(uint8_t)logical,cached_overlay))){
            rgb[logical]=force_off?0u:overlay_rgb(cached_overlay);
            lighting->from[logical]=lighting->target[logical]=0u;
            lighting->changed_ms[logical]=now_ms;continue;
        }
        thy_chord_cell chord;
        int has_chord = role < 48u &&
            thy_instrument_chord_cell(instrument, role, &chord) == THY_OK;
        uint8_t chord_degree=has_chord ? chord.degree : THY_CHORD_NO_DEGREE;
        if(has_chord && instrument->chord_layout!=THY_CHORD_LAYOUT_CLASSIC)
            lighting->source_color[logical]=chord_degree==THY_CHORD_NO_DEGREE?0u:(uint8_t)(chord_degree+1u);
        if (held) {
            /* First serialized surviving owner pitch, including queued strum
             * notes. Destination/channel changes cannot recolor this owner. */
            if (owner->count != 0u || instrument->held_input_valid[electrical]) {
                sounding_note = owner->count ? (int)(thy_pad_owner_key(owner, (uint8_t)(0)) & 127u) :
                    instrument->held_input_first[electrical];
                color_pc = pitch_class(sounding_note);
                if (role < 48u && config->chord_color_mode == THY_LIGHTING_CHORD_BASS) {
                    if (!owner->count) sounding_note = instrument->held_input_bass[electrical];
                    for (unsigned n = 1u; n < owner->count; ++n) {
                        int note = (int)(thy_pad_owner_key(owner, (uint8_t)(n)) & 127u);
                        if (note < sounding_note) sounding_note = note;
                    }
                    color_pc = pitch_class(sounding_note);
                }
                if (role < 48u && instrument->held_color_valid[electrical]) {
                    if (config->chord_color_mode == THY_LIGHTING_CHORD_ROOT)
                        color_pc = pitch_class(instrument->held_color_root[electrical]);
                    else if (config->chord_color_mode == THY_LIGHTING_CHORD_DEGREE)
                        color_pc = degree_color(instrument->held_color_degree[electrical],
                            pitch_class(instrument->held_color_root[electrical]));
                }
                color = thy_lighting_note_color_palette(color_pc, config->palette);
            }
            /* The core owner itself is the immutable snapshot. A complete
             * release/re-press can occur between lighting frames, so a cached
             * held edge must never select a previous owner's color. */
        } else if (thy_instrument_preview_pad(instrument, (uint8_t)logical,
                       notes, &count) == THY_OK && count != 0u) {
            sounding_note = notes[0];
            color_pc = pitch_class(sounding_note);
            if (role < 48u) {
                if (config->chord_color_mode == THY_LIGHTING_CHORD_BASS) {
                    for (size_t n = 1u; n < count; ++n)
                        if (notes[n] < sounding_note) sounding_note = notes[n];
                    color_pc = pitch_class(sounding_note);
                } else if (has_chord &&
                    (config->chord_color_mode == THY_LIGHTING_CHORD_ROOT ||
                     (config->chord_color_mode == THY_LIGHTING_CHORD_DEGREE &&
                      chord_degree != THY_CHORD_NO_DEGREE))) {
                    /* Reuse the cell and resolved pitches already observed
                     * above. The public root helper would plan and shape the
                     * same pad again. Classic falls back to its audible bass
                     * when the authored root is absent; generated maps retain
                     * their explicit harmonic root, including rootless chords. */
                    uint8_t root_pc = pitch_class(chord.root + instrument->transpose[0]);
                    int bass = notes[0], root_present = 0;
                    for (size_t n = 0u; n < count; ++n) {
                        if (notes[n] < bass) bass = notes[n];
                        if (pitch_class(notes[n]) == root_pc) root_present = 1;
                    }
                    if (instrument->chord_layout == THY_CHORD_LAYOUT_CLASSIC && !root_present)
                        root_pc = pitch_class(bass);
                    color_pc = config->chord_color_mode == THY_LIGHTING_CHORD_DEGREE ?
                        degree_color(chord_degree, root_pc) : root_pc;
                }
            }
            color = thy_lighting_note_color_palette(color_pc, config->palette);
            /* In-key-lit changes illumination only. Notes outside the key
             * remain playable and still get a visible held highlight. */
            if (role >= 48u &&
                (config->right_illumination == THY_LIGHTING_RIGHT_IN_KEY ||
                 instrument->performance.melody_layout == 7u) &&
                !thy_instrument_note_in_key(instrument, sounding_note)) color = 0u;
        }
        if (control) {
            /* Explicit contextual controls do not pretend to be harmonic
             * pads. Fixed GRB legends: down=teal, up=white, inversion=amber,
             * bass octave=violet. Their sensor feedback owns no MIDI notes. */
            held = !sensor->blocked && (sensor->held || instrument->context_held[electrical]);
            if (control == 1 || control == 5) color = UINT32_C(0xff00ff);
            else if (control == 2 || control == 6) color = UINT32_C(0xffffff);
            else if (control == 3) color = UINT32_C(0x55ff00);
            else color = UINT32_C(0x0055ff);
            if (held && config->held_feedback) color = UINT32_C(0xffffff);
        }
        uint8_t level = held && config->held_feedback ?
                        config->held_brightness : config->idle_brightness;
        if (!held && config->selected_enabled &&
            role == instrument->selected_pad) level = config->selected_brightness;
        /* Contrast belongs to absolute pitch class, never column parity:
         * transposition, guitar frets and piano layouts retain black keys.
         * Chromatic and All notes share the same contrast switch: choosing a
         * playable layout must not silently override its saved preference. */
        if (!held && !control && role >= 48u && config->key_contrast &&
            black_key(color_pc))
            level = (uint8_t)(((unsigned)level * config->key_dim_percent + 50u) / 100u);
        if (held && config->held_feedback &&
            config->held_style == THY_LIGHTING_HELD_WHITE && color != 0u)
            color = UINT32_C(0xffffff);
        desired = force_off ? 0u : brightness(color, level);
        if (!lighting->initialized || force_off || color == 0u) {
            lighting->from[logical] = lighting->target[logical] =
                rgb[logical] = desired;
            lighting->changed_ms[logical] = now_ms;
        } else {
            uint32_t current = interpolate(lighting->from[logical],
                lighting->target[logical], now_ms - lighting->changed_ms[logical],
                config->fade_ms);
            if (desired != lighting->target[logical]) {
                lighting->from[logical] = current;
                lighting->target[logical] = desired;
                lighting->changed_ms[logical] = now_ms;
            }
            rgb[logical] = interpolate(lighting->from[logical],
                lighting->target[logical], now_ms - lighting->changed_ms[logical],
                config->fade_ms);
        }
        if (!force_off && instrument->light_overlay) {
            uint16_t components[3];
            if (instrument->light_overlay(instrument->light_overlay_context,
                (uint8_t)logical,components)) {
                rgb[logical]=overlay_rgb(components);
            }
        }
    }
    lighting->last_ms = now_ms;
    lighting->initialized = 1u;
    return THY_OK;
}
