/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_ui.h"

static thy_core core;
static thy_instrument instrument;
static thy_ui ui, comparison;
static uint8_t frame[THY_UI_FRAME_BYTES], other[THY_UI_FRAME_BYTES];
static unsigned assertions;

#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 0; \
} } while (0)

static void initialize(void)
{
    thy_core_init(&core);
    thy_instrument_init(&instrument, &core);
    thy_ui_init(&ui);
    thy_ui_init(&comparison);
    ui.animations_enabled = comparison.animations_enabled = 0u;
}

static int pixel_at(const uint8_t *bytes, unsigned x, unsigned y)
{
    return !!(bytes[y / 8u * 128u + x] & (1u << (y % 8u)));
}

static int matches_footer(const uint8_t *actual, const char *label)
{
    uint8_t expected[THY_UI_FRAME_BYTES] = {0};
    CHECK(strlen(label) <= 25u);
    thy_ui_draw_text(expected, 2u, 57u, label, 25u, 0);
    for (unsigned y = 57u; y < 62u; ++y)
        for (unsigned x = 2u; x < 127u; ++x)
            CHECK(pixel_at(actual, x, y) == pixel_at(expected, x, y));
    return 1;
}

static int test_pitch_names(void)
{
    static const struct { int midi; const char *name; } cases[] = {
        {0, "C-1"}, {1, "C#-1"}, {12, "C0"}, {60, "C4"},
        {61, "C#4"}, {127, "G9"}, {-1, ""}, {128, ""}
    };
    char name[6];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        thy_ui_note_name(cases[i].midi, name);
        CHECK(strcmp(name, cases[i].name) == 0);
    }
    thy_ui_note_name(60, NULL);
    return 1;
}

static int test_font_clipping_and_page_geometry(void)
{
    struct { uint8_t before[32], pixels[1024], after[32]; } guarded;
    initialize();
    memset(&guarded, 0xa5, sizeof guarded);
    thy_ui_render(&ui, &instrument,
        "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC", 1, 1, 0, guarded.pixels);
    for (unsigned i = 0; i < 32u; ++i) {
        CHECK(guarded.before[i] == 0xa5u);
        CHECK(guarded.after[i] == 0xa5u);
    }
    /* Independent check of the source diagnostic's C glyph (0E 11 11 11),
     * at y=1 in the page-addressed output rather than a row-major bitmap. */
    const uint8_t c[] = {0x0e, 0x11, 0x11, 0x11};
    for (unsigned x = 0; x < 4u; ++x)
        for (unsigned y = 0; y < 5u; ++y)
            CHECK(pixel_at(guarded.pixels, x + 2u, y + 1u) == !!(c[x] & (1u << y)));
    for (unsigned y = 0; y < 5u; ++y) CHECK(!pixel_at(guarded.pixels, 126, y + 1u));
    for (unsigned x = 0; x < 128u; ++x) CHECK(pixel_at(guarded.pixels, x, 9));
    thy_ui_render(&comparison, &instrument,
        "CCCCCCCCCCCCCCCCCCCCCCCCC", 1, 1, 5000, other);
    CHECK(memcmp(guarded.pixels, other, sizeof other) == 0);
    thy_ui_render(&ui, NULL, NULL, 0, 0, 100, frame);
    CHECK(memcmp(frame, other, sizeof frame) != 0);
    thy_ui_render(NULL, &instrument, "C", 0, 0, 0, frame);
    thy_ui_render(&ui, &instrument, "C", 0, 0, 0, NULL);
    memset(frame,0,sizeof frame);
    thy_ui_draw_text(frame,0u,0u,"%",1u,0);
    unsigned percent_pixels=0u;
    for(unsigned y=0;y<5u;++y)for(unsigned x=0;x<4u;++x)
        percent_pixels+=(unsigned)pixel_at(frame,x,y);
    CHECK(percent_pixels>0u); /* Storage percentages must not be blank. */
    for(unsigned x=4u;x<128u;++x)CHECK(!pixel_at(frame,x,0u));
    return 1;
}

