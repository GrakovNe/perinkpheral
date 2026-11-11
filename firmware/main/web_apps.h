/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The Applications section of the device's page: what is installed, and
 * the settings of each, as its manifest declares them.
 *
 *   GET  /api/apps                  every installed application: name, title,
 *                                   version, its settings as the manifest has them,
 *                                   and their values (a secret only as set or not)
 *   POST /api/apps/settings?app=<name>   {"key": "value", ...}: stored, the
 *                                   application told, its polls fetched again
 *   POST /api/apps/remove?app=<name>     its files and everything it stored;
 *                                   the device restarts
 */
void web_apps_register(httpd_handle_t server);

#define WEB_APPS_ROUTES 3

#ifdef __cplusplus
}
#endif
