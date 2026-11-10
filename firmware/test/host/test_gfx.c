/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "gfx.h"

#include <string.h>

#include "tinytest.h"

#define W 64
#define H 16

static uint8_t bits[W / 8 * H];
static gfx_t g;

static void fresh(void)
{
    gfx_init(&g, bits, W, H);
    gfx_clear(&g, false);
}

static int count_black(void)
{
    int n = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            n += gfx_get(&g, x, y);
        }
    }
    return n;
}

static void test_clear_and_pixel(void)
{
    fresh();
    CHECK(bits[0] == 0xFF);
    CHECK(count_black() == 0);

    gfx_pixel(&g, 0, 0, true);
    CHECK(bits[0] == 0x7F); /* MSB is the left pixel */
    CHECK(gfx_get(&g, 0, 0));
    gfx_pixel(&g, 7, 1, true);
    CHECK(bits[W / 8] == 0xFE);
    gfx_pixel(&g, 0, 0, false);
    CHECK(!gfx_get(&g, 0, 0));

    /* out of range: ignored, not written */
    gfx_pixel(&g, -1, 0, true);
    gfx_pixel(&g, W, 0, true);
    gfx_pixel(&g, 0, H, true);
    CHECK(count_black() == 1);
    CHECK(!gfx_get(&g, W, 0));

    gfx_clear(&g, true);
    CHECK(count_black() == W * H);
}

static void test_viewport(void)
{
    fresh();
    gfx_t v;
    gfx_viewport(&g, &v, 8, 4, 16, 8);
    CHECK(v.w == 16 && v.h == 8);

    /* its 0,0 is the parent's 8,4 */
    gfx_pixel(&v, 0, 0, true);
    CHECK(gfx_get(&g, 8, 4) && gfx_get(&v, 0, 0));
    CHECK(count_black() == 1);

    /* nothing lands outside it, on any side */
    gfx_pixel(&v, -1, 0, true);
    gfx_pixel(&v, 0, -1, true);
    gfx_pixel(&v, 16, 0, true);
    gfx_pixel(&v, 0, 8, true);
    CHECK(count_black() == 1);

    /* nor is anything outside it read through it */
    gfx_pixel(&g, 7, 4, true);
    gfx_pixel(&g, 24, 4, true);
    CHECK(!gfx_get(&v, -1, 0) && !gfx_get(&v, 16, 0));
    gfx_pixel(&g, 7, 4, false);
    gfx_pixel(&g, 24, 4, false);

    /* a fill larger than the viewport is cut to it */
    gfx_fill(&v, -100, -100, 1000, 1000, true);
    CHECK(count_black() == 16 * 8);
    CHECK(gfx_get(&g, 23, 11) && !gfx_get(&g, 24, 11) && !gfx_get(&g, 23, 12));

    /* clear is the viewport's, the rest of the buffer stays */
    gfx_clear(&g, true);
    gfx_clear(&v, false);
    CHECK(count_black() == W * H - 16 * 8);

    /* a viewport in a viewport: origins add up, the outer one still clips */
    fresh();
    gfx_t inner;
    gfx_viewport(&v, &inner, 12, 4, 100, 100);
    gfx_clear(&inner, true);
    CHECK(count_black() == 4 * 4);
    CHECK(gfx_get(&g, 20, 8) && gfx_get(&g, 23, 11) && !gfx_get(&g, 19, 8));

    /* hanging over the parent's top left: only the part inside exists,
     * and the view's 0,0 is off the buffer */
    fresh();
    gfx_t corner;
    gfx_viewport(&g, &corner, -4, -2, 12, 6);
    gfx_clear(&corner, true);
    CHECK(count_black() == 8 * 4);
    CHECK(gfx_get(&g, 0, 0) && gfx_get(&g, 7, 3) && !gfx_get(&g, 8, 0) && !gfx_get(&g, 0, 4));
    CHECK(!gfx_get(&corner, 0, 0) && gfx_get(&corner, 4, 2));

    /* entirely outside the parent: draws nothing */
    fresh();
    gfx_t gone;
    gfx_viewport(&g, &gone, W + 10, 0, 8, 8);
    gfx_clear(&gone, true);
    CHECK(count_black() == 0);
}

static void test_fill_frame(void)
{
    fresh();
    gfx_fill(&g, 2, 3, 4, 5, true);
    CHECK(count_black() == 20);
    CHECK(gfx_get(&g, 2, 3) && gfx_get(&g, 5, 7));
    CHECK(!gfx_get(&g, 6, 7) && !gfx_get(&g, 5, 8));

    fresh();
    gfx_frame(&g, 0, 0, 10, 10, 2);
    CHECK(count_black() == 100 - 36);
    CHECK(gfx_get(&g, 1, 5) && gfx_get(&g, 8, 5));
    CHECK(!gfx_get(&g, 2, 2) && !gfx_get(&g, 5, 5));
}

