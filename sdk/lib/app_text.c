/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_text.h"

#include "host_api.h"

#include <stdbool.h>

#define INT32_MAX_DIGITS 10

void app_text_start(app_text_t *text, char *buffer, int32_t buffer_size)
{
    text->next = buffer;
    text->last = buffer + buffer_size - 1;
    *text->next = '\0';
}

static void add_char(app_text_t *text, char character)
{
    if (text->next < text->last) {
        *text->next++ = character;
        *text->next = '\0';
    }
}

void app_text_add(app_text_t *text, const char *more)
{
    int32_t added = 0;
    while (*more != '\0' && text->next < text->last) {
        add_char(text, *more++);
        added++;
    }
    /* The text is UTF-8: what did not fit must not end inside a character -
     * the bytes of the one cut in two go, back to its first. */
    if (((unsigned char)*more & 0xC0) == 0x80) {
        while (added > 0) {
            bool is_first_byte = ((unsigned char)text->next[-1] & 0xC0) != 0x80;
            text->next--;
            added--;
            if (is_first_byte) {
                break;
            }
        }
        *text->next = '\0';
    }
}

void app_text_add_padded(app_text_t *text, int32_t value, int32_t digits)
{
    char reversed[INT32_MAX_DIGITS];
    int32_t count = 0;
    uint32_t rest = (uint32_t)value;
    do {
        reversed[count++] = (char)('0' + rest % 10);
        rest /= 10;
    } while (rest > 0);
    for (int32_t pad = count; pad < digits; pad++) {
        add_char(text, '0');
    }
    while (count > 0) {
        add_char(text, reversed[--count]);
    }
}

void app_text_add_number(app_text_t *text, int32_t value)
{
    if (value < 0) {
        add_char(text, '-');
        /* Negated as unsigned: INT32_MIN has no positive twin. */
        value = (int32_t)(0u - (uint32_t)value);
    }
    app_text_add_padded(text, value, 1);
}

int32_t app_text_length(const char *text)
{
    int32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

void app_text_copy(char *to, int32_t size, const char *from)
{
    app_text_t text;
    app_text_start(&text, to, size);
    app_text_add(&text, from);
}

bool app_text_equal(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

bool app_text_starts(const char *text, const char *start)
{
    while (*start != '\0') {
        if (*text++ != *start++) {
            return false;
        }
    }
    return true;
}

int32_t app_time_text(int64_t moment, int32_t style, char *text, int32_t size)
{
    int32_t length = size > 0 ? time_text(moment, style, text, size - 1) : 0;
    length = length > 0 ? length : 0;
    if (size > 0) {
        text[length] = '\0';
    }
    return length;
}
