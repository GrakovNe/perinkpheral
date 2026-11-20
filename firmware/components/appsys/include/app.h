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

/* One loaded wasm application: the module, 64 KB of linear memory and
 * the host API. Independent of scenes, the panel and ESP-IDF: device
 * functions are supplied as app_services_t. The same code runs in the host
 * tests with the real interpreter. */

/* Arguments of http_send() (host_api.h). `body` points into the
 * application memory; a receiver that keeps the request copies it. */
typedef struct {
    int            tag;    /* request number of the answer; chosen by the application */
    int            method; /* HOST_HTTP_* */
    char           url[APP_URL_MAX + 1];
    app_header_t   headers[APP_HEADERS_MAX];
    int            header_count;
    const uint8_t *body;
    int32_t        body_length;
} app_request_t;

/* Device functions for an application, except drawing. */
typedef struct {
    int64_t (*unix_time)(void); /* 0 while the time is unknown */
    int32_t (*time_zone_offset)(void); /* seconds east of UTC, 0 while unknown */
    int32_t (*time_zone_offset_at)(int64_t unix_time); /* the offset at `unix_time` */
    int64_t (*uptime_ms)(void);
    uint32_t (*boot_id)(void); /* differs after every restart; never 0 */
    void (*log_write)(int level, const char *text, int length);
    /* One-shot timer of the current application. 0 ms: cancel. */
    void (*timer_set)(int id, int32_t milliseconds);
    /* Store of the application. Results as kv_get() and kv_set() of
     * host_api.h: a length or HOST_OK, or HOST_ERR_*. `key` is validated
     * and terminated. value_length 0: remove the key. */
    int32_t (*kv_get)(const char *app_name, const char *key, void *value, int32_t capacity);
    int32_t (*kv_set)(const char *app_name, const char *key, const void *value,
                      int32_t value_length);
    /* True: greys are dithered. False: a grey is drawn as the nearer of
     * black and white. */
    bool (*dithers)(void);
    /* Fetches poll `poll` now. False: no such poll. */
    bool (*poll_run)(const char *app_name, int poll);
    /* Queues the request. Returns HOST_OK or HOST_ERR_*. */
    int32_t (*http_send)(const char *app_name, const app_request_t *request);
    /* Sets or clears (0) the code override. Returns HOST_OK or HOST_ERR_*. */
    int32_t (*poll_set_interval)(const char *app_name, int poll, uint32_t every_s);
    /* Sets alarm `alarm`. `when`: seconds from now (0: cancel), or unix
     * seconds when `is_absolute`. False: the alarm table is full. */
    bool (*alarm_set)(const char *app_name, int alarm, int64_t when, bool is_absolute);
    /* Sets alarm `alarm` as recurring every `every_s`, first in
     * `first_in_s`. Replaced by any later set of the same alarm. */
    bool (*alarm_set_every)(const char *app_name, int alarm, int32_t every_s, int32_t first_in_s);
    /* Persistent header status of a side: a host icon (-1: none) and a
     * text. Both empty: release the side. */
    void (*header_set_status)(const char *app_name, int side, int icon, const char *text);
    /* Language code of the device: "en", "ru". May be NULL: English. */
    const char *(*language)(void);
    /* The firmware's version, "0.1.8" or a build's "0.1.8-0adbfb4": an
     * application made for a newer one does not start. May be NULL: any runs. */
    const char *(*firmware_version)(void);
    /* Queues a notification; the host shows it until dismissed. An empty
     * text is passed as NULL. False: the queue is full. May be NULL. */
    bool (*notification_post)(const char *first, const char *second, const char *detail, int icon);
    /* Files of the application. Results as file_size() and file_read() of
     * host_api.h. `file` is validated and terminated. */
    int32_t (*file_size)(const char *app_name, const char *file);
    int32_t (*file_read)(const char *app_name, const char *file, int32_t offset, void *bytes,
                         int32_t capacity);
    /* Returns HOST_OK or HOST_ERR_*. length 0 at offset 0: remove the file. */
    int32_t (*file_write)(const char *app_name, const char *file, int32_t offset,
                          const void *bytes, int32_t length);
    /* System API: device information SYS_INFO_* as text. Returns the
     * length or HOST_ERR_*. May be NULL. */
    int32_t (*system_info)(int what, char *text, int32_t capacity);
    /* System API: the other sys_* calls of host_api.h. Text arguments
     * are terminated. Any may be NULL. */
    int32_t (*system_setting_get)(const char *key, char *value, int32_t capacity);
    int32_t (*system_setting_set)(const char *key, const char *value);
    int32_t (*system_setting_range)(const char *key, int32_t *value, int32_t *least, int32_t *most);
    int32_t (*system_time_set)(int64_t unix_seconds);
    int32_t (*system_app_list)(uint32_t *ids, int32_t capacity);
    int32_t (*system_app_title)(uint32_t id, char *text, int32_t capacity);
    int32_t (*system_app_remove)(const char *app_name, uint32_t id); /* app_name: who asks */
    uint32_t (*system_app_default)(void);
    int32_t (*system_app_set_default)(uint32_t id);
    int32_t (*system_do)(int action);
} app_services_t;

