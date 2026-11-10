/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Not an application anyone would use: test_app.c loads it into a real
 * interpreter and reads what it did off the pixels. Every row it paints
 * answers one question about the host API.
 */
#include "host_api.h"

#define ROW_PRESSES        0  /* as many pixels as the buttons pressed add up to */
#define ROW_TIMER          2  /* timer id + 1 pixels, once a timer fired */
#define ROW_NOT_DRAWING    4  /* one pixel: drawing outside app_on_draw was refused */
#define ROW_BAD_ARGUMENTS  6  /* pixel 0: a wild pointer refused; pixel 2: an unknown font */
#define ROW_MEASURES       8  /* pixels 0 2 4 6: text width, font height, uptime, wall time */
#define ROW_MORE_REFUSALS   10 /* pixels 0..10: see refusals[] in app_on_draw */
#define ROW_BITMAP         12
#define ROW_LINE           14
#define ROW_TEXT           20

#define BUTTON_DRAW_OUTSIDE 3
#define BUTTON_TRAP         4
#define TIMER_ON_FOCUS      1
#define UNKNOWN_FONT        7
#define FAKE_UPTIME_MS      1234 /* what test_app.c's clock answers */
#define OFF_THE_SURFACE     300
#define HUGE                2000000000

static int32_t presses;
static int32_t fired_timer = -1;
static int32_t result_outside_draw;

static void mark(int32_t x, int32_t row)
{
    gfx_rect(x, row, 1, 1, HOST_RECT_FILLED);
}

void app_init(void)
{
    host_log(HOST_LOG_INFO, "init", 4);
}

void app_on_focus(void)
{
    timer_set(TIMER_ON_FOCUS, 500);
}

void app_on_blur(void)
{
    host_log(HOST_LOG_INFO, "blur", 4);
}

void app_on_button(int32_t button, int32_t press)
{
    (void)press;
    if (button == BUTTON_TRAP) {
        __builtin_trap();
    }
    if (button == BUTTON_DRAW_OUTSIDE) {
        result_outside_draw = gfx_rect(0, 0, 1, 1, HOST_RECT_FILLED);
        return;
    }
    presses += button;
    request_redraw();
}

void app_on_timer(int32_t id)
{
    fired_timer = id;
    request_redraw();
}

void app_on_draw(void)
{
    gfx_rect(0, ROW_PRESSES, presses, 1, HOST_RECT_FILLED);
    if (fired_timer >= 0) {
        gfx_rect(0, ROW_TIMER, fired_timer + 1, 1, HOST_RECT_FILLED);
    }
    if (result_outside_draw == HOST_ERR_NOT_DRAWING) {
        mark(0, ROW_NOT_DRAWING);
    }

    const char *far_outside_memory = (const char *)0xFFFFFF00u;
    if (gfx_text(0, ROW_TEXT, HOST_FONT_TEXT, far_outside_memory, 16) == HOST_ERR_ARGUMENT) {
        mark(0, ROW_BAD_ARGUMENTS);
    }
    if (gfx_text(0, ROW_TEXT, UNKNOWN_FONT, "x", 1) == HOST_ERR_ARGUMENT) {
        mark(2, ROW_BAD_ARGUMENTS);
    }

    int32_t one_glyph = gfx_text_width(HOST_FONT_TEXT, "I", 1);
    if (one_glyph > 0 && gfx_text_width(HOST_FONT_TEXT, "II", 2) == 2 * one_glyph) {
        mark(0, ROW_MEASURES);
    }
    if (gfx_font_height(HOST_FONT_TEXT) > 0 && gfx_font_height(UNKNOWN_FONT) == HOST_ERR_ARGUMENT) {
        mark(2, ROW_MEASURES);
    }
    if (time_uptime_ms() == FAKE_UPTIME_MS) {
        mark(4, ROW_MEASURES);
    }
    if (time_now() == 0) {
        mark(6, ROW_MEASURES);
    }

    /* Longer than the host takes at once: cut, not refused. */
    static char long_text[400];
    for (int32_t i = 0; i < (int32_t)sizeof long_text; i++) {
        long_text[i] = 'I';
    }
    const uint8_t *bits_outside_memory = (const uint8_t *)far_outside_memory;
    const int32_t refusals[] = {
        timer_set(HOST_TIMER_COUNT, 10),
        timer_set(0, -1),
        gfx_bitmap(0, 0, 8, 8, bits_outside_memory),
        gfx_text(0, 0, HOST_FONT_TEXT, "x", -1),
        gfx_rect(0, 0, HUGE, HUGE, HOST_RECT_FILLED),
        gfx_line(0, 0, HUGE, HUGE),
    };
    for (int32_t i = 0; i < (int32_t)(sizeof refusals / sizeof refusals[0]); i++) {
        if (refusals[i] == HOST_ERR_ARGUMENT) {
            mark(2 * i, ROW_MORE_REFUSALS);
        }
    }
    if (gfx_text(0, OFF_THE_SURFACE, HOST_FONT_TEXT, long_text, sizeof long_text) ==
        HOST_TEXT_MAX * one_glyph) {
        mark(12, ROW_MORE_REFUSALS);
    }

    static const uint8_t ink_at_0_and_2[] = { 0xA0 };
    gfx_bitmap(0, ROW_BITMAP, 3, 1, ink_at_0_and_2);
    gfx_line(0, ROW_LINE, 3, ROW_LINE);
    gfx_text(0, ROW_TEXT, HOST_FONT_TEXT, "I", 1);

    /* Far beyond the surface: has to vanish, not land somewhere. */
    gfx_rect(gfx_width(), gfx_height(), 5000, 5000, HOST_RECT_FILLED);
}
