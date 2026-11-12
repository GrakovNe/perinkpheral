/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "screens.h"

#include <stddef.h>
#include <stdio.h>

#include "gfx_icons.h"
#include "list.h"
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
#define ICON_BASELINE_ROWS 9 /* rows that stand on the text baseline */
static void icon_signal(gfx_t *g, int x, int y, bool connected)
{
    gfx_icon(g, connected ? GFX_ICON_SIGNAL : GFX_ICON_NO_SIGNAL, x, y, ICON_CELL, true);
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

int screens_menu_first_row(int count, int selected, int visible_rows)
{
    return list_first_row(count, selected, visible_rows);
}

void screens_menu(gfx_t *g, screens_menu_row_fn row_at, void *context, int count, int selected)
{
    int visible_rows = list_visible_rows(g);
    int first = list_first_row(count, selected, visible_rows);
    list_draw_scrollbar(g, count, selected);
    for (int index = first; index < first + visible_rows && index < count; index++) {
        screens_menu_row_t item;
        row_at(context, index, &item);
        /* One column for the titles whether a row has an icon or not. */
        list_row_t row = { .label = item.title, .icon = LIST_NO_ICON, .picture = item.icon };
        list_draw_row(g, count, selected, index, &row, true);
    }
}
