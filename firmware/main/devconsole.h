/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A development aid, off in release builds (APP_DEV_CONSOLE): single
 * characters typed into the USB console act on the device, so that it
 * can be driven and looked at without hands on it.
 *   1 2 3 4   a short press of that button
 *   q w e r   a long press of button 1 2 3 4
 *   p         print the frame the panel shows (see tools/devshot)
 *   l         print the end of the log the device keeps in memory
 *   m         print the free heap, now and at its lowest since boot, and the
 *             least stack the host task ever had left */
void devconsole_start(void);

/* Prints a 1 bpp frame as text, a line of hex per row between two marker
 * lines, for tools/devshot to turn back into a picture. Call it from the
 * task that owns the frame. */
void devconsole_dump_frame(const uint8_t *frame, int width, int height);

#ifdef __cplusplus
}
#endif
