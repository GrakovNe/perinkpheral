/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The smallest application: a greeting and a count of presses. It shows
 * the shape every application has - handlers change the state and ask for
 * a redraw, app_on_draw() paints the state.
 */
#include "app_text.h"
#include "host_api.h"

#define MARGIN 16

static int32_t presses;

static void draw_line(int32_t line, const char *text)
{
    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);
    gfx_text(MARGIN, MARGIN + line * line_height, HOST_FONT_TEXT, text, app_text_length(text));
}

void app_on_button(int32_t button, int32_t press)
{
    (void)button;
    if (press == HOST_PRESS_SHORT) {
        presses++;
        request_redraw();
    }
}

void app_on_draw(void)
{
    char counter_buffer[32];
    app_text_t counter;
    app_text_start(&counter, counter_buffer, sizeof counter_buffer);
    app_text_add(&counter, "Presses: ");
    app_text_add_number(&counter, presses);

    draw_line(0, "Hello from wasm");
    draw_line(2, counter_buffer);
}
