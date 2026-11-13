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

static void test_line(void)
{
    fresh();
    gfx_line(&g, 2, 3, 9, 3, true);
    CHECK(count_black() == 8); /* both ends are drawn */
    CHECK(gfx_get(&g, 2, 3) && gfx_get(&g, 9, 3));

    fresh();
    gfx_line(&g, 5, 10, 5, 4, true); /* backwards works the same */
    CHECK(count_black() == 7 && gfx_get(&g, 5, 4) && gfx_get(&g, 5, 10));

    fresh();
    gfx_line(&g, 0, 0, 7, 7, true);
    CHECK(count_black() == 8 && gfx_get(&g, 3, 3));

    fresh();
    gfx_line(&g, 4, 4, 4, 4, true);
    CHECK(count_black() == 1);

    /* a line that leaves the surface is cut, not wrapped */
    fresh();
    gfx_line(&g, W - 3, 0, W + 20, 0, true);
    CHECK(count_black() == 3);
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
    gfx_frame(&g, 0, 0, 10, 10, 2, true);
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
    /* scale 5: strokes 5, gaps 6, the 27 px square of the glyphs */
    CHECK(gfx_text_sq_height(5) == 27);
    CHECK(gfx_text_sq_width("0", 5) == 27);
    CHECK(gfx_text_sq_width("l", 5) == 5);           /* one stroke */
    CHECK(gfx_text_sq_width("1", 5) == 11);          /* flag and stem: 6 + 5 */
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
                                 .bits = tiny_bits, .scale = 1 };
static const gfx_font_t tiny_tripled = { .ascent = 3, .descent = 1, .glyphs = tiny_glyphs,
                                         .count = 2, .bits = tiny_bits, .scale = 3 };

/* The same dots, three pixels each. */
static void test_text_font_scaled(void)
{
    fresh();
    CHECK(gfx_font_height(&tiny_tripled) == 12 && gfx_font_ascent(&tiny_tripled) == 9);
    CHECK(gfx_font_cap_height(&tiny_tripled) == 6); /* no 'H': the '?' stands in */
    CHECK(gfx_text_font_width(&tiny_tripled, "II") == 18);
    /* baseline at y = 12: the bar is 3 x 9 pixels at x = 3..5, y = 3..11 */
    CHECK(gfx_text_font(&g, 0, 12, &tiny_tripled, "I", true) == 9);
    int black = 0;
    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < 20; x++) {
            bool inside = x >= 3 && x < 6 && y >= 3 && y < 12;
            CHECK(gfx_get(&g, x, y) == inside);
            black += gfx_get(&g, x, y);
        }
    }
    CHECK(black == 27);

    /* every size in the firmware is the same face */
    for (int id = 0; id < GFX_FONT_COUNT; id++) {
        const gfx_font_t *font = gfx_font((gfx_font_id_t)id);
        CHECK(font != NULL && gfx_text_font_width(font, "0") == 7 * font->scale);
        CHECK(gfx_font_cap_height(font) == 8 * font->scale);
    }
    CHECK(gfx_font(GFX_FONT_COUNT) == NULL && gfx_font((gfx_font_id_t)-1) == NULL);
    CHECK(gfx_font_height(gfx_font_system()) == 30);
}

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
    gfx_text_font(&g, 0, 5, &tiny, "\xE4\xB8\xADI", true);
    CHECK(gfx_get(&g, 0, 3) && gfx_get(&g, 1, 4) && gfx_get(&g, 4, 2));
    CHECK(count_black() == 7);
}

/* The degree sign is the one character beyond ASCII that is drawn: a
 * square with a hole at the top of the capitals. */
static void test_degree_sign(void)
{
    const gfx_font_t *font = gfx_font_system();
    int digits = gfx_text_font_width(font, "21");
    int with_sign = gfx_text_font_width(font, "21\xC2\xB0");
    int with_unit = gfx_text_font_width(font, "21\xC2\xB0" "C");
    CHECK(with_sign == digits + 2 + 6);          /* a gap and the sign, nothing after */
    CHECK(with_unit == with_sign + 2 + gfx_text_font_width(font, "C"));
    CHECK(gfx_text_font_width(font, "21\xC3\xA9") == digits + gfx_text_font_width(font, "?"));

    fresh();
    int baseline = H, capitals = gfx_font_cap_height(font);
    gfx_text_font(&g, 0, baseline, font, "\xC2\xB0", true);
    CHECK(gfx_get(&g, 2, baseline - capitals));         /* its corner */
    CHECK(!gfx_get(&g, 2 + 2, baseline - capitals + 2)); /* its hole */
    CHECK(gfx_get(&g, 2 + 5, baseline - capitals + 5));
    CHECK(!gfx_get(&g, 2 + 6, baseline - capitals));
}

