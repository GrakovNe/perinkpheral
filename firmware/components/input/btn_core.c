/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "btn_core.h"

void btn_core_init(btn_core_t *b)
{
    b->down = false;
    b->long_sent = false;
    b->lows = 0;
    b->down_at_ms = 0;
}

int btn_core_feed(btn_core_t *b, int level, uint32_t now_ms, const btn_core_cfg_t *cfg,
                  btn_event_t events[BTN_EVENTS_PER_SAMPLE])
{
    int count = 0;
    if (level != 0) {
        /* One high sample is a release: the module's pull-up is firm and
         * chatter on the way up stays below the debounce anyway. */
        if (b->down) {
            events[count++] = BTN_UP;
            if (!b->long_sent) {
                events[count++] = BTN_SHORT;
            }
        }
        b->down = false;
        b->long_sent = false;
        b->lows = 0;
        return count;
    }

    if (b->lows == 0) {
        b->down_at_ms = now_ms; /* a long press counts from the first contact */
    }
    if (b->lows < 255) {
        b->lows++;
    }
    if (!b->down) {
        if (b->lows >= cfg->debounce) {
            b->down = true;
            events[count++] = BTN_DOWN;
        }
        return count;
    }
    if (!b->long_sent && now_ms - b->down_at_ms >= cfg->long_ms) {
        b->long_sent = true;
        events[count++] = BTN_LONG;
    }
    return count;
}
