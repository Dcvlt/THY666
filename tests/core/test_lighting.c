/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../core/thy_lighting.h"

static thy_core core, core_before;
static thy_instrument instrument;
static thy_instrument instrument_before;
static thy_instrument_pad pads[THY_PAD_COUNT];
static thy_lighting lights, before;
static thy_lighting_config config;
static uint32_t frame[THY_PAD_COUNT];
static const int16_t degrees[] = {0,2,4,5,7,9,11};

static void setup(void)
{
    memset(pads, 0, sizeof pads);
    pads[0] = (thy_instrument_pad){{60,64,67}, 7u, 42u};
    pads[48] = (thy_instrument_pad){{60}, 1u, 19u};
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    assert(thy_instrument_set_layout(&instrument, pads, 60, degrees, 7) == THY_OK);
    thy_lighting_init(&lights);
    thy_lighting_defaults(&config);
    config.fade_ms = 0u;
    config.key_contrast = 0u; /* Existing cases isolate hue/ownership. */
}

static void render(uint32_t now)
{
    core_before = core;
    instrument_before = instrument;
    assert(thy_lighting_render(&lights, &instrument, &config, now, frame) == THY_OK);
    assert(memcmp(&core_before, &core, sizeof core) == 0);
    assert(memcmp(&instrument_before, &instrument, sizeof instrument) == 0);
}

static void idle_notes_metadata_and_ranges(void)
{
    setup();
    render(0);
    assert(frame[0] == 0x000300u && frame[48] == 0x000300u);
    assert(lights.source_color[0] == 42u && lights.source_color[48] == 19u);
    for (unsigned i = 0; i < THY_PAD_COUNT; ++i)
        if (i != 0 && i != 48) assert(frame[i] == 0u);
    assert(thy_lighting_note_color(60) == 0x00ff00u);
    assert(thy_lighting_note_color(62) == 0x74ff00u);
    assert(thy_lighting_note_color(128) == 0u);
    assert(thy_lighting_note_color_palette(60, THY_LIGHTING_SPECTRUM) == 0x00ff00u);
    assert(thy_lighting_note_color_palette(60, 2) == 0u);
    /* Every chromatic note remains visible at default dim brightness. */
    config.idle_brightness = 3u;
    for (unsigned pc = 0u; pc < 12u; ++pc) {
        pads[48].notes[0] = (int16_t)(60u + pc);
        render(pc + 1u);
        assert(frame[48] != 0u);
    }
    pads[48].notes[0] = 0; /* MIDI zero is active, not an empty sentinel. */
    render(20);
    assert(frame[48] == 0x000300u);
    assert(thy_instrument_set_route(&instrument, 0, 0, THY_ROUTE_DIN, 127) == THY_OK);
    render(21);
    assert(frame[0] == 0u); /* Cannot send the previewed voicing: dark. */
    puts("lighting idle note colors, opaque IDs, MIDI zero, silence and range passed");
}

static void generated_and_transposed_notes(void)
{
    setup();
    instrument.performance.melody_layout = 1u;
    config.idle_brightness = 255u;
    render(0);
    assert(frame[48] == thy_lighting_note_color(60));
    assert(frame[52] == thy_lighting_note_color(61));
    assert(frame[56] == thy_lighting_note_color(62));
    assert(lights.source_color[52] == 0u); /* Original data remains untouched. */
    assert(thy_instrument_set_route(&instrument, 1, 1, THY_ROUTE_DIN, 2) == THY_OK);
    render(1);
    assert(frame[48] == thy_lighting_note_color(62));
    instrument.performance.melody_layout = 2u;
    render(2);
    assert(frame[52] == thy_lighting_note_color(66)); /* Scale thirds+transpose. */
    instrument.performance.melody_layout = 3u;
    render(3);
    assert(frame[52] == thy_lighting_note_color(67));
    instrument.performance.melody_octave = -5;
    render(4);
    assert(frame[49] == 0u); /* Generated row below valid MIDI range. */
    puts("lighting generated layouts use actual pitch classes and transpose passed");
}

