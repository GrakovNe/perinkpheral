/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#include "refresh_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_WIDTH      400
#define DISPLAY_HEIGHT     300
#define DISPLAY_FRAME_SIZE (DISPLAY_WIDTH / 8 * DISPLAY_HEIGHT)

/* The panel as a service. It keeps a copy of the frame the panel shows,
 * so whoever draws needs a buffer of its own and hands it over whole;
 * the service decides whether the panel is touched at all and how. A
 * keep-alive task does the daily refresh. */
void display_start(void);

typedef enum {
    DISPLAY_SHOWN,     /* the panel was refreshed with the frame */
    DISPLAY_UNCHANGED, /* the panel already shows exactly this */
    DISPLAY_DEFERRED,  /* too soon for this reason: offer the frame again later */
    DISPLAY_FAILED,    /* the panel did not take it; what it shows is unknown */
} display_result_t;

/* frame: DISPLAY_FRAME_SIZE bytes, row-major, MSB is the left pixel,
 * 1 = white. */
display_result_t display_show(const uint8_t *frame, refresh_reason_t why);

/* After DISPLAY_DEFERRED: how long until a frame offered for this reason
 * would be taken. */
uint32_t display_wait_ms(refresh_reason_t why);

/* What the panel shows now, for diagnostics. Only for the task that
 * calls display_show(): nobody else writes it. */
const uint8_t *display_shown_frame(void);

#ifdef __cplusplus
}
#endif
