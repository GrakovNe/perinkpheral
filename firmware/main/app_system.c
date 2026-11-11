/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_system.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "app_store.h"
#include "display.h"
#include "events.h"
#include "host_api.h"
#include "launcher.h"
#include "net_time.h"
#include "poller.h"
#include "power.h"
#include "reset.h"
#include "settings.h"
#include "status.h"
#include "storage.h"
#include "sysinfo.h"
#include "web_server.h"
#include "wifi.h"
#include "wifi_qr.h"

static const char *TAG = "system";

static appconf_t  *s_config;
static const char *s_device_id;

void app_system_use(appconf_t *config, const char device_id[5])
{
    s_config = config;
    s_device_id = device_id;
}

int32_t app_system_info(int what, char *text, int32_t capacity)
{
    sysinfo_t     info;
    wifi_status_t wifi;
    const char   *known;
    char          number[12];
    char          join_code[WIFI_QR_TEXT_MAX];
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
        known = wifi.phase == WIFI_PHASE_ONLINE ? wifi.address : WIFI_SETUP_ADDRESS;
        break;
    case SYS_INFO_WIFI:
        wifi_get_status(&wifi);
        known = wifi.phase == WIFI_PHASE_ONLINE       ? SYS_WIFI_ONLINE
                : wifi.phase == WIFI_PHASE_CONNECTING ? SYS_WIFI_CONNECTING
                                                      : SYS_WIFI_SETUP;
        break;
    case SYS_INFO_SLOT:
        sysinfo_read(&info);
        known = info.running_slot;
        break;
    case SYS_INFO_FREE_MEMORY:
        sysinfo_read(&info);
        snprintf(number, sizeof number, "%" PRIu32, info.free_memory_kb);
        known = number;
        break;
    case SYS_INFO_TIME_ZONE:
        known = s_config->tz;
        break;
    case SYS_INFO_JOIN_CODE:
        wifi_get_status(&wifi);
        if (!wifi_qr_text(wifi.access_point_name, join_code, sizeof join_code)) {
            return HOST_ERR_FAILED;
        }
        known = join_code;
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

int32_t app_system_number_get(int what, int32_t *value, int32_t *least, int32_t *most)
{
    switch (what) {
    case SYS_NUMBER_TIME_AUTOMATIC:
        *value = s_config->time_auto;
        *least = 0;
        *most = 1;
        return HOST_OK;
    case SYS_NUMBER_POWER_SAVING:
        *value = s_config->power_saving;
        *least = 0;
        *most = 1;
        return HOST_OK;
    case SYS_NUMBER_REFRESH_FULL_EVERY:
        *value = s_config->refresh_full_every;
        *least = APPCONF_REFRESH_FULL_EVERY_LEAST;
        *most = APPCONF_REFRESH_FULL_EVERY_MOST;
        return HOST_OK;
    case SYS_NUMBER_REFRESH_MIN_SECONDS:
        *value = s_config->refresh_min_data_s;
        *least = APPCONF_REFRESH_MIN_DATA_S_LEAST;
        *most = APPCONF_REFRESH_MIN_DATA_S_MOST;
        return HOST_OK;
    case SYS_NUMBER_REFRESH_FORCE_HOURS:
        *value = s_config->refresh_keepalive_h;
        *least = APPCONF_REFRESH_KEEPALIVE_H_LEAST;
        *most = APPCONF_REFRESH_KEEPALIVE_H_MOST;
        return HOST_OK;
    default:
        return HOST_ERR_ARGUMENT;
    }
}

/* The settings change only if all of `wanted` is sound and was stored;
 * then they are in effect at once. */
static int32_t apply(const appconf_t *wanted)
{
    char problem[64];
    if (!appconf_validate(wanted, problem, sizeof problem)) {
        return HOST_ERR_ARGUMENT;
    }
    if (settings_save(wanted) != ESP_OK) {
        return HOST_ERR_FAILED;
    }
    *s_config = *wanted;
    appconf_derive(s_config, s_device_id);
    net_time_set_zone(s_config->tz);
    net_time_set_automatic(s_config->time_auto != 0);
    power_set_saving(s_config->power_saving != 0);
    poller_reschedule();
    refresh_policy_cfg_t policy = settings_refresh_policy(s_config);
    display_set_policy(&policy);
    status_check(); /* the date in the header may have just changed */
    return HOST_OK;
}

int32_t app_system_number_set(int what, int32_t value)
{
    /* What does not fit the field would fit after being cut: refuse it here. */
    if (value < 0 || value > UINT16_MAX) {
        return HOST_ERR_ARGUMENT;
    }
    appconf_t wanted = *s_config;
    switch (what) {
    case SYS_NUMBER_TIME_AUTOMATIC:
        wanted.time_auto = (uint8_t)(value > UINT8_MAX ? UINT8_MAX : value);
        break;
    case SYS_NUMBER_POWER_SAVING:
        wanted.power_saving = (uint8_t)(value > UINT8_MAX ? UINT8_MAX : value);
        break;
    case SYS_NUMBER_REFRESH_FULL_EVERY:
        wanted.refresh_full_every = (uint8_t)(value > UINT8_MAX ? UINT8_MAX - 1 : value);
        break;
    case SYS_NUMBER_REFRESH_MIN_SECONDS:
        wanted.refresh_min_data_s = (uint16_t)value;
        break;
    case SYS_NUMBER_REFRESH_FORCE_HOURS:
        wanted.refresh_keepalive_h = (uint8_t)(value > UINT8_MAX ? UINT8_MAX : value);
        break;
    default:
        return HOST_ERR_ARGUMENT;
    }
    return apply(&wanted);
}

int32_t app_system_text_set(int what, const char *text)
{
    if (what != SYS_TEXT_TIME_ZONE || strlen(text) >= sizeof s_config->tz) {
        return HOST_ERR_ARGUMENT;
    }
    appconf_t wanted = *s_config;
    strcpy(wanted.tz, text);
    return apply(&wanted);
}

int32_t app_system_time_set(int64_t unix_seconds)
{
    if (!net_time_set(unix_seconds)) {
        return HOST_ERR_FAILED;
    }
    status_check();
    return HOST_OK;
}

int32_t app_system_app_title(int index, char *text, int32_t capacity)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    if (index < 0 || index >= count) {
        return HOST_ERR_NOT_FOUND;
    }
    size_t length = strlen(installed[index].title);
    if (length > (size_t)capacity) {
        return HOST_ERR_NO_SPACE;
    }
    memcpy(text, installed[index].title, length);
    return (int32_t)length;
}

int32_t app_system_app_remove(int index)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    if (index < 0 || index >= count) {
        return HOST_ERR_NOT_FOUND;
    }
    if (!app_store_remove(STORAGE_APPS_ROOT, installed[index].name)) {
        return HOST_ERR_FAILED;
    }
    ESP_LOGI(TAG, "\"%s\" removed", installed[index].name);
    events_post_apps_changed();
    return HOST_OK;
}

int32_t app_system_do(int action)
{
    switch (action) {
    case SYS_DO_RESTART:
        break;
    case SYS_DO_FORGET_NETWORK: {
        appconf_t wanted = *s_config;
        wanted.wifi_ssid[0] = '\0';
        wanted.wifi_password[0] = '\0';
        if (settings_save(&wanted) != ESP_OK) {
            return HOST_ERR_FAILED;
        }
        ESP_LOGW(TAG, "the network is forgotten, restarting");
        break;
    }
    case SYS_DO_FACTORY_RESET:
        ESP_LOGW(TAG, "factory reset asked for, restarting");
        reset_mark_factory();
        break;
    default:
        return HOST_ERR_ARGUMENT;
    }
    web_server_restart_soon();
    return HOST_OK;
}
