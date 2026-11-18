/* SPDX-License-Identifier: MIT */
#include "app_update.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_install.h"
#include "app_abi.h"
#include "app_update_policy.h"
#include "app_update_schedule.h"
#include "catalog_stream.h"
#include "deadlines.h"
#include "events.h"
#include "host_api.h"
#include "launcher.h"
#include "poller.h"
#include "power.h"
#include "priorities.h"
#include "settings.h"
#include "wifi.h"

static const char *TAG = "app_update";
static bool s_running;
#define UPDATE_RETRIES 3
#define WIFI_WAIT_MS 30000
#define WIFI_STEP_MS 500

typedef struct {
    char name[APP_NAME_MAX + 1];
    char version[APP_VERSION_MAX + 1];
    uint32_t id;
    bool found;
} installed_app_t;

static bool enabled(void)
{
    appconf_t settings;
    settings_read(&settings);
    return settings.app_auto_update != 0;
}

static uint32_t now_seconds(void)
{
    time_t now = time(NULL);
    return now > 0 && (uint64_t)now <= UINT32_MAX ? (uint32_t)now : 0;
}

static bool take_catalog(void *context, const uint8_t *piece, size_t length)
{
    return catalog_stream_feed(context, (const char *)piece, length);
}

static bool retry_pause(int attempt)
{
    if (attempt >= UPDATE_RETRIES || !enabled()) return false;
    vTaskDelay(pdMS_TO_TICKS(2000u << attempt));
    return enabled();
}

static bool read_catalog(const char *source, const char *const *names, size_t count,
                          catalog_stream_entry_t *entries)
{
    char url[CATALOG_URL_MAX + 1];
    if (!catalog_url(source, NULL, CATALOG_INDEX_FILE, url, sizeof url)) return false;
    catalog_stream_t *stream = malloc(sizeof *stream);
    if (stream == NULL) return false;
    bool complete = false;
    for (int attempt = 0; ; attempt++) {
        int status;
        if (!catalog_stream_init(stream, names, count, entries)) break;
        bool received = poller_fetch(url, CATALOG_STREAM_MAX_BYTES, take_catalog, stream, &status);
        complete = received && catalog_stream_finish(stream);
        if (complete || !app_install_network_is_retryable(status) || !retry_pause(attempt)) break;
    }
    free(stream);
    return complete;
}

static json_t *entry_files(const catalog_stream_entry_t *entry)
{
    static const char HEX[] = "0123456789abcdef";
    json_t *files = json_new_array();
    if (files == NULL) return NULL;
    for (size_t i = 0; i < entry->file_count; i++) {
        const catalog_stream_file_t *file = &entry->files[i];
        char hash[65];
        for (size_t byte = 0; byte < sizeof file->sha256; byte++) {
            hash[byte * 2] = HEX[file->sha256[byte] >> 4];
            hash[byte * 2 + 1] = HEX[file->sha256[byte] & 15];
        }
        hash[64] = '\0';
        json_t *item = json_new_object();
        if (item == NULL) { json_free(files); return NULL; }
        if (json_add_string(item, "name", file->name) == NULL ||
            json_add_number(item, "size", file->size) == NULL ||
            json_add_string(item, "sha256", hash) == NULL) {
            json_free(item);
            json_free(files);
            return NULL;
        }
        json_array_add(files, item);
    }
    return files;
}

static void update_one(const char *source, int source_index, const installed_app_t *installed,
                        const catalog_stream_entry_t *entry)
{
    if (!entry->valid || !app_abi_supported(entry->abi) ||
        !catalog_author_is_allowed(entry->author, source_index) ||
        !app_update_version_is_newer(installed->version, entry->version)) return;
    uint32_t required, current;
    if (entry->firmware[0] != '\0' &&
        (!app_version_number(entry->firmware, &required) ||
         !app_version_number(esp_app_get_description()->version, &current) || required > current)) return;
    json_t *files = entry_files(entry);
    if (files == NULL) {
        ESP_LOGW(TAG, "%s: not enough memory", installed->name);
        return;
    }
    bool network_retryable;
    app_install_options_t options = { .installed_id = installed->id, .version = entry->version,
                                      .retryable = &network_retryable };
    const char *problem;
    for (int attempt = 0; ; attempt++) {
        if (!enabled()) { json_free(files); return; }
        problem = app_install_run(source, source_index, installed->name, files, &options);
        bool retryable = problem != NULL && (network_retryable ||
            strcmp(problem, "Not enough memory.") == 0 || strcmp(problem, "Device busy.") == 0);
        if (!retryable || !retry_pause(attempt)) break;
    }
    json_free(files);
    if (problem == NULL) ESP_LOGI(TAG, "%s updated to %s", installed->name, entry->version);
    else ESP_LOGW(TAG, "%s: %s", installed->name, problem);
}

