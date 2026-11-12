/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "list.h"

#include "gfx_icons.h"
#include "scrollbar.h"

#define MARGIN       16
#define TOP_GAP      8
#define ROW_HEIGHT   42
#define ROW_BASELINE 30 /* under the row's top */
/* The bar behind the chosen row. */
#define BAR_INSET    8  /* from both sides of the surface */
#define BAR_HEIGHT   36
#define BAR_RISE     26 /* its top above the row's baseline */
#define CELL         2  /* of a picture and of an icon */
#define PICTURE_GAP  12 /* between a picture and the label */
#define VALUE_GAP    14 /* the least between a label and its value */
#define VALUE_MAX    63

static int height_of(const gfx_t *g, const list_t *list)
{
    int room = g->h - list->top;
    return list->height > 0 && list->height < room ? list->height : room;
}

int list_visible_rows(const gfx_t *g, const list_t *list)
{
    int rows = (height_of(g, list) - TOP_GAP) / ROW_HEIGHT;
    return rows > 0 ? rows : 0;
}

int list_first_row(const gfx_t *g, const list_t *list)
{
    int visible = list_visible_rows(g, list);
    int last_first = list->count > visible ? list->count - visible : 0;
    /* Chosen: in the middle while there is more both ways. Not chosen:
     * where the user scrolled to. */
    int first = list->selected >= 0 ? list->selected - visible / 2 : list->first;
    if (first > last_first) {
        first = last_first;
    }
    return first > 0 ? first : 0;
}

static void draw_picture(gfx_t *g, int x, int y, const uint16_t *rows, bool black)
{
    for (int row = 0; row < LIST_PICTURE_CELLS; row++) {
        for (int column = 0; column < LIST_PICTURE_CELLS; column++) {
            if (rows[row] >> (LIST_PICTURE_CELLS - 1 - column) & 1) {
                gfx_fill(g, x + column * CELL, y + row * CELL, CELL, CELL, black);
            }
        }
    }
}

void list_draw_scrollbar(gfx_t *g, const list_t *list)
{
    int visible = list_visible_rows(g, list);
    if (visible == 0) {
        return;
    }
    scrollbar_draw(g, list->top + TOP_GAP + ROW_BASELINE - BAR_RISE,
                   (visible - 1) * ROW_HEIGHT + BAR_HEIGHT, list->count, visible,
                   list_first_row(g, list));
}

void list_draw_row(gfx_t *g, const list_t *list, int index, const list_row_t *row,
                   bool has_pictures)
{
    int visible = list_visible_rows(g, list);
    int slot = index - list_first_row(g, list);
    if (index < 0 || index >= list->count || slot < 0 || slot >= visible) {
        return;
    }
    const gfx_font_t *font = gfx_font_system();
    int  baseline = list->top + TOP_GAP + slot * ROW_HEIGHT + ROW_BASELINE;
    /* A list longer than its room gives its right edge to the bar. */
    int  right = g->w - (list->count > visible ? SCROLLBAR_ROOM : 0);
    bool is_selected = index == list->selected;
    if (is_selected) {
        gfx_fill(g, BAR_INSET, baseline - BAR_RISE, right - 2 * BAR_INSET, BAR_HEIGHT, true);
    }
    int label_left = MARGIN;
    if (has_pictures) {
        label_left += LIST_PICTURE_CELLS * CELL + PICTURE_GAP;
    }
    if (row->picture != NULL) {
        /* Standing on the baseline like a letter, two cells below its feet. */
        draw_picture(g, MARGIN, baseline - (LIST_PICTURE_CELLS - 2) * CELL, row->picture,
                     !is_selected);
    }
    int label_right = label_left + gfx_text_font(g, label_left, baseline, font, row->label,
                                                 !is_selected);
    int value_right = right - MARGIN;
    if (row->icon != LIST_NO_ICON) {
        int icon_left = value_right - GFX_ICON_CELLS * CELL;
        gfx_icon(g, (gfx_icon_id_t)row->icon, icon_left, baseline - (GFX_ICON_CELLS - 1) * CELL,
                 CELL, !is_selected);
        value_right = icon_left - VALUE_GAP;
    }
    if (row->value != NULL && row->value[0] != '\0') {
        char fitted[VALUE_MAX + 1];
        gfx_text_fit(font, row->value, value_right - label_right - VALUE_GAP, fitted,
                     sizeof fitted);
        gfx_text_font(g, value_right - gfx_text_font_width(font, fitted), baseline, font, fitted,
                      !is_selected);
    }
}
