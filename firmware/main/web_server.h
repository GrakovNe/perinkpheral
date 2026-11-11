/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "appconf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The device's own web page and the API behind it:
 *
 *   GET  /            the page
 *   GET  /api/status  what the device is and how it is doing, as JSON
 *   GET  /api/log     the end of the device's log, as text
 *   POST /api/wifi    {"ssid": "...", "password": "..."}: store and restart
 *   POST /api/ota     a firmware image as the request body: install and restart
 *
 * config is the live settings; the server changes them only through
 * settings_save(). device_id as everywhere: four hex digits. */
void web_server_start(appconf_t *config, const char device_id[5]);

bool web_server_is_up(void);

#ifdef __cplusplus
}
#endif
