/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The host's own icons: each on a canvas of eleven cells square, drawn in
 * the manner of the font - right angles, and a staircase where a line
 * would lean. A cell of two pixels is the size that stands beside the
 * host's text. The order is the contract's (HOST_ICON_* in host_api.h). */
#define GFX_ICON_CELLS 11

typedef enum {
    GFX_ICON_SIGNAL,
    GFX_ICON_NO_SIGNAL,
    GFX_ICON_UP,
    GFX_ICON_DOWN,
    GFX_ICON_LEFT,
    GFX_ICON_RIGHT,
    GFX_ICON_CHECK,
    GFX_ICON_CROSS,
    GFX_ICON_PLUS,
    GFX_ICON_MINUS,
    GFX_ICON_TRASH,
    GFX_ICON_GEAR,
    GFX_ICON_CLOCK,
    GFX_ICON_HOURGLASS,
    GFX_ICON_BELL,
    GFX_ICON_INFO,
    GFX_ICON_WARNING,
    GFX_ICON_PLAY,
    GFX_ICON_PAUSE,
    GFX_ICON_UPDATE,
    GFX_ICON_COUNT,
} gfx_icon_id_t;

/* The icon with its top left corner at (x, y), a cell `cell` pixels.
 * False for an id that is not one of the above. */
bool gfx_icon(gfx_t *g, gfx_icon_id_t id, int x, int y, int cell, bool black);

#ifdef __cplusplus
}
#endif
