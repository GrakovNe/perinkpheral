/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_state.h"
#include "manifest_limits.h"

#include "app_text.h"
#include "app_values.h"
#include "host_api.h"

#define SETTING_TEXT_MAX 256 /* the longest value a setting may have, and its end */

bool app_state_load(const char *key, void *state, int32_t size, int32_t version)
{
    return kv_get(key, app_text_length(key), state, size) == size &&
           *(const int32_t *)state == version;
}

bool app_state_save(const char *key, const void *state, int32_t size)
{
    return kv_set(key, app_text_length(key), state, size) == HOST_OK;
}

bool app_state_forget(const char *key)
{
    return kv_set(key, app_text_length(key), key, 0) == HOST_OK;
}

bool app_session_save(const void *state, int32_t size)
{
    return session_set(state, size) == HOST_OK;
}

bool app_session_load(void *state, int32_t size)
{
    /* Read straight into the caller's struct: a copy of HOST_SESSION_MAX bytes on the way
     * would be an eighth of the stack. */
    return size <= HOST_SESSION_MAX && session_get(state, size) == size;
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

int32_t app_setting_count(const char *key)
{
    char    value[SETTING_TEXT_MAX];
    int32_t length = app_setting(key, value, sizeof value);
    int32_t count = length > 0 ? 1 : 0;
    for (int32_t i = 0; i < length; i++) {
        count += value[i] == '\n';
    }
    return count;
}

int32_t app_setting_item(const char *key, int32_t index, char *text, int32_t text_size)
{
    char    value[SETTING_TEXT_MAX];
    int32_t length = app_setting(key, value, sizeof value);
    int32_t start = 0;
    for (int32_t item = 0; start < length; item++) {
        int32_t end = start;
        while (end < length && value[end] != '\n') {
            end++;
        }
        if (item == index) {
            int32_t item_length = end - start;
            if (item_length >= text_size) {
                break;
            }
            for (int32_t i = 0; i < item_length; i++) {
                text[i] = value[start + i];
            }
            text[item_length] = '\0';
            return item_length;
        }
        start = end + 1;
    }
    if (text_size > 0) {
        text[0] = '\0';
    }
    return 0;
}

#define SECONDS_PER_MINUTE 60
#define SECONDS_PER_HOUR   3600
#define SECONDS_PER_DAY    86400
#define PACE_DIGITS_MAX    6

int32_t app_seconds_of(const char *text, int32_t otherwise)
{
    int32_t number = 0, digits = 0;
    while (*text >= '0' && *text <= '9' && digits < PACE_DIGITS_MAX) {
        number = number * 10 + (*text++ - '0');
        digits++;
    }
    while (*text == ' ') {
        text++;
    }
    int32_t unit = *text == '\0' || *text == 's' ? 1
                   : *text == 'm'               ? SECONDS_PER_MINUTE
                   : *text == 'h'               ? SECONDS_PER_HOUR
                   : *text == 'd'               ? SECONDS_PER_DAY
                                                : 0;
    /* As the host reads a pace (app_manifest.c): more than a day is none, and would not fit. */
    return digits == 0 || unit == 0 || number > MANIFEST_POLL_EVERY_MAX_S / unit ? otherwise : number * unit;
}

int32_t app_setting_seconds(const char *key, int32_t otherwise)
{
    char text[32];
    return app_setting(key, text, sizeof text) == 0 ? otherwise : app_seconds_of(text, otherwise);
}

int32_t app_setting_place(const char *key, char *name, int32_t size)
{
    char text[SETTING_TEXT_MAX];
    if (app_setting(key, text, sizeof text) == 0) {
        name[0] = '\0';
        return 0;
    }
    int32_t comma = 0;
    while (text[comma] != '\0' && text[comma] != ',') {
        comma++;
    }
    text[comma] = '\0';
    app_text_copy(name, size, text);
    return app_text_length(name);
}
