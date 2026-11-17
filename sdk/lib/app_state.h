/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * State and settings in the application's store, by null-terminated key.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "host_api.h"

/* Loads a struct stored under `key`. The first field of the struct is an
 * int32_t version. Returns true if a value of exactly `size` bytes with
 * this `version` is stored. On false `state` may hold any part of what was
 * read: initialise it whole. */
bool app_state_load(const char *key, void *state, int32_t size, int32_t version);

/* Stores `size` bytes of `state` under `key`. One flash write: call it
 * when the state changed, in app_on_terminate() or at once for a value
 * that must survive a power loss. False when it was not stored - the
 * application's 2 KB are full, or the storage failed: what is lost is the
 * caller's to judge. */
bool app_state_save(const char *key, const void *state, int32_t size);

/* Removes the value stored under `key`; false when the storage failed. */
bool app_state_forget(const char *key);

/* Where the user is in the application - a page, a selection, a value half
 * set - kept while it is in the stack, in memory (session_set, host/app.h).
 * Save it in app_on_blur(), load it in app_on_focus(). The struct is at most
 * HOST_SESSION_MAX bytes: APP_SESSION_FITS(type) says so when it builds.
 * app_session_load() is true when what is kept is exactly `size` bytes, and
 * leaves `state` as it was otherwise. */
bool app_session_save(const void *state, int32_t size);
bool app_session_load(void *state, int32_t size);
#define APP_SESSION_FITS(type) \
    _Static_assert(sizeof(type) <= HOST_SESSION_MAX, #type " is larger than HOST_SESSION_MAX")

/* Copies a setting to `text` as a null-terminated string. Returns its
 * length; 0 if the setting is not set or does not fit, `text` then being
 * empty. A `list` setting comes as its items, each ended by '\n' but the
 * last: see app_setting_item(). */
int32_t app_setting(const char *key, char *text, int32_t text_size);

/* True if the setting equals `wanted`. */
bool app_setting_is(const char *key, const char *wanted);

/* A `toggle` setting: true unless it is set to "0". */
bool app_setting_is_on(const char *key);

/* A `number` setting scaled by 10^decimals, as app_parse_number() reads it.
 * Returns false if the setting is not set or is not a number. */
bool app_setting_number(const char *key, int32_t decimals, int32_t *number);

/* A `list` setting: the number of its items, and item number `index` as a
 * null-terminated string. app_setting_item() returns its length; 0 if there
 * is no such item or it does not fit, `text` then being empty. */
int32_t app_setting_count(const char *key);
int32_t app_setting_item(const char *key, int32_t index, char *text, int32_t text_size);

/* A pace or a delay as a person writes it - "30 s", "15 min", "1 h", "1 d" -
 * in seconds, read as the host reads a poll's "every"; `otherwise` when the
 * text says no such thing ("Off", "At start"). */
int32_t app_seconds_of(const char *text, int32_t otherwise);

/* The same of a setting; `otherwise` too when it is not set. */
int32_t app_setting_seconds(const char *key, int32_t otherwise);

/* A `place` setting's name: what stands before the first comma of "Saint
 * Petersburg, Saint Petersburg, Russia", cut between characters to fit.
 * Returns its length; 0 when the setting is not set. */
int32_t app_setting_place(const char *key, char *name, int32_t size);
