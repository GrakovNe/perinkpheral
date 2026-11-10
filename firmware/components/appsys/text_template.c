/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "text_template.h"

#include <string.h>

#define OPENING "{{"
#define CLOSING "}}"
#define BRACES  2 /* the length of either */

static bool fill(const char *template_text, text_template_value_fn value_of, void *context,
                 char *filled, size_t filled_size)
{
    size_t used = 0;
    const char *rest = template_text;
    for (;;) {
        const char *opening = strstr(rest, OPENING);
        size_t plain_length = opening != NULL ? (size_t)(opening - rest) : strlen(rest);
        if (plain_length >= filled_size - used) {
            return false;
        }
        memcpy(filled + used, rest, plain_length);
        used += plain_length;
        filled[used] = '\0';
        if (opening == NULL) {
            return true;
        }

        const char *key_start = opening + BRACES;
        const char *closing = strstr(key_start, CLOSING);
        size_t key_length = closing != NULL ? (size_t)(closing - key_start) : 0;
        if (key_length == 0 || key_length > TEXT_TEMPLATE_KEY_MAX) {
            return false;
        }
        char key[TEXT_TEMPLATE_KEY_MAX + 1];
        memcpy(key, key_start, key_length);
        key[key_length] = '\0';
        if (!value_of(context, key, filled + used, filled_size - used)) {
            return false;
        }
        used += strlen(filled + used);
        rest = closing + BRACES;
    }
}

bool text_template_fill(const char *template_text, text_template_value_fn value_of,
                        void *context, char *filled, size_t filled_size)
{
    if (filled_size == 0) {
        return false;
    }
    if (!fill(template_text, value_of, context, filled, filled_size)) {
        filled[0] = '\0';
        return false;
    }
    return true;
}
