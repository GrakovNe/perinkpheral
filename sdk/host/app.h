/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The application's life: the handlers it exports, the buttons, its strings,
 * asking for a redraw, the header, moving on the stack, timers and news.
 */
#pragma once

#include "base.h"

/* ---- Exports (all optional) ---- */

/* Called once, immediately after the module is loaded.
 * The host unloads an application every time it leaves the screen. State
 * that must persist is kept with kv_* calls, not in global variables. */
APP_EXPORT(app_init, void app_init(void));

/* app_on_focus: the application has received the screen.
 * app_on_blur: the application is about to lose the screen. */
APP_EXPORT(app_on_focus, void app_on_focus(void));
APP_EXPORT(app_on_blur, void app_on_blur(void));

/* The last call of an instance, before it is unloaded: leaving the
 * screen, end of a background delivery, device restart, removal of the
 * application. Not called after a fault stopped the instance, nor for
 * light sleep, which keeps it loaded. Save persistent state here.
 * app_on_blur() is called first if the application was on the screen. */
APP_EXPORT(app_on_terminate, void app_on_terminate(void));

/* app_on_installed: the application was installed - from a catalog, by the
 * development tools, or with the firmware - and has not run on this device
 * before, or was removed and installed again.
 * app_on_updated: another version of it replaced the one that ran before.
 * Called once, on or off the screen, after app_init(); off the screen the
 * application is loaded for the call. A new version does not inherit the
 * alarms set by the old one: set them again here. The store, the files
 * and poll intervals set from code are kept across an update. */
APP_EXPORT(app_on_installed, void app_on_installed(void));
APP_EXPORT(app_on_updated, void app_on_updated(void));

/* Buttons, in their order on the device. The host uses these meanings on
 * all of its screens. Applications should keep them. */
#define BTN_UP   1
#define BTN_OK   2
#define BTN_DOWN 3
#define BTN_BACK 4

/* Button actions. Each press produces two kinds of events:
 *   raw:         KEYDOWN, then KEYUP;
 *   interpreted: PRESSED when a short press ends, or LONG_PRESSED after
 *                600 ms while the button is still held. A long press
 *                produces no PRESSED; its release is a KEYUP only.
 * Most applications handle PRESSED and LONG_PRESSED only. */
#define BTN_KEYDOWN      0
#define BTN_KEYUP        1
#define BTN_PRESSED      2
#define BTN_LONG_PRESSED 3

/* Short press of BACK.
 * Without this export, a short press of BACK closes the application and
 * returns the user to the previous screen.
 * With this export, the press is delivered here. Typical handling: go one
 * page back; on the first page call request_exit().
 * BTN_BACK_PRESSED is never delivered to app_on_button(). BTN_BACK_KEYDOWN
 * and BTN_BACK_KEYUP are. A long press of BACK always opens the host menu. */
APP_EXPORT(app_on_back, void app_on_back(void));

/* A button event combines a button and an action. Each combination has a
 * constant:
 *
 *     void app_on_button(int32_t event)
 *     {
 *         switch (event) {
 *         case BTN_OK_PRESSED:        start();          break;
 *         case BTN_OK_LONG_PRESSED:   reset();          break;
 *         }
 *     }
 *
 * BTN_OF() and BTN_ACTION_OF() split an event into its two parts.
 * BTN_BACK_LONG_PRESSED is never delivered: a long press of BACK opens
 * the host menu. */
#define BTN_EVENT(button, action) ((button) << 2 | (action))
#define BTN_OF(event)             ((event) >> 2)
#define BTN_ACTION_OF(event)      ((event) & 3)

#define BTN_UP_KEYDOWN        BTN_EVENT(BTN_UP, BTN_KEYDOWN)
#define BTN_UP_KEYUP          BTN_EVENT(BTN_UP, BTN_KEYUP)
#define BTN_UP_PRESSED        BTN_EVENT(BTN_UP, BTN_PRESSED)
#define BTN_UP_LONG_PRESSED   BTN_EVENT(BTN_UP, BTN_LONG_PRESSED)
#define BTN_OK_KEYDOWN        BTN_EVENT(BTN_OK, BTN_KEYDOWN)
#define BTN_OK_KEYUP          BTN_EVENT(BTN_OK, BTN_KEYUP)
#define BTN_OK_PRESSED        BTN_EVENT(BTN_OK, BTN_PRESSED)
#define BTN_OK_LONG_PRESSED   BTN_EVENT(BTN_OK, BTN_LONG_PRESSED)
#define BTN_DOWN_KEYDOWN      BTN_EVENT(BTN_DOWN, BTN_KEYDOWN)
#define BTN_DOWN_KEYUP        BTN_EVENT(BTN_DOWN, BTN_KEYUP)
#define BTN_DOWN_PRESSED      BTN_EVENT(BTN_DOWN, BTN_PRESSED)
#define BTN_DOWN_LONG_PRESSED BTN_EVENT(BTN_DOWN, BTN_LONG_PRESSED)
#define BTN_BACK_KEYDOWN      BTN_EVENT(BTN_BACK, BTN_KEYDOWN)
#define BTN_BACK_KEYUP        BTN_EVENT(BTN_BACK, BTN_KEYUP)
#define BTN_BACK_PRESSED      BTN_EVENT(BTN_BACK, BTN_PRESSED)
#define BTN_BACK_LONG_PRESSED BTN_EVENT(BTN_BACK, BTN_LONG_PRESSED)

