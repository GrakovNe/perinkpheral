/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_DEFAULT 1u
size_t heap_caps_get_largest_free_block(uint32_t caps);
int heap_caps_register_failed_alloc_callback(void (*callback)(size_t, uint32_t, const char *));
