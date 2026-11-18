/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One response, owned by the network request until delivery returns. */
#define ANSWER_HEADERS_BYTES 1024
#define ANSWER_HEADER_NAME_MAX 63
typedef struct answer_headers {
    char text[ANSWER_HEADERS_BYTES];
    uint16_t used;
    bool overflow;
} answer_headers_t;

void answer_headers_reset(answer_headers_t *headers);
bool answer_header_name_valid(const char *name, size_t length);
void answer_headers_add(answer_headers_t *headers, const char *name, const char *value);
int32_t answer_headers_get(const answer_headers_t *headers, const char *name, size_t name_length,
                           int32_t occurrence, char *value, int32_t capacity);