static void held_owner_snapshot_and_mapping(void)
{
    setup();
    config.idle_brightness = 255u;
    config.held_brightness = 128u;
    uint8_t mapping[THY_PAD_COUNT];
    for (unsigned i = 0; i < THY_PAD_COUNT; ++i) mapping[i] = (uint8_t)i;
    mapping[0] = 95u; mapping[95] = 0u;
    assert(thy_instrument_set_mapping(&instrument, mapping) == THY_OK);
    assert(thy_core_press(&core, 95, pads[0].notes, 3, 0, 0, THY_ROUTE_DIN, 96) == THY_OK);
    instrument.sensors[95].held = 1u;
    render(0);
    assert(frame[0] == 0x008000u && frame[95] == 0u);
    pads[0].notes[0] = 62; pads[0].notes[1] = 66; pads[0].notes[2] = 69;
    assert(thy_instrument_set_route(&instrument, 0, 7, THY_ROUTE_USB0, 5) == THY_OK);
    render(1);
    assert(frame[0] == 0x008000u); /* Still the sounding C, not future G. */
    assert(thy_core_release(&core, 95) == THY_OK);
    instrument.sensors[95].held = 0u;
    render(2);
    assert(frame[0] == thy_lighting_note_color(67));
    instrument.sensors[95].blocked = 1u;
    render(3);
    assert(frame[0] == 0u);
    /* An intentionally silent held owner stays dark if a new layout assigns
     * a pitch before its physical release. */
    assert(thy_core_press(&core, 1, NULL, 0, 0, 0, THY_ROUTE_DIN, 96) == THY_OK);
    instrument.sensors[1].held = 1u;
    pads[1] = (thy_instrument_pad){{64}, 1u, 11u};
    render(4);
    assert(frame[1] == 0u);
    /* A release followed by a different press can happen entirely between
     * lighting frames. The new owner, not a stale observed-held edge, wins. */
    setup();
    config.idle_brightness = config.held_brightness = 255u;
    assert(thy_core_press(&core, 0, pads[0].notes, 3, 0, 0, THY_ROUTE_DIN, 96) == THY_OK);
    instrument.sensors[0].held = 1u;
    render(0);
    assert(frame[0] == thy_lighting_note_color(60));
    pads[0].notes[0] = 62; pads[0].notes[1] = 66; pads[0].notes[2] = 69;
    render(1);
    assert(frame[0] == thy_lighting_note_color(60)); /* Existing owner is C. */
    assert(thy_core_release(&core, 0) == THY_OK);
    instrument.sensors[0].held = 0u;
    assert(thy_core_press(&core, 0, pads[0].notes, 3, 0, 0, THY_ROUTE_DIN, 96) == THY_OK);
    instrument.sensors[0].held = 1u;
    render(2);
    assert(frame[0] == thy_lighting_note_color(62)); /* New owner is D. */
    /* Browser pointers use the same musical core directly, without invented
     * ADC samples. Their owners receive held brightness and clean release. */
    setup();
    render(0);
    assert(frame[0] == 0x000300u && !instrument.sensors[0].held);
    assert(thy_core_press(&core, 0, pads[0].notes, 3, 0, 0, THY_ROUTE_DIN, 96) == THY_OK);
    render(1);
    assert(frame[0] == 0x000800u && !instrument.sensors[0].held);
    assert(thy_core_release(&core, 0) == THY_OK);
    render(2);
    assert(frame[0] == 0x000300u && !instrument.sensors[0].held);
    puts("lighting immutable held voicing and electrical mapping passed");
}

