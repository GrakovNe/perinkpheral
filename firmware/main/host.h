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

/* Something every picture depends on changed: whatever is on the screen is
 * drawn anew and shown now - with a full refresh when every letter is
 * another (the typeface, the language), else a partial one. In the host loop. */
void host_redraw(bool is_clean);

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

/* Removes an installed application, asked for from inside another one's handler: when the
 * handler has returned, and the list is read anew after it. */
bool host_remove_app_later(const char *app_name);

/* The application the next start opens, before the default one: the development tools' run. */
void host_start_once(const char *app_name);

/* Gives the application what is for it, on the screen or off it. */
void host_deliver(const char *app_name, const app_delivery_t *delivery);

/* Takes the application off the screen and out of the stack, its last
 * handlers run, before its files go: what it saves in app_on_terminate()
 * is checked against a manifest that is still there. Host loop only. */
void host_close_app(const char *app_name);

/* For the development tools, in the host loop: `scene` on the screen as if
 * chosen in the menu; how many times the panel was refreshed since start,
 * which tells a new picture from one already seen; and whether a newer
 * frame waits for the panel. */
void     host_open_from_menu(scene_t *scene);
uint32_t host_frames_shown(void);
bool     host_frame_is_pending(void);

#ifdef __cplusplus
}
#endif
