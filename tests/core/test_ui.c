/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <limits.h>
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

static void reference_pixel(uint8_t *bytes,unsigned x,unsigned y,int enabled)
{
    if(x>=128u||y>=64u)return;
    uint8_t mask=(uint8_t)(1u<<(y%8u));
    if(enabled)bytes[(y/8u)*128u+x]|=mask;
    else bytes[(y/8u)*128u+x]&=(uint8_t)~mask;
}

static int test_page_rectangles_match_pixel_reference(void)
{
    static const unsigned sizes[]={0u,1u,3u,7u,8u,9u,63u,128u,129u,UINT_MAX};
    struct {uint8_t before[16],pixels[1024],after[16];} g;
    uint8_t seed[1024],expected[1024],guard[16];
    memset(&g,0xa5,sizeof g);memset(guard,0xa5,sizeof guard);
    for(unsigned n=0;n<sizeof seed;n++)seed[n]=(uint8_t)(n*37u+53u);
    for(unsigned y=0;y<=64u;y++)for(unsigned x=0;x<=128u;x++)
        for(unsigned n=0;n<sizeof sizes/sizeof sizes[0];n++)for(int enabled=0;enabled<2;enabled++){
            unsigned width=sizes[n],height=sizes[(n+x+y)%10u];
            memcpy(g.pixels,seed,sizeof seed);memcpy(expected,seed,sizeof seed);
            for(unsigned dy=0;dy<height&&y+dy<64u;dy++)
                for(unsigned dx=0;dx<width&&x+dx<128u;dx++)
                    reference_pixel(expected,x+dx,y+dy,enabled);
            thy_ui_draw_rectangle(g.pixels,x,y,width,height,enabled);
            CHECK(memcmp(g.pixels,expected,sizeof expected)==0);
            CHECK(memcmp(g.before,guard,sizeof guard)==0&&memcmp(g.after,guard,sizeof guard)==0);
        }
    memcpy(g.pixels,seed,sizeof seed);
    thy_ui_draw_rectangle(g.pixels,UINT_MAX,0u,UINT_MAX,UINT_MAX,1);
    thy_ui_draw_rectangle(g.pixels,0u,UINT_MAX,UINT_MAX,UINT_MAX,0);
    thy_ui_draw_rectangle(NULL,0u,0u,128u,64u,1);
    CHECK(memcmp(g.pixels,seed,sizeof seed)==0);
    return 1;
}

static int test_glyph_columns_preserve_clipping_and_existing_pixels(void)
{
    static const unsigned xs[]={0u,1u,120u,124u,125u,127u,128u,
        UINT_MAX-3u,UINT_MAX-2u,UINT_MAX-1u,UINT_MAX};
    uint8_t glyph_pixels[1024],expected[1024],seed[1024];
    struct {uint8_t before[16],pixels[1024],after[16];} g;
    uint8_t guard[16];memset(guard,0xa5,sizeof guard);memset(&g,0xa5,sizeof g);
    for(unsigned n=0;n<sizeof seed;n++)seed[n]=(uint8_t)(n*53u+37u);
    for(unsigned ch=1u;ch<256u;ch++){
        char label[2]={(char)ch,0};memset(glyph_pixels,0,sizeof glyph_pixels);
        thy_ui_draw_text(glyph_pixels,0u,0u,label,1u,0);
        for(unsigned yi=0;yi<70u;yi++)for(unsigned xi=0;xi<sizeof xs/sizeof xs[0];xi++)
            for(int inverse=0;inverse<2;inverse++){
                unsigned x=xs[xi],y=yi<66u?yi:UINT_MAX-(69u-yi);
                memcpy(g.pixels,seed,sizeof seed);memcpy(expected,seed,sizeof seed);
                if(x+4u<=128u)
                    for(unsigned dx=0;dx<4u;dx++)for(unsigned dy=0;dy<5u;dy++)
                        if(pixel_at(glyph_pixels,dx,dy))reference_pixel(expected,x+dx,y+dy,!inverse);
                thy_ui_draw_text(g.pixels,x,y,label,1u,inverse);
                CHECK(memcmp(g.pixels,expected,sizeof expected)==0);
                CHECK(memcmp(g.before,guard,sizeof guard)==0&&memcmp(g.after,guard,sizeof guard)==0);
            }
    }
    return 1;
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

static int test_content_rules(void)
{
    struct { uint8_t before[16], pixels[1024], after[16]; } g;
    CHECK(thy_ui_text_width("C MAJ",26)==24u);
    CHECK(thy_ui_text_width("C MAJ",1)==4u);
    CHECK(thy_ui_text_width(NULL,26)==0u);
    for(unsigned below=0;below<2;below++) {
        memset(&g,0xa5,sizeof g);
        memcpy(other,g.pixels,sizeof other);
        thy_ui_draw_rule(g.pixels,9,108,12,"C MAJ",26,(int)below);
        for(unsigned y=0;y<64;y++)for(unsigned x=0;x<128;x++) {
            if(y<8||y>10||x>=108)CHECK(pixel_at(g.pixels,x,y)==pixel_at(other,x,y));
        }
        for(unsigned x=0;x<108;x++) {
            unsigned depth=x>=10&&x<=38?0:(x==9||x==39?1:2);
            unsigned expected=below?10-depth:8+depth;
            for(unsigned y=8;y<=10;y++)CHECK(pixel_at(g.pixels,x,y)==(y==expected));
        }
        for(unsigned n=0;n<16;n++)CHECK(g.before[n]==0xa5&&g.after[n]==0xa5);
    }
    memset(frame,0,sizeof frame);
    thy_ui_draw_rule(frame,9,128,0,NULL,26,0);
    for(unsigned x=0;x<128;x++)CHECK(pixel_at(frame,x,10));
    thy_ui_draw_rule(frame,9,999,127,"LONG TITLE",999,0);
    thy_ui_draw_rule(NULL,9,128,2,"C",1,0);
    memcpy(other,frame,sizeof frame);
    thy_ui_draw_rule(frame,0,128,2,"C",1,0);
    thy_ui_draw_rule(frame,63,128,2,"C",1,0);
    CHECK(memcmp(frame,other,sizeof frame)==0);
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
    for (unsigned x = 0; x < 128u; ++x) CHECK(pixel_at(guarded.pixels,x,8)+pixel_at(guarded.pixels,x,9)+pixel_at(guarded.pixels,x,10)==1);
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
    if (!test_page_rectangles_match_pixel_reference() ||
        !test_glyph_columns_preserve_clipping_and_existing_pixels() ||
        !test_content_rules() || !test_pitch_names() || !test_font_clipping_and_page_geometry() ||
        !test_shared_live_note_rendering_and_routes() ||
        !test_menu_navigation_values_and_notice() ||
        !test_bounded_animation_wrap_and_reduce_motion()) return 1;
    printf("8 shared UI scenarios passed (%u assertions)\n", assertions);
    return 0;
}