APP_EXPORT(app_on_button, void app_on_button(int32_t event));

/* A timer set with timer_set() has expired. */
APP_EXPORT(app_on_timer, void app_on_timer(int32_t id));

/* ---- Localized strings ----
 *
 * Application text is displayed in the language of the device.
 * Translations are provided by the developer in the `strings` folder next
 * to the manifest: one file per language (en.json, ru.json), each an
 * object with the same keys:
 *
 *     { "no_events": "No events", "starts_in": "In {0} min" }
 *
 * tools/perinkpheral generates strings.h: the constants STR_NO_EVENTS,
 * STR_STARTS_IN and the function app_string(id). English is compiled into
 * the module. Every other language is written as a table file.
 * When the application is loaded, the host copies the table of the device
 * language over the English table. A language without a table is shown
 * in English.
 * The build fails if a language is not listed in sdk/languages.json, if a
 * translation has a missing or an extra key, or if a string contains a
 * character that the glyph packs of the language do not cover.
 *
 * Table format: 16 bits buffer size in bytes, 16 bits string count, one
 * 16-bit offset from the table start per string, then the strings,
 * null-terminated. */
APP_EXPORT(app_strings_buffer, uint8_t *app_strings_buffer(void));

/* Replaces "{0}", "{1}" ... in `pattern` with the arguments of those
 * numbers. Arguments can appear in any order.
 * An argument is a text; if `text` is NULL, it is a number.
 * Writes at most `capacity` bytes without a terminator, cut between
 * characters. Returns the length. */
typedef struct host_text_argument {
    int32_t     number;
    const char *text;
    int32_t     length;
} host_text_argument_t;
#ifdef __wasm__ /* a pointer: four bytes in the module, whatever the host is */
HOST_LAYOUT(host_text_argument_t, 12);
#endif
#define HOST_TEXT_ARGUMENTS_MAX 4
HOST_IMPORT(text_fill, int32_t text_fill(const char *pattern, int32_t pattern_length,
                                         const host_text_argument_t *arguments, int32_t count,
                                         char *text, int32_t capacity));

/* The user has saved the application settings on the device web page.
 * Settings are declared in the manifest. Each value is stored as text
 * under the key of the setting and is read with kv_get(). A "list" setting
 * is its items separated by '\n'; sdk/lib/app_state.h reads them.
 * Called on and off the screen, like the data handlers. */
APP_EXPORT(app_on_settings, void app_on_settings(void));

/* Draws the complete frame from the application state. The surface is
 * white on entry. Drawing calls work only inside this handler.
 * Called after request_redraw() and whenever the host rebuilds the frame.
 * The host compares frames and decides whether and how to refresh the
 * panel. */
APP_EXPORT(app_on_draw, void app_on_draw(void));

/* ---- Screen ---- */

/* Requests a redraw: the host calls app_on_draw().
 *
 * When the panel shows the frame depends on the calling handler:
 *   app_on_button(), app_on_back():  immediately;
 *   app_on_timer(), app_on_data(), app_on_alarm(), app_on_intent(),
 *   app_on_settings():               when the refresh policy of the user
 *                                    allows, by default once a minute.
 *
 * Flags:
 *   HOST_REDRAW_NORMAL    Default. Use after a button press and for data
 *                         that can wait.
 *   HOST_REDRAW_SOON      Show immediately without a button press. Use
 *                         for data the user is waiting for.
 *   HOST_REDRAW_CLEAN     Full refresh: 2 - 3 s, one flash, removes
 *                         ghosting. Use for an image that must be clean.
 *   HOST_REDRAW_NO_BLINK  One frame of a sequence: show as soon as the
 *                         panel is ready, never as the periodic full
 *                         refresh. Use for list navigation and for values
 *                         that change every second.
 *
 * SOON and CLEAN refreshes are at least one second apart.
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
 * The host does not limit redraw requests. Every refresh uses battery
 * energy and wears the panel. */