static void brightness_selection_off_and_fade(void)
{
    setup();
    config.selected_enabled = 1u;
    config.selected_brightness = 9u;
    render(0);
    assert(frame[0] == 0x000900u);
    config.selected_enabled = 0u;
    config.fade_ms = 100u;
    config.idle_brightness = 13u;
    render(10);
    assert(frame[0] == 0x000900u);
    render(60);
    assert(frame[0] == 0x000b00u);
    render(110);
    assert(frame[0] == 0x000d00u);
    /* Explicit global OFF bypasses animation immediately. */
    config.enabled = 0u;
    render(111);
    assert(frame[0] == 0u && frame[48] == 0u);
    config.enabled = 1u; config.fade_ms = 0u;
    render(112);
    assert(frame[0] == 0x000d00u);
    instrument.input_enabled = 0u;
    render(113);
    assert(frame[0] == 0u);
    puts("lighting independent brightness, selected state, fade and immediate OFF passed");
}

static void clock_wrap_invalid_and_configuration(void)
{
    setup();
    config.fade_ms = 100u;
    render(UINT32_MAX - 50u);
    config.idle_brightness = 13u;
    render(UINT32_MAX - 40u);
    render(9u);
    assert(frame[0] == 0x000800u);
    before = lights;
    uint32_t old[THY_PAD_COUNT]; memcpy(old, frame, sizeof old);
    assert(thy_lighting_render(&lights, &instrument, &config, 8u, frame) == THY_INVALID_ARGUMENT);
    assert(memcmp(&before, &lights, sizeof lights) == 0);
    assert(memcmp(old, frame, sizeof old) == 0);
    config.fade_ms = 2001u;
    assert(!thy_lighting_config_valid(&config));
    assert(thy_lighting_render(&lights, &instrument, &config, 10u, frame) == THY_INVALID_ARGUMENT);
    config.fade_ms = 0u;
    instrument.mapping[0] = instrument.mapping[1];
    assert(thy_lighting_render(&lights, &instrument, &config, 10u, frame) == THY_INVALID_ARGUMENT);
    assert(memcmp(&before, &lights, sizeof lights) == 0);
    puts("lighting monotonic clock, wrap, invalid configuration and atomic rejection passed");
}

static void harmonic_root_degree_and_held_styles(void)
{
    setup();
    pads[0] = (thy_instrument_pad){{57,62,65}, 7u, 2u}; /* Dm/A, degree II. */
    pads[52] = (thy_instrument_pad){{62}, 1u, 2u};
    int16_t root = -1;
    assert(thy_lighting_chord_root(&instrument, 0u, &root) == THY_OK && root == 2);
    config.idle_brightness = 255u;
    render(0u);
    assert(frame[0] == thy_lighting_note_color(62));
    assert(pads[0].notes[0] == 57); /* Color resolution never rotates voices. */
    config.chord_color_mode = THY_LIGHTING_CHORD_BASS;
    render(1u);
    assert(frame[0] == thy_lighting_note_color(57));
    config.chord_color_mode = THY_LIGHTING_CHORD_DEGREE;
    render(2u);
    assert(frame[0] == thy_lighting_note_color(2));
    config.chord_color_mode = THY_LIGHTING_CHORD_ROOT;
    assert(thy_core_press(&core, 0, pads[0].notes, 3u, 0, 0, THY_ROUTE_DIN, 96) == THY_OK);
    thy_instrument_capture_pad_color(&instrument, 0u);
    assert(instrument.held_color_valid[0] && instrument.held_color_root[0] % 12u == 2u &&
           instrument.held_color_degree[0] == 1u);
    pads[0] = (thy_instrument_pad){{59,64,67}, 7u, 3u};
    pads[56] = (thy_instrument_pad){{64}, 1u, 3u};
    assert(thy_instrument_set_route(&instrument, 0, 0, THY_ROUTE_DIN, 5) == THY_OK);
    config.held_brightness = 255u;
    render(3u);
    assert(frame[0] == thy_lighting_note_color(62)); /* Captured D, not future A. */
    config.chord_color_mode = THY_LIGHTING_CHORD_DEGREE;
    render(4u);
    assert(frame[0] == thy_lighting_note_color(2)); /* Captured degree II. */
    config.held_style = THY_LIGHTING_HELD_WHITE;
    config.held_brightness = 8u;
    render(5u);
    assert(frame[0] == 0x080808u);
    config.held_feedback = 0u;
    config.idle_brightness = 3u;
    render(6u);
    assert(frame[0] != 0x080808u); /* No white/held boost when feedback disabled. */
    assert(thy_core_release(&core, 0u) == THY_OK);
    config.chord_color_mode = THY_LIGHTING_CHORD_ROOT;
    config.idle_brightness = 255u;
    render(7u);
    assert(frame[0] == thy_lighting_note_color(69));
    /* Missing/untrusted archive metadata falls back to actual playable bass. */
    pads[0].color = 99u;
    render(8u);
    assert(frame[0] == thy_lighting_note_color(64));
    before = lights;
    config.chord_color_mode = 3u;
    assert(thy_lighting_render(&lights, &instrument, &config, 9u, frame) == THY_INVALID_ARGUMENT);
    assert(memcmp(&before, &lights, sizeof lights) == 0);
    puts("lighting harmonic root, explicit bass/degree policies and immutable held styles passed");
}

