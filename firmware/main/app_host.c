/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_host.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "app_kv.h"
#include "app_store.h"
#include "app_system.h"
#include "deadlines.h"
#include "host.h"
#include "launcher.h"
#include "storage.h"
#include "events.h"
#include "host_api.h"
#include "net_time.h"
#include "poller.h"
#include "settings.h"
#include "wifi.h"
#include "timers.h"

static const char *TAG = "app";

_Static_assert(HOST_TIMER_COUNT == TIMERS_COUNT, "an application gets every timer of the host");


static int64_t uptime_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* Drawn once a run. */
static uint32_t boot_id(void)
{
    static uint32_t id;
    while (id == 0) {
        id = esp_random();
    }
    return id;
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

/* Where the user said the server is decides where the application's
 * requests go: that setting is the user's to write, on the page, and the
 * application's only to read. */
static int32_t kv_set(const char *app_name, const char *key, const void *value, int32_t length)
{
    char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
    bool  is_the_users = manifest != NULL && app_manifest_is_server_setting(manifest, key);
    free(manifest);
    if (is_the_users) {
        ESP_LOGW(TAG, "%s: \"%s\" is the user's to set", app_name, key);
        return HOST_ERR_ARGUMENT;
    }
    return app_kv_set(app_name, key, value, length);
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

static void tell(int intent)
{
    if (!events_post_intent(intent)) {
        ESP_LOGW(TAG, "the queue is full: applications are not told (%d)", intent);
    }
}

void app_host_tell_time_changed(void)
{
    tell(HOST_INTENT_TIME_CHANGED);
}

void app_host_tell_network_is_up(void)
{
    tell(HOST_INTENT_NETWORK_UP);
}

void app_host_tell_started(void)
{
    tell(HOST_INTENT_STARTED);
}

#define MS_PER_SECOND 1000

/* The alarms that come again and again: how often each, and when it is
 * due next on the deadlines' own clock - so that a late call does not
 * make every one after it late. A restart forgets them with the alarms. */
#define RECURRING_MAX 8
typedef struct {
    uint32_t app_id; /* 0: free */
    int      alarm;
    int32_t  every_s;
    int64_t  due_ms;
} recurring_t;
static recurring_t s_recurring[RECURRING_MAX];

static recurring_t *recurring_of(uint32_t app_id, int alarm)
{
    for (int i = 0; i < RECURRING_MAX; i++) {
        if (s_recurring[i].app_id == app_id && s_recurring[i].alarm == alarm) {
            return &s_recurring[i];
        }
    }
    return NULL;
}

static void forget_recurrence(uint32_t app_id, int alarm)
{
    recurring_t *recurring = recurring_of(app_id, alarm);
    if (recurring != NULL) {
        recurring->app_id = 0;
    }
}

static uint32_t id_of(const char *app_name)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    for (int i = 0; i < count; i++) {
        if (strcmp(installed[i].name, app_name) == 0) {
            return installed[i].id;
        }
    }
    return 0;
}

static bool alarm_set_every(const char *app_name, int alarm, int32_t every_s, int32_t first_in_s)
{
    uint32_t app_id = id_of(app_name);
    if (app_id == 0) {
        return false;
    }
    recurring_t *recurring = recurring_of(app_id, alarm);
    if (recurring == NULL) {
        for (int i = 0; recurring == NULL && i < RECURRING_MAX; i++) {
            if (s_recurring[i].app_id == 0) {
                recurring = &s_recurring[i];
            }
        }
    }
    int64_t first_ms = (int64_t)first_in_s * MS_PER_SECOND;
    if (recurring == NULL || !deadlines_set_in(DEADLINE_ALARM, app_id, alarm, first_ms)) {
        return false;
    }
    *recurring = (recurring_t){ app_id, alarm, every_s, deadlines_now_ms() + first_ms };
    return true;
}

void app_host_alarm_rang(uint32_t app_id, int alarm)
{
    recurring_t *recurring = recurring_of(app_id, alarm);
    if (recurring == NULL) {
        return;
    }
    int64_t every_ms = (int64_t)recurring->every_s * MS_PER_SECOND;
    int64_t now_ms = deadlines_now_ms();
    int64_t next_ms = recurring->due_ms + every_ms;
    /* The clock was set meanwhile, or the device was away for longer than
     * a turn: the count starts from now. */
    if (next_ms <= now_ms || next_ms > now_ms + every_ms) {
        next_ms = now_ms + every_ms;
    }
    if (deadlines_set_in(DEADLINE_ALARM, app_id, alarm, next_ms - now_ms)) {
        recurring->due_ms = next_ms;
    } else {
        recurring->app_id = 0;
    }
}

void app_host_keep_recurring_of(const uint32_t *app_ids, int count)
{
    for (int i = 0; i < RECURRING_MAX; i++) {
        bool is_kept = false;
        for (int kept = 0; kept < count; kept++) {
            is_kept = is_kept || s_recurring[i].app_id == app_ids[kept];
        }
        if (!is_kept) {
            s_recurring[i].app_id = 0;
        }
    }
}

static void header_status(const char *app_name, int side, int icon, const char *text)
{
    uint32_t app_id = id_of(app_name);
    if (app_id != 0) {
        host_set_header_status(app_id, side, icon, text);
    }
}

/* The application's alarm: a deadline like any other, under the
 * application's id. `when` is seconds from now, 0 taking the alarm back,
 * or - `is_a_time` - a moment in unix seconds. */
static bool alarm_set(const char *app_name, int alarm, int64_t when, bool is_a_time)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    for (int i = 0; i < count; i++) {
        if (strcmp(installed[i].name, app_name) != 0) {
            continue;
        }
        uint32_t app_id = installed[i].id;
        forget_recurrence(app_id, alarm); /* set another way: once */
        if (is_a_time) {
            return deadlines_set_at(DEADLINE_ALARM, app_id, alarm, when * MS_PER_SECOND);
        }
        if (when == 0) {
            deadlines_cancel(DEADLINE_ALARM, app_id, alarm);
            return true;
        }
        return deadlines_set_in(DEADLINE_ALARM, app_id, alarm, when * MS_PER_SECOND);
    }
    return false;
}

