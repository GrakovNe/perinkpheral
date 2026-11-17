/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>
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

/* The same, but a setting of the manifest that has nothing stored reads as
 * its "default": what an application and the templates of its requests
 * see. Not for the page, which tells a value from a default. */
int32_t app_kv_get_or_default(const char *app_name, const char *key, void *value,
                              int32_t capacity);

/* value_length 0 removes the key. */
int32_t app_kv_set(const char *app_name, const char *key, const void *value, int32_t value_length);

/* A form value, including an explicit empty value that overrides a default. */
int32_t app_kv_set_setting(const char *app_name, const char *key, const char *value);

/* The pace an application asked for one of its polls (poll_set_interval),
 * kept for it through a restart; 0: none, the manifest's or its setting's.
 * Setting 0 forgets it; false when persistence fails. */
uint32_t app_kv_poll_pace(const char *app_name, int poll);
bool app_kv_set_poll_pace(const char *app_name, int poll, uint32_t every_s);

/* The id of the version of an application that last ran here, for telling
 * an installation and an update apart; 0: none - it is new, or was
 * removed. */
uint32_t app_kv_seen_id(const char *app_name);
void app_kv_set_seen_id(const char *app_name, uint32_t id);

/* Removed: the host keeps nothing for it - its paces go, and installed
 * again it is new again, whatever its store kept. */
void app_kv_forget_host_record(const char *app_name);

/* The names of applications that are not on the device and left a store
 * behind: what would come back with its settings. Returns how many. */
int app_kv_orphaned(char names[][APP_NAME_MAX + 1], int capacity);

/* Forgets everything the application stored: it is being removed. */
void app_kv_erase(const char *app_name);

/* The bytes of the stores' partition no value takes yet. */
size_t app_kv_free_bytes(void);

#ifdef __cplusplus
}
#endif
