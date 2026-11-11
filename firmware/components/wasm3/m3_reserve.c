/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "m3_reserve.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* A module's linear memory is one page, 64 KB, and the interpreter puts a
 * small header before it. There is one module at a time, so one block
 * serves them all: taken from the heap once, while the heap is whole, and
 * never given back. Looked for anew at every load, it is sometimes not
 * there - the heap has the bytes but, after a TLS session, not in one
 * piece - and the application that was on the screen cannot come back. */
#define PAGE_BYTES    65536
#define HEADER_BYTES  64 /* more than the interpreter's header */
#define BLOCK_BYTES   (PAGE_BYTES + HEADER_BYTES)

static void *s_block;
static bool  s_is_taken;

void m3_reserve(void)
{
    if (s_block == NULL) {
        s_block = malloc(BLOCK_BYTES);
    }
}

/* For m3_env.c, where linear memory is taken and let go. NULL when the block is in use, was never there or is too
 * small: the interpreter then asks the heap, as it would have. */
void *m3_Reserved_Take(size_t size)
{
    m3_reserve();
    if (s_block == NULL || s_is_taken || size > BLOCK_BYTES) {
        return NULL;
    }
    s_is_taken = true;
    memset(s_block, 0, size);
    return s_block;
}

bool m3_Reserved_Is(void *pointer)
{
    return pointer != NULL && pointer == s_block;
}

/* True when the pointer was the block, which is free again. */
bool m3_Reserved_Give(void *pointer)
{
    if (pointer == NULL || pointer != s_block) {
        return false;
    }
    s_is_taken = false;
    return true;
}
