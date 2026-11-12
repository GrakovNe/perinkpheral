/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scrollbar.h"

#define THUMB_WIDTH      5
#define THUMB_FROM_EDGE  4  /* from the right edge of the surface to the thumb */
#define TRACK_WIDTH      1  /* down the middle of the thumb */
#define THUMB_HEIGHT_MIN 12

scrollbar_thumb_t scrollbar_thumb(int track_height, int count, int visible, int first)
{
    scrollbar_thumb_t thumb = { 0, track_height };
    if (count <= visible || visible <= 0) {
        return thumb;
    }
    thumb.height = track_height * visible / count;
    if (thumb.height < THUMB_HEIGHT_MIN) {
        thumb.height = THUMB_HEIGHT_MIN;
    }
    /* The first row can be this far down the list at most: then the thumb
     * is at the end of the track, to the pixel. */
    int last_first = count - visible;
    if (first > last_first) {
        first = last_first;
    }
    thumb.top = (track_height - thumb.height) * (first > 0 ? first : 0) / last_first;
    return thumb;
}

void scrollbar_draw(gfx_t *g, int top, int height, int count, int visible, int first)
{
    if (count <= visible) {
        return;
    }
    int thumb_left = g->w - THUMB_FROM_EDGE - THUMB_WIDTH;
    gfx_fill(g, thumb_left + (THUMB_WIDTH - TRACK_WIDTH) / 2, top, TRACK_WIDTH, height, true);
    scrollbar_thumb_t thumb = scrollbar_thumb(height, count, visible, first);
    gfx_fill(g, thumb_left, top + thumb.top, THUMB_WIDTH, thumb.height, true);
}
