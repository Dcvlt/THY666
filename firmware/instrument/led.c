/* SPDX-License-Identifier: GPL-3.0-or-later
 * Registered-pad feedback, independently implemented from the electrical index
 * contract. Scales handler 08081a84 calls 0808787c(global electrical ID).
 * That helper writes color slot ID+7; encoder 08092440 emits slots 7..102 in
 * order, so electrical ID equals stock LED stream slot. Shared note colors
 * follow the default pitch-class component ratios; the owner photograph confirms GRB component order. Components retain
 * their original stream order.
 * The device-tested SPI/DMA framing/shutdown driver owns all electrical I/O.
 */
#include "led.h"
#include "../diagnostic/board.h"

static thy_instrument_lights_status lights;
static uint32_t last_update, last_renew, observed_errors;
static uint8_t selection_valid;
static thy_lighting note_lights;
static uint8_t accepted_components[THY_PAD_COUNT][3];
static uint32_t last_render;
static uint8_t render_valid;
static thy_instrument_lights_override_fn color_override;
static void *color_override_context;

void thy_instrument_lights_init(void)
{
    lights = (thy_instrument_lights_status){0};
    last_update = last_renew = observed_errors = 0u;
    selection_valid = 0u;
    thy_lighting_init(&note_lights);
    last_render = 0u;
    render_valid = 0u;
    color_override=NULL;color_override_context=NULL;
    for (unsigned i = 0u; i < THY_PAD_COUNT; ++i)
        for (unsigned c = 0u; c < 3u; ++c) accepted_components[i][c] = 0u;
}

const thy_instrument_lights_status *thy_instrument_lights_report(void) { return &lights; }
int thy_instrument_lights_copy_rgb(const thy_instrument *instrument,uint8_t rgb[288])
{
    if(!instrument||!rgb)return 0;
    for(unsigned electrical=0;electrical<THY_PAD_COUNT;++electrical){
        unsigned logical=instrument->mapping[electrical];if(logical>=THY_PAD_COUNT)return 0;
        /* The tested wire order is GRB; the companion wire names actual RGB. */
        rgb[logical*3u]=selection_valid?accepted_components[electrical][1]:0u;
        rgb[logical*3u+1u]=selection_valid?accepted_components[electrical][0]:0u;
        rgb[logical*3u+2u]=selection_valid?accepted_components[electrical][2]:0u;
    }
    return 1;
}
void thy_instrument_lights_set_override(thy_instrument_lights_override_fn fn,void *context)
{color_override=fn;color_override_context=context;render_valid=0u;}

