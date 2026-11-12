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

/* The host's views: each turns a small struct into pixels on the surface
 * it is given and lays out against that surface's own size, so the same
 * view works on the whole screen and under the header. They know nothing
 * about where the data came from. Pure: tools/preview renders them
 * without the device. */

/* The host's header: on the left an icon and a text beside it, on the
 * right a text and an icon after it, a rule along the bottom edge -
 * icon text ... text icon. A text that does not fit is cut with "...",
 * and the two do not meet. */
#define SCREENS_NO_ICON (-1)
typedef struct {
    int         left_icon;  /* a gfx_icon_id_t, or SCREENS_NO_ICON */
    const char *left;       /* NULL: nothing */
    const char *right;      /* "Wed 11 Nov"; NULL: nothing */
    int         right_icon; /* a gfx_icon_id_t, or SCREENS_NO_ICON */
} screens_header_t;

void screens_header(gfx_t *g, const screens_header_t *header);


/* The view asks for the rows it is about to draw, so a list can be of any
 * length and live anywhere. */
#define SCREENS_ICON_CELLS 11

typedef struct {
    const char     *title;
    const uint16_t *icon; /* SCREENS_ICON_CELLS rows, the leftmost cell the highest of
                             that many bits; NULL: a row without an icon */
} screens_menu_row_t;

typedef void (*screens_menu_row_fn)(void *context, int index, screens_menu_row_t *row);

/* A list of titles, each with its icon when it has one; the selected row
 * is white on a black bar. It scrolls when it is longer than the surface. */
void screens_menu(gfx_t *g, screens_menu_row_fn row_at, void *context, int count, int selected);

/* The index of the top row shown: the window keeps the selection in the
 * middle and stops at both ends of the list. */
int screens_menu_first_row(int count, int selected, int visible_rows);

#ifdef __cplusplus
}
#endif
