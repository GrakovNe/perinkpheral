/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The application area: the one piece of memory an application lives in.
 * Its page of 64 KB and everything the interpreter needs to run it - the
 * compiled code, the stack, its own records - are taken from here and from
 * nowhere else, so that the host's heap is the same whichever application
 * is on the screen, and an application that needs more than there is
 * fails to start, every time, instead of now and then. */
#define M3_AREA_PAGE_BYTES    (64 * 1024)
/* Measured, over the page: the sample 12 KB, the weather 26 KB, Settings -
 * the heaviest there is, 11 KB of code - 35 KB. The interpreter has about
 * 11 KB of its own and turns the code it runs into a form of its own,
 * about two bytes for a byte, so this, and not the size of a module's
 * file, is what an application has to fit. */
#define M3_AREA_WORKING_BYTES (48 * 1024)
#define M3_AREA_BYTES         (M3_AREA_PAGE_BYTES + M3_AREA_WORKING_BYTES)

/* Takes the area from the heap, once, for as long as the device runs.
 * Call it first of all, while the heap is whole. */
void m3_area_start(void);

/* Makes the area whole again: every instance starts in an area nobody has
 * been in. Only with no application loaded. */
void m3_area_reset(void);

/* The most of the area that was in use since the last reset, for sizing
 * it; 0 without an area. */
size_t m3_area_most_used(void);

#ifdef __cplusplus
}
#endif
