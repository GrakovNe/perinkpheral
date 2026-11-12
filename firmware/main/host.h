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

/* The deadlines of the whole host are kept from here on, and those that
 * come are answered by the host loop. Before anything that has one. */
void host_deadlines_start(void);

/* The device restarts, or is reset to what it was when new: about a second
 * from now - an answer to whoever asked has to leave it first - and in the
 * host loop, which tells the loaded application before it goes. From any
 * task. */
typedef enum {
    HOST_STOP_RESTART,
    HOST_STOP_FACTORY_RESET, /* the settings and what applications stored are erased on the way up */
    HOST_STOP_DEEP_SLEEP,    /* not asked for by anybody: the host loop's own, when nothing is left to do */
} host_stop_t;
void host_stop_soon(host_stop_t why);

/* What somebody on the network asked for cannot be taken back: the device
 * shows the question - `question` in its header, Cancel or `action` to
 * choose between - and this waits for whoever stands at it, a
 * minute at most. From any task but the host loop. One question at a
 * time: a second one meanwhile is answered HOST_ANSWER_BUSY at once. */
typedef enum {
    HOST_ANSWER_CONFIRMED,
    HOST_ANSWER_NOT_CONFIRMED,
    HOST_ANSWER_BUSY,
} host_answer_t;

host_answer_t host_ask_at_the_device(const char *question, const char *action);

/* For what runs in the host loop - a handler of events_call() - and
 * nothing else.
 *
 * Reads the list of installed applications anew: an application was
 * installed or removed. */
void host_reload_apps(void);

/* The same from inside an application's handler, where the list cannot be
 * read anew at once - the application is one of its scenes: done when the
 * handler has returned. Never lost. */
void host_reload_apps_later(void);

/* Gives the application what is for it, on the screen or off it. */
void host_deliver(const char *app_name, const app_delivery_t *delivery);

#ifdef __cplusplus
}
#endif
