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
    int64_t (*uptime_ms)(void);
    void (*log)(int level, const char *text, int length);
    /* A one-shot for the application on the screen; 0 ms cancels it. */
    void (*timer_set)(int id, int32_t milliseconds);
} app_services_t;

typedef struct app app_t;

typedef enum {
    APP_OK,
    APP_ERR_NO_MEMORY,
    APP_ERR_BAD_MODULE, /* not wasm, or it wants more than one page of memory */
} app_load_result_t;

/* wasm must stay where it is until app_unload(). Runs app_init(). */
app_load_result_t app_load(const uint8_t *wasm, size_t wasm_size, const app_services_t *services,
                           app_t **loaded);

void app_unload(app_t *app);

/* The application's handlers. A missing export is skipped. One that traps
 * stops the application for good: see app_has_stopped(). */
void app_focus(app_t *app);
void app_blur(app_t *app);
void app_button(app_t *app, int button, int press); /* as in host_api.h */
void app_timer(app_t *app, int id);
void app_draw(app_t *app, gfx_t *surface);

/* True once after the application called request_redraw(). */
bool app_take_redraw_request(app_t *app);

/* A handler trapped; nothing is called any more. */
bool app_has_stopped(const app_t *app);

/* Why it stopped, for the log and the screen. */
const char *app_stop_reason(const app_t *app);

#ifdef __cplusplus
}
#endif
