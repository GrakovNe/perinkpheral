/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "words.h"
#include "kit.h"

#include <stddef.h>
#include <stdio.h>

#include "gfx_icons.h"

#define TEXT_BUFFER 128 /* a line of the largest text an application may give */

/* ---- primitives ---- */

const gfx_font_t *kit_font(kit_style_t style)
{
    static const gfx_font_id_t FONT_OF[] = {
        [KIT_TEXT] = GFX_FONT_TEXT,
        [KIT_TITLE] = GFX_FONT_MEDIUM,
        [KIT_CAPTION] = GFX_FONT_SMALL,
        [KIT_VALUE] = GFX_FONT_DISPLAY,
    };
    return gfx_font(FONT_OF[style]);
}

int kit_text(gfx_t *g, int left, int top, kit_style_t style, const char *text, int max_width)
{
    const gfx_font_t *font = kit_font(style);
    char fitted[TEXT_BUFFER];
    if (max_width > 0) {
        gfx_text_fit(font, text, max_width, fitted, sizeof fitted);
        text = fitted;
    }
    return gfx_text_font(g, left, top + gfx_font_ascent(font), font, text, true);
}

void kit_text_centered(gfx_t *g, int top, kit_style_t style, const char *text)
{
    kit_text(g, (g->w - gfx_text_font_width(kit_font(style), text)) / 2, top, style, text, 0);
}

#define TITLE_TOP        10
#define TITLE_RULE_GAP   8  /* from the letters' feet to the line under them */
#define TITLE_AFTER_RULE 10

int kit_title(gfx_t *g, const char *text)
{
    const gfx_font_t *font = kit_font(KIT_TITLE);
    kit_text(g, KIT_MARGIN, TITLE_TOP, KIT_TITLE, text, g->w - 2 * KIT_MARGIN);
    int rule = TITLE_TOP + gfx_font_ascent(font) + TITLE_RULE_GAP;
    gfx_fill(g, KIT_MARGIN, rule, g->w - 2 * KIT_MARGIN, 1, true);
    return rule + 1 + TITLE_AFTER_RULE;
}

static int icon_cell(kit_icon_size_t size)
{
    static const int CELL_OF[] = { [KIT_ICON_SMALL] = 2, [KIT_ICON_MEDIUM] = 4,
                                   [KIT_ICON_LARGE] = 9 };
    return CELL_OF[size];
}

int kit_icon_side(kit_icon_size_t size)
{
    return GFX_ICON_CELLS * icon_cell(size);
}

void kit_icon(gfx_t *g, int left, int top, int icon, kit_icon_size_t size)
{
    gfx_icon(g, (gfx_icon_id_t)icon, left, top, icon_cell(size), true);
}

#define KEY_PAD_X      6 /* around a button's name, inside its black */
#define KEY_PAD_Y      9 /* above its capitals and below them alike: the name stands in the middle */
#define KEY_ACTION_GAP 8 /* from the name to what the button does */

static int key_width(const char *name)
{
    return gfx_text_font_width(kit_font(KIT_TEXT), name) + 2 * KEY_PAD_X;
}

static void draw_key(gfx_t *g, int left, int baseline, const char *name)
{
    const gfx_font_t *font = kit_font(KIT_TEXT);
    int capitals = gfx_font_cap_height(font);
    gfx_fill(g, left, baseline - capitals - KEY_PAD_Y, key_width(name), capitals + 2 * KEY_PAD_Y,
             true);
    gfx_text_font(g, left + KEY_PAD_X, baseline, font, name, false);
}

static bool has_text(const char *text)
{
    return text != NULL && text[0] != '\0';
}

int kit_button_width(const char *name, const char *action)
{
    int width = key_width(name);
    if (has_text(action)) {
        width += KEY_ACTION_GAP + gfx_text_font_width(kit_font(KIT_TEXT), action);
    }
    return width;
}

int kit_button(gfx_t *g, int left, int baseline, const char *name, const char *action,
               bool is_mirrored)
{
    const gfx_font_t *font = kit_font(KIT_TEXT);
    int width = kit_button_width(name, action);
    if (is_mirrored) {
        if (has_text(action)) {
            gfx_text_font(g, left, baseline, font, action, true);
        }
        draw_key(g, left + width - key_width(name), baseline, name);
    } else {
        draw_key(g, left, baseline, name);
        if (has_text(action)) {
            gfx_text_font(g, left + key_width(name) + KEY_ACTION_GAP, baseline, font, action, true);
        }
    }
    return width;
}

#define ACTIONS_MARGIN 24

