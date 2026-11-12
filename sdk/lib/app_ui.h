/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * What the buttons do on the host's screens (ui_* in host_api.h), so that
 * an application writes only what the result is for:
 *
 *     static app_date_t when;
 *
 *     void app_on_button(int32_t event)
 *     {
 *         switch (app_date_button(&when, event)) {
 *         case APP_UI_DONE:      save(&when); request_exit(); break;
 *         case APP_UI_CANCELLED: request_exit();              break;
 *         case APP_UI_CHANGED:   request_redraw(HOST_REDRAW_NO_BLINK); break;
 *         default:               break;
 *         }
 *     }
 *
 *     void app_on_back(void) { ... app_date_button(&when, BTN_BACK_PRESSED) ... }
 *
 *     void app_on_draw(void) { app_date_draw(&when); }
 *
 * The state is the application's: a struct it keeps and may read at any
 * time. The buttons are the host's layout - UP and DOWN change, OK goes
 * on, BACK goes back - and only short presses count. BACK comes to an
 * application through app_on_back(): hand it on as BTN_BACK_PRESSED.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_UI_IGNORED,   /* the press was not for this screen */
    APP_UI_CHANGED,   /* the picture is another: ask for a redraw */
    APP_UI_DONE,      /* OK on the last step: the value is the user's choice */
    APP_UI_CANCELLED, /* BACK on the first step: nothing was chosen */
} app_ui_result_t;

/* ---- a list ---- */

typedef struct {
    int32_t count;
    int32_t selected;
} app_list_t;

/* UP and DOWN move, around the ends; OK is APP_UI_DONE with `selected`
 * the row, BACK is APP_UI_CANCELLED. */
app_ui_result_t app_list_button(app_list_t *list, int32_t event);

/* ---- one of a row of values ---- */

typedef struct {
    int32_t count;
    int32_t chosen;
    bool    wraps; /* after the last comes the first: cities, not "never .. always" */
} app_picker_t;

app_ui_result_t app_picker_button(app_picker_t *picker, int32_t event);

/* Draws it with `labels[i]` for value number i. */
void app_picker_draw(const app_picker_t *picker, const char *const *labels);

/* The same when the labels are worked out: the three in sight are given. */
void app_picker_draw_texts(const app_picker_t *picker, const char *above, const char *chosen,
                           const char *below);

/* The values in sight beside the chosen one, or -1 where the row ends. */
int32_t app_picker_above(const app_picker_t *picker);
int32_t app_picker_below(const app_picker_t *picker);

/* ---- a time, a date, or both ---- */

typedef struct {
    int32_t year, month, day; /* month 1..12 */
    int32_t hour, minute;
    int32_t has;        /* HOST_DATE_HAS_TIME | HOST_DATE_HAS_DATE */
    int32_t part;       /* the one being set: HOST_DATE_HOUR .. HOST_DATE_YEAR */
    int32_t year_first; /* the years UP and DOWN go through */
    int32_t year_last;
} app_date_t;

/* Starts at the first part, from the device's local time when it knows it. */
void app_date_start(app_date_t *date, int32_t has, int32_t year_first, int32_t year_last);

/* UP is later and DOWN earlier, around the ends; OK is the next part and
 * APP_UI_DONE after the last; BACK is the part before and
 * APP_UI_CANCELLED from the first. */
app_ui_result_t app_date_button(app_date_t *date, int32_t event);

void app_date_draw(const app_date_t *date);

/* The moment set, as seconds since 1970 of a clock that shows it - local
 * time; take time_zone_offset() off for UTC. */
int64_t app_date_seconds(const app_date_t *date);
