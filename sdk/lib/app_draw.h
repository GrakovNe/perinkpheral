/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Text drawing for null-terminated strings. The host calls take a pointer
 * and a length; these take a string. Valid inside app_on_draw().
 */
#pragma once

#include <stdint.h>

#include "host_api.h"

/* Width of `text` in `font` (HOST_FONT_*), in pixels. */
int32_t app_text_width(int32_t font, const char *text);

/* Draws `text` with its left edge at `x` and the top of its line at `y`.
 * Returns the width drawn. */
int32_t app_draw_text(int32_t x, int32_t y, int32_t font, const char *text);

/* Where `text` starts for its ink to stand in the middle of the column
 * `center`: what is seen is centred, not the room its glyphs take, which in
 * some typefaces is more on one side. A text with no ink is centred by its
 * room. Needs gfx_text_ink: firmware 0.1.8. */
int32_t app_text_center_x(int32_t center, int32_t font, const char *text);

/* Draws `text` centred on the column `center`, by its ink. */
void app_draw_centered(int32_t center, int32_t y, int32_t font, const char *text);

/* Draws `text` with its right edge at `right`. */
void app_draw_right(int32_t right, int32_t y, int32_t font, const char *text);

/* The row of buttons along the bottom: what OK and BACK do. NULL or "" is
 * no button. */
void app_draw_actions(const char *ok, const char *back);

/* Cells that stand together, drawn in one size: the size the longest of
 * their values can have (ui_cell_fit), so that the values read as one row.
 * `size` of every cell is set. `pictures` and `icons` may be NULL for none;
 * otherwise one each, 0 and HOST_ICON_NONE for a cell without. What goes in
 * a cell and where the cells stand are the application's. */
void app_draw_cells(host_cell_t *cells, const uint8_t *const *pictures, const int32_t *icons,
                    int32_t count);
