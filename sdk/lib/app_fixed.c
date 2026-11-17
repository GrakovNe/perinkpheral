/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_fixed.h"

#define DECIMAL_BASE      10
#define MANTISSA_DIGITS   18 /* what fits in 63 bits whatever the digits */
#define EXPONENT_MAX      400
#define POWERS            20 /* 10^0 .. 10^19, all of which fit in 64 unsigned bits */
#define UINT64_DIGITS_MAX 20

static const uint64_t POWER_OF_TEN[POWERS] = {
    1ull,
    10ull,
    100ull,
    1000ull,
    10000ull,
    100000ull,
    1000000ull,
    10000000ull,
    100000000ull,
    1000000000ull,
    10000000000ull,
    100000000000ull,
    1000000000000ull,
    10000000000000ull,
    100000000000000ull,
    1000000000000000ull,
    10000000000000000ull,
    100000000000000000ull,
    1000000000000000000ull,
    10000000000000000000ull,
};

static bool is_digit(char character)
{
    return character >= '0' && character <= '9';
}

/* `magnitude` / 10^`places`, rounded half away from zero. */
static uint64_t divide_by_power(uint64_t magnitude, int32_t places)
{
    if (places >= POWERS) {
        return 0; /* anything that fits in 64 bits is less than half of 10^20 */
    }
    uint64_t divisor = POWER_OF_TEN[places];
    uint64_t quotient = magnitude / divisor, rest = magnitude % divisor;
    return rest >= divisor - rest ? quotient + 1 : quotient;
}

bool app_fixed_parse(const char *text, int32_t decimals, int64_t *value)
{
    if (text == 0 || decimals < 0 || decimals > APP_FIXED_DECIMALS_MAX) {
        return false;
    }
    const char *at = text;
    while (*at == ' ') {
        at++;
    }
    bool is_negative = *at == '-';
    if (*at == '-' || *at == '+') {
        at++;
    }
    /* The number is mantissa * 10^exponent. */
    uint64_t mantissa = 0;
    int32_t  exponent = 0, kept = 0;
    bool     has_digits = false, in_fraction = false, is_inexact = false;
    for (;; at++) {
        if (is_digit(*at)) {
            has_digits = true;
            if (kept < MANTISSA_DIGITS) {
                if (mantissa != 0 || *at != '0') {
                    kept++;
                }
                mantissa = mantissa * DECIMAL_BASE + (uint64_t)(*at - '0');
                exponent -= in_fraction;
            } else if (!in_fraction) {
                exponent++; /* a digit too many of the whole part: its place counts */
                /* ...and its value would be lost: "1234567890123456789" is no number of 18 digits,
                 * and one past INT64_MAX would pass for one under it. */
                is_inexact = is_inexact || *at != '0';
            }
        } else if (*at == '.' && !in_fraction) {
            in_fraction = true;
        } else {
            break;
        }
    }
    if (!has_digits || is_inexact) {
        return false;
    }
    if (*at == 'e' || *at == 'E') {
        at++;
        bool is_down = *at == '-';
        if (*at == '-' || *at == '+') {
            at++;
        }
        if (!is_digit(*at)) {
            return false;
        }
        int32_t power = 0;
        for (; is_digit(*at); at++) {
            if (power < EXPONENT_MAX) {
                power = power * DECIMAL_BASE + (*at - '0');
            }
        }
        exponent += is_down ? -power : power;
    }
    while (*at == ' ') {
        at++;
    }
    if (*at != '\0') {
        return false;
    }
    int32_t  shift = exponent + decimals;
    uint64_t magnitude;
    if (mantissa == 0) {
        magnitude = 0;
    } else if (shift >= 0) {
        if (shift >= POWERS || mantissa > (uint64_t)INT64_MAX / POWER_OF_TEN[shift]) {
            return false;
        }
        magnitude = mantissa * POWER_OF_TEN[shift];
    } else {
        magnitude = divide_by_power(mantissa, -shift);
    }
    if (magnitude > (uint64_t)INT64_MAX) {
        return false;
    }
    *value = is_negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return true;
}

static uint64_t magnitude_of(int64_t value)
{
    return value < 0 ? (uint64_t)0 - (uint64_t)value : (uint64_t)value;
}

int64_t app_fixed_rescale(int64_t value, int32_t from_decimals, int32_t to_decimals)
{
    int32_t shift = to_decimals - from_decimals;
    if (shift >= 0) {
        return shift < POWERS ? value * (int64_t)POWER_OF_TEN[shift] : 0;
    }
    uint64_t magnitude = divide_by_power(magnitude_of(value), -shift);
    return value < 0 ? -(int64_t)magnitude : (int64_t)magnitude;
}

int64_t app_fixed_ratio(int64_t part, int64_t whole, int32_t decimals)
{
    if (whole == 0) {
        return 0;
    }
    uint64_t dividend = magnitude_of(part), divisor = magnitude_of(whole);
    uint64_t result = dividend / divisor, rest = dividend % divisor;
    /* A decimal at a time: rest * 10^decimals would not fit. */
    for (int32_t i = 0; i < decimals; i++) {
        /* A divisor past a tenth of 64 bits: both halved until rest * 10 fits, the ratio kept. */
        while (rest > UINT64_MAX / DECIMAL_BASE) {
            rest >>= 1;
            divisor >>= 1;
        }
        rest *= DECIMAL_BASE;
        result = result * DECIMAL_BASE + rest / divisor;
        rest %= divisor;
    }
    if (rest >= divisor - rest) {
        result++;
    }
    return (part < 0) != (whole < 0) ? -(int64_t)result : (int64_t)result;
}

/* Appends the digits of `number`, at least `width` of them. */
static void add_digits(app_text_t *text, uint64_t number, int32_t width)
{
    char    digits[UINT64_DIGITS_MAX + 1];
    int32_t at = UINT64_DIGITS_MAX;
    digits[at] = '\0';
    do {
        digits[--at] = (char)('0' + number % DECIMAL_BASE);
        number /= DECIMAL_BASE;
        width--;
    } while ((number > 0 || width > 0) && at > 0);
    app_text_add(text, digits + at);
}

void app_text_add_fixed(app_text_t *text, int64_t value, int32_t decimals, int32_t flags)
{
    if (decimals < 0) {
        decimals = 0;
    } else if (decimals > APP_FIXED_DECIMALS_MAX) {
        decimals = APP_FIXED_DECIMALS_MAX;
    }
    if (value < 0) {
        app_text_add(text, "-");
    } else if (value > 0 && (flags & APP_FIXED_SIGN)) {
        app_text_add(text, "+");
    }
    uint64_t magnitude = magnitude_of(value);
    uint64_t unit = POWER_OF_TEN[decimals];
    add_digits(text, magnitude / unit, 1);
    if (decimals > 0) {
        app_text_add(text, ".");
        add_digits(text, magnitude % unit, decimals);
    }
}
