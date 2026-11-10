/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The few functions the compiler may call on its own even in code that
 * never names them: copying a struct becomes memcpy, clearing an array
 * becomes memset. Applications are linked without a C library, so they
 * come from here. Built with -fno-builtin, or the loops below would be
 * turned back into calls to themselves.
 */
#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *to = destination;
    const unsigned char *from = source;
    while (count-- > 0) {
        *to++ = *from++;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *to = destination;
    const unsigned char *from = source;
    if (to < from) {
        while (count-- > 0) {
            *to++ = *from++;
        }
    } else {
        while (count-- > 0) {
            to[count] = from[count];
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *to = destination;
    while (count-- > 0) {
        *to++ = (unsigned char)value;
    }
    return destination;
}

size_t strlen(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}