static void chromatic_contrast_and_distinct_palette(void)
{
    setup();
    instrument.performance.melody_layout = 1u;
    config.key_contrast = 1u;
    assert(config.key_dim_percent == 45u);
    render(0u);
    assert(frame[60] == 0x010100u); /* Default45% at idle3 rounds to1. */
    assert(frame[64] == 0x030300u);
    config.key_dim_percent = 0u; render(0u); assert(frame[60] == 0u && frame[64] == 0x030300u);
    config.key_dim_percent = 100u; render(0u); assert(frame[60] == frame[64]);
    config.key_dim_percent = 50u; render(0u); assert(frame[60] == 0x020200u);
    /* Transposing changes which physical columns are black keys. */
    assert(thy_instrument_set_route(&instrument, 1, 1, THY_ROUTE_DIN, 1) == THY_OK);
    render(1u);
    assert(frame[48] == 0x010200u); /* Column 1 now C#, therefore dim. */
    assert(thy_instrument_set_route(&instrument, 1, 1, THY_ROUTE_DIN, 0) == THY_OK);
    config.key_contrast = 0u;
    config.palette = THY_LIGHTING_SPECTRUM;
    render(2u);
    for (unsigned pc = 0u; pc < 12u; ++pc) {
        assert(frame[48u + 4u * pc] != 0u);
        for (unsigned prior = 0u; prior < pc; ++prior)
            assert(frame[48u + 4u * pc] != frame[48u + 4u * prior]);
    }
    config.palette = THY_LIGHTING_CLASSIC;
    config.right_illumination = THY_LIGHTING_RIGHT_IN_KEY;
    render(3u);
    assert(frame[48] != 0u && frame[52] == 0u && frame[56] != 0u);
    int16_t sharp = 61;
    assert(thy_core_press(&core, 52u, &sharp, 1u, 0, 1, THY_ROUTE_DIN, 96) == THY_OK);
    render(4u);
    assert(frame[52] != 0u); /* Outside-key notes still play and highlight. */
    assert(thy_core_release(&core, 52u) == THY_OK);
    render(5u);
    assert(frame[52] == 0u);
    config.right_illumination = THY_LIGHTING_RIGHT_ALL;
    config.key_contrast = 1u;
    instrument.performance.melody_layout = 8u;
    render(6u);
    assert(frame[60] == frame[64]); /* All-lit explicitly disables key dimming. */
    config.held_style = 2u;
    assert(!thy_lighting_config_valid(&config));
    config.held_style = 0u; config.held_feedback = 2u;
    assert(!thy_lighting_config_valid(&config));
    config.held_feedback = 1u; config.key_contrast = 2u;
    assert(!thy_lighting_config_valid(&config));
    config.key_contrast = 1u; config.key_dim_percent = 101u;
    assert(!thy_lighting_config_valid(&config));
    puts("lighting absolute black-key contrast, twelve low-brightness hues and in-key illumination passed");
}

