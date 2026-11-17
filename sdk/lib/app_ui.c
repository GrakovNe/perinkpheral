/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_ui.h"

#include <stddef.h>

#include "app_days.h"
#include "app_text.h"
#include "host_api.h"

/* Round the range, however far: 55 minutes and ten more are 5. */
static int32_t wrapped(int32_t value, int32_t first, int32_t last)
{
    int32_t span = last - first + 1;
    if (span <= 0) {
        return first;
    }
    int32_t into = (value - first) % span;
    return first + (into < 0 ? into + span : into);
}

/* ---- a list ---- */

/* How many rows the list showed when it was drawn last: a press, which draws nothing, pages by
 * it. A list not drawn yet pages by a few. */
#define PAGE_UNKNOWN 5
static const app_list_t *drawn_list;
static int32_t           drawn_rows;

static int32_t page_of(const app_list_t *list)
{
    return list == drawn_list && drawn_rows > 0 ? drawn_rows : PAGE_UNKNOWN;
}

static int32_t clamped(int32_t value, int32_t low, int32_t high)
{
    return value < low ? low : value > high ? high : value;
}

app_ui_result_t app_list_button(app_list_t *list, int32_t event)
{
    switch (event) {
    case BTN_UP_LONG_PRESSED:
    case BTN_DOWN_LONG_PRESSED: {
        /* A page at a time, and no further than the ends. */
        int32_t by = (event == BTN_UP_LONG_PRESSED ? -1 : 1) * page_of(list);
        int32_t *moved = list->selected < 0 ? &list->first : &list->selected;
        int32_t  to = clamped(*moved + by, 0, list->count > 0 ? list->count - 1 : 0);
        if (to == *moved) {
            return APP_UI_IGNORED;
        }
        *moved = to;
        if (list->selected >= 0) {
            list->first = clamped(list->first + by, 0, list->count - 1); /* the page moves with it */
        }
        return APP_UI_CHANGED;
    }
    case BTN_UP_PRESSED:
    case BTN_DOWN_PRESSED: {
        int32_t by = event == BTN_UP_PRESSED ? -1 : 1;
        if (list->selected < 0) {
            /* Nothing chosen: the list itself moves, as far as it goes -
             * the far end is settled when it is drawn. */
            if (list->first + by < 0 || list->first + by >= list->count) {
                return APP_UI_IGNORED;
            }
            list->first += by;
            return APP_UI_CHANGED;
        }
        if (list->count < 2) {
            return APP_UI_IGNORED;
        }
        list->selected = wrapped(list->selected + by, 0, list->count - 1);
        return APP_UI_CHANGED;
    }
    case BTN_OK_PRESSED:
        return list->selected >= 0 && list->count > 0 ? APP_UI_DONE : APP_UI_IGNORED;
    case BTN_BACK_PRESSED:
        return APP_UI_CANCELLED;
    default:
        return APP_UI_IGNORED;
    }
}

int32_t app_list_first(app_list_t *list)
{
    int32_t first = ui_list_first(list);
    if (first >= 0) {
        list->first = first; /* where it stands, for the next step */
    }
    drawn_list = list;
    drawn_rows = ui_list_visible(list);
    return first;
}

/* ---- one of a row of values ---- */

static int32_t picker_neighbour(const app_picker_t *picker, int32_t by)
{
    int32_t index = picker->chosen + by;
    if (index >= 0 && index < picker->count) {
        return index;
    }
    /* A row of two that wraps would show the same value over and under. */
    return picker->wraps && picker->count > 2 ? wrapped(index, 0, picker->count - 1) : -1;
}

int32_t app_picker_above(const app_picker_t *picker)
{
    return picker_neighbour(picker, -1);
}

int32_t app_picker_below(const app_picker_t *picker)
{
    return picker_neighbour(picker, 1);
}

app_ui_result_t app_picker_button(app_picker_t *picker, int32_t event)
{
    switch (event) {
    case BTN_UP_PRESSED:
    case BTN_DOWN_PRESSED: {
        int32_t index = picker->chosen + (event == BTN_UP_PRESSED ? -1 : 1);
        if (index < 0 || index >= picker->count) {
            if (!picker->wraps || picker->count < 2) {
                return APP_UI_IGNORED; /* the ends are ends */
            }
            index = wrapped(index, 0, picker->count - 1);
        }
        picker->chosen = index;
        return APP_UI_CHANGED;
    }
    case BTN_OK_PRESSED:
        return APP_UI_DONE;
    case BTN_BACK_PRESSED:
        return APP_UI_CANCELLED;
    default:
        return APP_UI_IGNORED;
    }
}

static void copy(char *to, int32_t size, const char *from)
{
    app_text_t text;
    app_text_start(&text, to, size);
    app_text_add(&text, from != NULL ? from : "");
}

