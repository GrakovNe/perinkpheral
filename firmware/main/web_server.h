/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "esp_http_server.h"

#include "memory.h"
#include "web_util.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The device's own web page and the API behind it. The routes are tables:
 * the page, the device and its settings in web_server.c, the applications,
 * catalogs and glyph packs in web_apps.c, the development tools in
 * devtools.c; CLAUDE.md (Web UI) says what each does.
 *
 * The server changes nothing itself: a handler asks the host loop
 * (events_call()) and answers with what came of it. */
void web_server_start(void);

bool web_server_is_up(void);

/* Room for a message the page is answered with: "Invalid value: <key>.". */
#define WEB_MESSAGE_MAX 64

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
