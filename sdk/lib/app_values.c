/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_values.h"

#include "app_fixed.h"
#include "host_api.h"

static bool to_text(int32_t length, char *text, int32_t text_size)
{
    if (length < 0) {
        if (text_size > 0) {
            text[0] = '\0';
        }
        return false;
    }
    text[length] = '\0';
    return true;
}

bool app_value_text(int32_t index, char *text, int32_t text_size)
{
    int32_t length = text_size > 0 ? data_value(index, text, text_size - 1) : HOST_ERR_NO_SPACE;
    return to_text(length, text, text_size);
}

bool app_parse_number(const char *text, int32_t decimals, int32_t *number)
{
    int64_t value;
    if (!app_fixed_parse(text, decimals, &value) || value > INT32_MAX || value < -INT32_MAX) {
        return false;
    }
    *number = (int32_t)value;
    return true;
}

bool app_value_number(int32_t index, int32_t decimals, int32_t *number)
{
    char text[HOST_VALUE_MAX + 1];
    return app_value_text(index, text, sizeof text) && app_parse_number(text, decimals, number);
}

bool app_value_fixed(int32_t index, int32_t decimals, int64_t *number)
{
    char text[HOST_VALUE_MAX + 1];
    return app_value_text(index, text, sizeof text) && app_fixed_parse(text, decimals, number);
}

int32_t app_value_count(int32_t index)
{
    int32_t count = data_count(index);
    return count > 0 ? count : 0;
}

bool app_value_element_text(int32_t index, int32_t element, char *text, int32_t text_size)
{
    int32_t length = text_size > 0 ? data_element(index, element, text, text_size - 1)
                                   : HOST_ERR_NO_SPACE;
    return to_text(length, text, text_size);
}

bool app_value_element_fixed(int32_t index, int32_t element, int32_t decimals, int64_t *number)
{
    char text[HOST_VALUE_MAX + 1];
    return app_value_element_text(index, element, text, sizeof text) &&
           app_fixed_parse(text, decimals, number);
}

int32_t app_value_series(int32_t index, int32_t decimals, int32_t *values, int32_t capacity)
{
    int32_t count = app_value_count(index);
    int32_t taken = count < capacity ? count : capacity;
    for (int32_t i = 0; i < taken; i++) {
        /* Spread over all of them: the first, the last, and evenly between. */
        int32_t element = taken < count && taken > 1
                              ? (int32_t)((int64_t)i * (count - 1) / (taken - 1))
                              : i;
        int64_t number;
        bool    fits = app_value_element_fixed(index, element, decimals, &number) &&
                       number <= INT32_MAX && number > HOST_CHART_NO_VALUE;
        values[i] = fits ? (int32_t)number : HOST_CHART_NO_VALUE;
    }
    return taken;
}
