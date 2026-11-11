/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "screens.h"

#include <stddef.h>
#include <stdio.h>

#include "wm.h"

/* Every host screen is black ink on the panel's white, no frame. The
 * logo word is the square font; everything else is Departure Mono. */

/* The prototype shows this, not the build's version. */
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

#define HEADER_BASELINE    30
#define HEADER_ICON_GAP    12 /* from the icon to the text beside it */
#define HEADER_TEXTS_GAP   14 /* the least between the two texts: a character */
#define HEADER_TEXT_BUFFER 48

void screens_header(gfx_t *g, const screens_header_t *header)
{
    icon_signal(g, MARGIN, HEADER_BASELINE - ICON_BASELINE_ROWS * ICON_CELL, header->wifi);

    const gfx_font_t *font = gfx_font_system();
    const char *left = header->left != NULL ? header->left : "";
    const char *right = header->right != NULL ? header->right : "";
    int text_left = MARGIN + ICON_COLS * ICON_CELL + HEADER_ICON_GAP;
    int available = g->w - MARGIN - text_left - HEADER_TEXTS_GAP;
    int left_max, right_max;
    wm_header_share(gfx_text_font_width(font, left), gfx_text_font_width(font, right), available,
                    &left_max, &right_max);
    char fitted[HEADER_TEXT_BUFFER];
    gfx_text_fit(font, left, left_max, fitted, sizeof fitted);
    gfx_text_font(g, text_left, HEADER_BASELINE, font, fitted, true);
    gfx_text_fit(font, right, right_max, fitted, sizeof fitted);
    right_aligned(g, HEADER_BASELINE, font, fitted);
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

#define MENU_ICON_GAP 12 /* between an icon and its title */

/* An application's icon on the cells the host's own icons use. */
static void menu_icon(gfx_t *g, int x, int y, const uint16_t *rows, bool black)
{
    for (int row = 0; row < SCREENS_ICON_CELLS; row++) {
        for (int column = 0; column < SCREENS_ICON_CELLS; column++) {
            if (rows[row] >> (SCREENS_ICON_CELLS - 1 - column) & 1) {
                gfx_fill(g, x + column * ICON_CELL, y + row * ICON_CELL, ICON_CELL, ICON_CELL,
                         black);
            }
        }
    }
}

void screens_menu(gfx_t *g, screens_menu_row_fn row_at, void *context, int count, int selected)
{
    int visible_rows = (g->h - MENU_TOP_GAP) / MENU_ROW_HEIGHT;
    int first = screens_menu_first_row(count, selected, visible_rows);
    /* One column for the titles whether a row has an icon or not. */
    int title_left = MARGIN + SCREENS_ICON_CELLS * ICON_CELL + MENU_ICON_GAP;
    for (int row = 0; row < visible_rows && first + row < count; row++) {
        screens_menu_row_t item;
        row_at(context, first + row, &item);
        int  baseline = MENU_TOP_GAP + row * MENU_ROW_HEIGHT + TEXT_BASELINE;
        bool is_selected = first + row == selected;
        if (is_selected) {
            gfx_fill(g, MENU_BAR_INSET, baseline - MENU_BAR_RISE, g->w - 2 * MENU_BAR_INSET,
                     MENU_BAR_HEIGHT, true);
        }
        if (item.icon != NULL) {
            /* Standing on the baseline like a letter, two cells below its feet. */
            menu_icon(g, MARGIN, baseline - (SCREENS_ICON_CELLS - 2) * ICON_CELL, item.icon,
                      !is_selected);
        }
        gfx_text_font(g, title_left, baseline, gfx_font_system(), item.title, !is_selected);
    }
}