void kit_actions(gfx_t *g, const char *ok, const char *back)
{
    if (ok == NULL && back == NULL) {
        return;
    }
    gfx_fill(g, 0, g->h - KIT_ACTIONS_HEIGHT, g->w, 1, true);
    int baseline = g->h - KIT_ACTIONS_HEIGHT / 2 + gfx_font_cap_height(kit_font(KIT_TEXT)) / 2;
    if (ok != NULL) {
        kit_button(g, ACTIONS_MARGIN, baseline, "OK", ok, true);
    }
    if (back != NULL) {
        kit_button(g, g->w - ACTIONS_MARGIN - kit_button_width("BACK", back), baseline, "BACK",
                   back, false);
    }
}

/* ---- elements ---- */

#define STROKE     2
#define BAR_HEIGHT 14
#define BAR_INSET  2 /* between the frame and what fills it */

static void draw_box(gfx_t *g, int left, int top, int width, int height)
{
    gfx_fill(g, left, top, width, STROKE, true);
    gfx_fill(g, left, top + height - STROKE, width, STROKE, true);
    gfx_fill(g, left, top, STROKE, height, true);
    gfx_fill(g, left + width - STROKE, top, STROKE, height, true);
}

void kit_bar(gfx_t *g, int left, int top, int width, int value, int maximum)
{
    draw_box(g, left, top, width, BAR_HEIGHT);
    if (maximum <= 0 || value <= 0) {
        return;
    }
    if (value > maximum) {
        value = maximum;
    }
    int room = width - 2 * (STROKE + BAR_INSET);
    gfx_fill(g, left + STROKE + BAR_INSET, top + STROKE + BAR_INSET,
             (int)((long long)room * value / maximum), BAR_HEIGHT - 2 * (STROKE + BAR_INSET), true);
}

/* The arrows of the icon set sit in the middle of their canvas: nine cells
 * from the second, five rows from the fourth. */
#define ARROW_CELL         2
#define ARROW_WIDTH        (9 * ARROW_CELL)
#define ARROW_HEIGHT       (5 * ARROW_CELL)
#define ARROW_FIRST_COLUMN 1
#define ARROW_FIRST_ROW    3
#define ARROW_GAP          12 /* from the frame to an arrow */

static void draw_arrow(gfx_t *g, gfx_icon_id_t arrow, int left, int top)
{
    gfx_icon(g, arrow, left - ARROW_FIRST_COLUMN * ARROW_CELL, top - ARROW_FIRST_ROW * ARROW_CELL,
             ARROW_CELL, true);
}

void kit_frame(gfx_t *g, int left, int top, int width, int height, bool has_up, bool has_down)
{
    draw_box(g, left, top, width, height);
    int arrow_left = left + (width - ARROW_WIDTH) / 2;
    if (has_up) {
        draw_arrow(g, GFX_ICON_UP, arrow_left, top - ARROW_GAP - ARROW_HEIGHT);
    }
    if (has_down) {
        draw_arrow(g, GFX_ICON_DOWN, arrow_left, top + height + ARROW_GAP);
    }
}

/* ---- screens ---- */

#define PICKER_FRAME_INSET  40 /* from both sides of the surface */
#define PICKER_FRAME_HEIGHT 56
#define PICKER_NEIGHBOUR    30 /* from the frame's arrow to the line beyond it */

void kit_picker(gfx_t *g, const kit_picker_t *picker)
{
    int body = g->h - KIT_ACTIONS_HEIGHT;
    int frame_top = (body - PICKER_FRAME_HEIGHT) / 2;
    bool has_above = has_text(picker->above), has_below = has_text(picker->below);
    kit_frame(g, PICKER_FRAME_INSET, frame_top, g->w - 2 * PICKER_FRAME_INSET, PICKER_FRAME_HEIGHT,
              has_above, has_below);

    const gfx_font_t *chosen_font = kit_font(KIT_TITLE);
    char fitted[TEXT_BUFFER];
    gfx_text_fit(chosen_font, picker->chosen, g->w - 2 * PICKER_FRAME_INSET - 2 * KIT_MARGIN, fitted,
                 sizeof fitted);
    gfx_text_font(g, (g->w - gfx_text_font_width(chosen_font, fitted)) / 2,
                  frame_top + (PICKER_FRAME_HEIGHT + gfx_font_cap_height(chosen_font)) / 2,
                  chosen_font, fitted, true);

    const gfx_font_t *font = kit_font(KIT_TEXT);
    int arrow_reach = ARROW_GAP + ARROW_HEIGHT;
    if (has_above) {
        int baseline = frame_top - arrow_reach - PICKER_NEIGHBOUR / 2;
        gfx_text_fit(font, picker->above, g->w - 2 * KIT_MARGIN, fitted, sizeof fitted);
        gfx_text_font(g, (g->w - gfx_text_font_width(font, fitted)) / 2, baseline, font, fitted,
                      true);
    }
    if (has_below) {
        int baseline = frame_top + PICKER_FRAME_HEIGHT + arrow_reach + PICKER_NEIGHBOUR / 2 +
                       gfx_font_cap_height(font);
        gfx_text_fit(font, picker->below, g->w - 2 * KIT_MARGIN, fitted, sizeof fitted);
        gfx_text_font(g, (g->w - gfx_text_font_width(font, fitted)) / 2, baseline, font, fitted,
                      true);
    }
    kit_actions(g, picker->ok, picker->back);
}

