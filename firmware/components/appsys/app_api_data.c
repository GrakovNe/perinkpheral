/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Data: answers, the store, files, requests, polls and alarms.
 */
#include "app_internal.h"

HOST_FUNCTION(data_value)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, capacity);

    app_t *app = APP_OF(runtime);
    uint8_t *text = in_app_memory_to_fill(runtime, _mem, text_offset, capacity);
    if (text == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    const char *value = app->picked != NULL ? json_pick_value(app->picked, index) : NULL;
    if (value == NULL) {
        m3ApiReturn(HOST_ERR_NOT_FOUND);
    }
    size_t length = strlen(value);
    if (length > (size_t)capacity) {
        m3ApiReturn(HOST_ERR_NO_SPACE);
    }
    memcpy(text, value, length);
    m3ApiReturn((int32_t)length);
}

HOST_FUNCTION(data_count)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);

    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->picked != NULL ? (int32_t)json_pick_count(app->picked, index) : 0);
}

HOST_FUNCTION(data_element)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    m3ApiGetArg(int32_t, element);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, capacity);

    app_t *app = APP_OF(runtime);
    uint8_t *text = in_app_memory_to_fill(runtime, _mem, text_offset, capacity);
    if (text == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    const char *value =
        app->picked != NULL ? json_pick_element(app->picked, index, element) : NULL;
    if (value == NULL) {
        m3ApiReturn(HOST_ERR_NOT_FOUND);
    }
    size_t length = strlen(value);
    if (length > (size_t)capacity) {
        m3ApiReturn(HOST_ERR_NO_SPACE);
    }
    memcpy(text, value, length);
    m3ApiReturn((int32_t)length);
}

HOST_FUNCTION(data_item)
{
    m3ApiReturnType(int32_t);

    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->is_in_answer ? (int32_t)app->answer_item : -1);
}

HOST_FUNCTION(data_response)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, response_offset);

    app_t *app = APP_OF(runtime);
    uint8_t *given = in_app_memory_to_fill(runtime, _mem, response_offset,
                                           (int32_t)sizeof(host_response_t));
    if (given == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (!app->is_in_answer) {
        m3ApiReturn(HOST_ERR_NOT_FOUND);
    }
    /* Put together here and copied: the application's address need not be
     * one a structure may stand at. */
    host_response_t response = { .length = -1 };
    if (app->response != NULL) {
        response = *app->response;
    }
    response.status = app->response_status;
    memcpy(given, &response, sizeof response);
    m3ApiReturn(HOST_OK);
}

/* The key as a terminated string; false when it is not a key at all. */
static bool copy_key(IM3Runtime runtime, void *memory, uint32_t offset, int32_t length,
                     char key[HOST_KV_KEY_MAX + 1])
{
    const uint8_t *bytes = in_app_memory(runtime, memory, offset, length);
    if (bytes == NULL || length < 1 || length > HOST_KV_KEY_MAX) {
        return false;
    }
    for (int32_t i = 0; i < length; i++) {
        bool allowed = bytes[i] >= '!' && bytes[i] <= '~';
        if (!allowed) {
            return false;
        }
        key[i] = (char)bytes[i];
    }
    key[length] = '\0';
    return true;
}

HOST_FUNCTION(kv_get)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, key_offset);
    m3ApiGetArg(int32_t, key_length);
    m3ApiGetArg(uint32_t, value_offset);
    m3ApiGetArg(int32_t, capacity);

    app_t *app = APP_OF(runtime);
    char key[HOST_KV_KEY_MAX + 1];
    uint8_t *value = in_app_memory_to_fill(runtime, _mem, value_offset, capacity);
    if (value == NULL || !copy_key(runtime, _mem, key_offset, key_length, key)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->kv_get(app->name, key, value, capacity));
}

/* Every write of the store is a write of the flash. More than this many
 * in a minute is said in the log, once a minute. */
#define KV_WRITES_PER_MINUTE 12

