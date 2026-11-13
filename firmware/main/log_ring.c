/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "log_ring.h"

#include <stdbool.h>

void log_ring_init(log_ring_t *ring, char *storage, size_t capacity)
{
    ring->storage = storage;
    ring->capacity = capacity;
    ring->start = 0;
    ring->length = 0;
}

void log_ring_write(log_ring_t *ring, const char *text, size_t length)
{
    if (length > ring->capacity) {
        /* Only its end can be kept anyway. */
        text += length - ring->capacity;
        length = ring->capacity;
    }
    for (size_t i = 0; i < length; i++) {
        size_t at = (ring->start + ring->length) % ring->capacity;
        ring->storage[at] = text[i];
        if (ring->length < ring->capacity) {
            ring->length++;
        } else {
            ring->start = (ring->start + 1) % ring->capacity;
        }
    }
}

static char byte_at(const log_ring_t *ring, size_t index)
{
    return ring->storage[(ring->start + index) % ring->capacity];
}

size_t log_ring_read(const log_ring_t *ring, char *text, size_t text_size)
{
    if (text_size == 0) {
        return 0;
    }
    size_t first = 0;
    size_t room = text_size - 1;
    bool is_truncated = ring->length == ring->capacity || ring->length > room;
    if (ring->length > room) {
        first = ring->length - room;
    }
    if (is_truncated) {
        /* What is left starts in the middle of a line: begin after it. */
        while (first < ring->length && byte_at(ring, first) != '\n') {
            first++;
        }
        if (first < ring->length) {
            first++;
        }
    }
    size_t copied = 0;
    for (size_t index = first; index < ring->length; index++) {
        text[copied++] = byte_at(ring, index);
    }
    text[copied] = '\0';
    return copied;
}
