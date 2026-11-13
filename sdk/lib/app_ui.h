/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Button handling for the kit screens (ui_* in host_api.h):
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
 * The application owns the state struct and can read it at any time.
 * Button meanings follow the host: UP and DOWN change the value, OK
 * confirms, BACK goes back. Only short presses are handled.
 * BACK is delivered to an application through app_on_back(); pass it to
 * these functions as BTN_BACK_PRESSED.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "host_api.h"

typedef enum {
    APP_UI_IGNORED,   /* the event is not handled by this screen */
    APP_UI_CHANGED,   /* the state changed: request a redraw */
    APP_UI_DONE,      /* OK on the last step: the value is confirmed */
    APP_UI_CANCELLED, /* BACK on the first step: nothing was selected */
} app_ui_result_t;

/* ---- List ---- */

/* List state: the host_list_t of the host API (row count, selected row,
 * position on the surface). */
typedef host_list_t app_list_t;

/* With a selection: UP and DOWN move the selection and wrap at the ends;
 * OK returns APP_UI_DONE, `selected` is the row.
 * Without a selection (`selected` = -1): UP and DOWN scroll by one row;
 * OK is ignored.
 * BACK returns APP_UI_CANCELLED in both cases. */
app_ui_result_t app_list_button(app_list_t *list, int32_t event);

/* Returns the first visible row. Call in app_on_draw() before drawing
 * rows. For a list without a selection it also clamps `first` to the
 * valid range. */
int32_t app_list_first(app_list_t *list);

/* ---- Picker ---- */

typedef struct {
    int32_t count;
    int32_t chosen;
    bool    wraps; /* the first value follows the last */
} app_picker_t;

app_ui_result_t app_picker_button(app_picker_t *picker, int32_t event);

/* Draws the picker; `labels[i]` is the text of value i. */
void app_picker_draw(const app_picker_t *picker, const char *const *labels);

/* Draws the picker with the three visible texts given directly. */
void app_picker_draw_texts(const app_picker_t *picker, const char *above, const char *chosen,
                           const char *below);

/* Return the index of the value above or below the selected one, or -1
 * at the end of the sequence. */
int32_t app_picker_above(const app_picker_t *picker);
int32_t app_picker_below(const app_picker_t *picker);

/* ---- Date and time ---- */

typedef struct {
    int32_t year, month, day; /* month 1..12 */
    int32_t hour, minute;
    int32_t has;        /* HOST_DATE_HAS_TIME | HOST_DATE_HAS_DATE */
    int32_t part;       /* the part being set: HOST_DATE_HOUR .. HOST_DATE_YEAR */
    int32_t year_first; /* range of years for UP and DOWN */
    int32_t year_last;
} app_date_t;

/* Initializes the editor at its first part, with the local time of the
 * device if it is known. */
void app_date_start(app_date_t *date, int32_t has, int32_t year_first, int32_t year_last);

/* UP increases and DOWN decreases the part, wrapping at the ends.
 * OK moves to the next part and returns APP_UI_DONE after the last one.
 * BACK moves to the previous part and returns APP_UI_CANCELLED from the
 * first one. */
app_ui_result_t app_date_button(app_date_t *date, int32_t event);

void app_date_draw(const app_date_t *date);

/* Returns the value as seconds since 1970 in local time. Subtract
 * time_zone_offset() for UTC. */
int64_t app_date_seconds(const app_date_t *date);