static void note_store_write(app_t *app)
{
    static struct {
        char    name[APP_NAME_MAX + 1];
        int64_t since_ms;
        int     count;
        bool    is_told;
    } writes;
    int64_t now_ms = app->services->uptime_ms();
    if (strncmp(writes.name, app->name, sizeof writes.name) != 0 ||
        now_ms - writes.since_ms >= MS_PER_MINUTE) {
        strncpy(writes.name, app->name, sizeof writes.name - 1);
        writes.since_ms = now_ms;
        writes.count = 0;
        writes.is_told = false;
    }
    if (++writes.count > KV_WRITES_PER_MINUTE && !writes.is_told && app->services->log != NULL) {
        static const char TOLD[] = "the store is written more than 12 times a minute: every "
                                   "write wears the flash; save in app_on_terminate()";
        writes.is_told = true;
        app->services->log(HOST_LOG_WARN, TOLD, (int)sizeof TOLD - 1);
    }
}

HOST_FUNCTION(kv_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, key_offset);
    m3ApiGetArg(int32_t, key_length);
    m3ApiGetArg(uint32_t, value_offset);
    m3ApiGetArg(int32_t, length);

    app_t *app = APP_OF(runtime);
    char key[HOST_KV_KEY_MAX + 1];
    const uint8_t *value = in_app_memory(runtime, _mem, value_offset, length);
    if (value == NULL || length > HOST_KV_VALUE_MAX ||
        !copy_key(runtime, _mem, key_offset, key_length, key)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    note_store_write(app);
    m3ApiReturn(app->services->kv_set(app->name, key, value, length));
}

HOST_FUNCTION(file_size)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, name_offset);
    m3ApiGetArg(int32_t, name_length);
    app_t *app = APP_OF(runtime);
    char name[HOST_FILE_NAME_MAX + 1];
    if (!copy_file_name(runtime, _mem, name_offset, name_length, name)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->file_size(app->name, name));
}

HOST_FUNCTION(file_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, name_offset);
    m3ApiGetArg(int32_t, name_length);
    m3ApiGetArg(int32_t, offset);
    m3ApiGetArg(uint32_t, bytes_offset);
    m3ApiGetArg(int32_t, capacity);
    app_t *app = APP_OF(runtime);
    char name[HOST_FILE_NAME_MAX + 1];
    uint8_t *bytes = in_app_memory_to_fill(runtime, _mem, bytes_offset, capacity);
    if (bytes == NULL || offset < 0 ||
        !copy_file_name(runtime, _mem, name_offset, name_length, name)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->file_read(app->name, name, offset, bytes, capacity));
}

HOST_FUNCTION(poll_now)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, poll);
    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->services->poll_now(app->name, poll) ? HOST_OK : HOST_ERR_ARGUMENT);
}

/* "Name: value" lines into headers; false when a line is not that or
 * there are too many. */
static bool parse_headers(const char *lines, int32_t length, app_request_t *request)
{
    request->header_count = 0;
    int32_t at = 0;
    while (at < length) {
        int32_t line_end = at;
        while (line_end < length && lines[line_end] != '\n') {
            line_end++;
        }
        int32_t colon = at;
        while (colon < line_end && lines[colon] != ':') {
            colon++;
        }
        int32_t value_start = colon + 1;
        while (value_start < line_end && lines[value_start] == ' ') {
            value_start++;
        }
        int32_t name_length = colon - at, value_length = line_end - value_start;
        if (line_end > at) { /* an empty line is skipped */
            if (colon == line_end || name_length < 1 || name_length > APP_HEADER_NAME_MAX ||
                value_length > APP_HEADER_VALUE_MAX || request->header_count == APP_HEADERS_MAX) {
                return false;
            }
            app_header_t *header = &request->headers[request->header_count++];
            memcpy(header->name, lines + at, (size_t)name_length);
            header->name[name_length] = '\0';
            memcpy(header->value, lines + value_start, (size_t)value_length);
            header->value[value_length] = '\0';
        }
        at = line_end + 1;
    }
    return true;
}

/* Text an application gives for a request: no control characters, which
 * in a URL or a header would be the start of something else. */
static bool is_request_text(const uint8_t *text, int32_t length, bool allow_line_breaks)
{
    for (int32_t i = 0; i < length; i++) {
        bool is_line_break = text[i] == '\n';
        if ((text[i] < ' ' || text[i] > '~') && !(allow_line_breaks && is_line_break)) {
            return false;
        }
    }
    return true;
}

