/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* The host loop: takes events off the queue one at a time, lets the
 * window manager and the scene on the screen react, and offers the frame
 * to the display when it changed. Never returns. */
void host_run(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
