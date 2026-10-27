/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_ui.h"

/* The independently drawn diagnostic 4x5 glyphs, plus original punctuation.
 * Columns carry five low bits; lower-case labels use their upper-case glyph. */
static void glyph(char ch, uint8_t out[4])
{
    uint32_t bits;
    /* Flat alterations must not be confused with the note B on the OLED. */
    if (ch >= 'a' && ch <= 'z' && ch != 'b') ch = (char)(ch - 'a' + 'A');
    switch (ch) {
    case '0': bits=0x1f11111fu; break; case '1': bits=0x001f1200u; break;
    case '2': bits=0x1715151du; break; case '3': bits=0x1f151511u; break;
    case '4': bits=0x1f040407u; break; case '5': bits=0x1d151517u; break;
    case '6': bits=0x1d15151fu; break; case '7': bits=0x03051901u; break;
    case '8': bits=0x1f15151fu; break; case '9': bits=0x1f151517u; break;
    case 'A': bits=0x1e05051eu; break; case 'B': bits=0x0a15151fu; break;
    case 'b': bits=0x0814141fu; break;
    case 'C': bits=0x1111110eu; break; case 'D': bits=0x0e11111fu; break;
    case 'E': bits=0x1115151fu; break; case 'F': bits=0x0105051fu; break;
    case 'G': bits=0x1d15110eu; break; case 'H': bits=0x1f04041fu; break;
    case 'I': bits=0x001f0000u; break; case 'J': bits=0x0f101008u; break;
    case 'K': bits=0x110a041fu; break; case 'L': bits=0x1010101fu; break;
    case 'M': bits=0x1f02021fu; break; case 'N': bits=0x1f08041fu; break;
    case 'O': bits=0x0e11110eu; break; case 'P': bits=0x0205051fu; break;
    case 'Q': bits=0x1e19110eu; break; case 'R': bits=0x120d051fu; break;
    case 'S': bits=0x09151512u; break; case 'T': bits=0x01011f01u; break;
    case 'U': bits=0x0f10100fu; break; case 'V': bits=0x030c1807u; break;
    case 'W': bits=0x1f08081fu; break; case 'X': bits=0x110a041bu; break;
    case 'Y': bits=0x03041803u; break; case 'Z': bits=0x13151911u; break;
    case '-': bits=0x04040404u; break; case ':': bits=0x000a0000u; break;
    case '/': bits=0x01020418u; break; case '#': bits=0x0a1f0a1fu; break;
    case '+': bits=0x040e0400u; break; case '>': bits=0x00040a11u; break;
    case '<': bits=0x110a0400u; break; case '?': bits=0x02051501u; break;
    case '%': bits=0x00130419u; break;
    case '(': bits=0x00110e00u; break; case ')': bits=0x000e1100u; break;
    case '.': bits=0x00100000u; break; default: bits=0u; break;
    }
    for (unsigned i = 0; i < 4u; ++i) out[i] = (uint8_t)(bits >> (i * 8u));
}

static void pixel(uint8_t *out, unsigned x, unsigned y, int enabled)
{
    if (x >= THY_UI_WIDTH || y >= THY_UI_HEIGHT) return;
    unsigned index = y / 8u * THY_UI_WIDTH + x;
    uint8_t mask = (uint8_t)(1u << (y % 8u));
    if (enabled) out[index] |= mask;
    else out[index] &= (uint8_t)~mask;
}

static void rectangle(uint8_t *out, unsigned x, unsigned y, unsigned w,
    unsigned h, int enabled)
{
    for (unsigned dy = 0; dy < h && y + dy < THY_UI_HEIGHT; ++dy)
        for (unsigned dx = 0; dx < w && x + dx < THY_UI_WIDTH; ++dx)
            pixel(out, x + dx, y + dy, enabled);
}

static void text(uint8_t *out, unsigned x, unsigned y, const char *label,
    unsigned limit, int inverse)
{
    if (label == NULL) return;
    while (*label && limit-- && x + 4u <= THY_UI_WIDTH) {
        uint8_t columns[4];
        glyph(*label++, columns);
        for (unsigned dx = 0; dx < 4u; ++dx)
            for (unsigned dy = 0; dy < 5u; ++dy)
                if (columns[dx] & (1u << dy)) pixel(out, x + dx, y + dy, !inverse);
        x += 5u;
    }
}

