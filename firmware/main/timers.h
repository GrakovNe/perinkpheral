/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The one-shot timers of the scene on the screen: deadlines like any
 * other (DEADLINE_SCENE_TIMER), under the numbers a scene has for them. */
#define TIMERS_COUNT 4

/* Starts or restarts timer `id`; 0 ms cancels it. */
void timers_set(int id, int32_t milliseconds);

#ifdef __cplusplus
}
#endif
