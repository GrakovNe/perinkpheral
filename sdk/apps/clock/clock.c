/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A clock, and the sample of an application that lives on a timer: it
 * wakes at the turn of every minute, asks for a redraw and goes back to
 * waiting. While the device does not know the time it counts how long it
 * has been running instead.
 */
#include <stdbool.h>

#include "app_text.h"
#include "host_api.h"

#define MINUTE_TIMER       0
#define MS_PER_SECOND      1000
#define SECONDS_PER_MINUTE 60
#define MINUTES_PER_HOUR   60
#define HOURS_PER_DAY      24
#define LINE_GAP           12

static bool time_is_known(void)
{
    return time_now() != 0;
}

/* Minutes since midnight when the time is known, since start otherwise. */
static int64_t minutes_to_show(void)
{
    if (time_is_known()) {
        return time_now() / SECONDS_PER_MINUTE % (MINUTES_PER_HOUR * HOURS_PER_DAY);
    }
    return time_uptime_ms() / MS_PER_SECOND / SECONDS_PER_MINUTE;
}

static void wake_at_next_minute(void)
{
    int64_t ms_into_minute = time_is_known()
                                 ? time_now() % SECONDS_PER_MINUTE * MS_PER_SECOND
                                 : time_uptime_ms() % (SECONDS_PER_MINUTE * MS_PER_SECOND);
    timer_set(MINUTE_TIMER, (int32_t)(SECONDS_PER_MINUTE * MS_PER_SECOND - ms_into_minute));
}

static void draw_centered(int32_t y, const char *text)
{
    int32_t length = app_text_length(text);
    int32_t width = gfx_text_width(HOST_FONT_TEXT, text, length);
    gfx_text((gfx_width() - width) / 2, y, HOST_FONT_TEXT, text, length);
}

void app_on_focus(void)
{
    wake_at_next_minute();
}

void app_on_timer(int32_t id)
{
    (void)id;
    request_redraw();
    wake_at_next_minute();
}

void app_on_draw(void)
{
    int64_t minutes = minutes_to_show();
    char time_buffer[16];
    app_text_t time_text;
    app_text_start(&time_text, time_buffer, sizeof time_buffer);
    app_text_add_number(&time_text, (int32_t)(minutes / MINUTES_PER_HOUR));
    app_text_add(&time_text, ":");
    app_text_add_padded(&time_text, (int32_t)(minutes % MINUTES_PER_HOUR), 2);

    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);
    int32_t top = (gfx_height() - 2 * line_height - LINE_GAP) / 2;
    draw_centered(top, time_is_known() ? "UTC" : "Running for");
    draw_centered(top + line_height + LINE_GAP, time_buffer);
}
