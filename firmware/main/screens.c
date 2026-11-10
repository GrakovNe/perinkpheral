/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "screens.h"

#include <stdio.h>

#include "tektur_20.h"
#include "tektur_24.h"
#include "tektur_64_digits.h"

/* Every host screen is black ink on the panel's white, no frame. The
 * logo word is the square font; everything else is Tektur. */

/* The prototype shows this, not the build's version. */
#define SPLASH_VERSION "ver 1.234.5"

static void centered(gfx_t *g, int baseline, const gfx_font_t *font, const char *s)
{
    gfx_text_font(g, (g->w - gfx_text_font_width(font, s)) / 2, baseline, font, s, true);
}

void screens_splash(gfx_t *g)
{
    gfx_clear(g, false);

    /* "perINKpheraL" in 27 px squares (scale 5), one stroke apart: 357 px. */
    const int scale = 5;
    int h = gfx_text_sq_height(scale);
    gfx_text_sq_ex(g, (g->w - gfx_text_sq_width_ex("perINKpheraL", scale, scale)) / 2,
                   118 - h / 2, "perINKpheraL", scale, true, scale);

    centered(g, 222, &tektur_24, SPLASH_VERSION);
    centered(g, 254, &tektur_24, "(c) 2026 Max Grakov");
    centered(g, 282, &tektur_24, "MIT License");
}

void screens_counters(gfx_t *g, const uint32_t counts[4])
{
    gfx_clear(g, false);
    gfx_text_font(g, 16, 26, &tektur_20, "Button counter test", true);
    gfx_fill(g, 0, 36, g->w, 1, true);

    const int cy0 = 37, cw = g->w / 2, ch = (g->h - 37) / 2;
    gfx_fill(g, cw, cy0, 1, g->h - cy0, true);
    gfx_fill(g, 0, cy0 + ch, g->w, 1, true);
    for (int i = 0; i < 4; i++) {
        int cx = (i % 2) * cw, cy = cy0 + (i / 2) * ch;
        char label[24], num[16];
        snprintf(label, sizeof label, "Button %d   D%d", i + 1, i);
        snprintf(num, sizeof num, "%lu", (unsigned long)counts[i]);
        gfx_text_font(g, cx + 16, cy + 28, &tektur_20, label, true);
        int tw = gfx_text_font_width(&tektur_64_digits, num);
        gfx_text_font(g, cx + (cw - tw) / 2, cy + ch / 2 + 32, &tektur_64_digits, num, true);
    }
}
