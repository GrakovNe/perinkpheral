/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The values the host picked out of an answer ("pick" in the manifest).
 * Value readers are valid inside app_on_picked(); app_parse_number()
 * accepts independent text and works in any handler.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Copies the value of path `index` to `text` as a null-terminated string.
 * `text` must hold `text_size` bytes. Returns false if the answer has no
 * such value or the value does not fit; `text` is then empty. */
bool app_value_text(int32_t index, char *text, int32_t text_size);

/* Reads the value as a number scaled by 10^decimals, rounded: "-3.46" is
 * -35 with one decimal and -3 with none. Floating point is not available.
 * Returns false if there is no such value, it is not a number, or it does
 * not fit in -INT32_MAX..INT32_MAX. app_value_fixed() accepts
 * -INT64_MAX..INT64_MAX. */
bool app_value_number(int32_t index, int32_t decimals, int32_t *number);
bool app_value_fixed(int32_t index, int32_t decimals, int64_t *number);

/* The same conversion for a given null-terminated text; anywhere, not only
 * in app_on_picked(). Exponents are read: "1.2e-05". */
bool app_parse_number(const char *text, int32_t decimals, int32_t *number);

/* A list path ("[*]" in "pick"): the number of its elements, and one of
 * them, as above. Any other path has 1 element, its value, or 0. */
int32_t app_value_count(int32_t index);
bool    app_value_element_text(int32_t index, int32_t element, char *text, int32_t text_size);
bool    app_value_element_fixed(int32_t index, int32_t element, int32_t decimals,
                                int64_t *number);

/* The elements of a list path as numbers for ui_draw_chart(): scaled by
 * 10^decimals, HOST_CHART_NO_VALUE for an element that is not a number
 * ("null") or does not fit in 32 bits. With more elements than `capacity`,
 * `capacity` of them evenly spread, the first and the last among them.
 * Returns the number of values written. */
int32_t app_value_series(int32_t index, int32_t decimals, int32_t *values, int32_t capacity);
