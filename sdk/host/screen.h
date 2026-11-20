/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The screen: drawing a frame, asking for a redraw, the header above the
 * application, moving on the stack of screens, and notifications.
 */
#pragma once

#include "base.h"
#include "ui.h"

/* Draws the complete frame from the application state. The surface is
 * white on entry. Drawing calls work only inside this handler.
 * Called after screen_redraw() and whenever Perinkpheral OS rebuilds the frame.
 * Perinkpheral OS compares frames and decides whether and how to refresh the
 * panel. */
APP_EXPORT(app_on_draw, void app_on_draw(void));

/* ---- Screen ---- */

/* Requests a redraw: Perinkpheral OS calls app_on_draw().
 *
 * When the panel shows the frame depends on the calling handler:
 *   app_on_button(), app_on_back():  immediately;
 *   app_on_timer(), app_on_data(), app_on_alarm(), app_on_intent():
 *                                    when the refresh policy of the user
 *                                    allows, by default once a minute.
 * Settings changes and deliveries to that application for 30 seconds
 * afterwards use the urgent refresh policy while it is on screen.
 *
 * Flags:
 *   HOST_REDRAW_NORMAL    Default. Use after a button press and for data
 *                         that can wait.
 *   HOST_REDRAW_SOON      Bypass the data interval. Use
 *                         for data the user is waiting for.
 *   HOST_REDRAW_CLEAN     Full refresh: 2 - 3 s, one flash, removes
 *                         ghosting. Use for an image that must be clean.
 *   HOST_REDRAW_NO_BLINK  One frame of a sequence: show as soon as the
 *                         panel is ready, never as the periodic full
 *                         refresh. Use for list navigation and for values
 *                         that change every second.
 *
 * SOON and CLEAN wait until at least one second after the preceding refresh.
 *
 * Frame sequences (NO_BLINK). A partial refresh takes about 0.5 s plus
 * the time of app_on_draw(); the maximum rate is about three frames in
 * two seconds. Timer events do not queue: a timer that expires while the
 * panel is busy fires once when the panel is ready. To keep time, select
 * the frame by time_uptime_ms() in app_on_draw(). Ghosting accumulates
 * during a sequence. End a sequence with HOST_REDRAW_CLEAN; otherwise
 * ghosting stays until the next full refresh or until the application
 * leaves the screen.
 *
 * Questions (QUESTION). The picture asks a question that OK answers - a
 * removal, a reset, anything that cannot be taken back. OK reaches the
 * application only when it was pressed after this picture reached the
 * panel: a press meant for the picture before, or made while the panel was
 * still drawing, is not an answer. The other buttons are not held back.
 * Pending redraw flags are combined with OR. Refresh precedence is
 * CLEAN, then NO_BLINK, then SOON; QUESTION is independent. Each consumed redraw with
 * QUESTION starts a new question; a consumed redraw without it ends it.
 * NORMAL cannot clear a QUESTION still pending. Use QUESTION when opening
 * a question, not for later redraws. OK KEYDOWN is consumed by Perinkpheral OS;
 * later OK events are delivered only if the press began after the question
 * reached the panel. QUESTION is included in the ABI 3 firmware floor.
 *
 * Perinkpheral OS does not limit redraw requests. Every refresh uses battery
 * energy and wears the panel. */
#define HOST_REDRAW_NORMAL   0
#define HOST_REDRAW_SOON     1
#define HOST_REDRAW_CLEAN    2
#define HOST_REDRAW_NO_BLINK 4
#define HOST_REDRAW_QUESTION 8
HOST_IMPORT(screen_redraw, void screen_redraw(int32_t flags));

/* Inverts the completed screen, including the Perinkpheral OS header and bottom
 * margin. Draw normally; zero disables inversion, any other value enables it.
 * Returns HOST_OK. A change requests a redraw. The preference belongs to this
 * scene, survives temporary unloads, and resets when the scene is closed.
 * Background handlers cannot change the retained foreground preference. */
#define HOST_SINCE_screen_set_inverted "0.1.54"
HOST_IMPORT(screen_set_inverted, int32_t screen_set_inverted(int32_t inverted));

