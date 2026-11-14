/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Text drawing for null-terminated strings. The host calls take a pointer
 * and a length; these take a string. Valid inside app_on_draw().
 */
#pragma once

#include <stdint.h>

/* Width of `text` in `font` (HOST_FONT_*), in pixels. */
int32_t app_text_width(int32_t font, const char *text);

/* Draws `text` with its left edge at `x` and the top of its line at `y`.
 * Returns the width drawn. */
int32_t app_draw_text(int32_t x, int32_t y, int32_t font, const char *text);

/* Draws `text` centred on the column `center`. */
void app_draw_centered(int32_t center, int32_t y, int32_t font, const char *text);

/* Draws `text` with its right edge at `right`. */
void app_draw_right(int32_t right, int32_t y, int32_t font, const char *text);
