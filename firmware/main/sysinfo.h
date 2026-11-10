/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the device knows about itself, as data: no screen is involved. */
typedef struct {
    const char *device_name;
    const char *firmware_version;
    const char *idf_version;
    const char *running_slot;
    uint32_t    free_memory_kb;
    uint32_t    uptime_minutes;
} sysinfo_t;

/* device_name must outlive every sysinfo_read(). */
void sysinfo_start(const char *device_name);

void sysinfo_read(sysinfo_t *info);

#ifdef __cplusplus
}
#endif
