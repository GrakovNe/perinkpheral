/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
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
    /* How dark "black" comes out: GFX_SHADE_SOLID, or less - then only
     * that many of every sixteen pixels are inked, in a fixed pattern, and
     * the eye reads a grey. 0 draws paper. */
    uint8_t  shade;
} gfx_t;

#define GFX_SHADE_SOLID 16

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
void gfx_frame(gfx_t *g, int x, int y, int w, int h, int thickness, bool black);

/* A black line one pixel wide, both ends included. */
void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, bool black);

/* A 1 bpp bitmap in the framebuffer's own format (row-major, MSB left,
 * 1 = white, rows padded to a byte). */
void gfx_bitmap(gfx_t *g, int x, int y, int w, int h, const uint8_t *bits);

/* The square font (font_square.c): every
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

/* A font is a set of glyphs and how many pixels a dot of them takes: a
 * pixel font drawn larger is the same dots, only bigger. The metrics in
 * here are in dots; the functions below answer in pixels. */
typedef struct {
    uint8_t            ascent;
    uint8_t            descent;
    const gfx_glyph_t *glyphs; /* sorted by cp */
    int                count;
    const uint8_t     *bits;
    uint8_t            scale; /* pixels per dot, at least 1 */
} gfx_font_t;

/* UTF-8 text with the baseline at y; code points the font lacks draw as
 * '?'. Returns the advance in pixels. */
int gfx_text_font(gfx_t *g, int x, int y, const gfx_font_t *font, const char *utf8, bool black);

int gfx_text_font_width(const gfx_font_t *font, const char *utf8);

/* `text` as it fits into `max_width`: whole, or its start with "..." after
 * it, or nothing when not even that fits. `fitted` takes up to `capacity`
 * bytes with the terminator. ASCII. */
void gfx_text_fit(const gfx_font_t *font, const char *text, int max_width, char *fitted,
                  size_t capacity);

/* The fonts in the firmware: Departure Mono, drawn on an 11 px grid and
 * shown at whole multiples of it. Every one has all of printable ASCII. */
typedef enum {
    GFX_FONT_TEXT,    /* 22 px: the host's text */
    GFX_FONT_MEDIUM,  /* 33 px */
    GFX_FONT_LARGE,   /* 44 px */
    GFX_FONT_DISPLAY, /* 66 px */
    GFX_FONT_HUGE,    /* 110 px */
    GFX_FONT_SMALL,   /* 11 px: the bitmap as it is drawn; for what stands beside larger text */
    GFX_FONT_COUNT,
} gfx_font_id_t;

/* The typeface all of the above are set in: the user's choice. Departure
 * Mono, monospaced, is what the device starts with; Jersey is heavier and
 * its letters are of their own widths. The sizes of one stand for the
 * sizes of the other. */
typedef enum {
    GFX_FAMILY_DEPARTURE_MONO,
    GFX_FAMILY_JERSEY,
    GFX_FAMILY_COUNT,
} gfx_family_t;

void         gfx_font_use(gfx_family_t family);
gfx_family_t gfx_font_family(void);

/* NULL for an id that is not one of the above. */
const gfx_font_t *gfx_font(gfx_font_id_t id);

/* GFX_FONT_TEXT, for the host's own screens. */
const gfx_font_t *gfx_font_system(void);

/* From the top of a line to its baseline. */
static inline int gfx_font_ascent(const gfx_font_t *font)
{
    return font->ascent * font->scale;
}

/* The whole line: the ascent and the room for descenders under it. */
static inline int gfx_font_height(const gfx_font_t *font)
{
    return (font->ascent + font->descent) * font->scale;
}

/* How far capitals and digits rise above the baseline. */
int gfx_font_cap_height(const gfx_font_t *font);

#ifdef __cplusplus
}
#endif
