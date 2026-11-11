/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wifi_qr.h"

#include <string.h>

#define PREFIX "WIFI:T:nopass;S:"
#define SUFFIX ";;"
/* These mean something to the format itself and go in behind a backslash. */
#define ESCAPED "\\;,:\""

bool wifi_qr_text(const char *ssid, char *text, size_t capacity)
{
    size_t length = 0;
    if (capacity < sizeof PREFIX) {
        return false;
    }
    memcpy(text, PREFIX, sizeof PREFIX - 1);
    length = sizeof PREFIX - 1;

    for (const char *c = ssid; *c != '\0'; c++) {
        bool   escape = strchr(ESCAPED, *c) != NULL;
        size_t needed = escape ? 2 : 1;
        if (length + needed + sizeof SUFFIX > capacity) {
            return false;
        }
        if (escape) {
            text[length++] = '\\';
        }
        text[length++] = *c;
    }
    if (length + sizeof SUFFIX > capacity) {
        return false;
    }
    memcpy(text + length, SUFFIX, sizeof SUFFIX);
    return true;
}
