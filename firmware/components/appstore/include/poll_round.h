/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "poll_schedule.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One item of a poll. A continuation has already consumed its wanted
 * entry; a new request for that poll must remain wanted for its next round. */
typedef struct {
    int app;
    int poll;
    int item;
    bool is_continuation;
} poll_turn_t;

/* Two rounds suffice if a third is left wanted until one finishes. A
 * waiting poll gets a turn between items whenever its rest can be kept. */
typedef struct {
    poll_turn_t current;
    poll_turn_t waiting;
    bool gives_way;
} poll_rounds_t;

bool poll_rounds_pending(const poll_rounds_t *rounds);

/* False when there is no work. The wanted entry is never consumed here. */
bool poll_rounds_next(poll_rounds_t *rounds, const poll_entry_t *wanted, poll_turn_t *turn);

/* Keeps the next item, if any. True when this poll's round is finished
 * and its next periodic deadline can be set. Zero items cancels a round. */
bool poll_rounds_answered(poll_rounds_t *rounds, const poll_turn_t *turn, int items);

#ifdef __cplusplus
}
#endif
