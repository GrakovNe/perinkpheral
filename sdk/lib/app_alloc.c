/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_alloc.h"

#include <stddef.h>

#define PAGE_BYTES 65536
#define ALIGNMENT  8

/* Where the linker ended the module's own data: the free part of the page
 * begins here. Only its address means anything. */
extern unsigned char __heap_base;

static int32_t taken; /* from the start of the free part */

static int32_t start(void)
{
    int32_t base = (int32_t)(uintptr_t)&__heap_base;
    return (base + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

int32_t app_alloc_left(void)
{
    return PAGE_BYTES - start() - taken;
}

void *app_alloc(int32_t bytes)
{
    if (bytes <= 0 || bytes > app_alloc_left()) {
        return NULL;
    }
    void *piece = (void *)(uintptr_t)(start() + taken);
    taken += (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    return piece;
}

int32_t app_alloc_mark(void)
{
    return taken;
}

void app_alloc_release(int32_t mark)
{
    if (mark >= 0 && mark < taken) {
        taken = mark;
    }
}
