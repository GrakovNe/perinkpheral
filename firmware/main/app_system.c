/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_system.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "app_code.h"
#include "app_store.h"
#include "display.h"
#include "host.h"
#include "events.h"
#include "host_api.h"
#include "launcher.h"
#include "net_time.h"
#include "poller.h"
#include "power.h"
#include "settings.h"
#include "status.h"
#include "storage.h"
#include "sysinfo.h"
#include "wifi.h"
#include "wifi_qr.h"

_Static_assert(SYS_REFRESH_FULL_NEVER == APPCONF_REFRESH_FULL_NEVER,
               "an application says \"never\" with the number the settings keep");

static const char *TAG = "system";

int32_t app_system_info(int what, char *text, int32_t capacity)
{
    sysinfo_t     info;
    wifi_status_t wifi;
    const char   *known;
    char          number[12];
    char          join_code[WIFI_QR_TEXT_MAX];
    appconf_t     settings;
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
        settings_read(&settings);
        known = settings.tz;
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
    appconf_t settings;
    settings_read(&settings);
    switch (what) {
    case SYS_NUMBER_TIME_AUTOMATIC:
        *value = settings.time_auto;
        *least = 0;
        *most = 1;
        return HOST_OK;
    case SYS_NUMBER_POWER_SAVING:
        *value = settings.power_saving;
        *least = APPCONF_POWER_PLUGGED;
        *most = APPCONF_POWER_EXTREME;
        return HOST_OK;
    case SYS_NUMBER_FONT:
        *value = settings.font;
        *least = 0;
        *most = APPCONF_FONT_COUNT - 1;
        return HOST_OK;
    case SYS_NUMBER_REFRESH_FAST:
        *value = settings.refresh_fast;
        *least = 0;
        *most = 1;
        return HOST_OK;
    case SYS_NUMBER_REFRESH_FULL_EVERY:
        *value = settings.refresh_full_every;
        *least = APPCONF_REFRESH_FULL_EVERY_LEAST;
        *most = APPCONF_REFRESH_FULL_EVERY_MOST;
        return HOST_OK;
    case SYS_NUMBER_REFRESH_MIN_SECONDS:
        *value = settings.refresh_min_data_s;
        *least = APPCONF_REFRESH_MIN_DATA_S_LEAST;
        *most = APPCONF_REFRESH_MIN_DATA_S_MOST;
        return HOST_OK;
    case SYS_NUMBER_REFRESH_FORCE_HOURS:
        *value = settings.refresh_keepalive_h;
        *least = APPCONF_REFRESH_KEEPALIVE_H_LEAST;
        *most = APPCONF_REFRESH_KEEPALIVE_H_MOST;
        return HOST_OK;
    default:
        return HOST_ERR_ARGUMENT;
    }
}

/* An application runs in the host loop: the settings are changed here and
 * now, by their owner. */
static int32_t change(const appconf_t *wanted)
{
    char problem[64];
    switch (settings_change(wanted, problem, sizeof problem, NULL)) {
    case SETTINGS_CHANGED:
        return HOST_OK;
    case SETTINGS_INVALID:
        return HOST_ERR_ARGUMENT;
    case SETTINGS_NOT_STORED:
        break;
    }
    return HOST_ERR_FAILED;
}

int32_t app_system_number_set(int what, int32_t value)
{
    /* What does not fit the field would fit after being cut: refuse it here. */
    if (value < 0 || value > (what == SYS_NUMBER_REFRESH_MIN_SECONDS ? UINT16_MAX : UINT8_MAX)) {
        return HOST_ERR_ARGUMENT;
    }
    appconf_t wanted;
    settings_read(&wanted);
    switch (what) {
    case SYS_NUMBER_TIME_AUTOMATIC:
        wanted.time_auto = (uint8_t)value;
        break;
    case SYS_NUMBER_POWER_SAVING:
        wanted.power_saving = (uint8_t)value;
        break;
    case SYS_NUMBER_FONT:
        wanted.font = (uint8_t)value;
        break;
    case SYS_NUMBER_REFRESH_FAST:
        wanted.refresh_fast = (uint8_t)value;
        break;
    case SYS_NUMBER_REFRESH_FULL_EVERY:
        wanted.refresh_full_every = (uint8_t)value;
        break;
    case SYS_NUMBER_REFRESH_MIN_SECONDS:
        wanted.refresh_min_data_s = (uint16_t)value;
        break;
    case SYS_NUMBER_REFRESH_FORCE_HOURS:
        wanted.refresh_keepalive_h = (uint8_t)value;
        break;
    default:
        return HOST_ERR_ARGUMENT;
    }
    return change(&wanted);
}

int32_t app_system_text_set(int what, const char *text)
{
    appconf_t wanted;
    settings_read(&wanted);
    if (what != SYS_TEXT_TIME_ZONE || strlen(text) >= sizeof wanted.tz) {
        return HOST_ERR_ARGUMENT;
    }
    strcpy(wanted.tz, text);
    return change(&wanted);
}

int32_t app_system_time_set(int64_t unix_seconds)
{
    if (!net_time_set(unix_seconds)) {
        return HOST_ERR_FAILED;
    }
    status_check();
    return HOST_OK;
}

int32_t app_system_app_list(uint32_t *ids, int32_t capacity)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    for (int i = 0; i < count && i < capacity; i++) {
        ids[i] = installed[i].id;
    }
    return count;
}

/* The installed application with the id; NULL when there is none. */
static const app_manifest_t *installed_with(uint32_t id)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    for (int i = 0; i < count; i++) {
        if (installed[i].id == id) {
            return &installed[i];
        }
    }
    return NULL;
}

int32_t app_system_app_title(uint32_t id, char *text, int32_t capacity)
{
    const app_manifest_t *app = installed_with(id);
    if (app == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    size_t length = strlen(app->title);
    if (length > (size_t)capacity) {
        return HOST_ERR_NO_SPACE;
    }
    memcpy(text, app->title, length);
    return (int32_t)length;
}

int32_t app_system_app_remove(uint32_t id)
{
    const app_manifest_t *app = installed_with(id);
    if (app == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    if (!launcher_remove(app->name, false)) {
        return HOST_ERR_FAILED;
    }
    ESP_LOGI(TAG, "\"%s\" removed", app->name);
    /* Not from inside the handler that asked: the list is read anew once
     * the application that asked has returned. */
    host_reload_apps_later();
    return HOST_OK;
}

int32_t app_system_do(int action)
{
    switch (action) {
    case SYS_DO_RESTART:
        break;
    case SYS_DO_FORGET_NETWORK: {
        appconf_t wanted;
        settings_read(&wanted);
        wanted.wifi_ssid[0] = '\0';
        wanted.wifi_password[0] = '\0';
        if (change(&wanted) != HOST_OK) {
            return HOST_ERR_FAILED;
        }
        ESP_LOGW(TAG, "the network is forgotten, restarting");
        break;
    }
    case SYS_DO_FACTORY_RESET:
        host_stop_soon(HOST_STOP_FACTORY_RESET);
        return HOST_OK;
    default:
        return HOST_ERR_ARGUMENT;
    }
    host_stop_soon(HOST_STOP_RESTART);
    return HOST_OK;
}
