/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Building short strings without a C library: an application has 64 KB
 * for everything, and printf alone would take a quarter of it.
 */
#pragma once

#include <stdint.h>

/* A string being built in a caller's buffer. It is always terminated and
 * never overruns: what does not fit is dropped. */
typedef struct {
    char *next;
    char *last; /* where the terminator goes when the buffer is full */
} app_text_t;

void app_text_start(app_text_t *text, char *buffer, int32_t buffer_size);

void app_text_add(app_text_t *text, const char *more);

/* Decimal, with a minus sign when negative. */
void app_text_add_number(app_text_t *text, int32_t value);

/* Decimal, at least `digits` wide, padded with zeros: 7 -> "07". For
 * clocks and dates; value must not be negative. */
void app_text_add_padded(app_text_t *text, int32_t value, int32_t digits);

int32_t app_text_length(const char *text);
