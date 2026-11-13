/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#include "app_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The applications' key-value stores: a partition of their own (`appkv`),
 * so that nothing an application keeps can crowd the host's settings. Each
 * application has a namespace, "a.<name>". An application that is removed
 * keeps its store for when it comes back - until the partition is full:
 * then the stores left behind are the first to go. */

/* Opens the partition. Once, after the host's settings are open. */
void app_kv_start(void);

/* A factory reset: every store, before anything is open. */
void app_kv_erase_all(void);

/* The results are the ones of kv_get and kv_set in host_api.h. */
int32_t app_kv_get(const char *app_name, const char *key, void *value, int32_t capacity);

/* value_length 0 removes the key. */
int32_t app_kv_set(const char *app_name, const char *key, const void *value, int32_t value_length);

/* The pace an application asked for one of its polls (poll_set_interval),
 * kept for it through a restart; 0: none, the manifest's. */
uint32_t app_kv_poll_pace(const char *app_name, int poll);
void app_kv_set_poll_pace(const char *app_name, int poll, uint32_t every_s);

/* The names of applications that are not on the device and left a store
 * behind: what would come back with its settings. Returns how many. */
int app_kv_left_behind(char names[][APP_NAME_MAX + 1], int capacity);

/* Forgets everything the application stored: it is being removed. */
void app_kv_erase(const char *app_name);

#ifdef __cplusplus
}
#endif
