/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The system API: for an application whose manifest asked for it.
 */
#include "catalog.h"
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



/* A key as the application gave it, terminated; false when it cannot be
 * one - no key is this long. */
#define SYS_KEY_MAX 31
static bool take_key(IM3Runtime runtime, void *_mem, uint32_t offset, int32_t length,
                     char key[SYS_KEY_MAX + 1])
{
    if (length <= 0 || length > SYS_KEY_MAX) {
        return false;
    }
    const uint8_t *bytes = app_memory_at(runtime, _mem, offset, length);
    if (bytes == NULL) {
        return false;
    }
    memcpy(key, bytes, (size_t)length);
    key[length] = '\0';
    return true;
}

HOST_FUNCTION(sys_setting_get)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, key_offset);
    m3ApiGetArg(int32_t, key_length);
    m3ApiGetArg(uint32_t, value_offset);
    m3ApiGetArg(int32_t, capacity);
    char     key[SYS_KEY_MAX + 1];
    uint8_t *value = app_memory_to_fill(runtime, _mem, value_offset, capacity);
    app_t   *app = APP_OF(runtime);
    if (!take_key(runtime, _mem, key_offset, key_length, key) || value == NULL ||
        app->services->system_setting_get == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_setting_get(key, (char *)value, capacity));
}

HOST_FUNCTION(sys_setting_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, key_offset);
    m3ApiGetArg(int32_t, key_length);
    m3ApiGetArg(uint32_t, value_offset);
    m3ApiGetArg(int32_t, length);
    char   key[SYS_KEY_MAX + 1];
    app_t *app = APP_OF(runtime);
    if (!take_key(runtime, _mem, key_offset, key_length, key) || length < 0 ||
        length > SYS_SETTING_MAX || app->services->system_setting_set == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    const uint8_t *value = app_memory_at(runtime, _mem, value_offset, length);
    if (value == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    char terminated[SYS_SETTING_MAX + 1];
    memcpy(terminated, value, (size_t)length);
    terminated[length] = '\0';
    m3ApiReturn(app->services->system_setting_set(key, terminated));
}

HOST_FUNCTION(sys_setting_range)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, key_offset);
    m3ApiGetArg(int32_t, key_length);
    m3ApiGetArg(uint32_t, number_offset);
    char     key[SYS_KEY_MAX + 1];
    uint8_t *number = app_memory_to_fill(runtime, _mem, number_offset, sizeof(sys_number_t));
    app_t   *app = APP_OF(runtime);
    if (!take_key(runtime, _mem, key_offset, key_length, key) || number == NULL ||
        app->services->system_setting_range == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    sys_number_t read = { 0 };
    int32_t result = app->services->system_setting_range(key, &read.value, &read.least, &read.most);
    if (result == HOST_OK) {
        memcpy(number, &read, sizeof read);
    }
    m3ApiReturn(result);
}

HOST_FUNCTION(sys_time_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int64_t, unix_seconds);
    app_t *app = APP_OF(runtime);
    if (unix_seconds < HOST_TIME_MIN || unix_seconds > HOST_TIME_MAX) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
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
    _Static_assert(CATALOG_INSTALLED_MAX <= SYS_APP_LIST_MAX, "the list holds every installed one");
    uint32_t listed[SYS_APP_LIST_MAX];
    int32_t  count = app->services->system_app_list(listed, capacity);
    memcpy(ids, listed, (size_t)(count < capacity ? count : capacity) * sizeof(uint32_t));
    m3ApiReturn(count);
}

HOST_FUNCTION(sys_app_title)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, app_id);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, capacity);
    uint8_t *text = app_memory_to_fill(runtime, _mem, text_offset, capacity);
    app_t *app = APP_OF(runtime);
    if (text == NULL || app->services->system_app_title == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_app_title(app_id, (char *)text, capacity));
}

HOST_FUNCTION(sys_app_remove)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, app_id);
    app_t *app = APP_OF(runtime);
    if (app->services->system_app_remove == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_app_remove(app->name, app_id));
}

HOST_FUNCTION(sys_app_get_default)
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

HOST_FUNCTION(sys_run)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, action);
    app_t *app = APP_OF(runtime);
    if (app->services->system_do == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_do(action));
}

HOST_FUNCTION(sys_draw_logo)
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
