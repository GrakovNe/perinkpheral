/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The parts every screen of the device is put together from - the host's
 * own, and an application's when it asks for them (ui_* in host_api.h),
 * so that the plain half of every application looks the same: text and a
 * title, an icon, a button with what it does, the row of buttons along
 * the bottom, a bar, the frame around what is being changed, and the two
 * screens that set a value. The list is list.h and the notice notice.h.
 * Pure: tools/preview renders the sheet. */

#define KIT_MARGIN         16
#define KIT_ACTIONS_HEIGHT 44 /* what the row of buttons takes at the bottom */

/* ---- primitives ---- */

typedef enum {
    KIT_TEXT,    /* what is read: the host's text */
    KIT_TITLE,   /* what a page is about */
    KIT_CAPTION, /* what stands beside something larger */
    KIT_VALUE,   /* the one number the screen is for */
} kit_style_t;

const gfx_font_t *kit_font(kit_style_t style);

/* A line of text with its top at `top`, cut with "..." at `max_width`
 * (0: not cut). Returns the width drawn. */
int kit_text(gfx_t *g, int left, int top, kit_style_t style, const char *text, int max_width);

/* The same in the middle of the surface's width. */
void kit_text_centered(gfx_t *g, int top, kit_style_t style, const char *text);

/* The title of a page at its usual place, a line under it. Returns where
 * the page's own content begins. */
int kit_title(gfx_t *g, const char *text);

typedef enum {
    KIT_ICON_SMALL,  /* 22 px: beside text */
    KIT_ICON_MEDIUM, /* 44 px */
    KIT_ICON_LARGE,  /* 99 px: what a notice is about */
} kit_icon_size_t;

int  kit_icon_side(kit_icon_size_t size);
void kit_icon(gfx_t *g, int left, int top, int icon, kit_icon_size_t size);

/* A button of the device and what it does: its name white on black, the
 * action beside it - after the name, or before it for a button that
 * stands at a right edge. `baseline` is the text's. Returns the width. */
int kit_button_width(const char *name, const char *action);
int kit_button(gfx_t *g, int left, int baseline, const char *name, const char *action,
               bool is_mirrored);

/* The row along the bottom of the surface: OK from the left edge, BACK
 * from the right. NULL: that button does nothing here. */
void kit_actions(gfx_t *g, const char *ok, const char *back);

/* ---- elements ---- */

/* How much of how much: a frame, filled from the left. Not a thing that
 * moves - it changes when the data does. */
void kit_bar(gfx_t *g, int left, int top, int width, int value, int maximum);

/* The frame around what UP and DOWN change, an arrow over it and one
 * under it for each way there is to go. */
void kit_frame(gfx_t *g, int left, int top, int width, int height, bool has_up, bool has_down);

/* ---- screens: the whole surface ---- */

/* One of a row of values: the chosen one framed in the middle, what UP
 * leads to over it and what DOWN leads to under it. NULL or empty: the
 * row ends there. */
typedef struct {
    const char *above;
    const char *chosen;
    const char *below;
    const char *ok;   /* for kit_actions() */
    const char *back;
} kit_picker_t;

void kit_picker(gfx_t *g, const kit_picker_t *picker);

/* A time, a date, or both, set a part at a time. */
typedef enum {
    KIT_DATE_HOUR,
    KIT_DATE_MINUTE,
    KIT_DATE_DAY,
    KIT_DATE_MONTH,
    KIT_DATE_YEAR,
    KIT_DATE_NONE, /* nothing is being set: only shown */
} kit_date_part_t;

typedef struct {
    int             year, month, day; /* month 1-12 */
    int             hour, minute;
    bool            has_time, has_date;
    kit_date_part_t selected;
    const char     *ok;
    const char     *back;
} kit_date_t;

void kit_date(gfx_t *g, const kit_date_t *date);

#ifdef __cplusplus
}
#endif
