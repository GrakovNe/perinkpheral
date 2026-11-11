/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "screens.h"

#include <stddef.h>
#include <stdio.h>

#include "qrcodegen.h"
#include "wifi_qr.h"
#include "wm.h"

/* Every host screen is black ink on the panel's white, no frame. The
 * logo word is the square font; everything else is Departure Mono. */

/* The prototype shows this, not the build's version. */
static void centered(gfx_t *g, int baseline, const gfx_font_t *font, const char *text)
{
    gfx_text_font(g, (g->w - gfx_text_font_width(font, text)) / 2, baseline, font, text, true);
}

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
#define ICON_ROWS          11
#define ICON_BASELINE_ROWS 9 /* rows that stand on the text baseline */
static const char *const SIGNAL[ICON_ROWS] = {
    ".......##..",
    ".......##..",
    ".......##..",
    "....##.##..",
    "....##.##..",
    "....##.##..",
    ".##.##.##..",
    ".##.##.##..",
    ".##.##.##..",
    "...........",
    "...........",
};

static void fill_cells(gfx_t *g, int x, int y, int col, int row, int count, bool black)
{
    gfx_fill(g, x + col * ICON_CELL, y + row * ICON_CELL, count * ICON_CELL, ICON_CELL, black);
}

static void icon_signal(gfx_t *g, int x, int y, bool connected)
{
    for (int row = 0; row < ICON_ROWS; row++) {
        for (int col = 0; col < ICON_COLS; col++) {
            if (SIGNAL[row][col] == '#') {
                fill_cells(g, x, y, col, row, 1, true);
            }
        }
    }
    if (!connected) {
        /* Struck through from the bottom left corner of the canvas to the
         * top right: a staircase two cells wide, with a cell of white on
         * both sides to keep it apart from the bars. */
        for (int row = 0; row < ICON_ROWS; row++) {
            int right = ICON_COLS - 1 - row, left = right > 0 ? right - 1 : right;
            int width = right - left + 1;
            fill_cells(g, x, y, left - 1, row, width + 2, false);
            fill_cells(g, x, y, left, row, width, true);
        }
    }
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

/* The setup screen: a code that joins the phone to the device's access
 * point, and under it the same in words for whoever has no camera. */
#define SETUP_TOP_GAP       8
#define SETUP_LINE_HEIGHT   32
#define SETUP_TEXT_LINES    2
#define SETUP_TEXT_MAX      48
#define SETUP_TEXT_DESCENT  10 /* from the bottom of a line to its baseline */
#define QR_VERSION_MAX       5 /* 37 modules: more than any access point name needs */
#define QR_QUIET_MODULES    4 /* the white a reader wants around the code */

/* Draws the code centered in the given box; false when the text does not
 * fit into a code this small. */
static bool qr_code(gfx_t *g, int top, int height, const char *text)
{
    uint8_t modules[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];
    uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];
    if (!qrcodegen_encodeText(text, scratch, modules, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                              QR_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
        return false;
    }
    int size = qrcodegen_getSize(modules);
    int module_pixels = height / (size + 2 * QR_QUIET_MODULES);
    if (module_pixels < 1) {
        return false;
    }
    int left = (g->w - size * module_pixels) / 2;
    top += (height - size * module_pixels) / 2;
    for (int row = 0; row < size; row++) {
        for (int col = 0; col < size; col++) {
            if (qrcodegen_getModule(modules, col, row)) {
                gfx_fill(g, left + col * module_pixels, top + row * module_pixels, module_pixels,
                         module_pixels, true);
            }
        }
    }
    return true;
}

void screens_setup(gfx_t *g, const char *access_point_name, const char *address)
{
    int text_top = g->h - SETUP_TEXT_LINES * SETUP_LINE_HEIGHT;
    char qr_text[WIFI_QR_TEXT_MAX];
    if (wifi_qr_text(access_point_name, qr_text, sizeof qr_text)) {
        qr_code(g, SETUP_TOP_GAP, text_top - SETUP_TOP_GAP, qr_text);
    }

    char line[SETUP_TEXT_MAX];
    snprintf(line, sizeof line, "Join %s", access_point_name);
    centered(g, text_top + SETUP_LINE_HEIGHT - SETUP_TEXT_DESCENT, gfx_font_system(), line);
    snprintf(line, sizeof line, "Open %s", address);
    centered(g, text_top + 2 * SETUP_LINE_HEIGHT - SETUP_TEXT_DESCENT, gfx_font_system(), line);
}
