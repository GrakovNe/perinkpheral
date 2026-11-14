/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_state.h"

#include "app_text.h"
#include "app_values.h"
#include "host_api.h"

#define SETTING_TEXT_MAX 32 /* of a setting compared or read as a number */

bool app_state_load(const char *key, void *state, int32_t size, int32_t version)
{
    return kv_get(key, app_text_length(key), state, size) == size &&
           *(const int32_t *)state == version;
}

void app_state_save(const char *key, const void *state, int32_t size)
{
    kv_set(key, app_text_length(key), state, size);
}

void app_state_forget(const char *key)
{
    kv_set(key, app_text_length(key), key, 0);
}

int32_t app_setting(const char *key, char *text, int32_t text_size)
{
    int32_t length = text_size > 0 ? kv_get(key, app_text_length(key), text, text_size - 1) : 0;
    if (length < 0) {
        length = 0;
    }
    if (text_size > 0) {
        text[length] = '\0';
    }
    return length;
}

bool app_setting_is(const char *key, const char *wanted)
{
    char    value[SETTING_TEXT_MAX];
    int32_t length = app_setting(key, value, sizeof value);
    for (int32_t i = 0; i <= length; i++) {
        if (value[i] != wanted[i]) {
            return false;
        }
    }
    return length > 0 || wanted[0] == '\0';
}

bool app_setting_is_on(const char *key)
{
    char value[4];
    return !(app_setting(key, value, sizeof value) == 1 && value[0] == '0');
}

bool app_setting_number(const char *key, int32_t decimals, int32_t *number)
{
    char value[SETTING_TEXT_MAX];
    return app_setting(key, value, sizeof value) > 0 && app_parse_number(value, decimals, number);
}
