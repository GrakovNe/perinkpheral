/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wifi_credentials.h"

#include <string.h>

size_t wifi_copy_field(void *field, size_t capacity, const char *text)
{
    size_t length = strlen(text);
    if (length > capacity) {
        length = capacity;
    }
    memset(field, 0, capacity);
    memcpy(field, text, length);
    return length;
}
