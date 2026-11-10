/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

/* Five rows of five cells ('#' ink) on the stroke/gap grid, plus
 * diagonals as (x0, y0, x1, y1) cell quadruples ended by -1. */
typedef struct {
    const char   *rows[5];
    const int8_t *diags;
} sq_glyph_t;

const sq_glyph_t *gfx_font_square_glyph(uint32_t cp);
