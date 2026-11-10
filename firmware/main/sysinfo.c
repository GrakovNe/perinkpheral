/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "sysinfo.h"

#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#define BYTES_PER_KB            1024
#define MICROSECONDS_PER_MINUTE (60LL * 1000 * 1000)

static const char *s_device_name;

void sysinfo_start(const char *device_name)
{
    s_device_name = device_name;
}

void sysinfo_read(sysinfo_t *info)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    info->device_name = s_device_name;
    info->firmware_version = esp_app_get_description()->version;
    info->idf_version = esp_get_idf_version();
    info->running_slot = running != NULL ? running->label : "?";
    info->free_memory_kb = esp_get_free_heap_size() / BYTES_PER_KB;
    info->uptime_minutes = (uint32_t)(esp_timer_get_time() / MICROSECONDS_PER_MINUTE);
}
