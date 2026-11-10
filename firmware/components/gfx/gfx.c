/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "gfx.h"

#include <stddef.h>
#include <string.h>

#include "../font_square.h"

void gfx_init(gfx_t *g, uint8_t *bits, int w, int h)
{
    g->bits = bits;
    g->w = w;
    g->h = h;
    g->stride = w / 8;
}

void gfx_clear(gfx_t *g, bool black)
{
    memset(g->bits, black ? 0x00 : 0xFF, (size_t)g->stride * (size_t)g->h);
}

void gfx_pixel(gfx_t *g, int x, int y, bool black)
{
    if (x < 0 || y < 0 || x >= g->w || y >= g->h) {
        return;
    }
    uint8_t m = (uint8_t)(0x80 >> (x & 7));
    uint8_t *b = &g->bits[y * g->stride + (x >> 3)];
    if (black) {
        *b &= (uint8_t)~m;
    } else {
        *b |= m;
    }
}

bool gfx_get(const gfx_t *g, int x, int y)
{
    if (x < 0 || y < 0 || x >= g->w || y >= g->h) {
        return false;
    }
    return (g->bits[y * g->stride + (x >> 3)] & (0x80 >> (x & 7))) == 0;
}

void gfx_fill(gfx_t *g, int x, int y, int w, int h, bool black)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            gfx_pixel(g, i, j, black);
        }
    }
}

void gfx_frame(gfx_t *g, int x, int y, int w, int h, int thickness)
{
    gfx_fill(g, x, y, w, thickness, true);
    gfx_fill(g, x, y + h - thickness, w, thickness, true);
    gfx_fill(g, x, y, thickness, h, true);
    gfx_fill(g, x + w - thickness, y, thickness, h, true);
}

void gfx_bitmap(gfx_t *g, int x, int y, int w, int h, const uint8_t *bits)
{
    int stride = (w + 7) / 8;
    for (int j = 0; j < h; j++) {
        const uint8_t *row = bits + j * stride;
        for (int i = 0; i < w; i++) {
            bool black = (row[i >> 3] & (0x80 >> (i & 7))) == 0;
            gfx_pixel(g, x + i, y + j, black);
        }
    }
}

/* One code point from UTF-8; invalid bytes come back as '?'. */
static uint32_t next_cp(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp;
    int extra;
    if (p[0] < 0x80) {
        cp = p[0];
        extra = 0;
    } else if ((p[0] & 0xE0) == 0xC0) {
        cp = p[0] & 0x1F;
        extra = 1;
    } else if ((p[0] & 0xF0) == 0xE0) {
        cp = p[0] & 0x0F;
        extra = 2;
    } else if ((p[0] & 0xF8) == 0xF0) {
        cp = p[0] & 0x07;
        extra = 3;
    } else {
        *s += 1;
        return '?';
    }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *s += i;
            return '?';
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += extra + 1;
    return cp;
}

/* ---- the square font: three strokes and two gaps, see font_square.c ---- */

/* The gap is a fifth wider than the stroke: 5 + 6 + 5 + 6 + 5 = 27. */
static int sq_gap(int scale)
{
    return (scale * 5 + 2) / 4;
}

int gfx_text_sq_height(int scale)
{
    return 3 * scale + 2 * sq_gap(scale);
}

/* Pixel offset and size of a cell (band) index 0..4. */
static int sq_cell_pos(int cell, int scale)
{
    int g = sq_gap(scale);
    static const int strokes_before[5] = { 0, 1, 1, 2, 2 };
    static const int gaps_before[5] = { 0, 0, 1, 1, 2 };
    return strokes_before[cell] * scale + gaps_before[cell] * g;
}

static int sq_cell_size(int cell, int scale)
{
    return (cell & 1) ? sq_gap(scale) : scale;
}

/* First and one past the last cell that carries ink; a blank glyph
 * (space) is two cells wide. */
static void sq_extent(const sq_glyph_t *gl, int *first, int *end)
{
    int lo = 5, hi = -1;
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 5; col++) {
            if (gl->rows[row][col] == '#') {
                if (col < lo) {
                    lo = col;
                }
                if (col > hi) {
                    hi = col;
                }
            }
        }
    }
    for (const int8_t *d = gl->diags; *d >= 0; d += 4) {
        int a = d[0] < d[2] ? d[0] : d[2], b = d[0] < d[2] ? d[2] : d[0];
        if (a < lo) {
            lo = a;
        }
        if (b > hi) {
            hi = b;
        }
    }
    if (hi < 0) {
        *first = 0;
        *end = 2;
    } else {
        *first = lo;
        *end = hi + 1;
    }
}

