/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The system API: for an application whose manifest asked for it.
 */
#include "app_internal.h"

/* ---- the system API ---- */

#define LOGO_WORD "perINKpheraL"

HOST_FUNCTION(sys_info)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, what);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, capacity);
    uint8_t *text = app_memory_to_fill(runtime, _mem, text_offset, capacity);
    app_t *app = APP_OF(runtime);
    if (text == NULL || app->services->system_info == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_info(what, (char *)text, capacity));
}



HOST_FUNCTION(sys_number_get)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, what);
    m3ApiGetArg(uint32_t, number_offset);
    uint8_t *number = app_memory_to_fill(runtime, _mem, number_offset, sizeof(sys_number_t));
    app_t *app = APP_OF(runtime);
    if (number == NULL || app->services->system_number_get == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    sys_number_t read = { 0 };
    int32_t result = app->services->system_number_get(what, &read.value, &read.least, &read.most);
    if (result == HOST_OK) {
        memcpy(number, &read, sizeof read);
    }
    m3ApiReturn(result);
}

HOST_FUNCTION(sys_number_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, what);
    m3ApiGetArg(int32_t, value);
    app_t *app = APP_OF(runtime);
    if (app->services->system_number_set == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_number_set(what, value));
}

HOST_FUNCTION(sys_text_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, what);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    const uint8_t *text = app_memory_at(runtime, _mem, text_offset, length);
    app_t *app = APP_OF(runtime);
    if (text == NULL || length > SYS_TEXT_MAX || app->services->system_text_set == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    char terminated[SYS_TEXT_MAX + 1];
    memcpy(terminated, text, (size_t)length);
    terminated[length] = '\0';
    m3ApiReturn(app->services->system_text_set(what, terminated));
}

HOST_FUNCTION(sys_time_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int64_t, unix_seconds);
    app_t *app = APP_OF(runtime);
    if (app->services->system_time_set == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_time_set(unix_seconds));
}

HOST_FUNCTION(sys_app_list)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, ids_offset);
    m3ApiGetArg(int32_t, capacity);
    app_t *app = APP_OF(runtime);
    /* Checked before it is multiplied: a capacity cannot wrap round. */
    if (capacity < 0 || capacity > SYS_APP_LIST_MAX ||
        app->services->system_app_list == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    uint8_t *ids = app_memory_to_fill(runtime, _mem, ids_offset,
                                         capacity * (int32_t)sizeof(uint32_t));
    if (ids == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    uint32_t listed[SYS_APP_LIST_MAX];
    int32_t  count = app->services->system_app_list(listed, capacity);
    memcpy(ids, listed, (size_t)(count < capacity ? count : capacity) * sizeof(uint32_t));
    m3ApiReturn(count);
}

HOST_FUNCTION(sys_app_title)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, index);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, capacity);
    uint8_t *text = app_memory_to_fill(runtime, _mem, text_offset, capacity);
    app_t *app = APP_OF(runtime);
    if (text == NULL || app->services->system_app_title == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_app_title(index, (char *)text, capacity));
}

HOST_FUNCTION(sys_app_remove)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, index);
    app_t *app = APP_OF(runtime);
    if (app->services->system_app_remove == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_app_remove(index));
}

HOST_FUNCTION(sys_app_default)
{
    m3ApiReturnType(uint32_t);
    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->services->system_app_default != NULL ? app->services->system_app_default()
                                                          : 0);
}

HOST_FUNCTION(sys_app_set_default)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, id);
    app_t *app = APP_OF(runtime);
    if (app->services->system_app_set_default == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_app_set_default(id));
}

HOST_FUNCTION(sys_do)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, action);
    app_t *app = APP_OF(runtime);
    if (app->services->system_do == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_do(action));
}

HOST_FUNCTION(sys_logo)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, scale);
    if (scale < 1 || scale > SYS_LOGO_SCALE_MAX || !is_drawable(x) || !is_drawable(y)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    /* One stroke between the letters, as on the splash. */
    gfx_text_sq_ex(app->surface, x, y, LOGO_WORD, scale, true, scale);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(sys_logo_width)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, scale);
    if (scale < 1 || scale > SYS_LOGO_SCALE_MAX) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(gfx_text_sq_width_ex(LOGO_WORD, scale, scale));
}

HOST_FUNCTION(sys_logo_height)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, scale);
    if (scale < 1 || scale > SYS_LOGO_SCALE_MAX) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(gfx_text_sq_height(scale));
}
