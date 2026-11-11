/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Reading what the host picked out of an answer, inside app_on_values().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The value of path `index` as a terminated string; `text` must hold
 * `text_size` bytes. False when the answer had no such value or it does
 * not fit; `text` is then empty. */
bool app_value_text(int32_t index, char *text, int32_t text_size);

/* The value as a number with `decimals` digits kept after the point,
 * rounded: "-3.46" is -35 with one decimal and -3 with none. Applications
 * have no floating point to spare, and a screen shows tenths at most.
 * False when there is no such value or it is not a number. */
bool app_value_number(int32_t index, int32_t decimals, int32_t *number);

/* The same for text that is already in hand. */
bool app_parse_number(const char *text, int32_t decimals, int32_t *number);
