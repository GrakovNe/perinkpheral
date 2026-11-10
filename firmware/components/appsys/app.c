/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app.h"

#include <stdlib.h>
#include <string.h>

#include "host_api.h"
#include "wasm3.h"

/* The interpreter's own stack of values, apart from the application's
 * memory. */
#define INTERPRETER_STACK_BYTES 4096

#define HOST_MODULE "host"

struct app {
    IM3Environment        environment;
    IM3Runtime            runtime;
    const app_services_t *services;

    IM3Function on_focus;
    IM3Function on_blur;
    IM3Function on_button;
    IM3Function on_timer;
    IM3Function on_draw;

    gfx_t      *surface; /* set only while app_on_draw() runs */
    bool        redraw_requested;
    const char *stop_reason; /* NULL while the application is alive */
};

/* ---- the host API as the application sees it ---- */

static const gfx_font_t *font_by_id(int32_t font)
{
    return font == HOST_FONT_TEXT ? gfx_font_system() : NULL;
}

/* An offset and a length from the application, which is not trusted, as
 * a place in its memory; NULL when any of it lies outside. */
static const uint8_t *in_app_memory(IM3Runtime runtime, void *memory, uint32_t offset,
                                    int32_t length)
{
    uint32_t memory_size = m3_GetMemorySize(runtime);
    bool inside = length >= 0 && offset <= memory_size &&
                  (uint32_t)length <= memory_size - offset;
    return inside ? (const uint8_t *)memory + offset : NULL;
}

/* The application's bytes as a terminated string the drawing code takes.
 * False when they are not the application's to give. */
static bool copy_text(IM3Runtime runtime, void *memory, uint32_t offset, int32_t length,
                      char terminated[HOST_TEXT_MAX + 1])
{
    const uint8_t *text = in_app_memory(runtime, memory, offset, length);
    if (text == NULL) {
        return false;
    }
    if (length > HOST_TEXT_MAX) {
        length = HOST_TEXT_MAX;
    }
    memcpy(terminated, text, (size_t)length);
    terminated[length] = '\0';
    return true;
}

#define APP_OF(runtime) ((app_t *)m3_GetUserData(runtime))

/* An import the way wasm3 calls it, for the m3Api* macros to work on. Not
 * every import needs every one of the four arguments. */
#define MAYBE_UNUSED __attribute__((unused))
#define HOST_FUNCTION(name)                                                              \
    static const void *name(IM3Runtime runtime MAYBE_UNUSED, IM3ImportContext _ctx MAYBE_UNUSED, \
                            uint64_t *_sp MAYBE_UNUSED, void *_mem MAYBE_UNUSED)

HOST_FUNCTION(host_gfx_width)
{
    m3ApiReturnType(int32_t);
    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->surface != NULL ? app->surface->w : HOST_ERR_NOT_DRAWING);
}

HOST_FUNCTION(host_gfx_height)
{
    m3ApiReturnType(int32_t);
    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->surface != NULL ? app->surface->h : HOST_ERR_NOT_DRAWING);
}

HOST_FUNCTION(host_gfx_text)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, font_id);
    m3ApiGetArg(uint32_t, text);
    m3ApiGetArg(int32_t, length);

    app_t *app = APP_OF(runtime);
    const gfx_font_t *font = font_by_id(font_id);
    char terminated[HOST_TEXT_MAX + 1];
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    if (font == NULL || !copy_text(runtime, _mem, text, length, terminated)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(gfx_text_font(app->surface, x, y + font->ascent, font, terminated, true));
}

HOST_FUNCTION(host_gfx_text_width)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    m3ApiGetArg(uint32_t, text);
    m3ApiGetArg(int32_t, length);

    const gfx_font_t *font = font_by_id(font_id);
    char terminated[HOST_TEXT_MAX + 1];
    if (font == NULL || !copy_text(runtime, _mem, text, length, terminated)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(gfx_text_font_width(font, terminated));
}

