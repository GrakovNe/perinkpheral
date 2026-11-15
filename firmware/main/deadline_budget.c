/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The table of deadlines holds every one that can be set at once: a
 * deadline that does not fit is a failure at run time - an alarm that is
 * not set, a stop that becomes a restart - so the sum is held here, and a
 * kind, a poll or an alarm more that breaks it does not compile. No code.
 */
#include "deadline_table.h"
#include "deadlines.h"
#include "host_api.h"
#include "launcher.h"
#include "manifest_limits.h"

/* Kinds of one deadline each; the rest are an application's or a scene's. */
#define SINGLE_KINDS (DEADLINE_KINDS - 3) /* not SCENE_TIMER, ALARM, POLL */

/* Every application the launcher holds - the system ones too: nothing
 * fences the calls off - with all its polls and alarms set; the scene's
 * timers; one of every other kind. */
#define DEADLINES_AT_MOST                                                         \
    (LAUNCHER_ENTRIES_MAX * (MANIFEST_POLLS_MAX + HOST_ALARM_COUNT) + HOST_TIMER_COUNT + \
     SINGLE_KINDS)

_Static_assert(DEADLINES_AT_MOST <= DEADLINES_MAX, "every deadline that can be set at once has a place");
