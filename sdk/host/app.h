/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The application's life: the handlers Perinkpheral OS calls as it is loaded,
 * shown, hidden and unloaded, its settings, where the user is in it, and news.
 */
#pragma once

#include "base.h"

/* ---- Exports (all optional) ---- */

/* Called once, immediately after the module is loaded.
 * Perinkpheral OS unloads an application every time it leaves the screen. State
 * that must persist is kept with kv_* calls, not in global variables. */
APP_EXPORT(app_on_load, void app_on_load(void));

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
 * app_on_updated: another package replaced the one that ran before.
 * Package identity is the id derived from both module and manifest, not
 * version-string ordering: changing either makes another package.
 * Called once, on or off the screen, after app_on_load(); off the screen the
 * application is loaded for the call. A new version does not inherit the
 * alarms set by the old one: set them again here. The store, the files
 * and poll intervals set from code are kept across an update. A load
 * failure leaves the notification pending for the next startup/reload.
 * Once loaded it is consumed before the callback (or a missing callback),
 * even if app_on_load() or the callback traps: it is not retried endlessly. */
APP_EXPORT(app_on_installed, void app_on_installed(void));
APP_EXPORT(app_on_updated, void app_on_updated(void));

/* The user has saved the application settings on the device web page.
 * Settings, including hidden ones, are declared in the manifest. Each value is stored as text
 * under the key of the setting and is read with kv_get(). A "list" setting
 * is its items separated by '\n'; sdk/lib/app_state.h reads them.
 * Called on and off the screen, like the data handlers. */
APP_EXPORT(app_on_settings, void app_on_settings(void));

/* Where the user is in the application: a page, a selection, a value half
 * set. The application is unloaded whenever another one needs the room - a
 * delivery in the background, a question of Perinkpheral OS - and enters again
 * from what it stored; what is worth no write of the flash goes here.
 *
 * session_set() keeps up to HOST_SESSION_MAX bytes for the application while
 * it is in the stack: across its unloads, and while another is on top of it.
 * They are gone when it leaves the stack - BACK from its first page,
 * screen_leave(), a long press of BACK, too many above it - when it is
 * opened anew from the menu, and at a restart. Length 0 forgets them.
 * Set them in app_on_blur(); read them in app_on_focus(), which is where an
 * application that comes back is told so. An instance loaded off the screen
 * for a delivery may read them and cannot change them (HOST_ERR_ARGUMENT).
 *
 * session_get() copies them into `buffer` and returns their length; 0 when
 * there are none; HOST_ERR_NO_SPACE, copying nothing, when `capacity` is
 * smaller. The session is attached after app_on_load() returns: read it in
 * app_on_focus() or a background delivery handler. session_set() in
 * app_on_load() returns HOST_ERR_ARGUMENT. sdk/lib/app_state.h keeps one struct
 * this way.
 *
 * The room is taken from Perinkpheral OS's memory when first set and grown
 * when more is asked: HOST_ERR_NO_SPACE when it cannot be given, the bytes
 * kept before staying as they were. Keep it small - a few fields; a packed
 * board of a game is the most it is for. Firmware before 0.1.61 keeps 128
 * bytes: an application that keeps more names "firmware": "0.1.61" in its
 * manifest. */
#define HOST_SESSION_MAX 1024
HOST_IMPORT(session_set, int32_t session_set(const void *data, int32_t length));
HOST_IMPORT(session_get, int32_t session_get(void *buffer, int32_t capacity));

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
 * app_on_load() is called before the handler, as before any handler. The
 * instance is the one on the screen if the application is there;
 * otherwise it is loaded for the call and unloaded after it.
 *
 * HOST_INTENT_STARTED is delivered after the device has started and shown
 * its first screen. An application reopened after the restart can be
 * drawn before the intent arrives; app_on_load() must not depend on it.
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
