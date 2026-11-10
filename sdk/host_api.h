/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * perINKpheral host API: the one contract between the host firmware and
 * the wasm applications. Applications include this header and link the
 * imports; the host builds its import table from the same names.
 *
 * The contract is still moving. It freezes as ABI 1 at the end of
 * milestone 2; from then on any change to a signature bumps
 * HOST_API_VERSION and the "abi" field of manifest.json.
 */
#pragma once

#include <stdint.h>

#define HOST_API_VERSION 1

/* The host firmware reads this header too, for the constants. It has
 * functions of its own under some of these names, so outside a wasm build
 * the declarations vanish. */
#ifdef __wasm__
#define HOST_IMPORT(name, ...) \
    __attribute__((import_module("host"), import_name(#name))) __VA_ARGS__
#define APP_EXPORT(name, ...) __attribute__((export_name(#name))) __VA_ARGS__
#else
#define HOST_IMPORT(name, ...)
#define APP_EXPORT(name, ...)
#endif

/* What an import returns when it does not return a value of its own. */
#define HOST_OK               0
#define HOST_ERR_NOT_DRAWING -1 /* a gfx_* call outside app_on_draw() */
#define HOST_ERR_ARGUMENT    -2 /* a pointer outside the memory, an unknown font or timer */
#define HOST_ERR_NOT_FOUND   -3 /* kv_get: nothing is stored under the key */
#define HOST_ERR_NO_SPACE    -4 /* kv_get: the value is larger than the buffer; kv_set: the storage is full */

/* ---- The application's side: exports, every one optional ---- */

/* Once, right after the module is loaded. The host unloads an application
 * whenever it leaves the screen, so state that must survive goes through
 * the kv_* calls, not into globals. */
APP_EXPORT(app_init, void app_init(void));

/* The application got the screen / is about to lose it. */
APP_EXPORT(app_on_focus, void app_on_focus(void));
APP_EXPORT(app_on_blur, void app_on_blur(void));

/* button: 1..4, as they sit on the device. The host's own layout is 1 up,
 * 2 ok, 3 down, 4 back; a long press on 4 never arrives, it opens the
 * menu. */
#define HOST_PRESS_SHORT 2
#define HOST_PRESS_LONG  3
APP_EXPORT(app_on_button, void app_on_button(int32_t button, int32_t press));

/* A timer set with timer_set() ran out. */
APP_EXPORT(app_on_timer, void app_on_timer(int32_t id));

/* Paint the whole picture from the application's state. The surface
 * arrives white; gfx_* calls work only in here. The host calls it when
 * the frame is needed, which is after request_redraw() and whenever the
 * host itself has to put the frame together again. Whether the panel is
 * then refreshed, and how, is the host's business: it compares frames. */
APP_EXPORT(app_on_draw, void app_on_draw(void));

/* ---- Drawing, inside app_on_draw() only ---- */

/* The surface is 400 x 300 for a full-screen application and 400 x 257
 * under the host's header otherwise; 0,0 is its top left corner. Ink is
 * black. Anything drawn outside the surface is cut off. */
HOST_IMPORT(gfx_width, int32_t gfx_width(void));
HOST_IMPORT(gfx_height, int32_t gfx_height(void));

#define HOST_FONT_TEXT 0 /* Departure Mono, 22 px: the host's own font */

/* y is the top of the line, not the baseline. Text longer than
 * HOST_TEXT_MAX bytes is cut. Returns the width drawn. */
#define HOST_TEXT_MAX 127
HOST_IMPORT(gfx_text, int32_t gfx_text(int32_t x, int32_t y, int32_t font, const char *text,
                                       int32_t length));
HOST_IMPORT(gfx_text_width, int32_t gfx_text_width(int32_t font, const char *text,
                                                   int32_t length));
HOST_IMPORT(gfx_font_height, int32_t gfx_font_height(int32_t font));

#define HOST_RECT_OUTLINE 0
#define HOST_RECT_FILLED  1
HOST_IMPORT(gfx_rect, int32_t gfx_rect(int32_t x, int32_t y, int32_t width, int32_t height,
                                       int32_t fill));
HOST_IMPORT(gfx_line, int32_t gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1));

/* bits: row after row, the most significant bit is the left pixel, a set
 * bit is ink, every row padded to a whole byte. */
HOST_IMPORT(gfx_bitmap, int32_t gfx_bitmap(int32_t x, int32_t y, int32_t width, int32_t height,
                                           const uint8_t *bits));

/* ---- The scene ---- */

/* The picture is out of date: call app_on_draw() soon. */
HOST_IMPORT(request_redraw, void request_redraw(void));

/* The application is done: give the screen back, as BACK does in the
 * host's own pages. The host's layout has it on a short press of button 4;
 * a long press of button 4 leaves any application without asking it. */
HOST_IMPORT(request_exit, void request_exit(void));

/* One shot after `milliseconds`, then app_on_timer(id); 0 cancels. Setting
 * a running timer restarts it. id: 0..HOST_TIMER_COUNT-1. Timers stop
 * when the application leaves the screen. */
#define HOST_TIMER_COUNT 4
HOST_IMPORT(timer_set, int32_t timer_set(int32_t id, int32_t milliseconds));

/* ---- Storage ---- */

/* A few small values that outlive the application: it is unloaded every
 * time it leaves the screen, and this is where its state waits. Every
 * application has a store of its own. Writing wears the flash, so save
 * when something worth keeping changed - in app_on_blur(), say - not on
 * every tick. */
#define HOST_KV_KEY_MAX   15  /* 1..15 bytes of printable ASCII */
#define HOST_KV_VALUE_MAX 256

/* Returns the length of the value, which is then in `value`. */
HOST_IMPORT(kv_get, int32_t kv_get(const char *key, int32_t key_length, void *value,
                                   int32_t capacity));

/* A length of 0 removes the key. */
HOST_IMPORT(kv_set, int32_t kv_set(const char *key, int32_t key_length, const void *value,
                                   int32_t length));

/* ---- Time ---- */

/* Unix seconds; 0 while the device does not know the time. */
HOST_IMPORT(time_now, int64_t time_now(void));

/* Milliseconds since the device started; always available. */
HOST_IMPORT(time_uptime_ms, int64_t time_uptime_ms(void));

/* ---- Log ---- */

#define HOST_LOG_ERROR 1
#define HOST_LOG_WARN  2
#define HOST_LOG_INFO  3
#define HOST_LOG_DEBUG 4
HOST_IMPORT(log, void host_log(int32_t level, const char *text, int32_t length));
