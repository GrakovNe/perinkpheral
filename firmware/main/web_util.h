/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_http_server.h"

#include "json_tree.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What every part of the web server answers and reads with: web_server.c,
 * web_apps.c, devtools.c. */

/* A route of the web server: an address, and what is checked before its
 * handler runs. The checks are the table's and not the handler's, so a
 * handler cannot be written without them. Each part keeps a table of its
 * own; web_server_start() registers them all and counts them. */
typedef enum {
    WEB_OWN_PAGE, /* refused to a page of another site, reading as well as changing */
    WEB_OPEN,     /* answered to anybody: the page itself */
} web_scope_t;

#define WEB_NO_MEMORY_NEED MEMORY_NEEDS

/* How long a handler may take. The server has one task: a handler that
 * waits for somebody at the device - a minute at most - or for a download
 * runs on a task of its own, or the page hears nothing all that while. */
typedef enum {
    WEB_AT_ONCE, /* on the server's task */
    WEB_WAITS,   /* on a task of its own, one such request at a time */
} web_pace_t;

typedef struct {
    const char     *uri;
    httpd_method_t  method;
    esp_err_t     (*handler)(httpd_req_t *request);
    memory_need_t   memory; /* refused with "Not enough memory." without it; WEB_NO_MEMORY_NEED */
    web_scope_t     scope;
    web_pace_t      pace;
} web_route_t;

#define WEB_ROUTE_COUNT(table) (sizeof(table) / sizeof((table)[0]))

/* JSON, freed after it is sent. */
esp_err_t web_send_json(httpd_req_t *request, const char *http_status, json_t *json);
/* {"<field>": "<message>"}: "ok" with what came of it, "error" with why not. */
esp_err_t web_send_message(httpd_req_t *request, const char *http_status, const char *field,
                           const char *message);
esp_err_t web_send_text(httpd_req_t *request, const char *http_status, const char *text);

/* The next bytes of the request body; 0 or less when the client is gone
 * or has stopped sending. One handler runs at a time, so a client that
 * goes quiet is not waited for without end: it would hold the server and,
 * in an upload, the firmware slot. */
int web_receive(httpd_req_t *request, char *buffer, size_t length);

/* The whole body as a terminated string, in `body` of `body_size`; false
 * when it is missing, too long or the client went away. */
bool web_read_body(httpd_req_t *request, char *body, size_t body_size);
/* The same, allocated: at most `max_bytes`; NULL when it is not there.
 * The caller frees it. */
char *web_read_body_alloc(httpd_req_t *request, size_t max_bytes);

/* A value of the query string; false when the key is not there or the
 * value does not fit. */
bool web_query_value(httpd_req_t *request, const char *key, char *value, size_t value_size);

#ifdef __cplusplus
}
#endif
