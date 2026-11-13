/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "gfx.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../font_square.h"
#include "spelling.h"

void gfx_init(gfx_t *g, uint8_t *bits, int w, int h)
{
    g->bits = bits;
    g->stride = w / 8;
    g->w = w;
    g->h = h;
    g->origin_x = 0;
    g->origin_y = 0;
    g->clip_x0 = 0;
    g->clip_y0 = 0;
    g->clip_x1 = w;
    g->clip_y1 = h;
    g->shade = GFX_SHADE_SOLID;
}

static int imax(int a, int b)
{
    return a > b ? a : b;
}

static int imin(int a, int b)
{
    return a < b ? a : b;
}

void gfx_viewport(const gfx_t *parent, gfx_t *view, int x, int y, int w, int h)
{
    *view = *parent;
    view->w = w;
    view->h = h;
    view->origin_x = parent->origin_x + x;
    view->origin_y = parent->origin_y + y;
    view->clip_x0 = imax(parent->clip_x0, view->origin_x);
    view->clip_y0 = imax(parent->clip_y0, view->origin_y);
    /* A view that misses its parent gets an empty clip, never an inverted one. */
    view->clip_x1 = imax(view->clip_x0, imin(parent->clip_x1, view->origin_x + w));
    view->clip_y1 = imax(view->clip_y0, imin(parent->clip_y1, view->origin_y + h));
}

static bool covers_whole_buffer(const gfx_t *g)
{
    return g->origin_x == 0 && g->origin_y == 0 && g->clip_x0 == 0 && g->clip_y0 == 0 &&
           g->clip_x1 == g->w && g->clip_y1 == g->h && g->stride * 8 == g->w;
}

void gfx_clear(gfx_t *g, bool black)
{
    if (covers_whole_buffer(g)) {
        memset(g->bits, black ? 0x00 : 0xFF, (size_t)g->stride * (size_t)g->h);
    } else {
        gfx_fill(g, 0, 0, g->w, g->h, black);
    }
}

/* Surface coordinates to a place in the buffer; false when clipped. */
static bool locate(const gfx_t *g, int x, int y, int *buffer_x, int *buffer_y)
{
    *buffer_x = x + g->origin_x;
    *buffer_y = y + g->origin_y;
    return *buffer_x >= g->clip_x0 && *buffer_y >= g->clip_y0 &&
           *buffer_x < g->clip_x1 && *buffer_y < g->clip_y1;
}

static uint8_t pixel_mask(int buffer_x)
{
    return (uint8_t)(0x80 >> (buffer_x & 7));
}

static uint8_t *pixel_byte(const gfx_t *g, int buffer_x, int buffer_y)
{
    return &g->bits[buffer_y * g->stride + (buffer_x >> 3)];
}

/* The 4 x 4 Bayer matrix: at shade s a pixel is inked where its entry is
 * below s. Taken by the pixel's place in the buffer, so that two shaded
 * shapes side by side continue one pattern. */
