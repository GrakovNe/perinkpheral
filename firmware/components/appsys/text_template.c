/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "text_template.h"

#include <string.h>

#define OPENING "{{"
#define CLOSING "}}"
#define BRACES  2 /* the length of either */

/* What a URL takes as it is; everything else goes in as %XX. */
static bool is_unreserved(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '-' || character == '.' ||
           character == '_' || character == '~';
}

/* Percent-encodes the terminated `value` where it lies; false when the
 * longer text does not fit into `capacity`. */
static bool is_printable(const char *text)
{
    for (; *text != '\0'; text++) {
        if (*text < ' ' || *text > '~') {
            return false;
        }
    }
    return true;
}

static bool encode_in_place(char *value, size_t capacity)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t length = strlen(value), encoded_length = 0;
    for (size_t i = 0; i < length; i++) {
        encoded_length += is_unreserved(value[i]) ? 1 : 3;
    }
    if (encoded_length >= capacity) {
        return false;
    }
    /* From the end, so that nothing is overwritten before it is read. */
    size_t to = encoded_length;
    value[to] = '\0';
    for (size_t from = length; from-- > 0;) {
        unsigned char character = (unsigned char)value[from];
        if (is_unreserved((char)character)) {
            value[--to] = (char)character;
        } else {
            value[--to] = HEX[character & 0x0F];
            value[--to] = HEX[character >> 4];
            value[--to] = '%';
        }
    }
    return true;
}

static bool fill(const char *template_text, text_template_value_fn value_of, void *context,
                 char *filled, size_t filled_size, bool for_url)
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
        /* What a URL begins with is the server - "https://ha.example.org",
         * a setting of the type that is one - and goes in as it is. Anywhere
         * else a value is a part of a path or of a query, and is encoded. */
        bool is_server_prefix = for_url && opening == template_text;
        if (!value_of(context, key, filled + used, filled_size - used)) {
            return false;
        }
        /* A value that goes in as it is - into a header, or as the server -
         * must not bring a line break along: that would be a header of its
         * own making. Whatever wrote the value, it is looked at here. */
        bool is_verbatim = !for_url || is_server_prefix;
        if (is_verbatim ? !is_printable(filled + used)
                             : !encode_in_place(filled + used, filled_size - used)) {
            return false;
        }
        used += strlen(filled + used);
        rest = closing + BRACES;
    }
}

static bool fill_or_empty(const char *template_text, text_template_value_fn value_of,
                          void *context, char *filled, size_t filled_size, bool for_url)
{
    if (filled_size == 0) {
        return false;
    }
    if (!fill(template_text, value_of, context, filled, filled_size, for_url)) {
        filled[0] = '\0';
        return false;
    }
    return true;
}

bool text_template_fill(const char *template_text, text_template_value_fn value_of,
                        void *context, char *filled, size_t filled_size)
{
    return fill_or_empty(template_text, value_of, context, filled, filled_size, false);
}

bool text_template_fill_url(const char *template_text, text_template_value_fn value_of,
                            void *context, char *filled, size_t filled_size)
{
    return fill_or_empty(template_text, value_of, context, filled, filled_size, true);
}
