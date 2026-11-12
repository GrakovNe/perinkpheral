/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

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

/* Leaves an empty file system in the partition, to be mounted at the
 * next start. */
bool storage_format(void);

#ifdef __cplusplus
}
#endif