/* Sets application text in Perinkpheral OS header. The header is shown above an
 * application that is not full-screen. Perinkpheral OS defaults: left side empty
 * (next to the connection icon); right side the time by default, or the
 * date when the user disables the header clock. Blank while time is unknown.
 * The text is drawn in Perinkpheral OS font and size. A text that does not fit
 * is cut with "...". The two sides never overlap.
 * Length 0 returns the side to Perinkpheral OS. A text of one space leaves the
 * side empty.
 * Not a drawing call: callable from any handler. The text is cleared when
 * the application leaves the screen; called off the screen, it has no
 * effect. */
#define HOST_HEADER_LEFT     0
#define HOST_HEADER_RIGHT    1
#define HOST_HEADER_TEXT_MAX 63
HOST_IMPORT(header_set_text, int32_t header_set_text(int32_t side, const char *text, int32_t length));

/* Sets a status in Perinkpheral OS header: an icon and a short text shown above
 * screens with a header, including the owning application's screen.
 * A screen's left text takes precedence over kept left status; kept right
 * status takes precedence over the screen's right text.
 * Use for an activity the user started, such as a countdown.
 * Layout: icon text ... text icon. On HOST_HEADER_LEFT the icon replaces
 * Perinkpheral OS icon and the text follows it. On HOST_HEADER_RIGHT the text
 * replaces the date and the icon follows it.
 * A side belongs to the application that set it last. Length 0 with
 * HOST_ICON_NONE releases the side, if this application still owns it.
 * The side is also released when the application is removed and when the
 * device restarts, or an update changes the application's id.
 * Callable from any handler, on and off the screen. A change is shown as
 * a data-driven redraw. */
#define HOST_STATUS_TEXT_MAX 31
HOST_IMPORT(header_set_status, int32_t header_set_status(int32_t side, int32_t icon, const char *text,
                                                 int32_t length));

/* ---- Navigation ----
 *
 * Navigation is a stack with the menu at the bottom. A Perinkpheral OS question or
 * an application that took the screen is placed on top; when it closes,
 * the entry below it returns. A long press of BACK clears the stack and
 * opens the menu.
 *   - An application covered by another entry is unloaded and loaded
 *     again when it returns: app_on_blur() and app_on_terminate() before,
 *     app_on_load() and app_on_focus() after. Keep persistent data in the
 *     store; save navigation and unfinished edits to the session in
 *     app_on_blur(), and restore them in app_on_focus().
 *   - screen_leave() goes one step down the stack: to the entry the
 *     application was opened over, or to the menu.
 *   - An application that took the screen with screen_request() is placed
 *     over the current screen and returns the user to it on exit. */

/* Requests the screen from a background handler (app_on_data(),
 * app_on_alarm() off the screen). Perinkpheral OS decides whether to grant it.
 * Ignored on the screen. For a streamed background response, focus is
 * considered only when delivery ends. Refused within 30 seconds of a
 * button press, over a question waiting for its answer, or within 30
 * seconds of the last granted focus request. A refused request is discarded, with
 * no retry. A request in app_on_terminate() is too late for that decision.
 * These focus rules are included in the ABI 3 firmware floor. */
HOST_IMPORT(screen_request, void screen_request(void));

/* Closes the application: one step down the stack. Takes effect when
 * app_on_button(), app_on_back() or app_on_timer() returns; ignored in
 * other handlers. A long press of BACK closes any application without
 * calling it. */
HOST_IMPORT(screen_leave, void screen_leave(void));

/* Queues a notification. Perinkpheral OS shows the notice below its header,
 * over the current screen, until the user presses OK or BACK, and then
 * restores the previous screen.
 * Callable from any handler, typically app_on_alarm() off the screen.
 * The application is not loaded while the notification is shown and is
 * not told when it is dismissed.
 * Fields used: first (required), second, detail, icon.
 * Several notifications queue. A restart clears the queue.
 * Returns HOST_OK, HOST_ERR_ARGUMENT, or HOST_ERR_NO_SPACE if the queue
 * is full. */
HOST_IMPORT(notification_post, int32_t notification_post(const host_notice_t *notice));