typedef struct app app_t;

typedef enum {
    APP_OK,
    APP_ERR_NO_MEMORY,
    APP_ERR_OTHER_API,  /* imports a call missing in this host or with another
                           signature: built for another host version */
    APP_ERR_BAD_MODULE, /* not wasm, more than one page of memory, or an import
                           outside the host API */
} app_load_result_t;

/* What a manifest asked for beyond the host API, as app_load() takes it. */
#define APP_GRANT_SYSTEM 1u /* "system": the sys_* calls */
#define APP_GRANT_FILES  2u /* "writes_files": file_write() */

/* Loads a module and calls app_on_load(). `wasm` and `name` must remain
 * valid until app_unload(). `name` selects the store of kv_*. `grants`:
 * APP_GRANT_* of what may be imported beside the host API; a module that
 * imports more is refused. On failure *loaded is NULL. */
app_load_result_t app_load(const char *name, uint32_t abi, const uint8_t *wasm, size_t wasm_size,
                           unsigned grants, const app_services_t *services, app_t **loaded);

/* `app` may be NULL. */
void app_unload(app_t *app);

/* Where the user is in an application (session_set, host/app.h): kept by its scene, which
 * outlives the instance. Closed between the scene leaving the stack and its next entry, so
 * that the last app_on_blur() does not write it back. */
typedef struct {
    uint8_t *bytes; /* allocated when first set, up to HOST_SESSION_MAX; NULL for none */
    int32_t  length;
    bool     is_closed;
    bool     screen_inverted;
} app_session_t;

/* The instance reads and writes `session`; one loaded off the screen only reads it. */
void app_attach_session(app_t *app, app_session_t *session, bool is_writable);

/* Handler calls. A missing export is skipped. A trap stops the
 * application permanently, see app_has_stopped(). */
void app_focus(app_t *app);
void app_blur(app_t *app);
/* Calls app_on_terminate(): the last handler before the instance is unloaded. */
void app_terminate(app_t *app);
void app_button(app_t *app, int button, int press); /* values of host_api.h */

/* True when the application exports app_on_back(). Without it a short
 * press of BACK closes the application. */
bool app_handles_back(const app_t *app);
void app_back(app_t *app);
void app_timer(app_t *app, int id);
void app_draw(app_t *app, gfx_t *surface);

/* Calls app_on_data() (host_api.h) with one chunk of an answer: at most
 * HOST_DATA_CHUNK_MAX bytes, copied into the buffer of the application.
 * Skipped when the handler is not exported. */
struct host_answer_info; /* host_answer_info_t of host_api.h */

/* Bracket the handler calls of one answer and define the result of
 * answer_info(), answer_header() and answer_item() between them. `response` NULL: no answer
 * was received; only `http_status` is known. `item`: the item of the list a
 * poll with "each" went over, or -1. */
struct answer_headers;
void app_answer_begins(app_t *app, const struct host_answer_info *response,
                       const struct answer_headers *headers, int http_status,
                       int item);
void app_answer_ends(app_t *app);

void app_data(app_t *app, int request, const uint8_t *data, size_t length, int flags,
              int http_status);

/* Calls app_on_picked() (host_api.h) with the values picked from a JSON
 * answer, which answer_value(), answer_count() and answer_element() read; NULL
 * when there are none. http_status < 0: the request failed, the value is
 * HOST_FAILED_*. The values are valid only during the call. */
void app_values(app_t *app, int request, int http_status, json_pick_t *picked);

/* Calls app_on_alarm(). */
void app_alarm(app_t *app, int id);

/* Calls app_on_settings(). */
void app_settings_changed(app_t *app);

/* Calls app_on_intent(). */
void app_intent(app_t *app, int intent);

/* Calls app_on_installed() or app_on_updated(). */
void app_installed(app_t *app);
void app_updated(app_t *app);

/* True once after a screen_request() call. */
bool app_take_focus_request(app_t *app);

/* Header text of the application for `side` (HOST_HEADER_*). Empty: the
 * side shows the host's content. */
const char *app_header_text(const app_t *app, int side);
bool app_screen_inverted(const app_t *app);

/* True once after a screen_redraw() call. */
bool app_take_redraw_request(app_t *app);

/* Union of the HOST_REDRAW_* flags of the screen_redraw() calls since
 * the previous call of this function. */
int app_take_redraw_flags(app_t *app);

/* True once after a screen_leave() call in a button or timer handler.
 * Calls from other handlers are ignored. */
bool app_take_exit_request(app_t *app);

/* True after a trap. No further handlers are called. */
bool app_has_stopped(const app_t *app);

/* Stop reason, for the log and the screen. */
const char *app_stop_reason(const app_t *app);

#ifdef __cplusplus
}
#endif
