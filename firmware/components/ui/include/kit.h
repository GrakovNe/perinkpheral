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

/* UI kit: the components of the host screens. Applications use the same
 * components through ui_* (host_api.h).
 * Primitives: text, title, icon, button, action row.
 * Elements: bar, frame, value with unit, cell.
 * Screens: picker, date and time.
 * See also: list.h, notice.h. Pure; rendered by tools/preview. */

#define KIT_MARGIN         16
#define KIT_ACTIONS_HEIGHT 44 /* height of the action row at the bottom */

/* ---- primitives ---- */

typedef enum {
    KIT_TEXT,    /* body text */
    KIT_TITLE,   /* page title */
    KIT_CAPTION, /* small text beside a larger element */
    KIT_VALUE,   /* main value of a screen */
} kit_style_t;

const gfx_font_t *kit_font(kit_style_t style);

/* Draws one line. `top`: top of the line. `max_width`: the text is cut
 * with "..." to this width; 0: no limit. Returns the width drawn. */
int kit_text(gfx_t *g, int left, int top, kit_style_t style, const char *text, int max_width);

/* Draws text on up to `max_lines` lines of `width`, broken at spaces.
 * The last line is cut with "..." when text remains. A word wider than
 * `width` is cut. Returns the number of lines drawn. */
int kit_text_lines(gfx_t *g, int left, int top, kit_style_t style, const char *text, int width,
                   int max_lines);

/* Draws one line centred on the surface width. */
void kit_text_centered(gfx_t *g, int top, kit_style_t style, const char *text);

/* Draws the page title and the rule under it. Returns the top of the
 * content area. */
int kit_title(gfx_t *g, const char *text);

typedef enum {
    KIT_ICON_SMALL,  /* 22 px, beside text */
    KIT_ICON_MEDIUM, /* 44 px */
    KIT_ICON_LARGE,  /* 99 px, the icon of a notice */
} kit_icon_size_t;

int  kit_icon_side(kit_icon_size_t size);
void kit_icon(gfx_t *g, int left, int top, int icon, kit_icon_size_t size);

/* Button label: the button name in white on black and its action beside
 * it. The action follows the name; `is_mirrored`: it precedes the name.
 * `baseline`: baseline of the text. Returns the width. */
int kit_button_width(const char *name, const char *action);
int kit_button(gfx_t *g, int left, int baseline, const char *name, const char *action,
               bool is_mirrored);

/* Action row at the bottom of the surface: the OK action and "OK" from
 * the left edge, "BACK" and its action from the right edge:
 * "Save OK        BACK Cancel". NULL: the button is not shown. A single
 * button is centred. */
void kit_actions(gfx_t *g, const char *ok, const char *back);

/* ---- elements ---- */

/* Bar: an outline filled from the left in proportion `value` / `maximum`. */
void kit_bar(gfx_t *g, int left, int top, int width, int value, int maximum);

/* Frame around the value changed by UP and DOWN. `has_up`, `has_down`:
 * an arrow above and below the frame. */
void kit_frame(gfx_t *g, int left, int top, int width, int height, bool has_up, bool has_down);

/* ---- screens (whole surface) ---- */

/* Picker of one value: `chosen` framed in the middle, `above` (reached by
 * UP) over it, `below` (reached by DOWN) under it. NULL or empty: no
 * value in that direction. */
typedef struct {
    const char *above;
    const char *chosen;
    const char *below;
    const char *ok;   /* as in kit_actions() */
    const char *back;
} kit_picker_t;

void kit_picker(gfx_t *g, const kit_picker_t *picker);

/* Date and time screen. One part is selected for editing. */
typedef enum {
    KIT_DATE_HOUR,
    KIT_DATE_MINUTE,
    KIT_DATE_DAY,
    KIT_DATE_MONTH,
    KIT_DATE_YEAR,
    KIT_DATE_NONE, /* display only */
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

/* Value with unit: "18 km/h", "25 min". The unit follows the value on
 * its baseline in a smaller font selected by `font`: the small font beside
 * the text font, the text font beside larger ones. `top`: top of the
 * value line. `tabular`: digits of equal width. `g` NULL: measure only.
 * Returns the width of value and unit. */
int kit_value(gfx_t *g, int left, int top, const gfx_font_t *font, bool tabular, const char *value,
              const char *unit);

/* Cell: a box of any size with `name` along the top and, centred in the
 * rest, `picture` and `value` side by side. The value takes the largest
 * font that fits. A cell that covers the whole surface is drawn one size
 * larger.
 * For cells of equal text size: call kit_cell_fit() for each cell and draw
 * all with the largest result (the smallest font). */
#define KIT_CELL_PICTURE_LARGE 88 /* picture side in a cell that covers the whole surface */
#define KIT_CELL_PICTURE_SMALL 44 /* picture side in other cells */
typedef struct {
    int            left, top, width, height;
    const char    *name;    /* NULL: none */
    const char    *value;
    const uint8_t *picture; /* gfx_picture format; NULL: none */
    int            size;    /* 0: largest that fits; else a result of kit_cell_fit() */
} kit_cell_t;

int  kit_cell_fit(const gfx_t *g, const kit_cell_t *cell);
void kit_cell(gfx_t *g, const kit_cell_t *cell);

#ifdef __cplusplus
}
#endif
