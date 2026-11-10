/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A drawing surface over a 1 bpp framebuffer: row-major, MSB is the left
 * pixel, 1 = white, the format the SSD1683 takes. A surface is either the
 * whole buffer or a viewport into it: its own origin and size, and
 * nothing drawn through it lands outside. Pure, host-tested. */
typedef struct {
    uint8_t *bits;
    int      stride; /* bytes per row of the buffer */
    int      w;      /* the surface's own size: lay out against these */
    int      h;
    int      origin_x;     /* where the surface's 0,0 is in the buffer */
    int      origin_y;
    /* The writable part of the buffer, x1/y1 exclusive. Always inside the
     * buffer and never inverted: set only by gfx_init and gfx_viewport. */
    int      clip_x0;
    int      clip_y0;
    int      clip_x1;
    int      clip_y1;
} gfx_t;

/* The whole buffer. w must be a multiple of 8; bits holds w / 8 * h bytes. */
void gfx_init(gfx_t *g, uint8_t *bits, int w, int h);

/* A w x h surface whose 0,0 is at x,y of the parent. It shares the
 * parent's buffer and can never draw outside the parent. */
void gfx_viewport(const gfx_t *parent, gfx_t *view, int x, int y, int w, int h);

/* Fills the surface, not the buffer under it. */
void gfx_clear(gfx_t *g, bool black);

/* Pixels outside the surface are ignored. */
void gfx_pixel(gfx_t *g, int x, int y, bool black);

/* True for black; false outside the surface. */
bool gfx_get(const gfx_t *g, int x, int y);

void gfx_fill(gfx_t *g, int x, int y, int w, int h, bool black);

/* A black rectangle outline of the given thickness, inside x,y,w,h. */
void gfx_frame(gfx_t *g, int x, int y, int w, int h, int thickness);

/* A 1 bpp bitmap in the framebuffer's own format (row-major, MSB left,
 * 1 = white, rows padded to a byte). */
void gfx_bitmap(gfx_t *g, int x, int y, int w, int h, const uint8_t *bits);

/* The square font (font_square.c), after the voc-sensor glyphs: every
 * glyph is a square on a grid of three strokes and two gaps, `scale`
 * pixels per stroke (scale 5 gives the 27 px original); no ascenders or
 * descenders, lower case in the same square. Proportional: a glyph is as
 * wide as its ink; the default gap between glyphs is one stroke. y is the
 * top of the square. */
int gfx_text_sq(gfx_t *g, int x, int y, const char *utf8, int scale, bool black);

int gfx_text_sq_width(const char *utf8, int scale);

int gfx_text_sq_height(int scale);

/* The same with the gap between glyphs given in pixels. */
int gfx_text_sq_ex(gfx_t *g, int x, int y, const char *utf8, int scale, bool black,
                   int tracking);

int gfx_text_sq_width_ex(const char *utf8, int scale, int tracking);

/* Bitmap fonts rasterized by tools/fontgen (Departure Mono for everything but
 * the logo). Glyph bits: 1 = ink, MSB left, rows padded to a byte. The
 * pen sits on the baseline; `top` is the glyph's rise above it. */
typedef struct {
    uint16_t cp;
    uint8_t  w;
    uint8_t  h;
    int8_t   left;
    int8_t   top;
    uint8_t  advance;
    uint32_t offset; /* into bits */
} gfx_glyph_t;

typedef struct {
    uint8_t            ascent;
    uint8_t            descent;
    const gfx_glyph_t *glyphs; /* sorted by cp */
    int                count;
    const uint8_t     *bits;
} gfx_font_t;

/* UTF-8 text with the baseline at y; code points the font lacks draw as
 * '?'. Returns the advance in pixels. */
int gfx_text_font(gfx_t *g, int x, int y, const gfx_font_t *font, const char *utf8, bool black);

int gfx_text_font_width(const gfx_font_t *font, const char *utf8);

static inline int gfx_font_height(const gfx_font_t *font)
{
    return font->ascent + font->descent;
}

#ifdef __cplusplus
}
#endif
