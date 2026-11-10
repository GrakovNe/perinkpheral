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

/* The host's header: the Wi-Fi state as of the last attempt on the left,
 * the date on the right, a rule along the bottom edge. */
typedef struct {
    bool        wifi; /* the last connection attempt succeeded */
    const char *date; /* "Wed 11 Nov"; NULL while the time is unknown */
} screens_header_t;

void screens_header(gfx_t *g, const screens_header_t *header);

typedef struct {
    const char *title;
    const char *hint; /* a few words on the right, may be NULL */
} screens_menu_item_t;

/* The view asks for the rows it is about to draw, so a list can be of any
 * length and live anywhere. */
typedef void (*screens_menu_item_fn)(void *context, int index, screens_menu_item_t *item);

/* A list with a square marker on the selected row; it scrolls when it is
 * longer than the surface. */
void screens_menu(gfx_t *g, screens_menu_item_fn item_at, void *context, int count, int selected);

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
