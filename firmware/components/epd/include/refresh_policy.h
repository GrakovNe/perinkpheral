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

/* When the panel may be refreshed and how, after Waveshare's rules for
 * the 4.2" V2: at least min_data_ms between data-driven updates, a full
 * refresh after full_every partial ones, at least one refresh per
 * keepalive_ms. A button is the user looking at the device: it redraws
 * at once (min_button_ms is 0 unless the user asks for more). A switch of the application on the scene is
 * always a full refresh. Pure, host-tested. */
typedef enum {
    REFRESH_FOR_DATA,      /* a poll result, a clock tick */
    REFRESH_FOR_BUTTON,    /* the user pressed something */
    REFRESH_FOR_SWITCH,    /* another application takes the scene */
    REFRESH_FOR_KEEPALIVE, /* nothing happened for a day */
} refresh_reason_t;

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
} refresh_policy_cfg_t;

typedef struct {
    bool    shown;   /* anything on the panel since boot */
    int64_t last_ms; /* of the last refresh of any kind */
    int     partials;
} refresh_policy_t;

void refresh_policy_init(refresh_policy_t *p);

refresh_kind_t refresh_policy_decide(const refresh_policy_t *p,
                                     const refresh_policy_cfg_t *cfg,
                                     refresh_reason_t why, int64_t now_ms);

/* Call with what was actually done. */
void refresh_policy_note(refresh_policy_t *p, refresh_kind_t kind, int64_t now_ms);

#ifdef __cplusplus
}
#endif
