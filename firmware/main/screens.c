/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "screens.h"

#include <stddef.h>
#include <stdio.h>

#include "gfx_icons.h"
#include "wm.h"

/* Every host screen is black ink on the panel's white, no frame. The
 * logo word is the square font; everything else is Departure Mono. */

/* The prototype shows this, not the build's version. */
#define MARGIN        16
#define TEXT_BASELINE 30 /* of the first line under a top edge */

static void right_aligned(gfx_t *g, int baseline, const gfx_font_t *font, const char *text)
{
    gfx_text_font(g, g->w - MARGIN - gfx_text_font_width(font, text), baseline, font, text, true);
}

/* The connection icon on the icon canvas: 11 x 11 cells of the font's own
 * 2 px. Three bars that grow to the right, nothing but right angles. */
#define ICON_CELL          2
#define ICON_COLS          11
#define ICON_BASELINE_ROWS 9 /* rows that stand on the text baseline */
static void icon_signal(gfx_t *g, int x, int y, bool connected)
{
    gfx_icon(g, connected ? GFX_ICON_SIGNAL : GFX_ICON_NO_SIGNAL, x, y, ICON_CELL, true);
}

#define HEADER_BASELINE    30
#define HEADER_ICON_GAP    12 /* from the icon to the text beside it */
#define HEADER_TEXTS_GAP   14 /* the least between the two texts: a character */
#define HEADER_TEXT_BUFFER 48

void screens_header(gfx_t *g, const screens_header_t *header)
{
    icon_signal(g, MARGIN, HEADER_BASELINE - ICON_BASELINE_ROWS * ICON_CELL, header->wifi);

    const gfx_font_t *font = gfx_font_system();
    const char *left = header->left != NULL ? header->left : "";
    const char *right = header->right != NULL ? header->right : "";
    int text_left = MARGIN + ICON_COLS * ICON_CELL + HEADER_ICON_GAP;
    int available = g->w - MARGIN - text_left - HEADER_TEXTS_GAP;
    int left_max, right_max;
    wm_header_share(gfx_text_font_width(font, left), gfx_text_font_width(font, right), available,
                    &left_max, &right_max);
    char fitted[HEADER_TEXT_BUFFER];
    gfx_text_fit(font, left, left_max, fitted, sizeof fitted);
    gfx_text_font(g, text_left, HEADER_BASELINE, font, fitted, true);
    gfx_text_fit(font, right, right_max, fitted, sizeof fitted);
    right_aligned(g, HEADER_BASELINE, font, fitted);
    gfx_fill(g, 0, g->h - 1, g->w, 1, true);
}

#define MENU_TOP_GAP    8
#define MENU_ROW_HEIGHT 42
/* The bar behind the selected row. */
#define MENU_BAR_INSET  8  /* from both sides of the surface */
#define MENU_BAR_HEIGHT 36
#define MENU_BAR_RISE   26 /* its top above the row's baseline */

int screens_menu_first_row(int count, int selected, int visible_rows)
{
    int first = selected - visible_rows / 2;
    if (first > count - visible_rows) {
        first = count - visible_rows;
    }
    return first > 0 ? first : 0;
}

#define MENU_ICON_GAP 12 /* between an icon and its title */

/* An application's icon on the cells the host's own icons use. */
static void menu_icon(gfx_t *g, int x, int y, const uint16_t *rows, bool black)
{
    for (int row = 0; row < SCREENS_ICON_CELLS; row++) {
        for (int column = 0; column < SCREENS_ICON_CELLS; column++) {
            if (rows[row] >> (SCREENS_ICON_CELLS - 1 - column) & 1) {
                gfx_fill(g, x + column * ICON_CELL, y + row * ICON_CELL, ICON_CELL, ICON_CELL,
                         black);
            }
        }
    }
}

