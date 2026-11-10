/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * perINKpheral host API: the one contract between the host firmware and
 * the wasm applications. Applications include this header and link the
 * imports; the host builds its wasm3 import table from the same names.
 * Once a third-party application exists the signatures are frozen and
 * any change bumps HOST_API_VERSION and the "abi" field of manifest.json.
 *
 * Status: draft for milestone 1 (hello, clock). Not implemented yet.
 */
#pragma once

#include <stdint.h>

#define HOST_API_VERSION 1

/* Screen: 400x300, 1 bit. Only the application on the scene may draw;
 * off the scene every gfx_* call is a no-op that returns HOST_ERR_NO_SCENE. */
#define HOST_SCREEN_W 400
#define HOST_SCREEN_H 300

#define HOST_OK              0
#define HOST_ERR_NO_SCENE   -1
#define HOST_ERR_ARG        -2
#define HOST_ERR_NO_KEY     -3
#define HOST_ERR_NO_SPACE   -4

/* gfx_flush modes; the host's refresh policy may still refuse or
 * upgrade to a full refresh. */
#define HOST_FLUSH_PARTIAL 0
#define HOST_FLUSH_FULL    1

/* Button events */
#define HOST_BUTTON_SHORT 0
#define HOST_BUTTON_LONG  1

/* Log levels */
#define HOST_LOG_ERROR 1
#define HOST_LOG_WARN  2
#define HOST_LOG_INFO  3
#define HOST_LOG_DEBUG 4

#define HOST_IMPORT(name) __attribute__((import_module("host"), import_name(#name)))

/* ---- drawing (scene only) ---- */
HOST_IMPORT(gfx_clear)  int32_t gfx_clear(void);
HOST_IMPORT(gfx_text)   int32_t gfx_text(int32_t x, int32_t y, int32_t font_id,
                                         const char *str, int32_t len);
HOST_IMPORT(gfx_rect)   int32_t gfx_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                                         int32_t fill);
HOST_IMPORT(gfx_line)   int32_t gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
HOST_IMPORT(gfx_bitmap) int32_t gfx_bitmap(int32_t x, int32_t y, int32_t w, int32_t h,
                                           const uint8_t *bits);
HOST_IMPORT(gfx_flush)  int32_t gfx_flush(int32_t mode);

/* ---- timers (scene only) ---- */
HOST_IMPORT(timer_set)  int32_t timer_set(int32_t ms);   /* 0 cancels; fires app_on_timer() */

/* ---- scene ---- */
HOST_IMPORT(request_focus) int32_t request_focus(void);  /* only meaningful inside app_on_data() */

/* ---- storage: NVS, namespace = the application's name ---- */
HOST_IMPORT(kv_get) int32_t kv_get(const char *key, int32_t key_len, uint8_t *buf, int32_t cap);
HOST_IMPORT(kv_set) int32_t kv_set(const char *key, int32_t key_len, const uint8_t *buf, int32_t len);

/* ---- log ---- */
HOST_IMPORT(host_log) void host_log(int32_t level, const char *str, int32_t len);

/* ---- exports the application provides; every one is optional ---- */
#define APP_EXPORT(name) __attribute__((export_name(#name)))

APP_EXPORT(app_init)      void app_init(void);
APP_EXPORT(app_on_focus)  void app_on_focus(void);
APP_EXPORT(app_on_blur)   void app_on_blur(void);
APP_EXPORT(app_on_button) void app_on_button(int32_t button, int32_t event); /* button 1..4 */
APP_EXPORT(app_on_timer)  void app_on_timer(void);
APP_EXPORT(app_on_data)   void app_on_data(int32_t poll_id, const uint8_t *body, int32_t len,
                                           int32_t truncated);
