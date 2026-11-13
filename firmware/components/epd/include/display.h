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

/* Display service. Holds a copy of the frame on the panel. The caller
 * draws into its own buffer and passes whole frames; the service selects
 * no refresh, a partial refresh or a full refresh.
 * `policy`: the refresh policy; copied. */
void display_start(const refresh_policy_cfg_t *policy);

/* Replaces the refresh policy. Applies from the next frame. */
void display_set_policy(const refresh_policy_cfg_t *policy);

typedef enum {
    DISPLAY_SHOWN,     /* the panel was refreshed */
    DISPLAY_UNCHANGED, /* the frame equals the frame on the panel */
    DISPLAY_DEFERRED,  /* refused by the policy; pass the frame again after display_wait_ms() */
    DISPLAY_FAILED,    /* refresh failed; the panel content is unknown */
} display_result_t;

/* `frame`: DISPLAY_FRAME_SIZE bytes, row-major, MSB is the left pixel,
 * 1 = white. */
display_result_t display_show(const uint8_t *frame, refresh_reason_t why);

/* Puts the panel into deep sleep. Called on DEADLINE_PANEL_SLEEP (after
 * the last partial refresh) and before a restart or sleep of the device. */
void display_rest(void);


/* Handles DEADLINE_PANEL_KEEPALIVE: refreshes the panel when no refresh
 * was made within the keep-alive period of the policy. */
void display_keepalive(void);

/* Time until a frame with reason `why` is accepted. For use after
 * DISPLAY_DEFERRED. */
uint32_t display_wait_ms(refresh_reason_t why);

/* The frame on the panel, for diagnostics. Valid only in the task that
 * calls display_show(). */
const uint8_t *display_shown_frame(void);

#ifdef __cplusplus
}
#endif