/* A picture: two bytes of size, then a bit a cell; drawn a cell so many
 * pixels. */
static void test_picture(void)
{
    static const uint8_t PICTURE[] = { 3, 2, 0xA0, 0x40 }; /* "#.#" over ".#." */
    CHECK(gfx_picture_width(PICTURE) == 3 && gfx_picture_height(PICTURE) == 2);
    CHECK(gfx_picture_bytes(PICTURE) == (int)sizeof PICTURE);
    fresh();
    gfx_picture(&g, 10, 4, PICTURE, 2, true);
    CHECK(count_black() == 3 * 2 * 2);
    CHECK(gfx_get(&g, 10, 4) && gfx_get(&g, 11, 5) && !gfx_get(&g, 12, 4) && gfx_get(&g, 14, 4));
    CHECK(gfx_get(&g, 12, 6) && gfx_get(&g, 13, 7) && !gfx_get(&g, 10, 6));
}

/* Russian is drawn in Latin letters, and so is a careful typist's punctuation. */
static void test_in_latin(void)
{
    const gfx_font_t *font = gfx_font_system();
    CHECK(gfx_text_font_width(font, "\xD0\x96\xD1\x83\xD0\xBA") == gfx_text_font_width(font, "Zhuk"));
    CHECK(gfx_text_font_width(font, "\xD0\xBE\xD0\xB1\xD1\x8A\xD0\xB5\xD0\xBA\xD1\x82") ==
          gfx_text_font_width(font, "obekt")); /* the hard sign has no sound */
    CHECK(gfx_text_font_width(font, "a \xE2\x80\x94 b\xE2\x80\xA6") == gfx_text_font_width(font, "a - b..."));
    CHECK(gfx_text_font_width(font, "\xE4\xB8\xAD") == gfx_text_font_width(font, "?"));
    char fitted[32];
    gfx_text_fit(font, "\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0", gfx_text_font_width(font, "Mo..."),
                 fitted, sizeof fitted);
    CHECK_STR(fitted, "\xD0\x9C\xD0\xBE...");
}

/* A glyph pack adds letters to a face; without it they are spelled. */
static void test_glyph_pack(void)
{
    /* One face, one glyph: U+0416 as a block 2 wide and 3 tall, advance 4. */
    static const union {
        uint32_t aligned;
        uint8_t  bytes[24 + 8 + 12 + 4];
    } PACK = { .bytes = {
        'P', 'G', 'L', 'Y', 1, 0, 1, 0, 48, 0, 0, 0, 't', 'e', 's', 't', 0, 0, 0, 0, 0, 0, 0, 0,
        GFX_FACE_DEPARTURE_11, 0, 1, 0, 3, 0, 0, 0,
        0x16, 0x04, 2, 3, 0, 3, 4, 0, 0, 0, 0, 0,
        0xC0, 0xC0, 0xC0, 0x00,
    } };
    const gfx_font_t *font = gfx_font_system(); /* Departure Mono, two pixels a dot */
    const char       *zhe = "\xD0\x96";
    gfx_packs_clear();
    CHECK(gfx_text_font_width(font, zhe) == gfx_text_font_width(font, "Zh"));

    CHECK(gfx_pack_bytes(PACK.bytes, sizeof PACK.bytes) == sizeof PACK.bytes);
    CHECK(gfx_pack_bytes(PACK.bytes, sizeof PACK.bytes - 1) == 0); /* cut short */
    CHECK(gfx_pack_add(PACK.bytes, sizeof PACK.bytes));
    CHECK_STR(gfx_pack_name(PACK.bytes), "test");
    CHECK(gfx_text_font_width(font, zhe) == 4 * font->scale);
    fresh();
    gfx_text_font(&g, 0, 8, font, zhe, true);
    CHECK(count_black() == 2 * 3 * font->scale * font->scale);
    /* another family's faces do not take it */
    gfx_font_use(GFX_FAMILY_JERSEY);
    const gfx_font_t *jersey = gfx_font_system();
    CHECK(gfx_text_font_width(jersey, zhe) == gfx_text_font_width(jersey, "Zh"));
    gfx_font_use(GFX_FAMILY_DEPARTURE_MONO);

    gfx_packs_clear();
    CHECK(gfx_text_font_width(font, zhe) == gfx_text_font_width(font, "Zh"));

    /* what is not a sound pack is not taken: a glyph that points past the bitmaps */
    union {
        uint32_t aligned;
        uint8_t  bytes[sizeof PACK.bytes];
    } broken;
    memcpy(broken.bytes, PACK.bytes, sizeof broken.bytes);
    broken.bytes[24 + 8 + 8] = 200;
    CHECK(!gfx_pack_add(broken.bytes, sizeof broken.bytes));
    memcpy(broken.bytes, PACK.bytes, sizeof broken.bytes);
    broken.bytes[0] = 'X';
    CHECK(!gfx_pack_add(broken.bytes, sizeof broken.bytes));
}