static void test_bitmap(void)
{
    fresh();
    /* 10x2, rows padded to 2 bytes: black at x=0 and x=9 in row 0, white row 1 */
    const uint8_t bits[] = { 0x7F, 0xBF, 0xFF, 0xFF };
    gfx_bitmap(&g, 3, 1, 10, 2, bits);
    CHECK(gfx_get(&g, 3, 1) && gfx_get(&g, 12, 1));
    CHECK(count_black() == 2);
    /* white pixels of the bitmap overwrite black below them */
    gfx_fill(&g, 0, 0, W, H, true);
    gfx_bitmap(&g, 0, 0, 10, 2, bits);
    CHECK(count_black() == W * H - 18);
    /* clipped at the edge, no write outside */
    fresh();
    gfx_bitmap(&g, W - 4, H - 1, 10, 2, bits);
    CHECK(count_black() == 1);
}

static void test_text_square(void)
{
    /* scale 5: strokes 5, gaps 6, the 27 px square of the voc glyphs */
    CHECK(gfx_text_sq_height(5) == 27);
    CHECK(gfx_text_sq_width("0", 5) == 27);
    CHECK(gfx_text_sq_width("l", 5) == 5);           /* one stroke */
    CHECK(gfx_text_sq_width("1", 5) == 11);          /* flag and stem: 6 + 5, as in voc */
    CHECK(gfx_text_sq_width("00", 5) == 27 + 5 + 27);
    CHECK(gfx_text_sq_width_ex("00", 5, 10) == 64);
    CHECK(gfx_text_sq_width("a b", 5) == 27 + 5 + 11 + 5 + 27); /* space is stroke + gap */

    /* '0' at scale 1 (strokes 1, gaps 1 -> 5 x 5): a ring */
    fresh();
    int w = gfx_text_sq(&g, 0, 0, "0", 1, true);
    CHECK(w == 5);
    CHECK(count_black() == 16);
    CHECK(gfx_get(&g, 0, 0) && gfx_get(&g, 4, 4) && !gfx_get(&g, 2, 2));

    /* 'e' as in "error": the middle bar stops at the centre stroke */
    fresh();
    gfx_text_sq(&g, 0, 0, "e", 1, true);
    CHECK(gfx_get(&g, 2, 2) && !gfx_get(&g, 4, 2) && gfx_get(&g, 4, 1) && !gfx_get(&g, 4, 3));

    /* 'N' has its diagonal: the centre pixel is ink, the stems too */
    fresh();
    gfx_text_sq(&g, 0, 0, "N", 1, true);
    CHECK(gfx_get(&g, 2, 2) && gfx_get(&g, 0, 2) && gfx_get(&g, 4, 2) && !gfx_get(&g, 2, 0));

    /* white ink on black */
    gfx_clear(&g, true);
    gfx_text_sq(&g, 0, 0, "l", 1, false);
    CHECK(count_black() == W * H - 5);

    /* unknown draws '?' and never reads past the string */
    fresh();
    gfx_text_sq(&g, 0, 0, "\xD0\x96", 1, true);
    int q = count_black();
    fresh();
    gfx_text_sq(&g, 0, 0, "?", 1, true);
    CHECK(q == count_black() && q > 0);
}

/* A two-glyph bitmap font by hand: 'I' is a 1x3 bar, '?' a 2x2 block,
 * as tools/fontgen would emit them. */
static const uint8_t tiny_bits[] = { 0x80, 0x80, 0x80, 0xC0, 0xC0 };
static const gfx_glyph_t tiny_glyphs[] = {
    { '?', 2, 2, 0, 2, 3, 3 },
    { 'I', 1, 3, 1, 3, 3, 0 },
};
static const gfx_font_t tiny = { .ascent = 3, .descent = 1, .glyphs = tiny_glyphs, .count = 2,
                                 .bits = tiny_bits };

static void test_text_font(void)
{
    fresh();
    CHECK(gfx_font_height(&tiny) == 4);
    CHECK(gfx_text_font_width(&tiny, "II") == 6);
    CHECK(gfx_text_font_width(&tiny, "x") == 3);        /* unknown -> '?' */

    /* baseline at y = 5: the bar occupies rows 2..4 at x = 1 (left bearing 1) */
    int adv = gfx_text_font(&g, 0, 5, &tiny, "I", true);
    CHECK(adv == 3);
    CHECK(count_black() == 3);
    CHECK(gfx_get(&g, 1, 2) && gfx_get(&g, 1, 4) && !gfx_get(&g, 1, 5) && !gfx_get(&g, 0, 3));

    /* unknown code points draw the '?' block, and the pen advances */
    fresh();
    gfx_text_font(&g, 0, 5, &tiny, "\xD0\x96I", true);
    CHECK(gfx_get(&g, 0, 3) && gfx_get(&g, 1, 4) && gfx_get(&g, 4, 2));
    CHECK(count_black() == 7);
}

int main(void)
{
    test_clear_and_pixel();
    test_fill_frame();
    test_viewport();
    test_bitmap();
    test_text_square();
    test_text_font();
    REPORT();
}
