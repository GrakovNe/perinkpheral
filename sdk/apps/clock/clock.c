/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A clock for the whole screen: the time, and under it the day of the
 * week and the date. It lives on a timer - it wakes at the turn of every
 * minute, asks for a redraw and goes back to waiting. Until the device
 * knows the time it says so instead of showing a wrong one.
 */
#include <stdbool.h>

#include "app_text.h"
#include "host_api.h"

#define MINUTE_TIMER       0
#define BUTTON_BACK        4
#define MS_PER_SECOND      1000
#define SECONDS_PER_MINUTE 60
#define MINUTES_PER_HOUR   60
#define SECONDS_PER_DAY    86400
#define DAYS_PER_WEEK      7
#define RETRY_MS           5000 /* how soon to look again while the time is unknown */

#define TIME_FONT HOST_FONT_HUGE
#define DATE_FONT HOST_FONT_MEDIUM
/* Where the tops of the digits are, the room between the digits and the
 * date, and between the two lines of the date. */
#define TIME_CAP_TOP     61
#define TIME_TO_DATE_GAP 50
#define DATE_LINE_GAP    16

static const char *const WEEKDAYS[DAYS_PER_WEEK] = { "Sunday",   "Monday", "Tuesday", "Wednesday",
                                                     "Thursday", "Friday", "Saturday" };
static const char *const MONTHS[] = { "January", "February", "March",     "April",
                                      "May",     "June",     "July",      "August",
                                      "September", "October", "November", "December" };

typedef struct {
    int32_t hour, minute;
    int32_t weekday; /* 0 is Sunday */
    int32_t day, month; /* both from 1 */
} local_time_t;

/* Days since 1 January 1970 to a day and a month, by Howard Hinnant's
 * civil_from_days: the calendar counted in 400-year eras that start on
 * 1 March, so that the leap day is the last day of a year. */
static void date_from_days(int64_t days, int32_t *day, int32_t *month)
{
    const int32_t DAYS_PER_ERA = 146097, DAYS_TO_ERA_START = 719468;
    int64_t shifted = days + DAYS_TO_ERA_START;
    int64_t era = (shifted >= 0 ? shifted : shifted - (DAYS_PER_ERA - 1)) / DAYS_PER_ERA;
    int32_t day_of_era = (int32_t)(shifted - era * DAYS_PER_ERA);
    int32_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int32_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int32_t month_from_march = (5 * day_of_year + 2) / 153;
    *day = day_of_year - (153 * month_from_march + 2) / 5 + 1;
    *month = month_from_march < 10 ? month_from_march + 3 : month_from_march - 9;
}

/* False while the device does not know the time. */
static bool local_time(local_time_t *now)
{
    int64_t utc_seconds = time_now();
    if (utc_seconds == 0) {
        return false;
    }
    int64_t seconds = utc_seconds + time_zone_offset();
    if (seconds < 0) {
        return false; /* before 1970: not a time this calendar counts */
    }
    int64_t days = seconds / SECONDS_PER_DAY;
    int32_t second_of_day = (int32_t)(seconds % SECONDS_PER_DAY);
    now->hour = second_of_day / (SECONDS_PER_MINUTE * MINUTES_PER_HOUR);
    now->minute = second_of_day / SECONDS_PER_MINUTE % MINUTES_PER_HOUR;
    /* 1 January 1970 was a Thursday. */
    now->weekday = (int32_t)((days + 4) % DAYS_PER_WEEK);
    date_from_days(days, &now->day, &now->month);
    return true;
}

static void wake_for_the_next_picture(void)
{
    int64_t utc_seconds = time_now();
    if (utc_seconds == 0) {
        timer_set(MINUTE_TIMER, RETRY_MS);
        return;
    }
    int32_t seconds_into_minute = (int32_t)(utc_seconds % SECONDS_PER_MINUTE);
    timer_set(MINUTE_TIMER, (SECONDS_PER_MINUTE - seconds_into_minute) * MS_PER_SECOND);
}

/* Draws the text centered, with the top of its capitals and digits at
 * `cap_top`, and returns where they end: the baseline. */
static int32_t draw_centered(int32_t cap_top, int32_t font, const char *text)
{
    int32_t length = app_text_length(text);
    int32_t width = gfx_text_width(font, text, length);
    int32_t cap_height = gfx_font_cap_height(font);
    gfx_text((gfx_width() - width) / 2, cap_top - (gfx_font_ascent(font) - cap_height), font, text,
             length);
    return cap_top + cap_height;
}

void app_on_focus(void)
{
    wake_for_the_next_picture();
}

void app_on_button(int32_t button, int32_t press)
{
    if (button == BUTTON_BACK && press == HOST_PRESS_SHORT) {
        request_exit();
    }
}

void app_on_timer(int32_t id)
{
    (void)id;
    request_redraw();
    wake_for_the_next_picture();
}

void app_on_draw(void)
{
    char time_line[8], weekday_line[24], date_line[24];
    app_text_t text;
    local_time_t now;
    if (local_time(&now)) {
        app_text_start(&text, time_line, sizeof time_line);
        app_text_add_padded(&text, now.hour, 2);
        app_text_add(&text, ":");
        app_text_add_padded(&text, now.minute, 2);

        app_text_start(&text, weekday_line, sizeof weekday_line);
        app_text_add(&text, WEEKDAYS[now.weekday]);

        app_text_start(&text, date_line, sizeof date_line);
        app_text_add_number(&text, now.day);
        app_text_add(&text, " ");
        app_text_add(&text, MONTHS[now.month - 1]);
    } else {
        app_text_start(&text, time_line, sizeof time_line);
        app_text_add(&text, "--:--");
        app_text_start(&text, weekday_line, sizeof weekday_line);
        app_text_add(&text, "No time yet");
        app_text_start(&text, date_line, sizeof date_line);
        app_text_add(&text, "Waiting for Wi-Fi");
    }

    int32_t time_bottom = draw_centered(TIME_CAP_TOP, TIME_FONT, time_line);
    int32_t weekday_bottom = draw_centered(time_bottom + TIME_TO_DATE_GAP, DATE_FONT, weekday_line);
    draw_centered(weekday_bottom + DATE_LINE_GAP, DATE_FONT, date_line);
}
