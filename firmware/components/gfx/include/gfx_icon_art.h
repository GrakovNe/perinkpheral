/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx_icons.h"

/* The drawings of the host's icons (gfx_icon_art.c, made by
 * tools/fontgen/mkicons.py): an icon has a drawing of 11 cells square, and
 * may have one of 22 and one of 44, each a bit a cell, row by row, a row
 * in whole bytes. */
#define GFX_ICON_DRAWINGS 3 /* of 11, 22 and 44 cells */

typedef struct {
    uint8_t  cells; /* 0: no drawing of this size */
    bool     made;  /* an eleven made from a larger drawing, not drawn */
    uint16_t at;    /* where it begins in GFX_ICON_BITS */
} gfx_icon_drawing_t;

typedef struct {
    gfx_icon_drawing_t drawings[GFX_ICON_DRAWINGS];
} gfx_icon_art_t;

extern const uint8_t        GFX_ICON_BITS[];
extern const gfx_icon_art_t GFX_ICON_ART[GFX_ICON_COUNT];