HOST_FUNCTION(host_gfx_font_height)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    const gfx_font_t *font = font_by_id(font_id);
    m3ApiReturn(font != NULL ? gfx_font_height(font) : HOST_ERR_ARGUMENT);
}

HOST_FUNCTION(host_gfx_rect)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, height);
    m3ApiGetArg(int32_t, fill);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    if (fill == HOST_RECT_FILLED) {
        gfx_fill(app->surface, x, y, width, height, true);
    } else {
        gfx_frame(app->surface, x, y, width, height, 1);
    }
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_gfx_line)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x0);
    m3ApiGetArg(int32_t, y0);
    m3ApiGetArg(int32_t, x1);
    m3ApiGetArg(int32_t, y1);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    gfx_line(app->surface, x0, y0, x1, y1);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_gfx_bitmap)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, height);
    m3ApiGetArg(uint32_t, bits_offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    /* Bounded by the surface first, so that the size below cannot overflow. */
    if (width <= 0 || height <= 0 || width > app->surface->w || height > app->surface->h) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int32_t bytes_per_row = (width + 7) / 8;
    const uint8_t *bits = in_app_memory(runtime, _mem, bits_offset, bytes_per_row * height);
    if (bits == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    for (int32_t row = 0; row < height; row++) {
        for (int32_t column = 0; column < width; column++) {
            bool ink = (bits[row * bytes_per_row + column / 8] & (0x80 >> (column % 8))) != 0;
            gfx_pixel(app->surface, x + column, y + row, ink);
        }
    }
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_request_redraw)
{
    APP_OF(runtime)->redraw_requested = true;
    m3ApiSuccess();
}

HOST_FUNCTION(host_timer_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int32_t, milliseconds);
    if (id < 0 || id >= HOST_TIMER_COUNT || milliseconds < 0) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    APP_OF(runtime)->services->timer_set(id, milliseconds);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_time_now)
{
    m3ApiReturnType(int64_t);
    m3ApiReturn(APP_OF(runtime)->services->unix_time());
}

HOST_FUNCTION(host_time_uptime_ms)
{
    m3ApiReturnType(int64_t);
    m3ApiReturn(APP_OF(runtime)->services->uptime_ms());
}

HOST_FUNCTION(host_log)
{
    m3ApiGetArg(int32_t, level);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    const uint8_t *text = in_app_memory(runtime, _mem, text_offset, length);
    if (text != NULL) {
        APP_OF(runtime)->services->log(level, (const char *)text, length);
    }
    m3ApiSuccess();
}

/* wasm3 signatures: the result, then the arguments. v nothing, i i32,
 * I i64, * a pointer into the application's memory. */
static const struct {
    const char *name;
    const char *signature;
    M3RawCall   function;
} HOST_API[] = {
    { "gfx_width", "i()", host_gfx_width },
    { "gfx_height", "i()", host_gfx_height },
    { "gfx_text", "i(iii*i)", host_gfx_text },
    { "gfx_text_width", "i(i*i)", host_gfx_text_width },
    { "gfx_font_height", "i(i)", host_gfx_font_height },
    { "gfx_rect", "i(iiiii)", host_gfx_rect },
    { "gfx_line", "i(iiii)", host_gfx_line },
    { "gfx_bitmap", "i(iiii*)", host_gfx_bitmap },
    { "request_redraw", "v()", host_request_redraw },
    { "timer_set", "i(ii)", host_timer_set },
    { "time_now", "I()", host_time_now },
    { "time_uptime_ms", "I()", host_time_uptime_ms },
    { "log", "v(i*i)", host_log },
};
#define HOST_API_COUNT (sizeof HOST_API / sizeof HOST_API[0])

/* False when the module declares one of these with another signature. A
 * function the module does not import is simply not linked. */
