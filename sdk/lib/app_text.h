/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * String building without a C library. printf alone would take about a
 * quarter of the 64 KB an application has.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The degree sign, which the host draws in any font: "21" APP_TEXT_DEGREE. */
#define APP_TEXT_DEGREE "\xC2\xB0"

/* A string builder over a caller-provided buffer. The buffer is always
 * null-terminated and never overrun; text that does not fit is dropped. */
typedef struct {
    char *next;
    char *last; /* position of the terminator when the buffer is full */
} app_text_t;

void app_text_start(app_text_t *text, char *buffer, int32_t buffer_size);

void app_text_add(app_text_t *text, const char *more);

/* Appends a decimal number, with a minus sign if negative. */
void app_text_add_number(app_text_t *text, int32_t value);

/* Appends a decimal number at least `digits` wide, padded with zeros:
 * 7 -> "07". `value` must not be negative. */
void app_text_add_padded(app_text_t *text, int32_t value, int32_t digits);

int32_t app_text_length(const char *text);

/* Copies `from` to `to` as a null-terminated string of at most
 * `size` - 1 characters. */
void app_text_copy(char *to, int32_t size, const char *from);

/* True if the two texts are the same. */
bool app_text_equal(const char *a, const char *b);

/* True if `text` begins with `start`. */
bool app_text_starts(const char *text, const char *start);

/* A moment in words, as time_text() says it in the device's language,
 * null-terminated in `text`. Returns its length. */
int32_t app_time_text(int64_t moment, int32_t style, char *text, int32_t size);
