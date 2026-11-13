/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Memory to ask for while the application runs.
 *
 * An application has one page of 64 KB. Its stack and its static data take
 * the beginning of it - tools/mkapp says how much - and the rest is free:
 * these calls hand it out. It is an arena, not malloc and free: pieces are
 * taken one after another and given back together, to a mark. That is all
 * an application needs that is unloaded whenever it leaves the screen, and
 * it costs a hundred bytes of code.
 *
 *     int32_t   before = app_alloc_mark();
 *     event_t  *events = app_alloc(count * sizeof *events);
 *     if (events == NULL) { ... }      // no room: show less
 *     ...
 *     app_alloc_release(before);       // the events, and whatever was taken after them
 */
#pragma once

#include <stdint.h>

/* `bytes` of memory, at an address anything may stand at, or NULL when so
 * much is not left. Not cleared: what a release gave back comes out as it
 * was left. */
void *app_alloc(int32_t bytes);

/* How much there is still to take. */
int32_t app_alloc_left(void);

/* Where the arena stands now; app_alloc_release() with it gives back
 * everything taken since. A mark from after the one released is no mark
 * any more. */
int32_t app_alloc_mark(void);
void app_alloc_release(int32_t mark);
