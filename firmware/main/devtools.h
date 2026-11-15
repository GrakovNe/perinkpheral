/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#include "web_util.h"


#ifdef __cplusplus
extern "C" {
#endif

/* Development tools over the network, off in release builds
 * (APP_DEV_CONSOLE): what tools/perinkpheral talks to. With them the device is
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
extern const web_route_t DEVTOOLS_ROUTES[];
extern const size_t      DEVTOOLS_ROUTE_COUNT;

#ifdef __cplusplus
}
#endif