#define PART_PAD      6  /* from the text of a part to its frame */
#define PARTS_MAX     3
#define TIME_PART_GAP 10 /* between the parts of the time, for their frames */
#define LINES_GAP     58 /* between the time's feet and the date's heads */

typedef struct {
    const char     *text;
    kit_date_part_t part; /* KIT_DATE_NONE: not something to set - the colon */
} date_piece_t;

/* A line of parts in the middle of the width, the one being set framed. */
static void draw_parts(gfx_t *g, int baseline, const gfx_font_t *font, const date_piece_t *pieces,
                       int count, int gap, kit_date_part_t selected)
{
    int width = (count - 1) * gap;
    for (int i = 0; i < count; i++) {
        width += gfx_text_font_width(font, pieces[i].text);
    }
    int left = (g->w - width) / 2;
    int cap = gfx_font_cap_height(font);
    for (int i = 0; i < count; i++) {
        int piece_width = gfx_text_font_width(font, pieces[i].text);
        gfx_text_font(g, left, baseline, font, pieces[i].text, true);
        if (pieces[i].part == selected && selected != KIT_DATE_NONE) {
            kit_frame(g, left - PART_PAD, baseline - cap - PART_PAD, piece_width + 2 * PART_PAD,
                      cap + 2 * PART_PAD, true, true);
        }
        left += piece_width + gap;
    }
}

void kit_date(gfx_t *g, const kit_date_t *date)
{
    const gfx_font_t *time_font = gfx_font(GFX_FONT_LARGE);
    const gfx_font_t *date_font = kit_font(KIT_TITLE);
    int time_cap = gfx_font_cap_height(time_font), date_cap = gfx_font_cap_height(date_font);
    int body = g->h - KIT_ACTIONS_HEIGHT;
    bool both = date->has_time && date->has_date;
    int height = both ? time_cap + LINES_GAP + date_cap : date->has_time ? time_cap : date_cap;
    int top = (body - height) / 2;

    if (date->has_time) {
        char hour[8], minute[8];
        snprintf(hour, sizeof hour, "%02d", date->hour % 100);
        snprintf(minute, sizeof minute, "%02d", date->minute % 100);
        const date_piece_t pieces[PARTS_MAX] = { { hour, KIT_DATE_HOUR },
                                                 { ":", KIT_DATE_NONE },
                                                 { minute, KIT_DATE_MINUTE } };
        draw_parts(g, top + time_cap, time_font, pieces, PARTS_MAX, TIME_PART_GAP, date->selected);
        top += time_cap + LINES_GAP;
    }
    if (date->has_date) {
        char day[8], year[8];
        snprintf(day, sizeof day, "%02d", date->day % 100);
        snprintf(year, sizeof year, "%d", date->year % 10000);
        int month = date->month >= 1 && date->month <= 12 ? date->month : 1;
        const date_piece_t pieces[PARTS_MAX] = { { day, KIT_DATE_DAY },
                                                 { word((word_t)(WORD_JAN + (month - 1))), KIT_DATE_MONTH },
                                                 { year, KIT_DATE_YEAR } };
        draw_parts(g, top + date_cap, date_font, pieces, PARTS_MAX,
                   gfx_text_font_width(date_font, " ") + PART_PAD, date->selected);
    }
    kit_actions(g, date->ok, date->back);
}

/* ---- the cell ---- */

#define CELL_PADDING     12
#define CELL_PICTURE_GAP 14

static const gfx_font_id_t CELL_FONTS[] = { GFX_FONT_HUGE, GFX_FONT_DISPLAY, GFX_FONT_LARGE,
                                            GFX_FONT_MEDIUM, GFX_FONT_TEXT };