HOST_FUNCTION(http_request)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, tag);
    m3ApiGetArg(int32_t, method);
    m3ApiGetArg(uint32_t, url_offset);
    m3ApiGetArg(int32_t, url_length);
    m3ApiGetArg(uint32_t, headers_offset);
    m3ApiGetArg(int32_t, headers_length);
    m3ApiGetArg(uint32_t, body_offset);
    m3ApiGetArg(int32_t, body_length);

    app_t *app = APP_OF(runtime);
    const uint8_t *url = in_app_memory(runtime, _mem, url_offset, url_length);
    const uint8_t *headers = in_app_memory(runtime, _mem, headers_offset, headers_length);
    const uint8_t *body = in_app_memory(runtime, _mem, body_offset, body_length);
    bool is_sound = url != NULL && headers != NULL && body != NULL && url_length >= 1 &&
                    url_length <= APP_URL_MAX && headers_length <= HOST_HTTP_HEADERS_MAX &&
                    body_length <= HOST_HTTP_BODY_MAX && method >= HOST_HTTP_GET &&
                    method <= HOST_HTTP_METHOD_LAST && tag >= HOST_REQUEST_FIRST &&
                    tag <= HOST_REQUEST_LAST && is_request_text(url, url_length, false) &&
                    is_request_text(headers, headers_length, true);
    /* Too large for the stack an application runs on. */
    app_request_t *request = is_sound ? malloc(sizeof *request) : NULL;
    if (request == NULL) {
        m3ApiReturn(is_sound ? HOST_ERR_NO_SPACE : HOST_ERR_ARGUMENT);
    }
    int32_t result = HOST_ERR_ARGUMENT;
    if (parse_headers((const char *)headers, headers_length, request)) {
        request->tag = tag;
        request->method = method;
        memcpy(request->url, url, (size_t)url_length);
        request->url[url_length] = '\0';
        request->body = body;
        request->body_length = body_length;
        result = app->services->http_request(app->name, request);
    }
    free(request);
    m3ApiReturn(result);
}

HOST_FUNCTION(poll_set_interval)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, poll);
    m3ApiGetArg(int32_t, seconds);
    app_t *app = APP_OF(runtime);
    bool changed = seconds > 0 && app->services->poll_set_interval(app->name, poll,
                                                                  (uint32_t)seconds);
    m3ApiReturn(changed ? HOST_OK : HOST_ERR_ARGUMENT);
}

HOST_FUNCTION(alarm_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int32_t, seconds);
    app_t *app = APP_OF(runtime);
    if (id < 0 || id >= HOST_ALARM_COUNT || seconds < 0) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->alarm_set(app->name, id, seconds, false) ? HOST_OK
                                                                         : HOST_ERR_NO_SPACE);
}

HOST_FUNCTION(alarm_set_every)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int32_t, seconds);
    m3ApiGetArg(int32_t, first_in);
    app_t *app = APP_OF(runtime);
    if (id < 0 || id >= HOST_ALARM_COUNT || seconds < HOST_ALARM_EVERY_MIN ||
        seconds > HOST_ALARM_EVERY_MAX || first_in < 0 || first_in > HOST_ALARM_EVERY_MAX ||
        app->services->alarm_set_every == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (first_in == 0) {
        first_in = seconds;
    }
    m3ApiReturn(app->services->alarm_set_every(app->name, id, seconds, first_in)
                    ? HOST_OK
                    : HOST_ERR_NO_SPACE);
}

#define LAST_SECOND_OF_9999 253402300799LL

HOST_FUNCTION(alarm_set_at)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int64_t, unix_time);
    app_t *app = APP_OF(runtime);
    /* Up to the end of the year 9999: beyond it the milliseconds the host
     * counts in would not fit their 64 bits. */
    if (id < 0 || id >= HOST_ALARM_COUNT || unix_time <= 0 || unix_time > LAST_SECOND_OF_9999) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->alarm_set(app->name, id, unix_time, true) ? HOST_OK
                                                                           : HOST_ERR_NO_SPACE);
}

HOST_FUNCTION(file_write)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, name_offset);
    m3ApiGetArg(int32_t, name_length);
    m3ApiGetArg(int32_t, offset);
    m3ApiGetArg(uint32_t, bytes_offset);
    m3ApiGetArg(int32_t, length);
    app_t *app = APP_OF(runtime);
    char name[HOST_FILE_NAME_MAX + 1];
    const uint8_t *bytes = in_app_memory(runtime, _mem, bytes_offset, length);
    if (bytes == NULL || offset < 0 ||
        !copy_file_name(runtime, _mem, name_offset, name_length, name)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->file_write(app->name, name, offset, bytes, length));
}