static int sq_glyph_width(const sq_glyph_t *gl, int scale)
{
    int first, end;
    sq_extent(gl, &first, &end);
    return sq_cell_pos(end - 1, scale) + sq_cell_size(end - 1, scale) - sq_cell_pos(first, scale);
}

/* A stroke-wide diagonal between two cell centres: a square brush walked
 * along the line, as the leg of the voc "r" is drawn. */
static void sq_diag(gfx_t *g, int x, int y, int scale, int x0c, int y0c, int x1c, int y1c, bool black)
{
    int x0 = x + sq_cell_pos(x0c, scale) + (sq_cell_size(x0c, scale) - scale) / 2;
    int y0 = y + sq_cell_pos(y0c, scale) + (sq_cell_size(y0c, scale) - scale) / 2;
    int x1 = x + sq_cell_pos(x1c, scale) + (sq_cell_size(x1c, scale) - scale) / 2;
    int y1 = y + sq_cell_pos(y1c, scale) + (sq_cell_size(y1c, scale) - scale) / 2;
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    for (int i = 0; i <= steps; i++) {
        int px = x0 + (steps ? dx * i / steps : 0);
        int py = y0 + (steps ? dy * i / steps : 0);
        gfx_fill(g, px, py, scale, scale, black);
    }
}

static void sq_draw(gfx_t *g, int x, int y, const sq_glyph_t *gl, int first, int scale, bool black)
{
    int shift = sq_cell_pos(first, scale);
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 5; col++) {
            if (gl->rows[row][col] == '#') {
                gfx_fill(g, x + sq_cell_pos(col, scale) - shift, y + sq_cell_pos(row, scale),
                         sq_cell_size(col, scale), sq_cell_size(row, scale), black);
            }
        }
    }
    for (const int8_t *d = gl->diags; *d >= 0; d += 4) {
        sq_diag(g, x - shift, y, scale, d[0], d[1], d[2], d[3], black);
    }
}

int gfx_text_sq_ex(gfx_t *g, int x, int y, const char *utf8, int scale, bool black,
                   int tracking)
{
    int x0 = x;
    const char *s = utf8;
    while (*s != '\0') {
        const sq_glyph_t *gl = gfx_font_square_glyph(next_cp(&s));
        int first, end;
        sq_extent(gl, &first, &end);
        sq_draw(g, x, y, gl, first, scale, black);
        x += sq_glyph_width(gl, scale) + tracking;
    }
    return x > x0 ? x - x0 - tracking : 0;
}

int gfx_text_sq_width_ex(const char *utf8, int scale, int tracking)
{
    int w = 0;
    const char *s = utf8;
    while (*s != '\0') {
        w += sq_glyph_width(gfx_font_square_glyph(next_cp(&s)), scale) + tracking;
    }
    return w > 0 ? w - tracking : 0;
}

int gfx_text_sq(gfx_t *g, int x, int y, const char *utf8, int scale, bool black)
{
    return gfx_text_sq_ex(g, x, y, utf8, scale, black, scale);
}

int gfx_text_sq_width(const char *utf8, int scale)
{
    return gfx_text_sq_width_ex(utf8, scale, scale);
}

/* ---- bitmap fonts from tools/fontgen ---- */

static const gfx_glyph_t *font_glyph(const gfx_font_t *font, uint32_t cp)
{
    int lo = 0, hi = font->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t c = font->glyphs[mid].cp;
        if (c == cp) {
            return &font->glyphs[mid];
        }
        if (c < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return cp == '?' ? NULL : font_glyph(font, '?');
}

int gfx_text_font(gfx_t *g, int x, int y, const gfx_font_t *font, const char *utf8, bool black)
{
    int x0 = x;
    const char *s = utf8;
    while (*s != '\0') {
        const gfx_glyph_t *gl = font_glyph(font, next_cp(&s));
        if (gl == NULL) {
            continue;
        }
        int stride = (gl->w + 7) / 8;
        const uint8_t *bits = font->bits + gl->offset;
        for (int row = 0; row < gl->h; row++) {
            for (int col = 0; col < gl->w; col++) {
                if (bits[row * stride + (col >> 3)] & (0x80 >> (col & 7))) {
                    gfx_pixel(g, x + gl->left + col, y - gl->top + row, black);
                }
            }
        }
        x += gl->advance;
    }
    return x - x0;
}

int gfx_text_font_width(const gfx_font_t *font, const char *utf8)
{
    int w = 0;
    const char *s = utf8;
    while (*s != '\0') {
        const gfx_glyph_t *gl = font_glyph(font, next_cp(&s));
        if (gl != NULL) {
            w += gl->advance;
        }
    }
    return w;
}
