/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "app_delivery.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The host loop: takes events off the queue one at a time, lets the
 * window manager and the scene on the screen react, and offers the frame
 * to the display when it changed. Never returns. */
void host_run(void) __attribute__((noreturn));

/* For what runs in the host loop - a handler of events_call() - and
 * nothing else.
 *
 * Reads the list of installed applications anew: an application was
 * installed or removed. */
void host_reload_apps(void);

/* Gives the application what is for it, on the screen or off it. */
void host_deliver(const char *app_name, const app_delivery_t *delivery);

#ifdef __cplusplus
}
#endif
