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

/* The device restarts, or is reset to what it was when new: about a second
 * from now - an answer to whoever asked has to leave it first - and in the
 * host loop, which tells the loaded application before it goes. From any
 * task. */
typedef enum {
    HOST_STOP_RESTART,
    HOST_STOP_FACTORY_RESET, /* the settings and what applications stored are erased on the way up */
} host_stop_t;
void host_stop_soon(host_stop_t why);

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
