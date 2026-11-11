/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "screens.h"

#include <stddef.h>

/* Every host screen is black ink on the panel's white, no frame. The
 * logo word is the square font; everything else is Departure Mono. */

/* The prototype shows this, not the build's version. */
#define SPLASH_VERSION "ver 1.234.5"
#define LOGO_WORD      "perINKpheraL"
#define LOGO_SCALE     5 /* 27 px squares, one stroke apart: 357 px wide */
#define LOGO_CENTER_Y  118
static const int SPLASH_LINE_BASELINES[] = { 222, 254, 282 };

static void centered(gfx_t *g, int baseline, const gfx_font_t *font, const char *text)
{
    gfx_text_font(g, (g->w - gfx_text_font_width(font, text)) / 2, baseline, font, text, true);
}

void screens_splash(gfx_t *g)
{
    int logo_width = gfx_text_sq_width_ex(LOGO_WORD, LOGO_SCALE, LOGO_SCALE);
    int logo_height = gfx_text_sq_height(LOGO_SCALE);
    gfx_text_sq_ex(g, (g->w - logo_width) / 2, LOGO_CENTER_Y - logo_height / 2, LOGO_WORD,
                   LOGO_SCALE, true, LOGO_SCALE);

    centered(g, SPLASH_LINE_BASELINES[0], gfx_font_system(), SPLASH_VERSION);
    centered(g, SPLASH_LINE_BASELINES[1], gfx_font_system(), "(c) 2026 Max Grakov");
    centered(g, SPLASH_LINE_BASELINES[2], gfx_font_system(), "MIT License");
}

#define MARGIN        16
#define TEXT_BASELINE 30 /* of the first line under a top edge */

static void right_aligned(gfx_t *g, int baseline, const gfx_font_t *font, const char *text)
{
    gfx_text_font(g, g->w - MARGIN - gfx_text_font_width(font, text), baseline, font, text, true);
}

/* The connection icon on the icon canvas: 11 x 11 cells of the font's own
 * 2 px. Three bars that grow to the right, nothing but right angles. */
#define ICON_CELL          2
#define ICON_COLS          11
#define ICON_ROWS          11
#define ICON_BASELINE_ROWS 9 /* rows that stand on the text baseline */
static const char *const SIGNAL[ICON_ROWS] = {
    ".......##..",
    ".......##..",
    ".......##..",
    "....##.##..",
    "....##.##..",
    "....##.##..",
    ".##.##.##..",
    ".##.##.##..",
    ".##.##.##..",
    "...........",
    "...........",
};

static void fill_cells(gfx_t *g, int x, int y, int col, int row, int count, bool black)
{
    gfx_fill(g, x + col * ICON_CELL, y + row * ICON_CELL, count * ICON_CELL, ICON_CELL, black);
}

static void icon_signal(gfx_t *g, int x, int y, bool connected)
{
    for (int row = 0; row < ICON_ROWS; row++) {
        for (int col = 0; col < ICON_COLS; col++) {
            if (SIGNAL[row][col] == '#') {
                fill_cells(g, x, y, col, row, 1, true);
            }
        }
    }
    if (!connected) {
        /* Struck through from the bottom left corner of the canvas to the
         * top right: a staircase two cells wide, with a cell of white on
         * both sides to keep it apart from the bars. */
        for (int row = 0; row < ICON_ROWS; row++) {
            int right = ICON_COLS - 1 - row, left = right > 0 ? right - 1 : right;
            int width = right - left + 1;
            fill_cells(g, x, y, left - 1, row, width + 2, false);
            fill_cells(g, x, y, left, row, width, true);
        }
    }
}

#define HEADER_BASELINE 30

void screens_header(gfx_t *g, const screens_header_t *header)
{
    icon_signal(g, MARGIN, HEADER_BASELINE - ICON_BASELINE_ROWS * ICON_CELL, header->wifi);
    if (header->date != NULL) {
        right_aligned(g, HEADER_BASELINE, gfx_font_system(), header->date);
    }
    gfx_fill(g, 0, g->h - 1, g->w, 1, true);
}

#define MENU_TOP_GAP    8
#define MENU_ROW_HEIGHT 42
/* The bar behind the selected row. */
#define MENU_BAR_INSET  8  /* from both sides of the surface */
#define MENU_BAR_HEIGHT 36
#define MENU_BAR_RISE   26 /* its top above the row's baseline */

int screens_menu_first_row(int count, int selected, int visible_rows)
{
    int first = selected - visible_rows / 2;
    if (first > count - visible_rows) {
        first = count - visible_rows;
    }
    return first > 0 ? first : 0;
}

void screens_menu(gfx_t *g, screens_menu_title_fn title_at, void *context, int count,
                  int selected)
{
    int visible_rows = (g->h - MENU_TOP_GAP) / MENU_ROW_HEIGHT;
    int first = screens_menu_first_row(count, selected, visible_rows);
    for (int row = 0; row < visible_rows && first + row < count; row++) {
        int  baseline = MENU_TOP_GAP + row * MENU_ROW_HEIGHT + TEXT_BASELINE;
        bool is_selected = first + row == selected;
        if (is_selected) {
            gfx_fill(g, MENU_BAR_INSET, baseline - MENU_BAR_RISE, g->w - 2 * MENU_BAR_INSET,
                     MENU_BAR_HEIGHT, true);
        }
        gfx_text_font(g, MARGIN, baseline, gfx_font_system(), title_at(context, first + row),
                      !is_selected);
    }
}

#define FIELD_ROW_HEIGHT 34

void screens_fields(gfx_t *g, const screens_field_t *fields, int count)
{
    for (int i = 0; i < count; i++) {
        int baseline = (i + 1) * FIELD_ROW_HEIGHT;
        gfx_text_font(g, MARGIN, baseline, gfx_font_system(), fields[i].label, true);
        right_aligned(g, baseline, gfx_font_system(), fields[i].value);
    }
}

#define SETUP_LINE_HEIGHT 32
#define SETUP_INDENT      40

void screens_setup(gfx_t *g, const char *access_point_name, const char *address)
{
    const struct {
        int         indent;
        const char *text;
    } lines[] = {
        { 0, "Set up Wi-Fi" },
        { 0, "" },
        { 0, "1. Join the network" },
        { SETUP_INDENT, access_point_name },
        { 0, "2. Open in a browser" },
        { SETUP_INDENT, address },
    };
    for (int line = 0; line < (int)(sizeof lines / sizeof lines[0]); line++) {
        gfx_text_font(g, MARGIN + lines[line].indent, (line + 1) * SETUP_LINE_HEIGHT,
                      gfx_font_system(), lines[line].text, true);
    }
}
