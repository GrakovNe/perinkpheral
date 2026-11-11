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

/* The brand splash, for the whole screen. */
void screens_splash(gfx_t *g);

/* The host's header: the Wi-Fi icon on the left, struck through when
 * the last connection attempt failed, a text beside it and a text against
 * the right edge, a rule along the bottom edge. A text that does not fit
 * is cut with "...", and the two do not meet. */
typedef struct {
    bool        wifi;  /* the last connection attempt succeeded */
    const char *left;  /* NULL: nothing */
    const char *right; /* "Wed 11 Nov"; NULL: nothing */
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

/* What to do to give the device a Wi-Fi network. */
void screens_setup(gfx_t *g, const char *access_point_name, const char *address);

typedef struct {
    const char *label;
    const char *value;
} screens_field_t;

/* A table of label / value rows. */
void screens_fields(gfx_t *g, const screens_field_t *fields, int count);

#ifdef __cplusplus
}
#endif
