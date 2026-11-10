/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "http://{{server}}/status?token={{token}}" with the values put in: how
 * a user's settings reach the URLs and headers of an application's polls
 * without the application's code ever holding them. Pure, host-tested. */

/* The longest key between the braces. */
#define TEXT_TEMPLATE_KEY_MAX 15

/* Writes the value of `key`, terminated, into `value`; false when there
 * is no such value or it does not fit. */
typedef bool (*text_template_value_fn)(void *context, const char *key, char *value,
                                       size_t value_size);

/* False when the result does not fit, a key has no value, or braces are
 * left open; `filled` is then empty. Text without braces is copied as it
 * is. */
bool text_template_fill(const char *template_text, text_template_value_fn value_of,
                        void *context, char *filled, size_t filled_size);

#ifdef __cplusplus
}
#endif
