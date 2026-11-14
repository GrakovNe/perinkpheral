/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * State and settings in the application's store, by null-terminated key.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Loads a struct stored under `key`. The first field of the struct is an
 * int32_t version. Returns true if a value of exactly `size` bytes with
 * this `version` is stored; otherwise `state` is unchanged in content that
 * matters: initialise it after a false return. */
bool app_state_load(const char *key, void *state, int32_t size, int32_t version);

/* Stores `size` bytes of `state` under `key`. One flash write: call it
 * when the state changed, in app_on_terminate() or at once for a value
 * that must survive a power loss. */
void app_state_save(const char *key, const void *state, int32_t size);

/* Removes the value stored under `key`. */
void app_state_forget(const char *key);

/* Copies a setting to `text` as a null-terminated string. Returns its
 * length; 0 if the setting is not set or does not fit. */
int32_t app_setting(const char *key, char *text, int32_t text_size);

/* True if the setting equals `wanted`. */
bool app_setting_is(const char *key, const char *wanted);

/* A `toggle` setting: true unless it is set to "0". */
bool app_setting_is_on(const char *key);

/* A `number` setting scaled by 10^decimals, as app_parse_number() reads it.
 * Returns false if the setting is not set or is not a number. */
bool app_setting_number(const char *key, int32_t decimals, int32_t *number);