static void explicit_context_control_colors(void)
{
    setup();
    pads[32] = pads[0]; pads[88] = pads[48];
    assert(thy_instrument_set_context_controls(&instrument, 1u) == THY_OK);
    render(0u);
    assert(frame[32] == 0x030003u && frame[36] == 0x030303u);
    assert(frame[40] == 0x010300u && frame[44] == 0x000103u);
    assert(frame[88] == frame[32] && frame[92] == frame[36]);
    assert(frame[0] == 0x000300u && frame[48] == 0x000300u);
    assert(core.sounding_count == 0u && core.queue_count == 0u);
    instrument.sensors[32].held = 1u;
    render(1u);
    assert(frame[32] == 0x080808u && !core.owners[32].held);
    instrument.sensors[32].held = 0u;
    instrument.context_held[88] = 1u;
    render(2u);
    assert(frame[88] == 0x080808u && !core.owners[88].held);
    instrument.context_held[88] = 0u;
    config.enabled = 0u;
    render(3u);
    assert(frame[32] == 0u && frame[88] == 0u);
    assert(thy_instrument_set_context_controls(&instrument, 0u) == THY_OK);
    config.enabled = 1u;
    render(4u);
    assert(frame[32] == 0x000300u && frame[88] == 0x000300u); /* Original musical pads restored. */
    puts("lighting context legends and feedback remain independent of musical ownership passed");
}

static void true_bass_and_unrelated_explicit_owner(void)
{
    setup();
    config.chord_color_mode = THY_LIGHTING_CHORD_BASS;
    config.idle_brightness = config.held_brightness = 255u;
    instrument.performance.inversion = 1u;
    instrument.performance.strum = 1u;
    instrument.performance.strum_order = THY_STRUM_DOWN;
    instrument.performance.strum_ms = 24u;
    render(0u);
    assert(frame[0] == thy_lighting_note_color(64)); /* [72,64,67]: E is bass. */
    assert(thy_instrument_press_pad(&instrument, 0u) == THY_OK);
    assert((thy_core_owner_key(&core, (uint8_t)(0), (uint8_t)(0)) & 127u) == 72u); /* Down strum starts at C5. */
    render(1u);
    assert(frame[0] == thy_lighting_note_color(64));
    pads[0] = (thy_instrument_pad){{62,66,69}, 7u, 2u};
    render(2u);
    assert(frame[0] == thy_lighting_note_color(64)); /* Captured complete voicing. */
    assert(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    render(3u);
    assert(frame[0] == thy_lighting_note_color(66)); /* Future inverted D/F#. */
    assert(!core.owners[0].held && !instrument.held_color_valid[0]);

    setup();
    pads[0].color = 1u; /* Catalog root C; explicit-note API supplies D major. */
    const int16_t direct[] = {62,66,69};
    assert(thy_core_press(&core, 0u, direct, 3u, 0, 0, THY_ROUTE_DIN, 96) == THY_OK);
    thy_instrument_capture_pad_color(&instrument, 0u);
    assert(!instrument.held_color_valid[0]);
    config.held_brightness = 255u;
    render(0u);
    assert(frame[0] == thy_lighting_note_color(62)); /* No unrelated C metadata. */
    assert(thy_instrument_release_pad(&instrument, 0u) == THY_OK);
    assert(thy_core_press(&core, 0u, NULL, 0u, 0, 0, THY_ROUTE_DIN, 96) == THY_OK);
    thy_instrument_capture_pad_color(&instrument, 0u);
    assert(!instrument.held_color_valid[0]);
    render(1u);
    assert(frame[0] == 0u);
    puts("lighting actual bass survives inversion/strum and unrelated explicit owners reject catalog metadata passed");
}

int main(void)
{
    idle_notes_metadata_and_ranges();
    generated_and_transposed_notes();
    held_owner_snapshot_and_mapping();
    brightness_selection_off_and_fade();
    clock_wrap_invalid_and_configuration();
    harmonic_root_degree_and_held_styles();
    chromatic_contrast_and_distinct_palette();
    explicit_context_control_colors();
    true_bass_and_unrelated_explicit_owner();
    puts("9 shared note lighting regression tests passed");
    return 0;
}
