/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "refresh_policy.h"

void refresh_policy_init(refresh_policy_t *p)
{
    p->shown = false;
    p->last_ms = 0;
    p->data_shown = false;
    p->last_data_ms = 0;
    p->partials = 0;
}

/* When a refresh for this reason may come, the earliest; a moment that is
 * past means now. */
static int64_t allowed_at_ms(const refresh_policy_t *p, const refresh_policy_cfg_t *cfg,
                             refresh_reason_t why)
{
    switch (why) {
    case REFRESH_FOR_KEEPALIVE:
        return p->last_ms + (int64_t)cfg->keepalive_ms;
    case REFRESH_FOR_BUTTON:
        return p->last_ms + (int64_t)cfg->min_button_ms;
    case REFRESH_FOR_ROW:
        return p->last_ms + REFRESH_OF_ROW_MIN_MS;
    case REFRESH_FOR_REQUEST:
    case REFRESH_FOR_CLEAN:
        return p->last_ms + REFRESH_ON_REQUEST_MIN_MS;
    case REFRESH_FOR_SWITCH:
        return p->last_ms;
    case REFRESH_FOR_DATA:
        break;
    }
    /* Refreshes nobody asked for are kept apart from one another, not
     * from what the user did: a clock opened at twenty seconds past still
     * shows the next minute as it comes. And a little early is in time -
     * what comes once a minute does not come to the millisecond. */
    if (!p->data_shown) {
        return p->last_ms;
    }
    return p->last_data_ms + (int64_t)cfg->min_data_ms - REFRESH_DATA_EARLY_MS;
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
    if (now_ms < allowed_at_ms(p, cfg, why)) {
        return REFRESH_NONE;
    }
    if (why == REFRESH_FOR_KEEPALIVE || why == REFRESH_FOR_CLEAN) {
        return REFRESH_FULL;
    }
    if (why == REFRESH_FOR_ROW) {
        /* The partial ones still count: the first refresh that is not of
         * the row finds the full one overdue and cleans the panel. */
        return REFRESH_PARTIAL;
    }
    return p->partials >= cfg->full_every ? REFRESH_FULL : REFRESH_PARTIAL;
}

uint32_t refresh_policy_wait_ms(const refresh_policy_t *p, const refresh_policy_cfg_t *cfg,
                                refresh_reason_t why, int64_t now_ms)
{
    if (!p->shown || why == REFRESH_FOR_SWITCH) {
        return 0;
    }
    int64_t at_ms = allowed_at_ms(p, cfg, why);
    return at_ms > now_ms ? (uint32_t)(at_ms - now_ms) : 0;
}

void refresh_policy_note(refresh_policy_t *p, refresh_kind_t kind, refresh_reason_t why,
                         int64_t now_ms)
{
    if (kind == REFRESH_NONE) {
        return;
    }
    p->shown = true;
    p->last_ms = now_ms;
    if (why == REFRESH_FOR_DATA) {
        p->data_shown = true;
        p->last_data_ms = now_ms;
    }
    p->partials = kind == REFRESH_FULL ? 0 : p->partials + 1;
}
