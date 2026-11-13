/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scroll bar of a list: a track at the right edge and a thumb on it.
 * Thumb height / track height = visible rows / all rows.
 * Thumb offset / track height = first visible row / all rows.
 * Not drawn when all rows are visible. Pure. */

/* Width reserved for the bar at the right edge of a list. */
#define SCROLLBAR_ROOM 12

typedef struct {
    int top;    /* offset from the top of the track */
    int height;
} scrollbar_thumb_t;

/* Thumb on a track of `track_height` for `visible` of `count` rows shown
 * from row `first`. The thumb has a minimum height and reaches the end of
 * the track when the last row is shown. */
scrollbar_thumb_t scrollbar_thumb(int track_height, int count, int visible, int first);

/* Draws the bar at the right edge of the surface from `top`, `height`
 * pixels high. Nothing is drawn when `visible` >= `count`. */
void scrollbar_draw(gfx_t *g, int top, int height, int count, int visible, int first);

#ifdef __cplusplus
}
#endif
