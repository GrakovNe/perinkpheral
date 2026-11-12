/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The bar beside a list that is longer than the surface shows: a thin
 * line down the right edge and, on it, a thumb that is as much of the
 * line as the rows on the screen are of the list, and as far down it as
 * the first of them is down the list. A list that fits has none. Pure. */

/* What a list leaves free at its right edge for the bar. */
#define SCROLLBAR_ROOM 12

typedef struct {
    int top;    /* from the top of the track */
    int height;
} scrollbar_thumb_t;

/* Where the thumb is on a track `track_height` high: `visible` of `count`
 * rows are shown, from row `first`. Never shorter than can be seen, and
 * at the very end of the track when the last row is shown. */
scrollbar_thumb_t scrollbar_thumb(int track_height, int count, int visible, int first);

/* The bar at the right edge of the surface, from `top` down `height`
 * pixels. Draws nothing when all `count` rows are among the `visible`. */
void scrollbar_draw(gfx_t *g, int top, int height, int count, int visible, int first);

#ifdef __cplusplus
}
#endif
