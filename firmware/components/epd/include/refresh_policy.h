/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Refresh policy, from the Waveshare rules for the 4.2" V2:
 *   - data-driven refreshes: at least min_data_ms apart;
 *   - a full refresh after full_every partial ones;
 *   - at least one refresh per keepalive_ms;
 *   - a refresh after a button press: at least min_button_ms apart (default 0);
 *   - a change of the current scene: always a full refresh.
 * Pure, host-tested. */
typedef enum {
    REFRESH_FOR_DATA,      /* poll result, timer, clock */
    REFRESH_FOR_REQUEST,   /* HOST_REDRAW_SOON */
    REFRESH_FOR_ROW,       /* HOST_REDRAW_NO_BLINK: at once, never full */
    REFRESH_FOR_BUTTON,    /* button press */
    REFRESH_FOR_CLEAN,     /* HOST_REDRAW_CLEAN: full */
    REFRESH_FOR_SWITCH,    /* change of the current scene: full */
    REFRESH_FOR_KEEPALIVE, /* no refresh within keepalive_ms */
} refresh_reason_t;

/* Minimum interval between refreshes for REFRESH_FOR_REQUEST. */
#define REFRESH_ON_REQUEST_MIN_MS 1000
/* A data-driven refresh is accepted this much before min_data_ms expires. */
#define REFRESH_DATA_EARLY_MS     1000
/* Minimum interval between refreshes for REFRESH_FOR_ROW. */
#define REFRESH_OF_ROW_MIN_MS     0

typedef enum {
    REFRESH_NONE,
    REFRESH_PARTIAL,
    REFRESH_FULL,
} refresh_kind_t;

typedef struct {
    uint32_t min_data_ms;
    uint32_t min_button_ms;
    int      full_every;   /* partial refreshes before a full one */
    uint32_t keepalive_ms;
    bool     full_is_fast; /* fast full refresh; passed to the driver, not used by the policy */
} refresh_policy_cfg_t;

typedef struct {
    bool    shown;   /* a refresh was made since boot */
    int64_t last_ms; /* time of the last refresh */
    bool    data_shown;   /* a data-driven refresh was made since boot */
    int64_t last_data_ms; /* time of the last data-driven refresh */
    int     partials;
} refresh_policy_t;

void refresh_policy_init(refresh_policy_t *p);

refresh_kind_t refresh_policy_decide(const refresh_policy_t *p,
                                     const refresh_policy_cfg_t *cfg,
                                     refresh_reason_t why, int64_t now_ms);

/* Time until refresh_policy_decide() allows a refresh for `why`; 0 when
 * it allows one now. */
uint32_t refresh_policy_wait_ms(const refresh_policy_t *p, const refresh_policy_cfg_t *cfg,
                                refresh_reason_t why, int64_t now_ms);

/* Records the refresh that was made. */
void refresh_policy_note(refresh_policy_t *p, refresh_kind_t kind, refresh_reason_t why,
                         int64_t now_ms);

#ifdef __cplusplus
}
#endif
