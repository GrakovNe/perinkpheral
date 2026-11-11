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

/* The least time between the last refresh and one for this reason. */
static int64_t least_gap_ms(const refresh_policy_cfg_t *cfg, refresh_reason_t why)
{
    switch (why) {
    case REFRESH_FOR_KEEPALIVE:
        return (int64_t)cfg->keepalive_ms;
    case REFRESH_FOR_BUTTON:
        return (int64_t)cfg->min_button_ms;
    case REFRESH_FOR_REQUEST:
    case REFRESH_FOR_CLEAN:
        return REFRESH_ON_REQUEST_MIN_MS;
    case REFRESH_FOR_SWITCH:
        return 0;
    case REFRESH_FOR_DATA:
        break;
    }
    return (int64_t)cfg->min_data_ms;
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
    if (now_ms - p->last_ms < least_gap_ms(cfg, why)) {
        return REFRESH_NONE;
    }
    if (why == REFRESH_FOR_KEEPALIVE || why == REFRESH_FOR_CLEAN) {
        return REFRESH_FULL;
    }
    return p->partials >= cfg->full_every ? REFRESH_FULL : REFRESH_PARTIAL;
}

uint32_t refresh_policy_wait_ms(const refresh_policy_t *p, const refresh_policy_cfg_t *cfg,
                                refresh_reason_t why, int64_t now_ms)
{
    if (!p->shown || why == REFRESH_FOR_SWITCH) {
        return 0;
    }
    int64_t allowed_at_ms = p->last_ms + least_gap_ms(cfg, why);
    return allowed_at_ms > now_ms ? (uint32_t)(allowed_at_ms - now_ms) : 0;
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