/* Presentation scratch must not overlap the foreground sequencer take stack. */
__attribute__((noinline)) void thy_instrument_lights_task(const thy_instrument *instrument, uint32_t now)
{
    const diag_led_report_t *driver = board_led_report();
    if (lights.faulted != 0u) return;
    if (driver->state == DIAG_LED_FAULT || driver->errors != observed_errors) {
        observed_errors = driver->errors;
        lights.faulted = 1u;
        ++lights.errors;
        lights.selected[0] = lights.selected[1] = lights.selected[2] = 0u;
        /* One bounded best effort only; a failed OFF is never retried here. */
        lights.last_result = board_led_request(DIAG_LED_OFF, 0u, 0u);
        return;
    }
    uint32_t desired[3] = {0u, 0u, 0u};
    uint8_t components[THY_PAD_COUNT][3] = {{0}};
    int enabled = instrument != NULL && instrument->input_enabled != 0u;
    int musical = instrument != NULL && instrument->core != NULL && instrument->layout != NULL;
    if (musical) {
        /* Keep all96 voicing previews out of the fast acquisition path.
         * Global OFF and a release to an entirely dark setup bypass this
         * bound, as does fault handling above. */
        int potentially_lit = instrument->light_idle != 0u ||
            instrument->engine_kind == THY_PLAY_ENGINE_HARMONY;
        if (!potentially_lit)
            for (unsigned i = 0u; i < THY_PAD_COUNT; ++i)
                potentially_lit |= instrument->sensors[i].held &&
                                   !instrument->sensors[i].blocked;
        if (render_valid && enabled && instrument->lights_enabled && potentially_lit &&
            (uint32_t)(now - last_render) < THY_INSTRUMENT_LIGHT_INTERVAL_MS) return;
        uint32_t logical_colors[THY_PAD_COUNT];
        thy_lighting_config config;
        thy_lighting_defaults(&config);
        config.enabled = instrument->lights_enabled;
        config.idle_brightness = instrument->light_idle;
        config.held_brightness = instrument->light_held;
        config.fade_ms = instrument->light_fade_ms;
        config.chord_color_mode = instrument->light_chord_mode;
        config.palette = instrument->light_palette;
        config.key_contrast = instrument->light_key_contrast;
        config.key_dim_percent = instrument->light_key_contrast_percent;
        config.held_style = instrument->light_held_style;
        config.held_feedback = instrument->light_held_feedback;
        config.right_illumination = instrument->performance.melody_layout == THY_LAYOUT_IN_KEY_LIT ? THY_LIGHTING_RIGHT_IN_KEY : THY_LIGHTING_RIGHT_ALL;
        if (thy_lighting_render(&note_lights, instrument, &config, now,
                                logical_colors) != THY_OK) {
            lights.last_result = DIAG_INVALID;
            lights.faulted = 1u;
            ++lights.errors;
            (void)board_led_request(DIAG_LED_OFF, 0u, 0u);
            return;
        }
        if(color_override)(void)color_override(color_override_context,now,logical_colors);
        last_render = now;
        render_valid = 1u;
        for (unsigned electrical = 0u; electrical < THY_PAD_COUNT; ++electrical) {
            uint32_t color = logical_colors[instrument->mapping[electrical]];
            for (unsigned c = 0u; c < 3u; ++c) {
                unsigned component = (color >> (16u - c * 8u)) & 255u;
                if (component > DIAG_LED_BRIGHTNESS) component = DIAG_LED_BRIGHTNESS;
                components[electrical][c] = (uint8_t)component;
            }
            if ((components[electrical][0] | components[electrical][1] |
                 components[electrical][2]) != 0u)
                desired[electrical / 32u] |= UINT32_C(1) << (electrical & 31u);
        }
    } else if (enabled) {
        /* Bring-up compatibility for an instrument without a musical layout.
         * This path remains useful for isolated sensor/driver regressions. */
        for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad)
            if (instrument->sensors[pad].held && !instrument->sensors[pad].blocked) {
                desired[pad / 32u] |= UINT32_C(1) << (pad & 31u);
                for (unsigned c = 0u; c < 3u; ++c) components[pad][c] = DIAG_LED_BRIGHTNESS;
            }
    }
    int changed = !selection_valid;
    for (unsigned i = 0u; i < 3u; ++i) if (desired[i] != lights.selected[i]) changed = 1;
    for (unsigned i = 0u; i < THY_PAD_COUNT; ++i)
        for (unsigned c = 0u; c < 3u; ++c)
            if (components[i][c] != accepted_components[i][c]) changed = 1;
    int any = (desired[0] | desired[1] | desired[2]) != 0u;
    int renew = any && (uint32_t)(now - last_renew) >= THY_INSTRUMENT_LIGHT_RENEW_MS;
    if (!changed && !renew) return;
    /* All-OFF must not wait for the ordinary update interval. Other changed
     * selections are coalesced to at most 50 frames/sec. */
    if (selection_valid && enabled && any && (uint32_t)(now - last_update) <
        THY_INSTRUMENT_LIGHT_INTERVAL_MS) return;
    int result = musical ? board_led_request_colors(components) : board_led_request_bitmap(desired);
    lights.last_result = result;
    if (result != DIAG_SUCCESS) {
        ++lights.errors;
        lights.faulted = 1u;
        lights.selected[0] = lights.selected[1] = lights.selected[2] = 0u;
        (void)board_led_request(DIAG_LED_OFF, 0u, 0u);
        return;
    }
    if (changed) {
        for (unsigned i = 0u; i < 3u; ++i) lights.selected[i] = desired[i];
        for (unsigned i = 0u; i < THY_PAD_COUNT; ++i)
            for (unsigned c = 0u; c < 3u; ++c) accepted_components[i][c] = components[i][c];
        ++lights.updates;
        last_update = now;
    }
    selection_valid = 1u;
    last_renew = now;
}
