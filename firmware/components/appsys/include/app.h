/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_manifest.h"
#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One wasm application in memory: the module, its 64 KB of linear memory
 * and the host API it can call. Knows nothing about scenes or the panel,
 * and no ESP-IDF: what it needs from the device comes in as app_services_t,
 * so the same code runs under the host tests with a real interpreter. */

/* A request an application makes itself, as http_request() of host_api.h
 * takes it. The body is the application's memory: whoever keeps the
 * request copies it. */
typedef struct {
    int            tag;    /* what the answer is delivered under: the application's choice */
    int            method; /* HOST_HTTP_* */
    char           url[APP_URL_MAX + 1];
    app_header_t   headers[APP_HEADERS_MAX];
    int            header_count;
    const uint8_t *body;
    int32_t        body_length;
} app_request_t;

/* What the device offers an application beyond drawing. */
typedef struct {
    int64_t (*unix_time)(void); /* 0 while the time is unknown */
    int32_t (*time_zone_offset)(void); /* seconds east of UTC, 0 while unknown */
    int32_t (*time_zone_offset_at)(int64_t unix_time); /* the same at any moment */
    int64_t (*uptime_ms)(void);
    void (*log)(int level, const char *text, int length);
    /* A one-shot for the application on the screen; 0 ms cancels it. */
    void (*timer_set)(int id, int32_t milliseconds);
    /* The application's own store, as kv_get and kv_set of host_api.h
     * answer: a length or HOST_OK, or a HOST_ERR_*. The key is checked
     * and terminated; value_length 0 removes it. */
    int32_t (*kv_get)(const char *app_name, const char *key, void *value, int32_t capacity);
    int32_t (*kv_set)(const char *app_name, const char *key, const void *value,
                      int32_t value_length);
    /* Whether greys are dithered; when not, a grey is black or white,
     * whichever is nearer. */
    bool (*dithers)(void);
    /* Fetch the application's poll number `poll` now instead of when it is
     * due; false when it has no such poll. */
    bool (*poll_now)(const char *app_name, int poll);
    /* Queues the request: HOST_OK or a HOST_ERR_*. */
    int32_t (*http_request)(const char *app_name, const app_request_t *request);
    /* Another pace for a poll; false when there is no such poll. */
    bool (*poll_set_interval)(const char *app_name, int poll, uint32_t every_s);
    /* The alarm numbered `alarm` of the application: `when` is seconds from
     * now, 0 taking the alarm back, or - `is_a_time` - a moment in unix
     * seconds. False when there is no room for one more. */
    bool (*alarm_set)(const char *app_name, int alarm, int64_t when, bool is_a_time);
    /* The same alarm every so many seconds, the first in `first_in_s`, until
     * it is set another way. */
    bool (*alarm_set_every)(const char *app_name, int alarm, int32_t every_s, int32_t first_in_s);
    /* What stays on a side of the header while the application is away: an
     * icon of the host's (or -1) and a line; both empty gives it back. */
    void (*header_status)(const char *app_name, int side, int icon, const char *text);
    /* The application's own files, as file_size and file_read of
     * host_api.h answer; the name is checked and terminated. */
    int32_t (*file_size)(const char *app_name, const char *file);
    int32_t (*file_read)(const char *app_name, const char *file, int32_t offset, void *bytes,
                         int32_t capacity);
    /* HOST_OK or a HOST_ERR_*; length 0 at offset 0 removes the file. */
    int32_t (*file_write)(const char *app_name, const char *file, int32_t offset,
                          const void *bytes, int32_t length);
    /* For the system API: what the device knows about itself (SYS_INFO_*)
     * as text; the length, or a HOST_ERR_*. May be NULL. */
    int32_t (*system_info)(int what, char *text, int32_t capacity);
    /* The rest of the system API, each as host_api.h says it under sys_*;
     * text arrives terminated. Any may be NULL. */
    int32_t (*system_number_get)(int what, int32_t *value, int32_t *least, int32_t *most);
    int32_t (*system_number_set)(int what, int32_t value);
    int32_t (*system_text_set)(int what, const char *text);
    int32_t (*system_time_set)(int64_t unix_seconds);
    int32_t (*system_app_list)(uint32_t *ids, int32_t capacity);
    int32_t (*system_app_title)(uint32_t id, char *text, int32_t capacity);
    int32_t (*system_app_remove)(uint32_t id);
    uint32_t (*system_app_default)(void);
    int32_t (*system_app_set_default)(uint32_t id);
    int32_t (*system_do)(int action);
} app_services_t;

