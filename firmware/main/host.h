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
} host_stop_t;
void host_stop_soon(host_stop_t why);

/* Something every picture depends on changed - the typeface: whatever is
 * on the screen is drawn anew. In the host loop. */
void host_redraw(void);

/* Ends an answer an application is taking off the screen, before the
 * screen is given to somebody: there is room for one application. In the
 * host loop. */
void host_abort_background_delivery(void);

/* What an application keeps on a side of the header (0 left, 1 right)
 * while it is away: an icon of the host's set (or -1) and a line. Both
 * empty: the side is free again. In the host loop. */
void host_set_header_status(uint32_t app_id, int side, int icon, const char *text);

/* An application's notification: shown over whatever has the screen once
 * the handler that asked has returned, and kept until somebody dismisses
 * it; several wait their turn. From the host loop. False: too many wait. */
bool host_notify(const char *first, const char *second, const char *detail, int icon);

/* What somebody on the network asked for cannot be taken back: the device
 * shows the question - two lines of it, an icon beside them (a
 * gfx_icon_id_t), and what OK does - and this waits for whoever stands at
 * it, a minute at most. From any task but the host loop. One question at
 * a time: a second one meanwhile is answered HOST_ANSWER_BUSY at once. */
typedef enum {
    HOST_ANSWER_CONFIRMED,
    HOST_ANSWER_NOT_CONFIRMED,
    HOST_ANSWER_BUSY,
} host_answer_t;

host_answer_t host_confirm_on_device(const char *first, const char *second, int icon,
                                     const char *action);

/* The device is busy with something that takes a while and must not be
 * interrupted: the screen says so, in two lines, until host_work_is_over()
 * - or until the device restarts, when the work was an update. From any
 * task but the host loop. */
void host_show_working(const char *first, const char *second);
void host_work_is_over(void);

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
