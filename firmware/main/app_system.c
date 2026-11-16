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
#include "app_kv.h"
#include "app_scene.h"
#include "host.h"
#include "events.h"
#include "host_api.h"
#include "launcher.h"
#include "net_time.h"
#include "power.h"
#include "settings.h"
#include "status.h"
#include "storage.h"
#include "sysinfo.h"
#include "wifi.h"
#include "wifi_qr.h"
#include "units.h"

#define BYTES_PER_KB 1024

_Static_assert(SYS_REFRESH_FULL_NEVER == APPCONF_REFRESH_FULL_NEVER,
               "an application says \"never\" with the number the settings keep");
_Static_assert(APPCONF_VALUE_MAX > SYS_SETTING_MAX, "every value an application may set fits");

static const char *TAG = "system";

int32_t app_system_info(int what, char *text, int32_t capacity)
{
    sysinfo_t     info;
    wifi_status_t wifi;
    const char   *known;
    char          number[INT32_TEXT_BYTES];
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
    case SYS_INFO_FREE_FLASH: {
        size_t used = 0, total = 0;
        storage_usage(&used, &total);
        snprintf(number, sizeof number, "%u",
                 (unsigned)((total - used + app_code_free_bytes()) / BYTES_PER_KB));
        known = number;
        break;
    }
    case SYS_INFO_FREE_STORE:
        snprintf(number, sizeof number, "%u", (unsigned)(app_kv_free_bytes() / BYTES_PER_KB));
        known = number;
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

/* ---- the host's settings, by key: the store's own rules, through
 * settings_set(), as the page writes them ---- */

int32_t app_system_setting_get(const char *key, char *value, int32_t capacity)
{
    const appconf_field_t *field = appconf_field(key, strlen(key));
    if (field == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    if ((field->flags & APPCONF_SECRET) != 0) {
        return HOST_ERR_ARGUMENT;
    }
    appconf_t settings;
    settings_read(&settings);
    char text[APPCONF_VALUE_MAX];
    appconf_format(&settings, field, text, sizeof text);
    size_t length = strlen(text);
    if (length > (size_t)capacity) {
        return HOST_ERR_NO_SPACE;
    }
    memcpy(value, text, length);
    return (int32_t)length;
}

int32_t app_system_setting_range(const char *key, int32_t *value, int32_t *least, int32_t *most)
{
    const appconf_field_t *field = appconf_field(key, strlen(key));
    if (field == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    if (field->kind == APPCONF_TEXT) {
        return HOST_ERR_ARGUMENT;
    }
    appconf_t settings;
    settings_read(&settings);
    *value = appconf_number(&settings, field);
    *least = field->least;
    *most = field->most;
    return HOST_OK;
}

/* An application runs in the host loop: the settings are changed here and
 * now, by their owner. */
static int32_t outcome(settings_outcome_t result)
{
    switch (result) {
    case SETTINGS_CHANGED:
        return HOST_OK;
    case SETTINGS_INVALID:
        return HOST_ERR_ARGUMENT;
    case SETTINGS_NOT_STORED:
        break;
    }
    return HOST_ERR_FAILED;
}

int32_t app_system_setting_set(const char *key, const char *value)
{
    char problem[APPCONF_PROBLEM_MAX];
    return outcome(settings_set(key, strlen(key), value, problem, sizeof problem, NULL));
}

static int32_t change(const appconf_t *wanted)
{
    char problem[APPCONF_PROBLEM_MAX];
    return outcome(settings_change(wanted, problem, sizeof problem, NULL));
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

int32_t app_system_app_title(uint32_t id, char *text, int32_t capacity)
{
    const app_manifest_t *app = launcher_find_id(id);
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
    const app_manifest_t *app = launcher_find_id(id);
    if (app == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    /* There is room for one application at a time, so one that is loaded
     * is the one asking: closing it would free the interpreter its handler
     * is running in. It is removed from another, or from the page. */
    if (app_scene_app(launcher_app_scene(app->name)) != NULL) {
        return HOST_ERR_ARGUMENT;
    }
    host_close_app(app->name);
    if (!launcher_remove(app->name, false)) {
        return HOST_ERR_FAILED;
    }
    ESP_LOGI(TAG, "\"%s\" removed", app->name);
    /* Not from inside the handler that asked: the list is read anew once
     * the application that asked has returned. */
    host_reload_apps_later();
    return HOST_OK;
}

uint32_t app_system_app_default(void)
{
    appconf_t settings;
    settings_read(&settings);
    const app_manifest_t *app = launcher_find(settings.default_app);
    return app != NULL ? app->id : 0;
}

int32_t app_system_app_set_default(uint32_t id)
{
    const app_manifest_t *app = id != 0 ? launcher_find_id(id) : NULL;
    if (id != 0 && app == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    return app_system_setting_set(SYS_KEY_DEFAULT_APP, app != NULL ? app->name : "");
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
