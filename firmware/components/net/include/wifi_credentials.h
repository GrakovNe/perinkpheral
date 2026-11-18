/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wi-Fi driver fields are byte arrays: full-length SSIDs and hexadecimal
 * keys occupy every byte, without a terminator. Shorter values are padded
 * with zeroes. Returns the number of bytes copied. */
size_t wifi_copy_field(void *field, size_t capacity, const char *text);

#ifdef __cplusplus
}
#endif
