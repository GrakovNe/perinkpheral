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
 * host's text. An icon may also be drawn on 22 and 44 cells, with lines
 * as much thicker: drawn larger, it takes the finest drawing that fits,
 * so a curve or a slant stays smooth where the eleven would be steps.
 * The drawings are firmware/components/gfx/icons, made into
 * gfx_icon_art.c by tools/fontgen/mkicons.py. The order is the
 * contract's (HOST_ICON_* in host_api.h). */
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
    GFX_ICON_SUN,
    GFX_ICON_PARTLY_CLOUDY,
    GFX_ICON_CLOUD,
    GFX_ICON_RAIN,
    GFX_ICON_SNOW,
    GFX_ICON_THUNDER,
    GFX_ICON_WIND,
    GFX_ICON_UMBRELLA,
    GFX_ICON_DROP,
    GFX_ICON_THERMOMETER,
    GFX_ICON_BOLT,
    GFX_ICON_PLUG,
    GFX_ICON_BATTERY,
    GFX_ICON_HOME,
    GFX_ICON_BULB,
    GFX_ICON_DOOR,
    GFX_ICON_MOON,
    GFX_ICON_CHIP,
    GFX_ICON_NETWORK,
    GFX_ICON_GAUGE,
    GFX_ICON_CO2,
    GFX_ICON_SUNRISE,
    GFX_ICON_SUNSET,
    GFX_ICON_COUNT,
} gfx_icon_id_t;

/* The icon with its top left corner at (x, y), in a square of 11 cells of
 * `cell` pixels. False for an id that is not one of the above. */
bool gfx_icon(gfx_t *g, gfx_icon_id_t id, int x, int y, int cell, bool black);

/* The pixels of an icon drawn at `cell` that have ink in them, from its top
 * left corner: an icon does not fill its square, and what stands beside
 * text is placed by what is seen. False for no such icon, or no ink. */
typedef struct {
    int first_x, last_x;
    int first_y, last_y;
} gfx_icon_ink_t;

bool gfx_icon_ink(gfx_icon_id_t id, int cell, gfx_icon_ink_t *ink);


#ifdef __cplusplus
}
#endif
