/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "notice.h"

#include <stddef.h>

#include "kit.h"

/* A notice of the host's own, on the whole screen: two lines on the left,
 * a large icon on the right, and along the bottom either what the buttons
 * do - each button's name white on black, what it does beside it - or
 * three strokes that stand for work going on. */
#define NOTICE_MARGIN          24
#define NOTICE_ICON_CELL       9  /* the icon is 99 pixels square */
#define NOTICE_ICON_FROM_EDGE   36 /* further in than the text is: it stands free */
#define NOTICE_FOOTER_HEIGHT   44
#define NOTICE_LINE_STEP       44 /* from one line's baseline to the next */
#define NOTICE_TEXT_ICON_GAP   16
#define NOTICE_STROKES         3
#define NOTICE_STROKE_GAP      24
#define NOTICE_STROKE_HEIGHT   6
#define NOTICE_TEXT_BUFFER     128
#define NOTICE_DETAIL_BUFFER   48
#define NOTICE_DETAIL_STEP     34 /* from the second line's baseline to the detail's */

void notice_draw(gfx_t *g, const notice_t *notice)
{
    /* With nothing along the bottom the notice has the whole surface. */
    bool has_footer = notice->is_working || notice->ok != NULL || notice->back != NULL;
    int  body_height = has_footer ? g->h - NOTICE_FOOTER_HEIGHT : g->h;
    int text_right = g->w - NOTICE_MARGIN;
    if (notice->icon != NOTICE_NO_ICON) {
        int icon_size = GFX_ICON_CELLS * NOTICE_ICON_CELL;
        int icon_left = g->w - NOTICE_ICON_FROM_EDGE - icon_size;
        gfx_icon(g, (gfx_icon_id_t)notice->icon, icon_left, (body_height - icon_size) / 2,
                 NOTICE_ICON_CELL, true);
        text_right = icon_left - NOTICE_TEXT_ICON_GAP;
    }
    const gfx_font_t *large = gfx_font(GFX_FONT_MEDIUM);
    const char *const lines[] = { notice->first, notice->second };
    /* The two lines stand around the middle of what is above the footer. */
    int line_count = notice->second != NULL ? 2 : 1;
    int baseline = body_height / 2 - (line_count - 1) * NOTICE_LINE_STEP / 2 +
                   gfx_font_cap_height(large) / 2;
    if (notice->detail != NULL) {
        baseline -= NOTICE_DETAIL_STEP / 2;
    }
    for (int line = 0; line < line_count; line++, baseline += NOTICE_LINE_STEP) {
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

    if (notice->detail != NULL) {
        const gfx_font_t *font = gfx_font_system();
        char fitted[NOTICE_DETAIL_BUFFER];
        gfx_text_fit(font, notice->detail, text_right - NOTICE_MARGIN, fitted, sizeof fitted);
        gfx_text_font(g, NOTICE_MARGIN, baseline - NOTICE_LINE_STEP + NOTICE_DETAIL_STEP, font,
                      fitted, true);
    }

    if (notice->is_working) {
        int stroke_width =
            (g->w - 2 * NOTICE_MARGIN - (NOTICE_STROKES - 1) * NOTICE_STROKE_GAP) / NOTICE_STROKES;
        for (int stroke = 0; stroke < NOTICE_STROKES; stroke++) {
            gfx_fill(g, NOTICE_MARGIN + stroke * (stroke_width + NOTICE_STROKE_GAP),
                     g->h - NOTICE_FOOTER_HEIGHT / 2 - NOTICE_STROKE_HEIGHT / 2, stroke_width,
                     NOTICE_STROKE_HEIGHT, true);
        }
    } else {
        /* The row every screen has: OK at the left edge, BACK at the right. */
        kit_actions(g, notice->ok, notice->back);
    }
}
