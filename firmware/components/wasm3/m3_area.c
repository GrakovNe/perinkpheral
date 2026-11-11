/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "m3_area.h"

#include <stdlib.h>
#include <string.h>

#include "multi_heap.h"

/* A heap of its own inside one block: the allocator of ESP-IDF, told to
 * work in the area instead of in the memory of the host. */
static void               *s_block;
static multi_heap_handle_t s_heap;

void m3_area_start(void)
{
    if (s_block == NULL) {
        s_block = malloc(M3_AREA_BYTES);
        m3_area_reset();
    }
}

void m3_area_reset(void)
{
    s_heap = s_block != NULL ? multi_heap_register(s_block, M3_AREA_BYTES) : NULL;
}

size_t m3_area_most_used(void)
{
    if (s_heap == NULL) {
        return 0;
    }
    multi_heap_info_t info;
    multi_heap_get_info(s_heap, &info);
    return M3_AREA_BYTES - info.minimum_free_bytes;
}

/* For m3_core.c: the interpreter's calloc, free and realloc. An area that
 * was never there or is full answers NULL, and the interpreter says
 * "out of memory" as it would of any heap. */
void *m3_Area_Alloc(size_t size)
{
    void *memory = s_heap != NULL ? multi_heap_malloc(s_heap, size) : NULL;
    if (memory != NULL) {
        memset(memory, 0, size);
    }
    return memory;
}

void m3_Area_Free(void *memory)
{
    if (memory != NULL && s_heap != NULL) {
        multi_heap_free(s_heap, memory);
    }
}

void *m3_Area_Realloc(void *memory, size_t size)
{
    return s_heap != NULL ? multi_heap_realloc(s_heap, memory, size) : NULL;
}
