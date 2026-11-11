/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The smallest application: a greeting and a count of presses. It shows
 * the shape every application has - handlers change the state and ask for
 * a redraw, app_on_draw() paints the state - and where the state waits
 * while the application is not on the screen: the host unloads it then,
 * so it reads the count back when it starts and saves it when it leaves.
 */
#include "app_text.h"
#include "host_api.h"

#define MARGIN      16

static const char PRESSES_KEY[] = "presses";
#define PRESSES_KEY_LENGTH ((int32_t)sizeof PRESSES_KEY - 1)

static int32_t presses;

static void draw_line(int32_t line, const char *text)
{
    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);
    gfx_text(MARGIN, MARGIN + line * line_height, HOST_FONT_TEXT, text, app_text_length(text));
}

void app_init(void)
{
    /* Nothing stored yet leaves the count at zero. */
    kv_get(PRESSES_KEY, PRESSES_KEY_LENGTH, &presses, sizeof presses);
}

void app_on_blur(void)
{
    kv_set(PRESSES_KEY, PRESSES_KEY_LENGTH, &presses, sizeof presses);
}

void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_BACK_PRESSED:
        request_exit();
        break;
    case BTN_UP_PRESSED:
    case BTN_OK_PRESSED:
    case BTN_DOWN_PRESSED:
        presses++;
        request_redraw(HOST_REDRAW_NORMAL);
        break;
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
