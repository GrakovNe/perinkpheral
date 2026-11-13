/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Keeps the end of the device's log in memory, next to sending it to the
 * console as before, so that it can be read without a cable. Call it
 * first of all: what was logged before it is not kept. */
void log_capture_start(void);

#define LOG_CAPTURE_BYTES 4096

/* The log kept so far, oldest first, terminated. text_size of
 * LOG_CAPTURE_BYTES + 1 takes all of it. Returns its length. `end`, when
 * not NULL, gets how many bytes were ever logged: the place this text
 * ends at, for whoever comes back for what was logged after it. */
size_t log_capture_read(char *text, size_t text_size, uint32_t *end);

#ifdef __cplusplus
}
#endif
