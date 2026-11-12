/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#include "esp_http_server.h"

#include "wm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Development tools over the network, off in release builds
 * (APP_DEV_CONSOLE): what tools/devtool talks to. With them the device is
 * looked at, driven and given a new build of an application without a
 * cable and without hands on it.
 *
 *   GET  /api/dev/screen                    the frame the panel shows, a 1-bit BMP;
 *                                           X-Frame counts refreshes, X-Pending is 1
 *                                           while a newer frame waits for the panel
 *   POST /api/dev/button?button=1..4&press=short|long|down|up
 *   POST /api/dev/open?app=<name>           put an installed application on the screen
 *   POST /api/dev/app?name=<name>&file=manifest.json|app.wasm|<data file>
 *                                           the file as the request body
 *   POST /api/dev/run?app=<name>            restart into that application: how a
 *                                           freshly uploaded one is started
 *
 *   POST /api/dev/kv?app=<name>&key=<key>   a value into the application's store, the body
 *   POST /api/dev/restart                   restart, to read the list of applications anew
 *
 * Like the rest of the device's API these ask for nothing. */
void devtools_register(httpd_handle_t server);

/* How many routes devtools_register() adds. */
#define DEVTOOLS_ROUTES 8

/* The host loop's side: it took EVENT_DEV_REQUEST off the queue. Whatever
 * touches the frame or the scenes is done here, on the host's own task.
 * frames_shown and frame_pending are what the screen request reports. */
void devtools_serve(void *request, wm_t *wm, uint32_t frames_shown, bool frame_pending);

#ifdef __cplusplus
}
#endif
