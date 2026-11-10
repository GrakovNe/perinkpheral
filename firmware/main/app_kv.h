/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The applications' key-value stores, in NVS. Each application has a
 * namespace of its own, "a.<name>": an application's name cannot hold a
 * dot, so no application can reach the host's settings or another
 * application's values, whatever it is called. The results are the ones
 * of kv_get and kv_set in host_api.h. */
int32_t app_kv_get(const char *app_name, const char *key, void *value, int32_t capacity);

/* value_length 0 removes the key. */
int32_t app_kv_set(const char *app_name, const char *key, const void *value, int32_t value_length);

#ifdef __cplusplus
}
#endif
