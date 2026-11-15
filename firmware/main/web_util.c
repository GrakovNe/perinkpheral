/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "web_util.h"

#include <stdlib.h>

/* How many receive timeouts in a row a body may take: the server's own is
 * RECEIVE_TIMEOUT_S of web_server.c. */
#define QUIET_PERIODS_MAX 3
#define QUERY_MAX         160

esp_err_t web_send_json(httpd_req_t *request, const char *http_status, json_t *json)
{
    char *text = json_print(json);
    json_free(json);
    if (text == NULL) {
        return httpd_resp_send_500(request);
    }
    httpd_resp_set_status(request, http_status);
    httpd_resp_set_type(request, "application/json");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

esp_err_t web_send_message(httpd_req_t *request, const char *http_status, const char *field,
                           const char *message)
{
    json_t *json = json_new_object();
    json_add_string(json, field, message);
    return web_send_json(request, http_status, json);
}

esp_err_t web_send_text(httpd_req_t *request, const char *http_status, const char *text)
{
    httpd_resp_set_status(request, http_status);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, text);
}

int web_receive(httpd_req_t *request, char *buffer, size_t length)
{
    for (int quiet_periods = 0; quiet_periods < QUIET_PERIODS_MAX; quiet_periods++) {
        int count = httpd_req_recv(request, buffer, length);
        if (count != HTTPD_SOCK_ERR_TIMEOUT) {
            return count;
        }
    }
    return 0;
}

static bool receive_whole(httpd_req_t *request, char *body)
{
    size_t received = 0;
    while (received < request->content_len) {
        int count = web_receive(request, body + received, request->content_len - received);
        if (count <= 0) {
            return false;
        }
        received += (size_t)count;
    }
    body[received] = '\0';
    return true;
}

bool web_read_body(httpd_req_t *request, char *body, size_t body_size)
{
    return request->content_len > 0 && request->content_len < body_size &&
           receive_whole(request, body);
}

char *web_read_body_alloc(httpd_req_t *request, size_t max_bytes)
{
    if (request->content_len == 0 || request->content_len > max_bytes) {
        return NULL;
    }
    char *body = malloc(request->content_len + 1);
    if (body != NULL && !receive_whole(request, body)) {
        free(body);
        body = NULL;
    }
    return body;
}

bool web_query_value(httpd_req_t *request, const char *key, char *value, size_t value_size)
{
    char query[QUERY_MAX];
    return httpd_req_get_url_query_str(request, query, sizeof query) == ESP_OK &&
           httpd_query_key_value(query, key, value, value_size) == ESP_OK;
}
