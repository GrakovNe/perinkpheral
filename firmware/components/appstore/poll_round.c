/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "poll_round.h"

bool poll_rounds_pending(const poll_rounds_t *rounds)
{
    return rounds->current.app != 0 || rounds->waiting.app != 0;
}

bool poll_rounds_next(poll_rounds_t *rounds, const poll_entry_t *wanted, poll_turn_t *turn)
{
    if (rounds->current.app == 0 && rounds->waiting.app != 0) {
        rounds->current = rounds->waiting;
        rounds->waiting = (poll_turn_t){ 0 };
    }
    if (rounds->current.app != 0 &&
        (wanted == NULL || !rounds->gives_way || rounds->waiting.app != 0)) {
        *turn = rounds->current;
    } else if (wanted != NULL) {
        *turn = (poll_turn_t){ .app = wanted->app, .poll = wanted->poll };
    } else {
        return false;
    }
    return true;
}

bool poll_rounds_answered(poll_rounds_t *rounds, const poll_turn_t *turn, int items)
{
    bool has_more = turn->item + 1 < items;
    poll_turn_t rest = { 0 };
    if (has_more) {
        rest = *turn;
        rest.item++;
        rest.is_continuation = true;
    }
    if (turn->is_continuation || rounds->current.app == 0) {
        rounds->current = rest;
        rounds->gives_way = has_more;
    } else {
        rounds->waiting = rest;
        rounds->gives_way = false;
    }
    return !has_more;
}
