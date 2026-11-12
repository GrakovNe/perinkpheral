/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A list the user moves through with UP and DOWN, as the host draws its
 * own: a row a line of text, the chosen one white on a black bar, and a
 * scroll bar down the right edge when there are more rows than fit. The
 * chosen row stays in the middle while there is more both ways. The host's
 * menu is this, and so is a list an application draws with ui_list_row().
 * Pure: tools/preview renders it. */

#define LIST_NO_ICON       (-1)
#define LIST_NONE_SELECTED (-1)
#define LIST_PICTURE_CELLS 11

/* Where a list stands and what it holds. It has the whole width of the
 * surface and, of its height, what is between `top` and `top + height` -
 * a title may stand over it, a row of buttons under it. */
typedef struct {
    int count;
    int selected; /* or LIST_NONE_SELECTED: facts, nobody chooses from them */
    int top;
    int height;   /* 0: down to the bottom of the surface */
    int first;    /* without a selection: the row the user scrolled to the top */
} list_t;

typedef struct {
    const char     *label;
    const char     *value;   /* at the right edge, cut to what the label leaves; NULL: none */
    int             icon;    /* a gfx_icon_id_t at the right edge, or LIST_NO_ICON */
    const uint16_t *picture; /* before the label: LIST_PICTURE_CELLS rows, the leftmost cell
                                the highest of that many bits; NULL: none */
} list_row_t;

/* How many rows the list shows at once. */
int list_visible_rows(const gfx_t *g, const list_t *list);

/* The first of them: the chosen row is kept in sight, or - with nothing
 * chosen - `first` as far as the list goes. */
int list_first_row(const gfx_t *g, const list_t *list);

/* Draws row number `index` where it belongs, and nothing when it is out
 * of sight. `has_pictures`: the labels stand in a column that leaves room
 * for pictures, whether this row has one or not. */
void list_draw_row(gfx_t *g, const list_t *list, int index, const list_row_t *row,
                   bool has_pictures);

/* The bar beside a list longer than its room; nothing for a shorter one. */
void list_draw_scrollbar(gfx_t *g, const list_t *list);

#ifdef __cplusplus
}
#endif