void app_picker_draw_texts(const app_picker_t *picker, const char *above, const char *chosen,
                           const char *below)
{
    (void)picker;
    host_picker_t shown;
    copy(shown.above, sizeof shown.above, above);
    copy(shown.chosen, sizeof shown.chosen, chosen);
    copy(shown.below, sizeof shown.below, below);
    /* the buttons' words are the host's, in the device's language */
    shown.ok[0] = '\0';
    shown.back[0] = '\0';
    ui_picker(&shown);
}

void app_picker_draw(const app_picker_t *picker, const char *const *labels)
{
    int32_t above = app_picker_above(picker), below = app_picker_below(picker);
    app_picker_draw_texts(picker, above >= 0 ? labels[above] : "", labels[picker->chosen],
                          below >= 0 ? labels[below] : "");
}

/* ---- a time, a date, or both ---- */

#define SECONDS_PER_MINUTE 60
#define SECONDS_PER_HOUR   3600
#define SECONDS_PER_DAY    86400

static int32_t days_in_month(int32_t month, int32_t year)
{
    static const int32_t DAYS[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool is_leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && is_leap ? 29 : DAYS[month - 1];
}

static int32_t first_part(const app_date_t *date)
{
    return (date->has & HOST_DATE_HAS_TIME) != 0 ? HOST_DATE_HOUR : HOST_DATE_DAY;
}

static int32_t last_part(const app_date_t *date)
{
    return (date->has & HOST_DATE_HAS_DATE) != 0 ? HOST_DATE_YEAR : HOST_DATE_MINUTE;
}

void app_date_start(app_date_t *date, int32_t has, int32_t year_first, int32_t year_last)
{
    date->has = has;
    date->year_first = year_first;
    date->year_last = year_last;
    host_local_time_t now;
    if (time_local(&now) == HOST_OK && now.year >= year_first && now.year <= year_last) {
        date->year = now.year;
        date->month = now.month;
        date->day = now.day;
        date->hour = now.hour;
        date->minute = now.minute;
    } else {
        date->year = year_first;
        date->month = 1;
        date->day = 1;
        date->hour = 12;
        date->minute = 0;
    }
    date->part = first_part(date);
}

static void step(app_date_t *date, int32_t by)
{
    switch (date->part) {
    case HOST_DATE_HOUR:
        date->hour = wrapped(date->hour + by, 0, 23);
        break;
    case HOST_DATE_MINUTE:
        date->minute = wrapped(date->minute + by, 0, 59);
        break;
    case HOST_DATE_DAY:
        date->day = wrapped(date->day + by, 1, days_in_month(date->month, date->year));
        break;
    case HOST_DATE_MONTH:
        date->month = wrapped(date->month + by, 1, 12);
        break;
    case HOST_DATE_YEAR:
        date->year = wrapped(date->year + by, date->year_first, date->year_last);
        break;
    }
    /* 31 March, and the month goes to April. */
    int32_t last_day = days_in_month(date->month, date->year);
    if (date->day > last_day) {
        date->day = last_day;
    }
}

app_ui_result_t app_date_button(app_date_t *date, int32_t event)
{
    switch (event) {
    case BTN_UP_PRESSED: /* UP is later, as on any clock set with two buttons */
        step(date, 1);
        return APP_UI_CHANGED;
    case BTN_DOWN_PRESSED:
        step(date, -1);
        return APP_UI_CHANGED;
    case BTN_UP_LONG_PRESSED:
    case BTN_DOWN_LONG_PRESSED: {
        /* A long press steps far: minutes by ten, days by a week, years by ten - one picture
         * for what would be many presses. */
        int32_t far = date->part == HOST_DATE_MINUTE ? 10
                      : date->part == HOST_DATE_DAY  ? 7
                      : date->part == HOST_DATE_YEAR ? 10
                      : date->part == HOST_DATE_HOUR ? 6
                                                     : 3;
        step(date, event == BTN_UP_LONG_PRESSED ? far : -far);
        return APP_UI_CHANGED;
    }
    case BTN_OK_PRESSED:
        if (date->part == last_part(date)) {
            return APP_UI_DONE;
        }
        date->part++;
        return APP_UI_CHANGED;
    case BTN_BACK_PRESSED:
        if (date->part == first_part(date)) {
            return APP_UI_CANCELLED;
        }
        date->part--;
        return APP_UI_CHANGED;
    default:
        return APP_UI_IGNORED;
    }
}

void app_date_draw(const app_date_t *date)
{
    host_date_t shown = { .year = date->year, .month = date->month, .day = date->day,
                          .hour = date->hour, .minute = date->minute, .has = date->has,
                          .selected = date->part };
    /* the buttons' words are the host's: Next or Save, Back or Cancel */
    shown.ok[0] = '\0';
    shown.back[0] = '\0';
    ui_date(&shown);
}

int64_t app_date_seconds(const app_date_t *date)
{
    return (int64_t)app_days_from_date(date->year, date->month, date->day) * SECONDS_PER_DAY +
           date->hour * SECONDS_PER_HOUR + date->minute * SECONDS_PER_MINUTE;
}
