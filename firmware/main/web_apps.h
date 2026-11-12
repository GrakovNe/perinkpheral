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
 *   POST /api/apps/remove?app=<name>     its files; what it stored stays for when
 *                                   it comes back, unless &forget=1
 *   GET  /api/sources               the catalogs the device installs from
 *   POST /api/sources               {"sources": ["https://...", ...]}
 *   GET  /api/catalog?source=<n>    the index of source number n, passed on as it comes
 *   POST /api/apps/install?source=<n>&app=<name>   {"files": [...]} as the index
 *                                   lists them: downloaded, checked against their
 *                                   sizes and SHA-256, the manifest read, and put
 *                                   in place by the host loop
 */
void web_apps_register(httpd_handle_t server);

#define WEB_APPS_ROUTES 7

#ifdef __cplusplus
}
#endif
