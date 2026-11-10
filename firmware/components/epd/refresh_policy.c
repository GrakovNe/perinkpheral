/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "refresh_policy.h"

void refresh_policy_init(refresh_policy_t *p)
{
    p->shown = false;
    p->last_ms = 0;
    p->partials = 0;
}

refresh_kind_t refresh_policy_decide(const refresh_policy_t *p,
                                     const refresh_policy_cfg_t *cfg,
                                     refresh_reason_t why, int64_t now_ms)
{
    /* The first image must be full: it fills the RAM that partial
     * updates are compared against. */
    if (!p->shown || why == REFRESH_FOR_SWITCH) {
        return REFRESH_FULL;
    }
    int64_t since = now_ms - p->last_ms;
    switch (why) {
    case REFRESH_FOR_KEEPALIVE:
        return since >= (int64_t)cfg->keepalive_ms ? REFRESH_FULL : REFRESH_NONE;
    case REFRESH_FOR_BUTTON:
        if (since < (int64_t)cfg->min_button_ms) {
            return REFRESH_NONE;
        }
        break;
    case REFRESH_FOR_DATA:
        if (since < (int64_t)cfg->min_data_ms) {
            return REFRESH_NONE;
        }
        break;
    default:
        break;
    }
    return p->partials >= cfg->full_every ? REFRESH_FULL : REFRESH_PARTIAL;
}

void refresh_policy_note(refresh_policy_t *p, refresh_kind_t kind, int64_t now_ms)
{
    if (kind == REFRESH_NONE) {
        return;
    }
    p->shown = true;
    p->last_ms = now_ms;
    p->partials = kind == REFRESH_FULL ? 0 : p->partials + 1;
}
