/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "esp_http_server.h"

#include "memory.h"

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
 * The server changes nothing itself: a handler asks the host loop
 * (events_call()) and answers with what came of it. */
void web_server_start(void);

bool web_server_is_up(void);

/* For every handler that changes something: true when the request was
 * made by a page of another site, and has been answered with a refusal. */
bool web_server_refuses_cross_site(httpd_req_t *request);

/* The request's body is a firmware image: written into the spare slot,
 * answered, and the device restarts into it. Nobody is asked: whoever
 * calls this has seen to that. */
esp_err_t web_server_take_firmware(httpd_req_t *request);

/* Whether there is room for an answer of this kind; when not, the request
 * is answered "Not enough memory." and false comes back. */
bool web_server_has_memory(httpd_req_t *request, memory_need_t need);

#ifdef __cplusplus
}
#endif