static const uint8_t DITHER[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

void gfx_pixel(gfx_t *g, int x, int y, bool black)
{
    int buffer_x, buffer_y;
    if (!locate(g, x, y, &buffer_x, &buffer_y)) {
        return;
    }
    if (black && g->shade < GFX_SHADE_SOLID) {
        black = DITHER[buffer_y & 3][buffer_x & 3] < g->shade;
    }
    uint8_t *byte = pixel_byte(g, buffer_x, buffer_y);
    if (black) {
        *byte &= (uint8_t)~pixel_mask(buffer_x);
    } else {
        *byte |= pixel_mask(buffer_x);
    }
}

bool gfx_get(const gfx_t *g, int x, int y)
{
    int buffer_x, buffer_y;
    if (!locate(g, x, y, &buffer_x, &buffer_y)) {
        return false;
    }
    return (*pixel_byte(g, buffer_x, buffer_y) & pixel_mask(buffer_x)) == 0;
}

void gfx_fill(gfx_t *g, int x, int y, int w, int h, bool black)
{
    /* Cut to what can be seen first: the cost is then the surface's, not
     * the rectangle's, however large a caller makes it. */
    int left = imax(x, g->clip_x0 - g->origin_x);
    int top = imax(y, g->clip_y0 - g->origin_y);
    int right = imin(x + w, g->clip_x1 - g->origin_x);
    int bottom = imin(y + h, g->clip_y1 - g->origin_y);
    for (int row = top; row < bottom; row++) {
        for (int column = left; column < right; column++) {
            gfx_pixel(g, column, row, black);
        }
    }
}

void gfx_frame(gfx_t *g, int x, int y, int w, int h, int thickness, bool black)
{
    gfx_fill(g, x, y, w, thickness, black);
    gfx_fill(g, x, y + h - thickness, w, thickness, black);
    gfx_fill(g, x, y, thickness, h, black);
    gfx_fill(g, x + w - thickness, y, thickness, h, black);
}

void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, bool black)
{
    int delta_x = x1 - x0, delta_y = y1 - y0;
    int steps = imax(delta_x < 0 ? -delta_x : delta_x, delta_y < 0 ? -delta_y : delta_y);
    if (steps == 0) {
        gfx_pixel(g, x0, y0, black);
        return;
    }
    for (int step = 0; step <= steps; step++) {
        gfx_pixel(g, x0 + delta_x * step / steps, y0 + delta_y * step / steps, black);
    }
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

int gfx_picture_width(const uint8_t *picture)
{
    return picture[0];
}

int gfx_picture_height(const uint8_t *picture)
{
    return picture[1];
}

int gfx_picture_bytes(const uint8_t *picture)
{
    return GFX_PICTURE_HEADER + (picture[0] + 7) / 8 * picture[1];
}

void gfx_picture(gfx_t *g, int x, int y, const uint8_t *picture, int cell, bool black)
{
    int width = picture[0], height = picture[1], stride = (width + 7) / 8;
    const uint8_t *bits = picture + GFX_PICTURE_HEADER;
    for (int row = 0; row < height; row++) {
        /* A run of ink in a row is one rectangle. */
        for (int column = 0; column < width;) {
            if ((bits[row * stride + column / 8] & (0x80 >> (column % 8))) == 0) {
                column++;
                continue;
            }
            int start = column;
            while (column < width &&
                   (bits[row * stride + column / 8] & (0x80 >> (column % 8))) != 0) {
                column++;
            }
            gfx_fill(g, x + start * cell, y + row * cell, (column - start) * cell, cell, black);
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
 * along the line, as the leg of the "r" is drawn. */
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

static const gfx_glyph_t *glyph_among(const gfx_glyph_t *glyphs, int count, uint32_t cp)
{
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t c = glyphs[mid].cp;
        if (c == cp) {
            return &glyphs[mid];
        }
        if (c < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

/* ---- glyph packs ----
 *
 * The fonts in the firmware are ASCII. Letters of other alphabets come in
 * packs the user adds: for a face of a font, more glyphs in the font's own
 * form, somewhere that stays put - a partition of flash, mapped. */
#define PACK_MAGIC   0x594C4750u /* "PGLY" */
#define PACK_VERSION 1
#define GLYPH_SIDE_MAX 64
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  face_count;
    uint8_t  reserved;
    uint32_t bytes; /* the whole pack */
    char     name[GFX_PACK_NAME_MAX + 1];
} pack_header_t;
typedef struct {
    uint8_t  face; /* a gfx_face_t */
    uint8_t  reserved;
    uint16_t glyph_count;
    uint32_t bits_bytes;
} pack_face_t;
_Static_assert(sizeof(pack_header_t) == 24 && sizeof(pack_face_t) == 8 && sizeof(gfx_glyph_t) == 12,
               "a pack is read in place: its records are the structures");

static struct {
    uint8_t            face;
    const gfx_glyph_t *glyphs;
    int                count;
    const uint8_t     *bits;
} s_packs[GFX_PACK_FACES_MAX];
static int s_pack_count;

void gfx_packs_clear(void)
{
    s_pack_count = 0;
}

static size_t padded(size_t bytes)
{
    return (bytes + 3) & ~(size_t)3;
}

size_t gfx_pack_bytes(const void *data, size_t available)
{
    const pack_header_t *header = data;
    if (available < sizeof *header || header->magic != PACK_MAGIC ||
        header->version != PACK_VERSION || header->bytes > available ||
        header->bytes < sizeof *header ||
        memchr(header->name, '\0', sizeof header->name) == NULL) {
        return 0;
    }
    /* Every face has to lie inside the pack, whole. */
    size_t at = sizeof *header;
    for (int i = 0; i < header->face_count; i++) {
        const pack_face_t *face = (const pack_face_t *)((const uint8_t *)data + at);
        if (at + sizeof *face > header->bytes) {
            return 0;
        }
        size_t glyphs_bytes = (size_t)face->glyph_count * sizeof(gfx_glyph_t);
        at += sizeof *face;
        if (glyphs_bytes > header->bytes - at ||
            padded(face->bits_bytes) > header->bytes - at - glyphs_bytes) {
            return 0;
        }
        const gfx_glyph_t *glyphs = (const gfx_glyph_t *)((const uint8_t *)data + at);
        for (int g = 0; g < face->glyph_count; g++) {
            size_t glyph_bytes = (size_t)((glyphs[g].w + 7) / 8) * glyphs[g].h;
            /* A glyph the size of the screen would make a line of text a
             * minute's work: no face here is drawn larger than this. */
            if (glyphs[g].w > GLYPH_SIDE_MAX || glyphs[g].h > GLYPH_SIDE_MAX ||
                glyphs[g].offset > face->bits_bytes ||
                glyph_bytes > face->bits_bytes - glyphs[g].offset ||
                (g > 0 && glyphs[g].cp <= glyphs[g - 1].cp)) {
                return 0;
            }
        }
        at += glyphs_bytes + padded(face->bits_bytes);
    }
    return header->bytes;
}

bool gfx_pack_add(const void *data, size_t available)
{
    const pack_header_t *header = data;
    if (((uintptr_t)data & 3) != 0 || gfx_pack_bytes(data, available) == 0 ||
        s_pack_count + header->face_count > GFX_PACK_FACES_MAX) {
        return false;
    }
    size_t at = sizeof *header;
    for (int i = 0; i < header->face_count; i++) {
        const pack_face_t *face = (const pack_face_t *)((const uint8_t *)data + at);
        at += sizeof *face;
        s_packs[s_pack_count].face = face->face;
        s_packs[s_pack_count].glyphs = (const gfx_glyph_t *)((const uint8_t *)data + at);
        s_packs[s_pack_count].count = face->glyph_count;
        at += (size_t)face->glyph_count * sizeof(gfx_glyph_t);
        s_packs[s_pack_count].bits = (const uint8_t *)data + at;
        at += padded(face->bits_bytes);
        s_pack_count++;
    }
    return true;
}

const char *gfx_pack_name(const void *data)
{
    return ((const pack_header_t *)data)->name;
}

/* The glyph of a character and the bitmaps it points into: the font's
 * own, or a pack's for the font's face; NULL when nobody has it. */
static const gfx_glyph_t *glyph_of(const gfx_font_t *font, uint32_t cp, const uint8_t **bits)
{
    const gfx_glyph_t *glyph = glyph_among(font->glyphs, font->count, cp);
    *bits = font->bits;
    for (int i = 0; glyph == NULL && cp >= 0x80 && i < s_pack_count; i++) {
        if (s_packs[i].face == font->face) {
            glyph = glyph_among(s_packs[i].glyphs, s_packs[i].count, cp);
            *bits = s_packs[i].bits;
        }
    }
    return glyph;
}

static const gfx_glyph_t *font_glyph(const gfx_font_t *font, uint32_t cp)
{
    const gfx_glyph_t *glyph = glyph_among(font->glyphs, font->count, cp);
    return glyph != NULL || cp == '?' ? glyph : glyph_among(font->glyphs, font->count, '?');
}

static bool is_digit(uint32_t cp)
{
    return cp >= '0' && cp <= '9';
}

/* The widest of the ten digits, in the font's own dots. */
static int digit_cell(const gfx_font_t *font)
{
    int widest = 0;
    for (uint32_t digit = '0'; digit <= '9'; digit++) {
        const gfx_glyph_t *gl = font_glyph(font, digit);
        if (gl != NULL && gl->advance > widest) {
            widest = gl->advance;
        }
    }
    return widest;
}

/* What the glyph takes of the line, and how far into that it is drawn:
 * its own advance, or - a digit among digits that keep their columns - a
 * cell as wide as the widest digit, with the glyph in the middle of it. */
static int glyph_room(const gfx_glyph_t *gl, uint32_t cp, int cell, int *inset)
{
    *inset = 0;
    if (cell == 0 || !is_digit(cp)) {
        return gl->advance;
    }
    *inset = (cell - gl->advance) / 2;
    return cell;
}

/* The one sign that is not ASCII and is drawn all the same: a temperature
 * is nothing without its degree. No font has it; it is a square with a
 * hole at the top of the capitals, sized by them. Returns what it takes of
 * the line; nothing stands after it at the end of one. */
#define DEGREE_SIGN 0xB0

static int draw_degree(gfx_t *g, int x, int y, const gfx_font_t *font, bool black, bool is_last)
{
    int capitals = gfx_font_cap_height(font);
    int size = capitals >= 70 ? 20 : capitals >= 40 ? 14 : capitals >= 24 ? 10 : capitals >= 12 ? 6 : 4;
    int stroke = size >= 20 ? 6 : size >= 14 ? 4 : size >= 10 ? 3 : size >= 6 ? 2 : 1;
    int gap = size >= 20 ? 8 : size >= 14 ? 6 : size >= 10 ? 3 : 2;
    if (g != NULL) {
        int left = x + gap, top = y - capitals;
        gfx_fill(g, left, top, size, stroke, black);
        gfx_fill(g, left, top + size - stroke, size, stroke, black);
        gfx_fill(g, left, top, stroke, size, black);
        gfx_fill(g, left + size - stroke, top, stroke, size, black);
    }
    return gap + size + (is_last ? 0 : gap);
}

/* What no font and no pack has is spelled in ASCII where a table says how
 * (spelling.h: Latin letters with marks, Cyrillic, Greek, a careful
 * typist's punctuation), and is "?" where none does. Returns the letters
 * to draw, or NULL when there is no spelling. */
static const char *in_latin(uint32_t cp)
{
    for (size_t i = 0; i < sizeof SPELLINGS / sizeof SPELLINGS[0]; i++) {
        if (cp >= SPELLINGS[i].first && cp <= SPELLINGS[i].last) {
            return SPELLINGS[i].spelled[cp - SPELLINGS[i].first];
        }
    }
    for (size_t i = 0; i < sizeof SPELLING_SINGLES / sizeof SPELLING_SINGLES[0]; i++) {
        if (SPELLING_SINGLES[i].cp == cp) {
            return SPELLING_SINGLES[i].spelled;
        }
    }
    return NULL;
}

static int draw_text(gfx_t *g, int x, int y, const gfx_font_t *font, const char *utf8, bool black,
                     int cell)
{
    int x0 = x;
    int scale = font->scale;
    const char *s = utf8;
    const char *spelled = NULL; /* the Latin letters of a character being spelled out */
    while (*s != '\0' || (spelled != NULL && *spelled != '\0')) {
        uint32_t           cp;
        const uint8_t     *bits = font->bits;
        const gfx_glyph_t *gl = NULL;
        if (spelled != NULL && *spelled != '\0') {
            cp = (uint8_t)*spelled++;
        } else {
            cp = next_cp(&s);
            /* Its own glyph, in the font or in a pack, comes before all else. */
            gl = glyph_of(font, cp, &bits);
            if (gl == NULL && cp == DEGREE_SIGN) {
                x += draw_degree(g, x, y, font, black, *s == '\0');
                continue;
            }
            spelled = gl == NULL && cp >= 0x80 ? in_latin(cp) : NULL;
            if (spelled != NULL) {
                continue; /* its letters come next, or nothing: a sign with no sound */
            }
        }
        if (gl == NULL) {
            bits = font->bits;
            gl = font_glyph(font, cp);
        }
        if (gl == NULL) {
            continue;
        }
        int inset;
        int room = glyph_room(gl, cp, cell, &inset);
        if (g != NULL) {
            int stride = (gl->w + 7) / 8;
            bits += gl->offset;
            for (int row = 0; row < gl->h; row++) {
                for (int col = 0; col < gl->w; col++) {
                    if (bits[row * stride + (col >> 3)] & (0x80 >> (col & 7))) {
                        gfx_fill(g, x + (inset + gl->left + col) * scale,
                                 y + (row - gl->top) * scale, scale, scale, black);
                    }
                }
            }
        }
        x += room * scale;
    }
    return x - x0;
}

int gfx_text_font(gfx_t *g, int x, int y, const gfx_font_t *font, const char *utf8, bool black)
{
    return draw_text(g, x, y, font, utf8, black, 0);
}

int gfx_text_font_width(const gfx_font_t *font, const char *utf8)
{
    return draw_text(NULL, 0, 0, font, utf8, true, 0);
}

int gfx_text_tabular(gfx_t *g, int x, int y, const gfx_font_t *font, const char *utf8, bool black)
{
    return draw_text(g, x, y, font, utf8, black, digit_cell(font));
}

int gfx_text_tabular_width(const gfx_font_t *font, const char *utf8)
{
    return draw_text(NULL, 0, 0, font, utf8, true, digit_cell(font));
}

size_t gfx_text_cut(const char *text, size_t max_bytes)
{
    size_t length = strlen(text);
    if (length <= max_bytes) {
        return length;
    }
    length = max_bytes;
    while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) {
        length--;
    }
    return length;
}

void gfx_text_fit(const gfx_font_t *font, const char *text, int max_width, char *fitted,
                  size_t capacity)
{
    static const char ELLIPSIS[] = "...";
    if (capacity == 0) {
        return;
    }
    size_t whole = gfx_text_cut(text, capacity - 1);
    memcpy(fitted, text, whole);
    fitted[whole] = '\0';
    if (gfx_text_font_width(font, fitted) <= max_width) {
        return;
    }
    size_t room = capacity > sizeof ELLIPSIS ? capacity - sizeof ELLIPSIS : 0;
    size_t kept = strlen(text) < room ? strlen(text) : room;
    for (; kept > 0; kept--) {
        /* No space before the dots: "Saint...", not "Saint ...". And the
         * cut is between characters: not inside one of several bytes. */
        if (text[kept - 1] == ' ' || ((unsigned char)text[kept] & 0xC0) == 0x80) {
            continue;
        }
        snprintf(fitted, capacity, "%.*s%s", (int)kept, text, ELLIPSIS);
        if (gfx_text_font_width(font, fitted) <= max_width) {
            return;
        }
    }
    fitted[0] = '\0';
}

int gfx_font_cap_height(const gfx_font_t *font)
{
    const gfx_glyph_t *capital = font_glyph(font, 'H');
    return capital != NULL ? capital->top * font->scale : gfx_font_ascent(font);
}

#include "departure_11.h"
#include "jersey_10.h"
#include "jersey_15.h"
#include "jersey_20.h"
#include "jersey_25.h"

#define DEPARTURE_MONO(pixels_per_dot)                                              \
    {                                                                               \
        .ascent = departure_11_ascent, .descent = departure_11_descent,             \
        .glyphs = departure_11_glyphs, .count = departure_11_count,                 \
        .bits = departure_11_bits, .scale = (pixels_per_dot),                       \
        .face = GFX_FACE_DEPARTURE_11,                                              \
    }

/* One of the Jersey faces - each is drawn for capitals of so many pixels,
 * which is its number - with a dot so many pixels large. */
#define JERSEY(drawn, pixels_per_dot)                                               \
    {                                                                               \
        .ascent = jersey_##drawn##_ascent, .descent = jersey_##drawn##_descent,     \
        .glyphs = jersey_##drawn##_glyphs, .count = jersey_##drawn##_count,         \
        .bits = jersey_##drawn##_bits, .scale = (pixels_per_dot),                   \
        .face = GFX_FACE_JERSEY_##drawn,                                            \
    }

/* A family has every size. Departure Mono is one bitmap at whole
 * multiples; Jersey is four faces, picked so that the capitals of a size
 * are as tall as Departure Mono's - 10, 15, 25, 30, 50 and 80 pixels for
 * its 8, 16, 24, 32, 48 and 80 - and what is laid out for one family
 * holds for the other. */
static const gfx_font_t FONTS[GFX_FAMILY_COUNT][GFX_FONT_COUNT] = {
    [GFX_FAMILY_DEPARTURE_MONO] = {
        [GFX_FONT_TEXT] = DEPARTURE_MONO(2),    [GFX_FONT_MEDIUM] = DEPARTURE_MONO(3),
        [GFX_FONT_LARGE] = DEPARTURE_MONO(4),   [GFX_FONT_DISPLAY] = DEPARTURE_MONO(6),
        [GFX_FONT_HUGE] = DEPARTURE_MONO(10),   [GFX_FONT_SMALL] = DEPARTURE_MONO(1),
    },
    [GFX_FAMILY_JERSEY] = {
        [GFX_FONT_TEXT] = JERSEY(15, 1),    [GFX_FONT_MEDIUM] = JERSEY(25, 1),
        [GFX_FONT_LARGE] = JERSEY(15, 2),   [GFX_FONT_DISPLAY] = JERSEY(25, 2),
        [GFX_FONT_HUGE] = JERSEY(20, 4),    [GFX_FONT_SMALL] = JERSEY(10, 1),
    },
};

static gfx_family_t s_family = GFX_FAMILY_DEPARTURE_MONO;

void gfx_font_use(gfx_family_t family)
{
    if ((unsigned)family < GFX_FAMILY_COUNT) {
        s_family = family;
    }
}

gfx_family_t gfx_font_family(void)
{
    return s_family;
}

const gfx_font_t *gfx_font(gfx_font_id_t id)
{
    return (unsigned)id < GFX_FONT_COUNT ? &FONTS[s_family][id] : NULL;
}

const gfx_font_t *gfx_font_system(void)
{
    return &FONTS[s_family][GFX_FONT_TEXT];
}
