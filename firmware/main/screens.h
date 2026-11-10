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

/* The host's own screens, drawn into a framebuffer. Pure: no ESP-IDF, so
 * tools/preview can render them on the host. */

/* The brand splash. */
void screens_splash(gfx_t *g);

/* The header the host draws above its own screens and above every
 * application that is not full-screen: the Wi-Fi state as of the last
 * attempt on the left, the date on the right. */
typedef struct {
    bool        wifi; /* the last connection attempt succeeded */
    const char *date; /* "Wed 11 Nov"; NULL while the time is unknown */
} screens_header_t;

void screens_header(gfx_t *g, const screens_header_t *h);

typedef struct {
    const char *title;
    const char *hint; /* a few words on the right, may be NULL */
} screens_menu_item_t;

/* The host menu: a list with a square marker on the selected row. The
 * list scrolls when it is longer than the screen. */
void screens_menu(gfx_t *g, const screens_menu_item_t *items, int count, int selected,
                  const screens_header_t *header);

typedef struct {
    const char *label;
    const char *value;
} screens_field_t;

/* A table of label / value rows, for "about this device". */
void screens_fields(gfx_t *g, const screens_field_t *fields, int count,
                    const screens_header_t *header);

#ifdef __cplusplus
}
#endif
