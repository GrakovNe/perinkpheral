/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_draw.h"

#include <stddef.h>

#include "app_text.h"
#include "host_api.h"

int32_t app_text_width(int32_t font, const char *text)
{
    return gfx_text_width(font, text, app_text_length(text));
}

int32_t app_draw_text(int32_t x, int32_t y, int32_t font, const char *text)
{
    return gfx_draw_text(x, y, font, text, app_text_length(text));
}

int32_t app_text_center_x(int32_t center, int32_t font, const char *text)
{
    int32_t         length = app_text_length(text);
    host_text_ink_t ink;
    if (gfx_text_ink(font, text, length, &ink) != HOST_OK || ink.end_x <= ink.first_x) {
        return center - gfx_text_width(font, text, length) / 2; /* no ink - an empty text - or no such font */
    }
    return center - (ink.first_x + ink.end_x) / 2;
}

void app_draw_centered(int32_t center, int32_t y, int32_t font, const char *text)
{
    gfx_draw_text(app_text_center_x(center, font, text), y, font, text, app_text_length(text));
}

void app_draw_right(int32_t right, int32_t y, int32_t font, const char *text)
{
    int32_t length = app_text_length(text);
    gfx_draw_text(right - gfx_text_width(font, text, length), y, font, text, length);
}

void app_draw_actions(const char *ok, const char *back)
{
    ok = ok != NULL ? ok : "";
    back = back != NULL ? back : "";
    ui_draw_actions(ok, app_text_length(ok), back, app_text_length(back));
}

void app_draw_cells(host_cell_t *cells, const uint8_t *const *pictures, const int32_t *icons,
                    int32_t count)
{
    int32_t size = 0;
    for (int32_t i = 0; i < count; i++) {
        int32_t fits = ui_cell_fit(&cells[i], pictures != NULL ? pictures[i] : 0,
                                   icons != NULL ? icons[i] : HOST_ICON_NONE);
        size = fits > size ? fits : size;
    }
    for (int32_t i = 0; i < count; i++) {
        cells[i].size = size;
        ui_draw_cell(&cells[i], pictures != NULL ? pictures[i] : 0, icons != NULL ? icons[i] : HOST_ICON_NONE);
    }
}
