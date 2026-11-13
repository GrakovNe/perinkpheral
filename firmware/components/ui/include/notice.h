/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "gfx_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Notice: a question, an error or work in progress on the whole surface.
 * Layout: two lines of large text on the left, a large icon on the right.
 * Bottom: the action row (OK from the left edge, BACK from the right), or
 * three strokes for work in progress. Pure; rendered by tools/preview. */
#define NOTICE_NO_ICON (-1)

typedef struct {
    const char *first;
    const char *second;  /* NULL: one line */
    const char *detail;  /* one line of body text under them; NULL: none */
    int         icon;    /* gfx_icon_id_t, or NOTICE_NO_ICON */
    const char *ok;      /* action of OK; NULL: not shown */
    const char *back;    /* action of BACK; NULL: not shown */
    bool        is_working;
} notice_t;

void notice_draw(gfx_t *g, const notice_t *notice);

#ifdef __cplusplus
}
#endif
