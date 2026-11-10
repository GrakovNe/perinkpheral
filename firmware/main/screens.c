/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "screens.h"

#include <stdio.h>

#include "departure_22.h"

/* Every host screen is black ink on the panel's white, no frame. The
 * logo word is the square font; everything else is Departure Mono. */

/* The prototype shows this, not the build's version. */
#define SPLASH_VERSION "ver 1.234.5"

static void centered(gfx_t *g, int baseline, const gfx_font_t *font, const char *s)
{
    gfx_text_font(g, (g->w - gfx_text_font_width(font, s)) / 2, baseline, font, s, true);
}

void screens_splash(gfx_t *g)
{
    gfx_clear(g, false);

    /* "perINKpheraL" in 27 px squares (scale 5), one stroke apart: 357 px. */
    const int scale = 5;
    int h = gfx_text_sq_height(scale);
    gfx_text_sq_ex(g, (g->w - gfx_text_sq_width_ex("perINKpheraL", scale, scale)) / 2,
                   118 - h / 2, "perINKpheraL", scale, true, scale);

    centered(g, 222, &departure_22, SPLASH_VERSION);
    centered(g, 254, &departure_22, "(c) 2026 Max Grakov");
    centered(g, 282, &departure_22, "MIT License");
}

#define MARGIN       16
#define HEADER_BASE  30
#define HEADER_RULE  42

static void right_aligned(gfx_t *g, int baseline, const gfx_font_t *font, const char *s)
{
    gfx_text_font(g, g->w - MARGIN - gfx_text_font_width(font, s), baseline, font, s, true);
}

/* Wi-Fi, drawn on the font's own 2 px grid: 22 x 16. */
#define WIFI_CELL 2
#define WIFI_COLS 11
#define WIFI_ROWS 8
static const char *const WIFI[WIFI_ROWS] = {
    "..#######..",
    ".#.......#.",
    "#..#####..#",
    "..#.....#..",
    "....###....",
    "...#...#...",
    ".....#.....",
    ".....#.....",
};

static void icon_wifi(gfx_t *g, int x, int y, bool connected)
{
    for (int r = 0; r < WIFI_ROWS; r++) {
        for (int c = 0; c < WIFI_COLS; c++) {
            if (WIFI[r][c] == '#') {
                gfx_fill(g, x + c * WIFI_CELL, y + r * WIFI_CELL, WIFI_CELL, WIFI_CELL, true);
            }
        }
    }
    if (!connected) {
        /* A cross beside it, on the baseline. */
        const int n = 5;
        int cx = x + (WIFI_COLS + 1) * WIFI_CELL, cy = y + (WIFI_ROWS - n) * WIFI_CELL;
        for (int i = 0; i < n; i++) {
            gfx_fill(g, cx + i * WIFI_CELL, cy + i * WIFI_CELL, WIFI_CELL, WIFI_CELL, true);
            gfx_fill(g, cx + (n - 1 - i) * WIFI_CELL, cy + i * WIFI_CELL, WIFI_CELL, WIFI_CELL, true);
        }
    }
}

void screens_header(gfx_t *g, const screens_header_t *h)
{
    icon_wifi(g, MARGIN, HEADER_BASE - WIFI_ROWS * WIFI_CELL, h->wifi);
    if (h->date != NULL) {
        right_aligned(g, HEADER_BASE, &departure_22, h->date);
    }
    gfx_fill(g, 0, HEADER_RULE, g->w, 1, true);
}

#define MENU_ROW_H   42
#define MENU_ROWS    6
#define MENU_TOP     (HEADER_RULE + 8)
#define MENU_MARKER  12

void screens_menu(gfx_t *g, const screens_menu_item_t *items, int count, int selected,
                  const screens_header_t *header)
{
    gfx_clear(g, false);
    screens_header(g, header);

    /* The window follows the selection and stops at the ends. */
    int first = selected - MENU_ROWS / 2;
    if (first > count - MENU_ROWS) {
        first = count - MENU_ROWS;
    }
    if (first < 0) {
        first = 0;
    }
    for (int row = 0; row < MENU_ROWS && first + row < count; row++) {
        const screens_menu_item_t *it = &items[first + row];
        int top = MENU_TOP + row * MENU_ROW_H;
        int base = top + 30;
        if (first + row == selected) {
            gfx_fill(g, MARGIN, base - 17, MENU_MARKER, MENU_MARKER, true);
        }
        gfx_text_font(g, MARGIN + MENU_MARKER + 12, base, &departure_22, it->title, true);
        if (it->hint != NULL) {
            right_aligned(g, base, &departure_22, it->hint);
        }
    }
}

void screens_fields(gfx_t *g, const screens_field_t *fields, int count,
                    const screens_header_t *header)
{
    gfx_clear(g, false);
    screens_header(g, header);
    for (int i = 0; i < count; i++) {
        int base = HEADER_RULE + 34 + i * 34;
        gfx_text_font(g, MARGIN, base, &departure_22, fields[i].label, true);
        right_aligned(g, base, &departure_22, fields[i].value);
    }
}
