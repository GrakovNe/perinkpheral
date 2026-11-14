/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_draw.h"

#include "app_text.h"
#include "host_api.h"

int32_t app_text_width(int32_t font, const char *text)
{
    return gfx_text_width(font, text, app_text_length(text));
}

int32_t app_draw_text(int32_t x, int32_t y, int32_t font, const char *text)
{
    return gfx_text(x, y, font, text, app_text_length(text));
}

void app_draw_centered(int32_t center, int32_t y, int32_t font, const char *text)
{
    int32_t length = app_text_length(text);
    gfx_text(center - gfx_text_width(font, text, length) / 2, y, font, text, length);
}

void app_draw_right(int32_t right, int32_t y, int32_t font, const char *text)
{
    int32_t length = app_text_length(text);
    gfx_text(right - gfx_text_width(font, text, length), y, font, text, length);
}
