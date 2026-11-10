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

/* The one-shot timers of the scene on the screen. A timer that runs out
 * posts EVENT_TIMER to the host's queue; nothing is called from the
 * timer's own context. */

#define TIMERS_COUNT 4

void timers_start(void);

/* Starts or restarts timer `id`; 0 ms cancels it. */
void timers_set(int id, int32_t milliseconds);

/* An EVENT_TIMER can still be in the queue when its timer is cancelled or
 * set again. Each setting has a generation, the event carries the one it
 * was posted for, and only the current one counts. */
bool timers_is_current(int id, uint32_t generation);

#ifdef __cplusplus
}
#endif
