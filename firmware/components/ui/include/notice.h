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

/* A notice of the host's own, on the whole of the surface it is given:
 * what it has to ask, what went wrong, what it is busy with. Two lines of
 * large text on the left, a large icon on the right; along the bottom
 * what the buttons do when somebody is expected to press one - OK from
 * the left edge, BACK from the right - or three strokes for work that is
 * going on. Pure: tools/preview renders it. */
#define NOTICE_NO_ICON (-1)

typedef struct {
    const char *first;
    const char *second;  /* NULL: one line */
    const char *detail;  /* a line of ordinary text under them - why exactly; NULL: none */
    int         icon;    /* a gfx_icon_id_t, or NOTICE_NO_ICON */
    const char *ok;      /* what OK does; NULL: nothing */
    const char *back;    /* what BACK does; NULL: nothing */
    bool        is_working;
} notice_t;

void notice_draw(gfx_t *g, const notice_t *notice);

#ifdef __cplusplus
}
#endif
