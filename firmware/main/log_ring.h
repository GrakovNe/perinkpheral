/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The last few kilobytes of text written to it: new text pushes the
 * oldest out. Not safe from several tasks at once - whoever shares it
 * locks around it. Pure, host-tested. */
typedef struct {
    char  *storage;
    size_t capacity;
    size_t start;  /* where the oldest byte is */
    size_t length; /* how many bytes are kept */
} log_ring_t;

void log_ring_init(log_ring_t *ring, char *storage, size_t capacity);

void log_ring_write(log_ring_t *ring, const char *text, size_t length);

/* Copies what is kept, oldest first and terminated, into `text` and
 * returns its length. What was cut by the wrap is dropped up to the first
 * whole line, and what does not fit `text` is dropped from the old end. */
size_t log_ring_read(const log_ring_t *ring, char *text, size_t text_size);

#ifdef __cplusplus
}
#endif