static unsigned append(char *out, unsigned used, unsigned capacity, const char *value)
{
    if (value != NULL)
        while (*value && used + 1u < capacity) out[used++] = *value++;
    out[used] = 0;
    return used;
}

void thy_ui_draw_pixel(uint8_t *out, unsigned x, unsigned y, int enabled)
{ if (out != NULL) pixel(out, x, y, enabled); }
void thy_ui_draw_rectangle(uint8_t *out, unsigned x, unsigned y,
    unsigned width, unsigned height, int enabled)
{ if (out != NULL) rectangle(out, x, y, width, height, enabled); }
void thy_ui_draw_text(uint8_t *out, unsigned x, unsigned y,
    const char *label, unsigned limit, int inverse)
{ if (out != NULL) text(out, x, y, label, limit, inverse); }

static unsigned number(char *out, unsigned used, unsigned capacity, int value)
{
    char reverse[10];
    unsigned count = 0;
    uint32_t magnitude;
    if (value < 0) {
        used = append(out, used, capacity, "-");
        magnitude = (uint32_t)(-(int64_t)value);
    } else magnitude = (uint32_t)value;
    do {reverse[count++] = (char)('0' + magnitude % 10u); magnitude /= 10u;} while (magnitude);
    while (count && used + 1u < capacity) out[used++] = reverse[--count];
    out[used] = 0;
    return used;
}

static const char *pitch_class(unsigned pitch)
{
    static const char *const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[pitch % 12u];
}

void thy_ui_note_name(int note, char out[6])
{
    if (out == NULL) return;
    out[0] = 0;
    if (note < 0 || note > 127) return;
    unsigned used = append(out, 0, 6, pitch_class((unsigned)note));
    (void)number(out, used, 6, note / 12 - 1);
}

static void menu_value(const thy_instrument *state, unsigned row,
    const char *scale_name, char out[26])
{
    static const char *const orders[] = {"SERIAL", "UP", "DOWN", "ALTERNATE"};
    static const char *const layouts[] = {"STORED", "CHROMATIC", "THIRDS", "FOURTHS",
        "CONDENSED", "GUITAR", "PIANO", "IN KEY LIT", "ALL LIT"};
    static const char *const curves[] = {"LINEAR", "SOFT", "FIRM"};
    const char *label = NULL;
    int value = 0;
    out[0] = 0;
    switch (row) {
    case 0: label = scale_name; break;
    case 1: label = pitch_class(state->performance.root); break;
    case 2: value = state->performance.inversion; break;
    case 3: value = state->performance.spread; break;
    case 4: label = state->performance.strum ? "ON" : "OFF"; break;
    case 5: value = state->performance.strum_ms; break;
    case 6: label = orders[state->performance.strum_order % 4u]; break;
    case 7: label = state->performance.melody_layout < THY_LAYOUT_COUNT ?
        layouts[state->performance.melody_layout] : "UNKNOWN"; break;
    case 8: value = state->performance.chord_octave; break;
    case 9: value = state->performance.melody_octave; break;
    case 10: value = state->performance.upper_octaves; break;
    case 11: label = state->pressure_enabled ? "ON" : "OFF"; break;
    case 12: value = state->fixed_velocity; break;
    case 13: label = "SELECT"; break;
    case 14: value = state->response.attack_raw; break;
    case 15: value = state->response.release_raw; break;
    case 16: value = state->response.pressure_floor; break;
    case 17: value = state->response.pressure_full_scale; break;
    case 18: label = curves[state->response.pressure_curve % 3u]; break;
    case 19: label = state->lights_enabled ? "ON" : "OFF"; break;
    case 20: value = state->light_idle; break;
    case 21: value = state->light_held; break;
    case 22: value = state->light_fade_ms; break;
    default: label = "-"; break;
    }
    if (label != NULL) (void)append(out, 0, 26, label);
    else (void)number(out, 0, 26, value);
}