typedef struct app app_t;

typedef enum {
    APP_OK,
    APP_ERR_NO_MEMORY,
    APP_ERR_OTHER_API,  /* asks the host for a call this host does not have, or has
                           otherwise: made for another version of the host */
    APP_ERR_BAD_MODULE, /* not wasm, more than one page of memory, or an import
                           the host API does not have */
} app_load_result_t;

/* wasm and name must stay where they are until app_unload(); the name is
 * whose store kv_* reads and writes. `with_system`: the application asked
 * for the system API and may import it. Runs app_init(). On failure
 * *loaded is NULL. */
app_load_result_t app_load(const char *name, const uint8_t *wasm, size_t wasm_size,
                           bool with_system, const app_services_t *services, app_t **loaded);

/* NULL is fine. */
void app_unload(app_t *app);

/* The application's handlers. A missing export is skipped. One that traps
 * stops the application for good: see app_has_stopped(). */
void app_focus(app_t *app);
void app_blur(app_t *app);
/* The instance is about to be unloaded, whatever the reason: its last word. */
void app_terminate(app_t *app);
void app_button(app_t *app, int button, int press); /* as in host_api.h */

/* A short press of BACK belongs to an application that has app_on_back();
 * one that has not is simply left. */
bool app_handles_back(const app_t *app);
void app_back(app_t *app);
void app_timer(app_t *app, int id);
void app_draw(app_t *app, gfx_t *surface);

/* A piece of the answer to a poll, as app_on_data() of host_api.h takes
 * it: at most HOST_DATA_CHUNK_MAX bytes, copied into the application's
 * own buffer. An application without the handler is left alone. */
struct host_response; /* host_response_t of host_api.h */

/* Around the handlers of an answer: what data_response() says meanwhile.
 * `response` may be NULL - there was no answer - and `http_status` is
 * then all that is known. */
void app_answer_begins(app_t *app, const struct host_response *response, int http_status);
void app_answer_ends(app_t *app);

void app_data(app_t *app, int request, const uint8_t *data, size_t length, int flags,
              int http_status);

/* The values the host picked out of a JSON answer, as app_on_values() of
 * host_api.h takes them: `values[i]` is the text of path number i of the
 * poll, NULL when the answer had none. http_status below zero: the request failed, and it is a HOST_FAILED_*.
 * The values are read by the application only while the call runs. */
void app_values(app_t *app, int request, int http_status, const char *const *values, int count);

/* The alarm the application set has come: app_on_alarm(). */
void app_alarm(app_t *app, int id);

/* The user changed the application's settings: app_on_settings(). */
void app_settings_changed(app_t *app);

/* News it asked for in its manifest: app_on_intent(). */
void app_intent(app_t *app, int intent);

/* True once after the application called request_focus(). */
bool app_take_focus_request(app_t *app);

/* What the application put into the host's header on `side`
 * (HOST_HEADER_*); empty when it left the side to the host. */
const char *app_header_text(const app_t *app, int side);

/* True once after the application called request_redraw(). */
bool app_take_redraw_request(app_t *app);

/* HOST_REDRAW_* of every request_redraw() since the last call: how the
 * application wants its picture shown. */
int app_take_redraw_flags(app_t *app);

/* True once after the application called request_exit() in a button or
 * timer handler; a call from anywhere else is dropped. */
bool app_take_exit_request(app_t *app);

/* A handler trapped; nothing is called any more. */
bool app_has_stopped(const app_t *app);

/* Why it stopped, for the log and the screen. */
const char *app_stop_reason(const app_t *app);

#ifdef __cplusplus
}
#endif
