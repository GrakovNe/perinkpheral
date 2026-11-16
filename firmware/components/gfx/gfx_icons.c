/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "gfx_icons.h"

#include "gfx_icon_art.h"

/* The drawing an icon is drawn from in a square of `side` pixels, and how
 * many pixels a cell of it takes: the finest one drawn at two pixels a cell
 * or more - a line of a drawing is meant for twice its cells, as the text's
 * lines are two pixels at the text's size - and below that, the finest
 * that fits at all. An eleven made from a larger drawing comes last: where
 * one of the icon's own fits, that is drawn instead. Every icon has an
 * eleven, which fits any. */
static int rank(const gfx_icon_drawing_t *drawing, int side)
{
    if (drawing->cells == 0 || drawing->cells > side) {
        return -1;
    }
    int tier = drawing->made ? 0 : side / drawing->cells >= 2 ? 2 : 1;
    return tier * 100 + drawing->cells;
}

static const gfx_icon_drawing_t *drawing_for(gfx_icon_id_t id, int side, int *scale)
{
    const gfx_icon_drawing_t *best = NULL;
    int best_rank = -1;
    for (int i = 0; i < GFX_ICON_DRAWINGS; i++) {
        const gfx_icon_drawing_t *drawing = &GFX_ICON_ART[id].drawings[i];
        int r = rank(drawing, side);
        if (r > best_rank) {
            best = drawing;
            best_rank = r;
        }
    }
    if (best != NULL) {
        *scale = side / best->cells;
    }
    return best;
}

static bool is_ink(const gfx_icon_drawing_t *drawing, int row, int column)
{
    int row_bytes = (drawing->cells + 7) / 8;
    uint8_t byte = GFX_ICON_BITS[drawing->at + row * row_bytes + column / 8];
    return (byte >> (7 - column % 8)) & 1;
}

bool gfx_icon(gfx_t *g, gfx_icon_id_t id, int x, int y, int cell, bool black)
{
    if ((int)id < 0 || id >= GFX_ICON_COUNT || cell < 1) {
        return false;
    }
    int side = GFX_ICON_CELLS * cell;
    int scale = 1;
    const gfx_icon_drawing_t *drawing = drawing_for(id, side, &scale);
    if (drawing == NULL) {
        return false;
    }
    /* A drawing that does not fill the square to the pixel stands in its middle. */
    int offset = (side - drawing->cells * scale) / 2;
    for (int row = 0; row < drawing->cells; row++) {
        /* A run of ink in a row is one rectangle. */
        for (int column = 0; column < drawing->cells;) {
            if (!is_ink(drawing, row, column)) {
                column++;
                continue;
            }
            int run_start = column;
            while (column < drawing->cells && is_ink(drawing, row, column)) {
                column++;
            }
            gfx_fill(g, x + offset + run_start * scale, y + offset + row * scale,
                     (column - run_start) * scale, scale, black);
        }
    }
    return true;
}

bool gfx_icon_ink(gfx_icon_id_t id, int cell, gfx_icon_ink_t *ink)
{
    if ((int)id < 0 || id >= GFX_ICON_COUNT || cell < 1) {
        return false;
    }
    int side = GFX_ICON_CELLS * cell;
    int scale = 1;
    const gfx_icon_drawing_t *drawing = drawing_for(id, side, &scale);
    if (drawing == NULL) {
        return false;
    }
    int first_row = drawing->cells, last_row = -1, first_column = drawing->cells, last_column = -1;
    for (int row = 0; row < drawing->cells; row++) {
        for (int column = 0; column < drawing->cells; column++) {
            if (!is_ink(drawing, row, column)) {
                continue;
            }
            first_row = row < first_row ? row : first_row;
            last_row = row > last_row ? row : last_row;
            first_column = column < first_column ? column : first_column;
            last_column = column > last_column ? column : last_column;
        }
    }
    if (last_row < 0) {
        return false;
    }
    int offset = (side - drawing->cells * scale) / 2;
    *ink = (gfx_icon_ink_t){ offset + first_column * scale, offset + (last_column + 1) * scale - 1,
                             offset + first_row * scale, offset + (last_row + 1) * scale - 1 };
    return true;
}