static void menu(uint8_t *out, const thy_instrument *state, const char *scale_name)
{
    static const char *const labels[] = {"SCALE", "ROOT", "INVERSION", "SPREAD",
        "STRUM", "STRUM MS", "STRUM ORDER", "MELODY LAYOUT", "CHORD OCTAVE",
        "MELODY OCTAVE", "OCTAVE STACKS", "AFTERTOUCH", "VELOCITY", "PANIC",
        "TRIGGER ADC", "RELEASE ADC", "PRESSURE FLOOR", "PRESSURE MAX", "PRESSURE CURVE",
        "NOTE LIGHTS", "IDLE LEVEL", "HELD LEVEL", "FADE MS"};
    unsigned row = state->menu_row < THY_INSTRUMENT_MENU_ROWS ? state->menu_row : 0u;
    unsigned start = row ? row - 1u : 0u;
    if (start + 3u > THY_INSTRUMENT_MENU_ROWS) start = THY_INSTRUMENT_MENU_ROWS - 3u;
    text(out, 2, 1, row >= 19u ? "LIGHTING" : row >= 14u ? "RESPONSE" : "PERFORMANCE", 18, 0);
    char value[26];
    unsigned used = number(value, 0, sizeof value, (int)row + 1);
    used = append(value, used, sizeof value, "/");
    (void)number(value, used, sizeof value, THY_INSTRUMENT_MENU_ROWS);
    text(out, 100, 1, value, 5, 0);
    rectangle(out, 0, 9, 128, 1, 1);
    for (unsigned i = 0; i < 3u; ++i) {
        unsigned item = start + i, y = 16u + i * 12u;
        int selected = item == row;
        if (selected) rectangle(out, 0, y - 2u, 128, 10, 1);
        text(out, 3, y, labels[item], 14, selected);
        menu_value(state, item, scale_name, value);
        text(out, 85, y, value, 8, selected);
        if (selected && state->menu_editing) {
            /* Editing has a stable edge marker in addition to the row fill. */
            rectangle(out, 125, y - 2u, 3, 10, 0);
            rectangle(out, 126, y, 1, 5, 1);
        }
    }
    rectangle(out, 0, 51, 128, 1, 1);
    text(out, 2, 57, state->menu_notice ? "RELEASE PADS  O PLAY" :
        state->menu_editing ? "> DONE O PLAY < BACK" : "> EDIT O PLAY < BACK", 25, 0);
}

static void play(uint8_t *out, const thy_instrument *state, const char *scale_name,
    int din_ready, int usb_ready)
{
    char line[26];
    uint8_t pitches[128] = {0};
    unsigned held = 0, notes = 0, shown = 0;
    text(out, 2, 1, scale_name != NULL ? scale_name : "THY666", 25, 0);
    rectangle(out, 0, 9, 128, 1, 1);
    unsigned used = append(line, 0, sizeof line, "ROOT ");
    used = append(line, used, sizeof line, pitch_class(state->performance.root));
    used = append(line, used, sizeof line, state->selected_pad < 48u ? "   CHORD " : "   MELODY ");
    (void)number(line, used, sizeof line, state->selected_pad < 48u ? state->selected_pad + 1 : state->selected_pad - 47);
    text(out, 2, 14, line, 25, 0);
    if (state->core != NULL) {
        for (unsigned pad = 0; pad < THY_PAD_COUNT; ++pad)
            held += state->core->owners[pad].held != 0u;
        for (unsigned key = 0; key < THY_KEY_COUNT; ++key)
            if (state->core->refs[key] || thy_core_deferred(state->core, (uint16_t)key)) pitches[key % 128u] = 1u;
    }
    for (unsigned pitch = 0; pitch < 128u; ++pitch) notes += pitches[pitch];
    used = 0;
    line[0] = 0;
    for (unsigned pitch = 0; pitch < 128u && shown < 10u; ++pitch) {
        if (!pitches[pitch]) continue;
        char name[6];
        thy_ui_note_name((int)pitch, name);
        if (shown == 5u) {
            text(out, 2, 23, line, 25, 0);
            used = 0;
            line[0] = 0;
        }
        if (used) used = append(line, used, sizeof line, " ");
        used = append(line, used, sizeof line, name);
        ++shown;
    }
    if (!shown) text(out, 2, 23, state->input_enabled ? "READY TO PLAY" : "WAIT / QUIET START", 25, 0);
    else text(out, 2, shown > 5u ? 31u : 23u, line, 25, 0);
    used = append(line, 0, sizeof line, "INV ");
    used = number(line, used, sizeof line, state->performance.inversion);
    used = append(line, used, sizeof line, " SPR ");
    used = number(line, used, sizeof line, state->performance.spread);
    used = append(line, used, sizeof line, " STR ");
    (void)append(line, used, sizeof line, state->performance.strum ? "ON" : "OFF");
    text(out, 2, 40, line, 25, 0);
    used = append(line, 0, sizeof line, din_ready ? "DIN ON" : "DIN --");
    used = append(line, used, sizeof line, usb_ready ? "  USB ON  V" : "  USB --  V");
    (void)number(line, used, sizeof line, state->fixed_velocity);
    text(out, 2, 48, line, 25, 0);
    rectangle(out, 0, 55, 128, 1, 1);
    used = number(line, 0, sizeof line, (int)held);
    used = append(line, used, sizeof line, " PADS / ");
    used = number(line, used, sizeof line, (int)notes);
    (void)append(line, used, sizeof line, " NOTES   O MENU");
    text(out, 2, 58, line, 25, 0);
}

