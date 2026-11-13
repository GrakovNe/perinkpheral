/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Arena allocator for application memory.
 *
 * An application has one 64 KB page. The stack and the static data take
 * its beginning (tools/mkapp reports the size). The rest is the arena.
 * Allocations are sequential and are released together, back to a mark.
 * There is no free() for a single allocation.
 *
 *     int32_t   before = app_alloc_mark();
 *     event_t  *events = app_alloc(count * sizeof *events);
 *     if (events == NULL) { ... }      // not enough memory
 *     ...
 *     app_alloc_release(before);       // releases `events` and everything allocated after it
 */
#pragma once

#include <stdint.h>

/* Returns `bytes` of memory, aligned for any type, or NULL if the arena
 * is too small. The memory is not zeroed. */
void *app_alloc(int32_t bytes);

/* Returns the number of bytes left in the arena. */
int32_t app_alloc_left(void);

/* app_alloc_mark() returns the current position of the arena.
 * app_alloc_release(mark) releases everything allocated after the mark.
 * Marks taken after a released mark become invalid. */
int32_t app_alloc_mark(void);
void app_alloc_release(int32_t mark);