static bool s_dithers = true;

void app_host_set_dithering(bool dithers)
{
    s_dithers = dithers;
}

static bool dithers(void)
{
    return s_dithers;
}

/* The language applications speak: the host's setting. */
static const char *language(void)
{
    static char code[APPCONF_LANGUAGE_MAX + 1];
    appconf_t   settings;
    settings_read(&settings);
    strlcpy(code, settings.language, sizeof code);
    return code;
}

const app_services_t *app_host_services(void)
{
    static const app_services_t SERVICES = {
        .unix_time = net_time_now,
        .time_zone_offset = net_time_zone_offset,
        .time_zone_offset_at = net_time_zone_offset_at,
        .uptime_ms = uptime_ms,
        .boot_id = boot_id,
        .log = log_from_app,
        .timer_set = timers_set,
        .kv_get = app_kv_get,
        .kv_set = kv_set,
        .dithers = dithers,
        .poll_now = poller_poll_now,
        .http_request = poller_request,
        .poll_set_interval = poller_set_interval,
        .alarm_set = alarm_set,
        .alarm_set_every = alarm_set_every,
        .header_status = header_status,
        .notify = host_notify,
        .language = language,
        .file_size = file_size,
        .file_read = file_read,
        .file_write = file_write,
        .system_info = app_system_info,
        .system_number_get = app_system_number_get,
        .system_number_set = app_system_number_set,
        .system_text_set = app_system_text_set,
        .system_time_set = app_system_time_set,
        .system_app_list = app_system_app_list,
        .system_app_title = app_system_app_title,
        .system_app_remove = app_system_app_remove,
        .system_app_default = app_system_app_default,
        .system_app_set_default = app_system_app_set_default,
        .system_do = app_system_do,
    };
    return &SERVICES;
}