void thy_ui_draw_play(uint8_t *out, const thy_instrument *instrument,
    const char *scale_name, int din_ready, int usb_ready)
{
    if (out != NULL && instrument != NULL)
        play(out, instrument, scale_name, din_ready, usb_ready);
}

void thy_ui_init(thy_ui *ui)
{
    if (ui != NULL) {
        *ui = (thy_ui){0};
        ui->animations_enabled = 1u;
    }
}

void thy_ui_render(thy_ui *ui, const thy_instrument *instrument,
    const char *scale_name, int din_ready, int usb_ready, uint32_t now_ms,
    uint8_t out[THY_UI_FRAME_BYTES])
{
    if (ui == NULL || out == NULL) return;
    for (unsigned i = 0; i < THY_UI_FRAME_BYTES; ++i) out[i] = 0;
    if (instrument == NULL) {
        text(out, 2, 20, "THY666 / STARTING", 25, 0);
        return;
    }
    uint8_t open = instrument->menu_open != 0u;
    if (ui->initialized && open != ui->menu_open && ui->animations_enabled) {
        for (unsigned i = 0; i < THY_UI_FRAME_BYTES; ++i) ui->transition_frame[i] = ui->previous_frame[i];
        ui->transition_started_ms = now_ms;
        ui->transitioning = 1u;
    }
    ui->menu_open = open;
    if (open) menu(out, instrument, scale_name);
    else play(out, instrument, scale_name, din_ready, usb_ready);
    uint32_t elapsed = now_ms - ui->transition_started_ms;
    if (!ui->animations_enabled || elapsed >= THY_UI_TRANSITION_MS) ui->transitioning = 0u;
    if (ui->transitioning) {
        unsigned progress = (unsigned)(elapsed * THY_UI_WIDTH / THY_UI_TRANSITION_MS);
        uint8_t row[THY_UI_WIDTH];
        for (unsigned page = 0; page < 8u; ++page) {
            unsigned offset = page * THY_UI_WIDTH;
            for (unsigned x = 0; x < THY_UI_WIDTH; ++x) row[x] = out[offset + x];
            for (unsigned x = 0; x < THY_UI_WIDTH; ++x) {
                if (open) out[offset + x] = x < THY_UI_WIDTH - progress ?
                    ui->transition_frame[offset + x + progress] : row[x + progress - THY_UI_WIDTH];
                else out[offset + x] = x >= progress ?
                    ui->transition_frame[offset + x - progress] : row[x + THY_UI_WIDTH - progress];
            }
        }
    }
    for (unsigned i = 0; i < THY_UI_FRAME_BYTES; ++i) ui->previous_frame[i] = out[i];
    ui->initialized = 1u;
}
