/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "timers.h"

#include "deadlines.h"

void timers_set(int id, int32_t milliseconds)
{
    if (milliseconds > 0) {
        deadlines_set_in(DEADLINE_SCENE_TIMER, 0, id, milliseconds);
    } else {
        deadlines_cancel(DEADLINE_SCENE_TIMER, 0, id);
    }
}
