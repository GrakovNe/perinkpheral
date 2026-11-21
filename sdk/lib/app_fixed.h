/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Numbers with a fixed number of decimals, in 64 bits. Floating point is not
 * available: a price of 356.27 is 35627 with 2 decimals, a percentage of
 * -0.54 is -54 with 2 decimals.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_text.h"

#define APP_FIXED_DECIMALS_MAX 9

/* Reads a decimal number as written in JSON or typed by a person: a sign,
 * digits, a point, digits, an exponent ("1.2e-05"). The result is scaled by
 * 10^decimals and rounded half away from zero: "-3.46" is -35 with 1
 * decimal. Returns false if the text is not a number, the result does
 * not fit in -INT64_MAX..INT64_MAX, or digits discarded after the first
 * 18 significant digits would change the result at the requested decimals.
 * `value` is then unchanged. */
bool app_fixed_parse(const char *text, int32_t decimals, int64_t *value);

/* The same number with another number of decimals, rounded half away from
 * zero: 35627 from 2 to 0 decimals is 356. */
int64_t app_fixed_rescale(int64_t value, int32_t from_decimals, int32_t to_decimals);

/* part / whole with `decimals` decimals, rounded half away from zero; 0 if
 * `whole` is 0 or the rounded result does not fit in int64_t.
 * A percentage with 2 decimals: app_fixed_ratio(change * 100,
 * previous, 2). Both numbers must have the same number of decimals. */
int64_t app_fixed_ratio(int64_t part, int64_t whole, int32_t decimals);

/* Appends the number with its decimals: 35627 with 2 is "356.27", -5 with 2
 * is "-0.05". APP_FIXED_SIGN adds "+" before a positive number. */
#define APP_FIXED_SIGN 1
void app_text_add_fixed(app_text_t *text, int64_t value, int32_t decimals, int32_t flags);