static bool link_host_api(IM3Module module)
{
    for (size_t i = 0; i < HOST_API_COUNT; i++) {
        M3Result result = m3_LinkRawFunction(module, HOST_MODULE, HOST_API[i].name,
                                             HOST_API[i].signature, HOST_API[i].function);
        if (result != m3Err_none && result != m3Err_functionLookupFailed) {
            return false;
        }
    }
    return true;
}

/* ---- lifecycle ---- */

static IM3Function find_export(app_t *app, const char *name)
{
    IM3Function function = NULL;
    return m3_FindFunction(&function, app->runtime, name) == m3Err_none ? function : NULL;
}

/* The one place a handler's result is looked at: a trap stops the
 * application. */
static void note_result(app_t *app, M3Result result)
{
    if (result != m3Err_none) {
        app->stop_reason = result;
        app->redraw_requested = true;
    }
}

#define CALL_HANDLER(app, handler, ...)                                  \
    do {                                                                 \
        if ((app)->stop_reason == NULL && (handler) != NULL) {           \
            note_result((app), m3_CallV((handler), ##__VA_ARGS__));      \
        }                                                                \
    } while (0)

app_load_result_t app_load(const uint8_t *wasm, size_t wasm_size, const app_services_t *services,
                           app_t **loaded)
{
    app_t *app = calloc(1, sizeof *app);
    if (app == NULL) {
        return APP_ERR_NO_MEMORY;
    }
    app->services = services;
    app->environment = m3_NewEnvironment();
    app->runtime = app->environment != NULL
                       ? m3_NewRuntime(app->environment, INTERPRETER_STACK_BYTES, app)
                       : NULL;
    if (app->runtime == NULL) {
        app_unload(app);
        return APP_ERR_NO_MEMORY;
    }

    IM3Module module = NULL;
    if (m3_ParseModule(app->environment, &module, wasm, (uint32_t)wasm_size) != m3Err_none) {
        app_unload(app);
        return APP_ERR_BAD_MODULE;
    }
    M3Result load_result = m3_LoadModule(app->runtime, module);
    if (load_result != m3Err_none) {
        /* Until it is loaded the module is ours to free. */
        m3_FreeModule(module);
        app_unload(app);
        return load_result == m3Err_mallocFailed ? APP_ERR_NO_MEMORY : APP_ERR_BAD_MODULE;
    }
    if (!link_host_api(module)) {
        app_unload(app);
        return APP_ERR_BAD_MODULE;
    }

    app->on_focus = find_export(app, "app_on_focus");
    app->on_blur = find_export(app, "app_on_blur");
    app->on_button = find_export(app, "app_on_button");
    app->on_timer = find_export(app, "app_on_timer");
    app->on_draw = find_export(app, "app_on_draw");
    CALL_HANDLER(app, find_export(app, "app_init"));

    *loaded = app;
    return APP_OK;
}

void app_unload(app_t *app)
{
    if (app->runtime != NULL) {
        m3_FreeRuntime(app->runtime);
    }
    if (app->environment != NULL) {
        m3_FreeEnvironment(app->environment);
    }
    free(app);
}

void app_focus(app_t *app)
{
    CALL_HANDLER(app, app->on_focus);
}

void app_blur(app_t *app)
{
    CALL_HANDLER(app, app->on_blur);
}

void app_button(app_t *app, int button, int press)
{
    CALL_HANDLER(app, app->on_button, (int32_t)button, (int32_t)press);
}

void app_timer(app_t *app, int id)
{
    CALL_HANDLER(app, app->on_timer, (int32_t)id);
}

void app_draw(app_t *app, gfx_t *surface)
{
    app->surface = surface;
    CALL_HANDLER(app, app->on_draw);
    app->surface = NULL;
}

bool app_take_redraw_request(app_t *app)
{
    bool requested = app->redraw_requested;
    app->redraw_requested = false;
    return requested;
}

bool app_has_stopped(const app_t *app)
{
    return app->stop_reason != NULL;
}

const char *app_stop_reason(const app_t *app)
{
    return app->stop_reason != NULL ? app->stop_reason : "";
}
