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

/* List navigated with UP and DOWN. One row is one line of text. The
 * selected row is white on a black bar and is kept in the middle while
 * rows remain in both directions. A scroll bar is drawn at the right edge
 * when the rows exceed the height. Used by the host menu and by
 * ui_list_row(). Pure; rendered by tools/preview. */

#define LIST_NO_ICON       (-1)
#define LIST_NONE_SELECTED (-1)
#define LIST_PICTURE_CELLS 11

/* Position and state of a list. The list takes the whole surface width
 * and the rows from `top` to `top + height`. */
typedef struct {
    int count;
    int selected; /* LIST_NONE_SELECTED: a read-only list */
    int top;
    int height;   /* 0: to the bottom of the surface */
    int first;    /* read-only list: index of the top row */
} list_t;

typedef struct {
    const char     *label;
    const char     *value;   /* right-aligned; NULL: none. Up to half the row is kept whole */
    int             icon;    /* gfx_icon_id_t at the right edge, or LIST_NO_ICON */
    const uint16_t *picture; /* before the label: LIST_PICTURE_CELLS rows of LIST_PICTURE_CELLS
                                bits, leftmost cell in the highest bit; NULL: none */
} list_row_t;

/* Number of rows visible at once. */
int list_visible_rows(const gfx_t *g, const list_t *list);

/* Index of the first visible row: derived from `selected`, or `first`
 * clamped to the list for a read-only list. */
int list_first_row(const gfx_t *g, const list_t *list);

/* Draws row `index` at its position; nothing when the row is not visible.
 * `has_pictures`: labels are indented by the picture column in every row. */
void list_draw_row(gfx_t *g, const list_t *list, int index, const list_row_t *row,
                   bool has_pictures);

/* Draws the scroll bar; nothing when all rows are visible. */
void list_draw_scrollbar(gfx_t *g, const list_t *list);

#ifdef __cplusplus
}
#endif
