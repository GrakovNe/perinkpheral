/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_host.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "app_kv.h"
#include "app_store.h"
#include "storage.h"
#include "host_api.h"
#include "net_time.h"
#include "timers.h"

static const char *TAG = "app";

_Static_assert(HOST_TIMER_COUNT == TIMERS_COUNT, "an application gets every timer of the host");


static int64_t uptime_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void log_from_app(int level, const char *text, int length)
{
    switch (level) {
    case HOST_LOG_ERROR:
        ESP_LOGE(TAG, "%.*s", length, text);
        break;
    case HOST_LOG_WARN:
        ESP_LOGW(TAG, "%.*s", length, text);
        break;
    case HOST_LOG_DEBUG:
        ESP_LOGD(TAG, "%.*s", length, text);
        break;
    default:
        ESP_LOGI(TAG, "%.*s", length, text);
        break;
    }
}

static int32_t file_size(const char *app_name, const char *file)
{
    long size = app_store_file_size(STORAGE_APPS_ROOT, app_name, file);
    return size >= 0 ? (int32_t)size : HOST_ERR_NOT_FOUND;
}

static int32_t file_read(const char *app_name, const char *file, int32_t offset, void *bytes,
                         int32_t capacity)
{
    long count = app_store_read_file(STORAGE_APPS_ROOT, app_name, file, offset, bytes,
                                     (size_t)capacity);
    return count >= 0 ? (int32_t)count : HOST_ERR_NOT_FOUND;
}

const app_services_t *app_host_services(void)
{
    static const app_services_t SERVICES = {
        .unix_time = net_time_now,
        .time_zone_offset = net_time_zone_offset,
        .uptime_ms = uptime_ms,
        .log = log_from_app,
        .timer_set = timers_set,
        .kv_get = app_kv_get,
        .kv_set = app_kv_set,
        .file_size = file_size,
        .file_read = file_read,
    };
    return &SERVICES;
}