static int test_shared_live_note_rendering_and_routes(void)
{
    initialize();
    const int16_t c4 = 60, c5 = 72;
    CHECK(thy_core_press(&core, 0, &c4, 1, 0, 0, THY_ROUTE_ALL, 96) == THY_OK);
    thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 1, frame);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(thy_core_press(&core, 0, &c4, 1, 0, 0, THY_ROUTE_DIN, 96) == THY_OK);
    thy_ui_render(&comparison, &instrument, "MAJOR C", 1, 1, 500, other);
    /* Displayed notes count pitches, independent of duplicated destinations. */
    CHECK(memcmp(frame, other, sizeof frame) == 0);
    CHECK(thy_core_release(&core, 0) == THY_OK);
    CHECK(thy_core_press(&core, 0, &c5, 1, 0, 0, THY_ROUTE_DIN, 96) == THY_OK);
    thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 501, frame);
    CHECK(memcmp(frame, other, sizeof frame) != 0);
    thy_ui_render(&comparison, &instrument, "MAJOR C", 0, 0, 501, other);
    CHECK(memcmp(frame, other, sizeof frame) != 0);
    instrument.selected_pad = 48;
    thy_ui_render(&comparison, &instrument, "MAJOR C", 1, 1, 501, other);
    CHECK(memcmp(frame, other, sizeof frame) != 0);
    return 1;
}

static int test_menu_navigation_values_and_notice(void)
{
    initialize();
    instrument.menu_open = 1;
    instrument.menu_row = 14;
    thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 0, frame);
    CHECK(pixel_at(frame, 0, 26));  /* Selected middle row is filled. */
    CHECK(!pixel_at(frame, 0, 14)); /* Adjacent row is not filled. */
    CHECK(matches_footer(frame, "> EDIT O PLAY < BACK"));
    ++instrument.response.attack_raw;
    thy_ui_render(&comparison, &instrument, "MAJOR C", 1, 1, 0, other);
    CHECK(memcmp(frame, other, sizeof frame) != 0);
    instrument.menu_editing = 1;
    thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 0, frame);
    CHECK(!pixel_at(frame, 127, 26));
    CHECK(pixel_at(frame, 126, 28));
    CHECK(matches_footer(frame, "> DONE O PLAY < BACK"));
    instrument.menu_notice = 1;
    thy_ui_render(&comparison, &instrument, "MAJOR C", 1, 1, 0, other);
    CHECK(memcmp(frame, other, sizeof frame) != 0);
    CHECK(matches_footer(other, "RELEASE PADS  O PLAY"));
    instrument.menu_notice = 0;
    instrument.menu_row = 22;
    thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 0, frame);
    CHECK(pixel_at(frame, 0, 38)); /* Last item stays inside the final window. */
    ++instrument.light_fade_ms;
    thy_ui_render(&comparison, &instrument, "MAJOR C", 1, 1, 0, other);
    CHECK(memcmp(frame, other, sizeof frame) != 0);
    /* Every current menu item can be displayed, including negative octaves. */
    instrument.performance.chord_octave = -3;
    for (unsigned row = 0; row < THY_INSTRUMENT_MENU_ROWS; ++row) {
        instrument.menu_row = (uint8_t)row;
        thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 0, frame);
        CHECK(ui.menu_open);
    }
    return 1;
}

static int test_bounded_animation_wrap_and_reduce_motion(void)
{
    initialize();
    ui.animations_enabled = 1;
    thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 0xffffffe0u, frame);
    memcpy(other, frame, sizeof frame);
    instrument.menu_open = 1;
    thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 0xfffffff0u, frame);
    CHECK(ui.transitioning);
    CHECK(memcmp(frame, other, sizeof frame) == 0); /* Slide starts at prior frame. */
    thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 0x2cu, frame);
    CHECK(ui.transitioning);
    CHECK(memcmp(frame, other, sizeof frame) != 0);
    thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 0x68u, frame);
    CHECK(!ui.transitioning);
    thy_ui_render(&comparison, &instrument, "MAJOR C", 1, 1, 0x68u, other);
    CHECK(memcmp(frame, other, sizeof frame) == 0);
    instrument.menu_open = 0;
    thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 0x69u, frame);
    CHECK(ui.transitioning);
    ui.animations_enabled = 0;
    thy_ui_render(&ui, &instrument, "MAJOR C", 1, 1, 0x6au, frame);
    thy_ui_render(&comparison, &instrument, "MAJOR C", 1, 1, 0x6au, other);
    CHECK(!ui.transitioning);
    CHECK(memcmp(frame, other, sizeof frame) == 0);
    instrument.performance.root = 2;
    thy_ui_render(&ui, &instrument, "MAJOR D", 1, 1, 0x6au, frame);
    CHECK(memcmp(frame, other, sizeof frame) != 0); /* Live changes never await animation time. */
    return 1;
}

int main(void)
{
    if (!test_pitch_names() || !test_font_clipping_and_page_geometry() ||
        !test_shared_live_note_rendering_and_routes() ||
        !test_menu_navigation_values_and_notice() ||
        !test_bounded_animation_wrap_and_reduce_motion()) return 1;
    printf("5 shared UI scenarios passed (%u assertions)\n", assertions);
    return 0;
}