void screens_menu(gfx_t *g, screens_menu_row_fn row_at, void *context, int count, int selected)
{
    int visible_rows = (g->h - MENU_TOP_GAP) / MENU_ROW_HEIGHT;
    int first = screens_menu_first_row(count, selected, visible_rows);
    /* One column for the titles whether a row has an icon or not. */
    int title_left = MARGIN + SCREENS_ICON_CELLS * ICON_CELL + MENU_ICON_GAP;
    for (int row = 0; row < visible_rows && first + row < count; row++) {
        screens_menu_row_t item;
        row_at(context, first + row, &item);
        int  baseline = MENU_TOP_GAP + row * MENU_ROW_HEIGHT + TEXT_BASELINE;
        bool is_selected = first + row == selected;
        if (is_selected) {
            gfx_fill(g, MENU_BAR_INSET, baseline - MENU_BAR_RISE, g->w - 2 * MENU_BAR_INSET,
                     MENU_BAR_HEIGHT, true);
        }
        if (item.icon != NULL) {
            /* Standing on the baseline like a letter, two cells below its feet. */
            menu_icon(g, MARGIN, baseline - (SCREENS_ICON_CELLS - 2) * ICON_CELL, item.icon,
                      !is_selected);
        }
        gfx_text_font(g, title_left, baseline, gfx_font_system(), item.title, !is_selected);
    }
}

/* One of two answers, as the Settings application shows one of a row of
 * values: the chosen one in a frame in the middle, the other over it or
 * under it with an arrow that way, and what the buttons do along the
 * bottom. The numbers are that application's (system/settings). */
#define CHOICE_BOX_LEFT       40
#define CHOICE_BOX_TOP        96
#define CHOICE_BOX_HEIGHT     56
#define CHOICE_BOX_STROKE     2
#define CHOICE_BASELINE       136
#define CHOICE_ABOVE_BASELINE 76
#define CHOICE_BELOW_BASELINE 196
#define CHOICE_UP_TOP         26
#define CHOICE_DOWN_TOP       212
#define CHOICE_HINT_BASELINE  250

/* The arrow of the set is the middle of its canvas: five rows from the
 * fourth. `top` is where the arrow begins. */
#define CHEVRON_FIRST_ROW 3

static void chevron(gfx_t *g, gfx_icon_id_t arrow, int top)
{
    gfx_icon(g, arrow, (g->w - GFX_ICON_CELLS * ICON_CELL) / 2, top - CHEVRON_FIRST_ROW * ICON_CELL,
             ICON_CELL, true);
}

static void centered(gfx_t *g, int baseline, const gfx_font_t *font, const char *text)
{
    gfx_text_font(g, (g->w - gfx_text_font_width(font, text)) / 2, baseline, font, text, true);
}

static void frame(gfx_t *g, int left, int top, int width, int height, int stroke)
{
    gfx_fill(g, left, top, width, stroke, true);
    gfx_fill(g, left, top + height - stroke, width, stroke, true);
    gfx_fill(g, left, top, stroke, height, true);
    gfx_fill(g, left + width - stroke, top, stroke, height, true);
}

void screens_question(gfx_t *g, const screens_question_t *question)
{
    const gfx_font_t *text = gfx_font_system();
    frame(g, CHOICE_BOX_LEFT, CHOICE_BOX_TOP, g->w - 2 * CHOICE_BOX_LEFT, CHOICE_BOX_HEIGHT,
          CHOICE_BOX_STROKE);
    if (question->action_is_selected) {
        chevron(g, GFX_ICON_UP, CHOICE_UP_TOP);
        centered(g, CHOICE_ABOVE_BASELINE, text, question->cancel);
        centered(g, CHOICE_BASELINE, gfx_font(GFX_FONT_MEDIUM), question->action);
    } else {
        centered(g, CHOICE_BASELINE, gfx_font(GFX_FONT_MEDIUM), question->cancel);
        centered(g, CHOICE_BELOW_BASELINE, text, question->action);
        chevron(g, GFX_ICON_DOWN, CHOICE_DOWN_TOP);
    }
    centered(g, CHOICE_HINT_BASELINE, gfx_font(GFX_FONT_SMALL), "OK: choose   BACK: cancel");
}
