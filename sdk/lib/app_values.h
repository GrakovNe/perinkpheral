/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Access to the values extracted by the host. Valid inside app_on_values().
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
 * Returns false if there is no such value or it is not a number. */
bool app_value_number(int32_t index, int32_t decimals, int32_t *number);

/* The same conversion for a given text. */
bool app_parse_number(const char *text, int32_t decimals, int32_t *number);
