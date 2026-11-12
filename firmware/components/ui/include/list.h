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
#define LIST_PICTURE_CELLS 11

typedef struct {
    const char     *label;
    const char     *value;   /* at the right edge, cut to what the label leaves; NULL: none */
    int             icon;    /* a gfx_icon_id_t at the right edge, or LIST_NO_ICON */
    const uint16_t *picture; /* before the label: LIST_PICTURE_CELLS rows, the leftmost cell
                                the highest of that many bits; NULL: none */
} list_row_t;

/* How many rows the surface shows at once. */
int list_visible_rows(const gfx_t *g);

/* The first of them, in a list of `count` with row `selected` chosen. */
int list_first_row(int count, int selected, int visible_rows);

/* Draws row number `index` of the list where it belongs on the surface,
 * and nothing when it is out of sight. `has_pictures`: the labels stand
 * in a column that leaves room for pictures, whether this row has one or
 * not. */
void list_draw_row(gfx_t *g, int count, int selected, int index, const list_row_t *row,
                   bool has_pictures);

/* The bar beside a list longer than the surface; nothing for a shorter one. */
void list_draw_scrollbar(gfx_t *g, int count, int selected);

#ifdef __cplusplus
}
#endif
