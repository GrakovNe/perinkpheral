/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_values.h"

#include "host_api.h"

#define DECIMAL_BASE     10
#define ROUND_UP_FROM    5
#define WHOLE_DIGITS_MAX 9 /* what fits in 32 bits with room to scale */

bool app_value_text(int32_t index, char *text, int32_t text_size)
{
    int32_t length = text_size > 0 ? data_value(index, text, text_size - 1) : HOST_ERR_NO_SPACE;
    if (length < 0) {
        if (text_size > 0) {
            text[0] = '\0';
        }
        return false;
    }
    text[length] = '\0';
    return true;
}

static bool is_digit(char character)
{
    return character >= '0' && character <= '9';
}

bool app_parse_number(const char *text, int32_t decimals, int32_t *number)
{
    const char *at = text;
    bool is_negative = *at == '-';
    if (is_negative) {
        at++;
    }
    if (!is_digit(*at)) {
        return false;
    }
    int64_t magnitude = 0;
    int32_t digits = 0;
    for (; is_digit(*at); at++) {
        if (++digits > WHOLE_DIGITS_MAX) {
            return false;
        }
        magnitude = magnitude * DECIMAL_BASE + (*at - '0');
    }
    int32_t kept = 0;
    bool round_up = false;
    if (*at == '.') {
        at++;
        for (; is_digit(*at); at++) {
            if (kept < decimals) {
                magnitude = magnitude * DECIMAL_BASE + (*at - '0');
                kept++;
            } else if (kept == decimals) {
                round_up = *at - '0' >= ROUND_UP_FROM;
                kept++; /* only the first dropped digit decides */
            }
        }
    }
    for (; kept < decimals; kept++) {
        magnitude *= DECIMAL_BASE;
    }
    /* An exponent or anything else after the digits: not a number a
     * screen can show as it is. */
    if (*at != '\0' || magnitude > INT32_MAX - 1) {
        return false;
    }
    if (round_up) {
        magnitude++;
    }
    *number = (int32_t)(is_negative ? -magnitude : magnitude);
    return true;
}

bool app_value_number(int32_t index, int32_t decimals, int32_t *number)
{
    char text[HOST_VALUE_MAX + 1];
    return app_value_text(index, text, sizeof text) && app_parse_number(text, decimals, number);
}
