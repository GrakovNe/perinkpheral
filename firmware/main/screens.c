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

/* A notice of the host's own, on the whole screen: two lines on the left,
 * a large icon on the right, and along the bottom either what the buttons
 * do - each button's name white on black, what it does beside it - or
 * three strokes that stand for work going on. */
#define NOTICE_MARGIN          24
#define NOTICE_ICON_CELL       9  /* the icon is 99 pixels square */
#define NOTICE_FOOTER_HEIGHT   44
#define NOTICE_LINE_STEP       44 /* from one line's baseline to the next */
#define NOTICE_TEXT_ICON_GAP   16
#define NOTICE_KEY_PAD_X       6  /* around a button's name, inside its black */
#define NOTICE_KEY_PAD_TOP     5
#define NOTICE_KEY_PAD_BOTTOM  7
#define NOTICE_KEY_LABEL_GAP   8  /* from the name to what the button does */
#define NOTICE_KEYS_GAP        22 /* between the two buttons */
#define NOTICE_STROKES         3
#define NOTICE_STROKE_GAP      24
#define NOTICE_STROKE_HEIGHT   6
#define NOTICE_TEXT_BUFFER     32

/* A button's name white on black with what it does beside it; returns
 * where the next one may start. */
static int key_hint(gfx_t *g, int left, int baseline, const char *key, const char *does)
{
    const gfx_font_t *font = gfx_font_system();
    int key_width = gfx_text_font_width(font, key);
    gfx_fill(g, left, baseline - gfx_font_ascent(font) - NOTICE_KEY_PAD_TOP,
             key_width + 2 * NOTICE_KEY_PAD_X,
             gfx_font_ascent(font) + NOTICE_KEY_PAD_TOP + NOTICE_KEY_PAD_BOTTOM, true);
    gfx_text_font(g, left + NOTICE_KEY_PAD_X, baseline, font, key, false);
    int label_left = left + key_width + 2 * NOTICE_KEY_PAD_X + NOTICE_KEY_LABEL_GAP;
    gfx_text_font(g, label_left, baseline, font, does, true);
    return label_left + gfx_text_font_width(font, does) + NOTICE_KEYS_GAP;
}

void screens_notice(gfx_t *g, const screens_notice_t *notice)
{
    int body_height = g->h - NOTICE_FOOTER_HEIGHT;
    int text_right = g->w - NOTICE_MARGIN;
    if (notice->icon != SCREENS_NO_ICON) {
        int icon_size = GFX_ICON_CELLS * NOTICE_ICON_CELL;
        int icon_left = g->w - NOTICE_MARGIN - icon_size;
        gfx_icon(g, (gfx_icon_id_t)notice->icon, icon_left, (body_height - icon_size) / 2,
                 NOTICE_ICON_CELL, true);
        text_right = icon_left - NOTICE_TEXT_ICON_GAP;
    }
    const gfx_font_t *large = gfx_font(GFX_FONT_MEDIUM);
    const char *const lines[] = { notice->first, notice->second };
    /* The two lines stand around the middle of what is above the footer. */
    int baseline = body_height / 2 - NOTICE_LINE_STEP / 2 + gfx_font_cap_height(large) / 2;
    for (int line = 0; line < 2; line++, baseline += NOTICE_LINE_STEP) {
        const char *text = lines[line] != NULL ? lines[line] : "";
        int         room = text_right - NOTICE_MARGIN;
        /* A name too long for the large letters - a network's - is set in
         * the host's own before it is cut. */
        const gfx_font_t *font = gfx_text_font_width(large, text) <= room ? large
                                                                            : gfx_font_system();
        char fitted[NOTICE_TEXT_BUFFER];
        gfx_text_fit(font, text, room, fitted, sizeof fitted);
        gfx_text_font(g, NOTICE_MARGIN, baseline, font, fitted, true);
    }

    int footer_baseline = g->h - NOTICE_FOOTER_HEIGHT / 2 + gfx_font_cap_height(gfx_font_system()) / 2;
    if (notice->is_working) {
        int stroke_width =
            (g->w - 2 * NOTICE_MARGIN - (NOTICE_STROKES - 1) * NOTICE_STROKE_GAP) / NOTICE_STROKES;
        for (int stroke = 0; stroke < NOTICE_STROKES; stroke++) {
            gfx_fill(g, NOTICE_MARGIN + stroke * (stroke_width + NOTICE_STROKE_GAP),
                     g->h - NOTICE_FOOTER_HEIGHT / 2 - NOTICE_STROKE_HEIGHT / 2, stroke_width,
                     NOTICE_STROKE_HEIGHT, true);
        }
    } else if (notice->ok != NULL || notice->back != NULL) {
        gfx_fill(g, 0, g->h - NOTICE_FOOTER_HEIGHT, g->w, 1, true);
        int left = NOTICE_MARGIN;
        if (notice->ok != NULL) {
            left = key_hint(g, left, footer_baseline, "OK", notice->ok);
        }
        if (notice->back != NULL) {
            key_hint(g, left, footer_baseline, "BACK", notice->back);
        }
    }
}
