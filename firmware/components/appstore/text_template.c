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

#define LIST_SEPARATOR '\n' /* between the items of a list setting, as stored */
#define LIST_JOINED    ','  /* between them in a URL or a header */
#define VALUE_MAX      256  /* a setting's value and its terminator */

/* The items of a list, each encoded, joined with commas: "AAPL,BRK.B". */
static bool encode_list_in_place(char *value, size_t capacity)
{
    char items[VALUE_MAX];
    if (strlen(value) >= sizeof items) {
        return false;
    }
    strcpy(items, value);
    size_t used = 0;
    for (char *item = items;;) {
        char *end = strchr(item, LIST_SEPARATOR);
        if (end != NULL) {
            *end = '\0';
        }
        if (used + strlen(item) >= capacity) {
            return false;
        }
        strcpy(value + used, item);
        if (!encode_in_place(value + used, capacity - used)) {
            return false;
        }
        used += strlen(value + used);
        if (end == NULL) {
            return true;
        }
        if (used + 1 >= capacity) {
            return false;
        }
        value[used++] = LIST_JOINED;
        value[used] = '\0';
        item = end + 1;
    }
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
        /* A list setting goes in as its items with commas between them. */
        char *value = filled + used;
        bool  is_list = strchr(value, LIST_SEPARATOR) != NULL;
        if (is_list && is_verbatim) {
            for (char *at = value; (at = strchr(at, LIST_SEPARATOR)) != NULL;) {
                *at = LIST_JOINED;
            }
        }
        if (is_verbatim ? !is_printable(value)
            : is_list   ? !encode_list_in_place(value, filled_size - used)
                        : !encode_in_place(value, filled_size - used)) {
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

text_request_result_t text_template_prepare_request(app_poll_t *poll, const char *manifest,
                                                     text_template_value_fn value_of, void *context,
                                                     char *url, size_t url_size)
{
    if (manifest != NULL) {
        /* A queued request already expanded its URL. A brace in a link's
         * literal path must not become another template at send time. */
        size_t length = strlen(poll->url);
        if (length >= url_size) {
            return TEXT_REQUEST_NOT_READY;
        }
        memcpy(url, poll->url, length + 1);
    } else if (!text_template_fill_url(poll->url, value_of, context, url, url_size)) {
        return TEXT_REQUEST_NOT_READY;
    }
    /* A setting saved empty is a value, and a URL made of it alone is no request: the
     * user took the address out. Sent on, it was an error of the HTTP client's at every poll. */
    if (url[0] == '\0') {
        return TEXT_REQUEST_NOT_READY;
    }
    if (manifest != NULL && !app_manifest_allows_url(manifest, url, value_of, context)) {
        return TEXT_REQUEST_NOT_ALLOWED;
    }
    char expanded[APP_HEADER_VALUE_MAX + 1];
    for (int i = 0; i < poll->header_count; i++) {
        if (!text_template_fill(poll->headers[i].value, value_of, context, expanded, sizeof expanded)) {
            return TEXT_REQUEST_NOT_READY;
        }
        memcpy(poll->headers[i].value, expanded, strlen(expanded) + 1);
    }
    return TEXT_REQUEST_READY;
}
