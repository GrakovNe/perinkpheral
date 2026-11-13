/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Where the applications live once the file system is mounted. */
#define STORAGE_APPS_ROOT "/apps"

/* Mounts the "apps" partition (LittleFS) at STORAGE_APPS_ROOT. False when
 * it cannot be mounted: the host then runs with no applications rather
 * than not at all, and nothing is erased: the user is asked. */
bool storage_start(void);

bool storage_is_mounted(void);

/* How much of the applications' partition is taken, the file system's own
 * part included, and how large it is; zeroes while it is not mounted. */
void storage_usage(size_t *used_bytes, size_t *total_bytes);

/* Leaves an empty file system in the partition, to be mounted at the
 * next start. */
bool storage_format(void);

#ifdef __cplusplus
}
#endif