#define CELL_SIZES          ((int)(sizeof CELL_FONTS / sizeof CELL_FONTS[0]))
#define CELL_FIRST_OF_SMALL 3 /* a cell that is not the whole surface begins at this size */

static bool cell_is_large(const gfx_t *g, const kit_cell_t *cell)
{
    return cell->width >= g->w && cell->height >= g->h;
}

/* The side of the square the picture stands in; 0 with no picture. */
static int cell_picture_side(const gfx_t *g, const kit_cell_t *cell)
{
    return cell->picture == NULL    ? 0
           : cell_is_large(g, cell) ? KIT_CELL_PICTURE_LARGE
                                    : KIT_CELL_PICTURE_SMALL;
}

int kit_cell_fit(const gfx_t *g, const kit_cell_t *cell)
{
    int side = cell_picture_side(g, cell);
    int room = cell->width - 2 * CELL_PADDING - (side > 0 ? side + CELL_PICTURE_GAP : 0);
    int size = cell_is_large(g, cell) ? 1 : CELL_FIRST_OF_SMALL;
    while (size < CELL_SIZES &&
           gfx_text_tabular_width(gfx_font(CELL_FONTS[size - 1]), cell->value) > room) {
        size++;
    }
    return size;
}

void kit_cell(gfx_t *g, const kit_cell_t *cell)
{
    int size = cell->size >= 1 && cell->size <= CELL_SIZES ? cell->size : kit_cell_fit(g, cell);
    const gfx_font_t *font = gfx_font(CELL_FONTS[size - 1]);
    kit_style_t       name_style = cell_is_large(g, cell) ? KIT_TEXT : KIT_CAPTION;
    int               name_height = 0;
    if (has_text(cell->name)) {
        const gfx_font_t *name_font = kit_font(name_style);
        char              fitted[TEXT_BUFFER];
        gfx_text_fit(name_font, cell->name, cell->width - 2 * CELL_PADDING, fitted, sizeof fitted);
        kit_text(g, cell->left + (cell->width - gfx_text_font_width(name_font, fitted)) / 2,
                 cell->top + CELL_PADDING, name_style, fitted, 0);
        name_height = gfx_font_height(name_font) + CELL_PADDING / 2;
    }

    int side = cell_picture_side(g, cell);
    char value[TEXT_BUFFER];
    gfx_text_fit(font, cell->value,
                 cell->width - 2 * CELL_PADDING - (side > 0 ? side + CELL_PICTURE_GAP : 0), value,
                 sizeof value);
    int row_width = gfx_text_tabular_width(font, value) + (side > 0 ? side + CELL_PICTURE_GAP : 0);
    int row_left = cell->left + (cell->width - row_width) / 2;
    int middle = cell->top + CELL_PADDING + name_height +
                 (cell->height - 2 * CELL_PADDING - name_height) / 2;
    if (side > 0) {
        /* The drawing as large as whole pixels a cell let it be, in the
         * middle of its square. */
        int width = gfx_picture_width(cell->picture), height = gfx_picture_height(cell->picture);
        int longest = width > height ? width : height;
        int pixels = longest > 0 && side / longest > 0 ? side / longest : 1;
        gfx_picture(g, row_left + (side - width * pixels) / 2, middle - height * pixels / 2,
                    cell->picture, pixels, true);
        row_left += side + CELL_PICTURE_GAP;
    }
    gfx_text_tabular(g, row_left, middle + gfx_font_cap_height(font) / 2, font, value, true);
}

/* ---- a value with its unit ---- */

int kit_value(gfx_t *g, int left, int top, const gfx_font_t *font, bool tabular, const char *value,
              const char *unit)
{
    int baseline = top + gfx_font_ascent(font);
    int width = tabular ? gfx_text_tabular_width(font, value) : gfx_text_font_width(font, value);
    if (g != NULL) {
        if (tabular) {
            gfx_text_tabular(g, left, baseline, font, value, true);
        } else {
            gfx_text_font(g, left, baseline, font, value, true);
        }
    }
    if (!has_text(unit)) {
        return width;
    }
    /* Beside large digits a unit is the size of the text; beside text it
     * is the small print. */
    bool              is_large = gfx_font_cap_height(font) >= 2 * gfx_font_cap_height(kit_font(KIT_TEXT));
    const gfx_font_t *unit_font = kit_font(is_large ? KIT_TEXT : KIT_CAPTION);
    int               gap = is_large ? 12 : 5;
    if (g != NULL) {
        gfx_text_font(g, left + width + gap, baseline, unit_font, unit, true);
    }
    return width + gap + gfx_text_font_width(unit_font, unit);
}
