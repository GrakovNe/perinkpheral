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

/* The connection icon on the icon canvas: 11 x 11 cells of the font's own
 * 2 px. Three bars that grow to the right, nothing but right angles. */
#define ICON_CELL          2
#define HEADER_BASELINE    30
#define HEADER_ICON_GAP    12 /* from the icon to the text beside it */
#define HEADER_TEXTS_GAP   14 /* the least between the two texts: a character */
#define HEADER_TEXT_BUFFER 48

/* An icon beside the header's text, placed by its ink and not by its
 * canvas: the ink begins (or, `from_right`, ends) at `edge` and is as much
 * above the capitals as below them. Returns the ink's width. */
static int header_icon(gfx_t *g, int icon, int edge, bool from_right)
{
    gfx_icon_ink_t ink;
    if (!gfx_icon_ink((gfx_icon_id_t)icon, &ink)) {
        return 0;
    }
    int width = (ink.last_column - ink.first_column + 1) * ICON_CELL;
    int height = (ink.last_row - ink.first_row + 1) * ICON_CELL;
    int cap = gfx_font_cap_height(gfx_font_system());
    int ink_left = from_right ? edge - width : edge;
    int ink_top = HEADER_BASELINE - cap / 2 - height / 2;
    gfx_icon(g, (gfx_icon_id_t)icon, ink_left - ink.first_column * ICON_CELL,
             ink_top - ink.first_row * ICON_CELL, ICON_CELL, true);
    return width;
}

void screens_header(gfx_t *g, const screens_header_t *header)
{
    const gfx_font_t *font = gfx_font_system();
    int text_left = MARGIN, text_right = g->w - MARGIN;
    if (header->left_icon != SCREENS_NO_ICON) {
        text_left += header_icon(g, header->left_icon, MARGIN, false) + HEADER_ICON_GAP;
    }
    if (header->right_icon != SCREENS_NO_ICON) {
        text_right -= header_icon(g, header->right_icon, g->w - MARGIN, true) + HEADER_ICON_GAP;
    }

    const char *left = header->left != NULL ? header->left : "";
    const char *right = header->right != NULL ? header->right : "";
    int available = text_right - text_left - HEADER_TEXTS_GAP;
    int left_max, right_max;
    wm_header_share(gfx_text_font_width(font, left), gfx_text_font_width(font, right), available,
                    &left_max, &right_max);
    char fitted[HEADER_TEXT_BUFFER];
    gfx_text_fit(font, left, left_max, fitted, sizeof fitted);
    gfx_text_font(g, text_left, HEADER_BASELINE, font, fitted, true);
    gfx_text_fit(font, right, right_max, fitted, sizeof fitted);
    gfx_text_font(g, text_right - gfx_text_font_width(font, fitted), HEADER_BASELINE, font, fitted,
                  true);
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
