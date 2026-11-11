/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One wasm application in memory: the module, its 64 KB of linear memory
 * and the host API it can call. Knows nothing about scenes or the panel,
 * and no ESP-IDF: what it needs from the device comes in as app_services_t,
 * so the same code runs under the host tests with a real interpreter. */

/* What the device offers an application beyond drawing. */
typedef struct {
    int64_t (*unix_time)(void); /* 0 while the time is unknown */
    int32_t (*time_zone_offset)(void); /* seconds east of UTC, 0 while unknown */
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
    /* Whether the device is on its network now. */
    bool (*is_online)(void);
    /* Fetch the application's poll number `poll` now instead of when it is
     * due; false when it has no such poll. */
    bool (*poll_now)(const char *app_name, int poll);
    /* The application's own files, as file_size and file_read of
     * host_api.h answer; the name is checked and terminated. */
    int32_t (*file_size)(const char *app_name, const char *file);
    int32_t (*file_read)(const char *app_name, const char *file, int32_t offset, void *bytes,
                         int32_t capacity);
} app_services_t;

typedef struct app app_t;

typedef enum {
    APP_OK,
    APP_ERR_NO_MEMORY,
    APP_ERR_BAD_MODULE, /* not wasm, more than one page of memory, or an import
                           the host API does not have */
} app_load_result_t;

/* wasm and name must stay where they are until app_unload(); the name is
 * whose store kv_* reads and writes. Runs app_init(). On failure *loaded
 * is NULL. */
app_load_result_t app_load(const char *name, const uint8_t *wasm, size_t wasm_size,
                           const app_services_t *services, app_t **loaded);

/* NULL is fine. */
void app_unload(app_t *app);

/* The application's handlers. A missing export is skipped. One that traps
 * stops the application for good: see app_has_stopped(). */
void app_focus(app_t *app);
void app_blur(app_t *app);
void app_button(app_t *app, int button, int press); /* as in host_api.h */
void app_timer(app_t *app, int id);
void app_draw(app_t *app, gfx_t *surface);

/* A piece of the answer to a poll, as app_on_data() of host_api.h takes
 * it: at most HOST_DATA_CHUNK_MAX bytes, copied into the application's
 * own buffer. An application without the handler is left alone. */
void app_data(app_t *app, int request, const uint8_t *data, size_t length, int flags,
              int http_status);

/* The values the host picked out of a JSON answer, as app_on_values() of
 * host_api.h takes them: `values[i]` is the text of path number i of the
 * poll, NULL when the answer had none. http_status 0: the request failed.
 * The values are read by the application only while the call runs. */
void app_values(app_t *app, int request, int http_status, const char *const *values, int count);

/* The user changed the application's settings: app_on_settings(). */
void app_settings_changed(app_t *app);

/* True once after the application called request_focus(). */
bool app_take_focus_request(app_t *app);

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
