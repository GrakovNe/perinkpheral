/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * How to give the device a Wi-Fi network: shown when it has none. A code
 * that joins a phone to the device's own access point, and under it the
 * same in words for whoever has no camera - the name of the access point
 * and the address its page is at. A system application.
 */
#include "app_text.h"
#include "host_api.h"

#define TEXT_MAX       95
#define TOP_GAP        8
#define LINE_HEIGHT    32
#define TEXT_LINES     2
#define TEXT_DESCENT   10 /* from a line's bottom up to its baseline */
#define QUIET_MODULES  4  /* the white a reader wants around the code */

/* Any press is enough to leave it. */
void app_on_button(int32_t event)
{
    if (BTN_ACTION_OF(event) == BTN_PRESSED) {
        request_exit();
    }
}

static void read_info(int32_t what, char value[TEXT_MAX + 1])
{
    int32_t length = sys_info(what, value, TEXT_MAX);
    value[length > 0 ? length : 0] = '\0';
}

static void draw_line(int32_t line, const char *verb, const char *what)
{
    char text[TEXT_MAX + 1];
    app_text_t out;
    app_text_start(&out, text, sizeof text);
    app_text_add(&out, verb);
    app_text_add(&out, what);
    int32_t length = app_text_length(text);
    int32_t baseline = gfx_height() - (TEXT_LINES - line) * LINE_HEIGHT + LINE_HEIGHT -
                       TEXT_DESCENT;
    gfx_text((gfx_width() - gfx_text_width(HOST_FONT_TEXT, text, length)) / 2,
             baseline - gfx_font_ascent(HOST_FONT_TEXT), HOST_FONT_TEXT, text, length);
}

void app_on_draw(void)
{
    char value[TEXT_MAX + 1];

    /* The code as large as fits over the two lines of text, in the middle. */
    read_info(SYS_INFO_JOIN_CODE, value);
    int32_t length = app_text_length(value);
    int32_t modules = gfx_qr(0, 0, 0, value, length);
    int32_t height = gfx_height() - TEXT_LINES * LINE_HEIGHT - TOP_GAP;
    int32_t cell = modules > 0 ? height / (modules + 2 * QUIET_MODULES) : 0;
    if (cell > 0) {
        gfx_qr((gfx_width() - modules * cell) / 2, TOP_GAP + (height - modules * cell) / 2, cell,
               value, length);
    }

    read_info(SYS_INFO_NAME, value);
    draw_line(0, "Join ", value);
    read_info(SYS_INFO_ADDRESS, value);
    draw_line(1, "Open ", value);
}