static void test_text_fit(void)
{
    const gfx_font_t *font = gfx_font_system();
    int character = gfx_text_font_width(font, "x");
    char fitted[24];

    gfx_text_fit(font, "Berlin", 6 * character, fitted, sizeof fitted);
    CHECK_STR(fitted, "Berlin");
    /* one character short: the end gives way to three dots */
    gfx_text_fit(font, "Saint Petersburg", 15 * character, fitted, sizeof fitted);
    CHECK_STR(fitted, "Saint Peters...");
    /* no space is left before the dots */
    gfx_text_fit(font, "Saint Petersburg", 9 * character, fitted, sizeof fitted);
    CHECK_STR(fitted, "Saint...");
    gfx_text_fit(font, "Berlin", 4 * character, fitted, sizeof fitted);
    CHECK_STR(fitted, "B...");
    /* not even one character and the dots: nothing */
    gfx_text_fit(font, "Berlin", 3 * character, fitted, sizeof fitted);
    CHECK_STR(fitted, "");
    gfx_text_fit(font, "", 0, fitted, sizeof fitted);
    CHECK_STR(fitted, "");
    /* the buffer is a limit too */
    char small[8];
    gfx_text_fit(font, "Saint Petersburg", 100 * character, small, sizeof small);
    CHECK_STR(small, "Saint P");
}

static void test_digits_in_columns(void)
{
    /* a typeface whose digits are of their own widths: a time that changes
     * is as wide as any other, once its digits keep their columns */
    gfx_font_use(GFX_FAMILY_JERSEY);
    const gfx_font_t *jersey = gfx_font(GFX_FONT_HUGE);
    CHECK(gfx_text_font_width(jersey, "11:11") != gfx_text_font_width(jersey, "00:00"));
    CHECK(gfx_text_tabular_width(jersey, "11:11") == gfx_text_tabular_width(jersey, "00:00"));
    CHECK(gfx_text_tabular_width(jersey, "12:34") == gfx_text_tabular_width(jersey, "56:78"));
    /* what is not a digit keeps its own width */
    CHECK(gfx_text_tabular_width(jersey, "min") == gfx_text_font_width(jersey, "min"));
    /* and what is drawn is as wide as what was measured */
    static uint8_t wide_bits[400 / 8 * 120];
    gfx_t wide;
    gfx_init(&wide, wide_bits, 400, 120);
    gfx_clear(&wide, false);
    CHECK(gfx_text_tabular(&wide, 0, 100, jersey, "11:11", true) ==
          gfx_text_tabular_width(jersey, "11:11"));

    /* a typeface that is of one width already is not changed by it */
    gfx_font_use(GFX_FAMILY_DEPARTURE_MONO);
    const gfx_font_t *mono = gfx_font(GFX_FONT_HUGE);
    CHECK(gfx_text_tabular_width(mono, "11:11") == gfx_text_font_width(mono, "11:11"));
}

int main(void)
{
    test_digits_in_columns();
    test_picture();
    test_degree_sign();
    test_in_latin();
    test_glyph_pack();
    test_text_fit();
    test_clear_and_pixel();
    test_fill_frame();
    test_viewport();
    test_line();
    test_bitmap();
    test_text_square();
    test_text_font();
    test_text_font_scaled();
    REPORT();
}