static void check_catalogs(void)
{
    app_manifest_t *snapshot = malloc(LAUNCHER_INSTALLED_MAX * sizeof *snapshot);
    if (snapshot == NULL) return;
    int count = launcher_read_installed(snapshot, LAUNCHER_INSTALLED_MAX);
    installed_app_t installed[LAUNCHER_INSTALLED_MAX] = { 0 };
    const char *names[LAUNCHER_INSTALLED_MAX];
    for (int i = 0; i < count; i++) {
        strcpy(installed[i].name, snapshot[i].name);
        strcpy(installed[i].version, snapshot[i].version);
        installed[i].id = snapshot[i].id;
        names[i] = installed[i].name;
    }
    free(snapshot);
    if (count == 0) return;
    catalog_stream_entry_t *entries = calloc((size_t)count, sizeof *entries);
    if (entries == NULL) return;
    char sources[CATALOG_SOURCES_TEXT_MAX], source[CATALOG_SOURCE_URL_MAX + 1];
    settings_load_sources(sources, sizeof sources);
    for (int source_index = 0; enabled() && catalog_source_at(sources, source_index, source, sizeof source); source_index++) {
        if (!read_catalog(source, names, (size_t)count, entries)) {
            /* An unavailable earlier source may own a name offered by a
             * later source. Do not silently change that application's origin. */
            ESP_LOGW(TAG, "source %d unavailable; keeping source precedence", source_index);
            break;
        }
        for (int i = 0; i < count && enabled(); i++) {
            if (!installed[i].found && entries[i].found) {
                installed[i].found = true;
                update_one(source, source_index, &installed[i], &entries[i]);
            }
        }
    }
    free(entries);
}

static void completed(void *unused)
{
    (void)unused;
    s_running = false;
    if (enabled()) deadlines_set_in(DEADLINE_APP_UPDATE, 0, 0, APP_UPDATE_DAY_MS);
}

static void update_task(void *unused)
{
    (void)unused;
    power_hold_radio();
    for (int waited = 0; !wifi_is_online() && enabled() && waited < WIFI_WAIT_MS; waited += WIFI_STEP_MS)
        vTaskDelay(pdMS_TO_TICKS(WIFI_STEP_MS));
    if (enabled()) {
        uint32_t now = now_seconds();
        /* 1 records an attempt made before the wall clock was available. */
        if (!settings_save_app_update_time(now >= APP_UPDATE_CLOCK_FIRST ? now : 1))
            ESP_LOGW(TAG, "could not persist update check time");
        if (wifi_is_online()) check_catalogs();
        else ESP_LOGI(TAG, "offline; next check in 24 hours");
    }
    power_release_radio();
    events_call_patiently(completed, NULL);
    vTaskDelete(NULL);
}

void app_update_is_due(void)
{
    if (!enabled() || s_running) return;
    s_running = true;
    if (xTaskCreate(update_task, "app_update", STACK_APP_UPDATE, NULL, PRIORITY_BACKGROUND, NULL) != pdPASS) {
        s_running = false;
        deadlines_set_in(DEADLINE_APP_UPDATE, 0, 0, 60 * 1000);
    }
}

static void setting_changed(const appconf_t *before, const appconf_t *now)
{
    if (before->app_auto_update == now->app_auto_update) return;
    if (!now->app_auto_update) deadlines_cancel(DEADLINE_APP_UPDATE, 0, 0);
    else if (!s_running) deadlines_set_in(DEADLINE_APP_UPDATE, 0, 0, APP_UPDATE_FIRST_MS);
}

void app_update_start(void)
{
    settings_watch(setting_changed);
    if (enabled()) deadlines_set_in(DEADLINE_APP_UPDATE, 0, 0,
        app_update_wait_ms(now_seconds(), settings_load_app_update_time()));
}
