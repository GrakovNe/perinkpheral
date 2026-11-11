/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Keeps the end of the device's log in memory, next to sending it to the
 * console as before, so that it can be read without a cable. Call it
 * first of all: what was logged before it is not kept. */
void log_capture_start(void);

#define LOG_CAPTURE_BYTES 4096

/* The log kept so far, oldest first, terminated. text_size of
 * LOG_CAPTURE_BYTES + 1 takes all of it. Returns its length. */
size_t log_capture_read(char *text, size_t text_size);

#ifdef __cplusplus
}
#endif
