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
 * timer's own context. Everything here but that post belongs to the host
 * task. */

#define TIMERS_COUNT 4

void timers_start(void);

/* Starts or restarts timer `id`; 0 ms cancels it. */
void timers_set(int id, int32_t milliseconds);

/* An EVENT_TIMER arrived for `id`: true when the timer is really due, and
 * then it is no longer running. An event can outlive its timer - posted
 * just before the timer was cancelled or set again - and such an event
 * finds the timer stopped or not due yet. */
bool timers_take_due(int id);

#ifdef __cplusplus
}
#endif