#define HOST_REDRAW_NORMAL   0
#define HOST_REDRAW_SOON     1
#define HOST_REDRAW_CLEAN    2
#define HOST_REDRAW_NO_BLINK 4
HOST_IMPORT(request_redraw, void request_redraw(int32_t flags));

/* Sets application text in the host header. The header is shown above an
 * application that is not full-screen. Host defaults: left side empty
 * (next to the connection icon), right side the date.
 * The text is drawn in the host font and size. A text that does not fit
 * is cut with "...". The two sides never overlap.
 * Length 0 returns the side to the host. A text of one space leaves the
 * side empty.
 * Not a drawing call: callable from any handler. The text is cleared when
 * the application leaves the screen; called off the screen, it has no
 * effect. */
#define HOST_HEADER_LEFT     0
#define HOST_HEADER_RIGHT    1
#define HOST_HEADER_TEXT_MAX 63
HOST_IMPORT(header_text, int32_t header_text(int32_t side, const char *text, int32_t length));

/* Sets a status in the host header: an icon and a short text shown above
 * every screen with a header while the application is off the screen.
 * Use for an activity the user started, such as a countdown.
 * Layout: icon text ... text icon. On HOST_HEADER_LEFT the icon replaces
 * the host icon and the text follows it. On HOST_HEADER_RIGHT the text
 * replaces the date and the icon follows it.
 * A side belongs to the application that set it last. Length 0 with
 * HOST_ICON_NONE releases the side, if this application still owns it.
 * The side is also released when the application is removed and when the
 * device restarts.
 * Callable from any handler, on and off the screen. A change is shown as
 * a data-driven redraw. */
#define HOST_STATUS_TEXT_MAX 31
HOST_IMPORT(header_status, int32_t header_status(int32_t side, int32_t icon, const char *text,
                                                 int32_t length));

/* ---- Navigation ----
 *
 * Navigation is a stack with the menu at the bottom. A host question or
 * an application that took the screen is placed on top; when it closes,
 * the entry below it returns. A long press of BACK clears the stack and
 * opens the menu.
 *   - An application covered by another entry is unloaded and loaded
 *     again when it returns: app_on_blur() and app_on_terminate() before,
 *     app_init() and app_on_focus() after. Keep the state to restore in
 *     the store.
 *   - request_exit() goes one step down the stack: to the entry the
 *     application was opened over, or to the menu.
 *   - An application that took the screen with request_focus() is placed
 *     over the current screen and returns the user to it on exit. */

/* Requests the screen from a background handler (app_on_data(),
 * app_on_alarm() off the screen). The host decides whether to grant it.
 * Ignored on the screen. */
HOST_IMPORT(request_focus, void request_focus(void));

/* Closes the application: one step down the stack. Takes effect when
 * app_on_button(), app_on_back() or app_on_timer() returns; ignored in
 * other handlers. A long press of BACK closes any application without
 * calling it. */
HOST_IMPORT(request_exit, void request_exit(void));

/* One-shot timer: app_on_timer(id) after `milliseconds`. 0 cancels.
 * Setting a running timer restarts it. id: 0..HOST_TIMER_COUNT-1.
 * Timers stop when the application leaves the screen. */
#define HOST_TIMER_COUNT 4
HOST_IMPORT(timer_set, int32_t timer_set(int32_t id, int32_t milliseconds));

/* ---- Intents ----
 *
 * Device events delivered to an application on or off the screen. The
 * manifest lists the events to receive:
 *
 *     "intents": ["time_changed", "network_up", "started"]
 *
 * Each event is delivered through app_on_intent(). An application that is
 * not loaded is loaded for the call. An application without "intents"
 * receives none.
 *
 * app_init() is called before the handler, as before any handler. The
 * instance is the one on the screen if the application is there;
 * otherwise it is loaded for the call and unloaded after it.
 *
 * HOST_INTENT_STARTED is delivered after the device has started and shown
 * its first screen. An application reopened after the restart can be
 * drawn before the intent arrives; app_init() must not depend on it.
 * HOST_INTENT_TIME_CHANGED is also delivered when the device obtains the
 * time for the first time. time_now() already returns the new time inside
 * the handler. Corrections of a few seconds are not reported. */
#define HOST_INTENT_TIME_CHANGED 1 /* the clock or the time zone was changed: recompute
                                      values derived from the time */
#define HOST_INTENT_NETWORK_UP   2 /* the device is connected to its network again */
#define HOST_INTENT_STARTED      3 /* the device was powered up or restarted: restore from
                                      the store what a restart clears - alarms and
                                      pending requests */
APP_EXPORT(app_on_intent, void app_on_intent(int32_t intent));
