/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_host.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "app_kv.h"
#include "app_store.h"
#include "storage.h"
#include "sysinfo.h"
#include "host_api.h"
#include "net_time.h"
#include "poller.h"
#include "wifi.h"
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

static int32_t file_write(const char *app_name, const char *file, int32_t offset,
                          const void *bytes, int32_t length)
{
    switch (app_store_write_file(STORAGE_APPS_ROOT, app_name, file, offset, bytes,
                                 (size_t)length)) {
    case APP_WRITE_OK:
        return HOST_OK;
    case APP_WRITE_REFUSED:
        return HOST_ERR_ARGUMENT;
    case APP_WRITE_NO_SPACE:
        return HOST_ERR_NO_SPACE;
    case APP_WRITE_FAILED:
        break;
    }
    return HOST_ERR_FAILED;
}

/* What an application with the system API may learn about the device. */
static int32_t system_info(int what, char *text, int32_t capacity)
{
    sysinfo_t     info;
    wifi_status_t wifi;
    const char   *known;
    switch (what) {
    case SYS_INFO_VERSION:
        sysinfo_read(&info);
        known = info.firmware_version;
        break;
    case SYS_INFO_NAME:
        sysinfo_read(&info);
        known = info.device_name;
        break;
    case SYS_INFO_NETWORK:
        wifi_get_status(&wifi);
        known = wifi.ssid;
        break;
    case SYS_INFO_ADDRESS:
        wifi_get_status(&wifi);
        known = wifi.address;
        break;
    default:
        return HOST_ERR_ARGUMENT;
    }
    size_t length = strlen(known);
    if (length > (size_t)capacity) {
        return HOST_ERR_NO_SPACE;
    }
    memcpy(text, known, length);
    return (int32_t)length;
}

static const appconf_t *s_config;

void app_host_use(const appconf_t *config)
{
    s_config = config;
}

static bool dithers(void)
{
    return s_config == NULL || s_config->dither != 0;
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
        .dithers = dithers,
        .is_online = wifi_is_online,
        .poll_now = poller_poll_now,
        .http_request = poller_request,
        .poll_set_interval = poller_set_interval,
        .alarm_set = poller_alarm_set,
        .file_size = file_size,
        .file_read = file_read,
        .file_write = file_write,
        .system_info = system_info,
    };
    return &SERVICES;
}
