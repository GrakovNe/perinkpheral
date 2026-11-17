/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "catalog.h"

static const char *TAG = "settings";

#define CFG_NS "cfg"

/* The host's own records beside the settings; the settings' keys are in
 * the table of appconf_fields(). */
#define KEY_LAST_APP   "last_app"
#define KEY_SOURCES    "sources"
#define KEY_MENU_ORDER "menu_order"

void settings_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs: %s, erasing once", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* nvs_get_str() fills the buffer before it checks the data CRC, so a
 * corrupt entry read straight into *out would wipe the default. */
static void get_str(nvs_handle_t h, const char *key, char *out, size_t out_sz)
{
    char *tmp = malloc(out_sz);
    if (tmp == NULL) {
        ESP_LOGW(TAG, "key %s: no memory, using default", key);
        return;
    }
    size_t sz = out_sz;
    esp_err_t err = nvs_get_str(h, key, tmp, &sz);
    if (err == ESP_OK && sz > 0) {
        memcpy(out, tmp, sz);
        out[sz - 1] = '\0';
    } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "key %s: %s, using default", key, esp_err_to_name(err));
    }
    free(tmp);
}

/* A number stored by a firmware that allowed more - another typeface,
 * another way of saving power - is not a reason to throw every setting
 * away, the network's among them: that one setting is its default. */
static void get_number(nvs_handle_t h, const appconf_field_t *field, appconf_t *c)
{
    int32_t   value;
    esp_err_t err;
    if (field->size == sizeof(uint16_t)) {
        uint16_t stored = 0;
        err = nvs_get_u16(h, field->stored, &stored);
        value = stored;
    } else {
        uint8_t stored = 0;
        err = nvs_get_u8(h, field->stored, &stored);
        value = stored;
    }
    if (err == ESP_OK && !appconf_put_number(c, field, value)) {
        ESP_LOGW(TAG, "%s: %ld is out of range, using default", field->key, (long)value);
    }
}

static void load(appconf_t *c, const char device_id[APPCONF_DEVICE_ID_SIZE])
{
    appconf_defaults(c, device_id);

    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no saved settings, using factory defaults");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s, using factory defaults", esp_err_to_name(err));
        return;
    }
    /* A key that is not there leaves the default in place. */
    int count;
    const appconf_field_t *fields = appconf_fields(&count);
    for (int i = 0; i < count; i++) {
        if (fields[i].kind == APPCONF_TEXT) {
            get_str(h, fields[i].stored, (char *)c + fields[i].offset, fields[i].size);
        } else {
            get_number(h, &fields[i], c);
        }
    }
    nvs_close(h);

    appconf_derive(c, device_id);
    char why[APPCONF_PROBLEM_MAX];
    if (!appconf_validate(c, why, sizeof why)) {
        ESP_LOGW(TAG, "stored settings invalid (%s), using factory defaults", why);
        appconf_defaults(c, device_id);
    }
}

static esp_err_t save(const appconf_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    int count;
    const appconf_field_t *fields = appconf_fields(&count);
    for (int i = 0; i < count && err == ESP_OK; i++) {
        const appconf_field_t *field = &fields[i];
        if (field->kind == APPCONF_TEXT) {
            err = nvs_set_str(h, field->stored, appconf_text(c, field));
        } else if (field->size == sizeof(uint16_t)) {
            err = nvs_set_u16(h, field->stored, (uint16_t)appconf_number(c, field));
        } else {
            err = nvs_set_u8(h, field->stored, (uint8_t)appconf_number(c, field));
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    return err;
}

void settings_load_last_app(char *name, size_t name_size)
{
    name[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(CFG_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    get_str(h, KEY_LAST_APP, name, name_size);
    nvs_close(h);
}

void settings_save_last_app(const char *name)
{
    char stored[APPCONF_APP_NAME_MAX + 1];
    settings_load_last_app(stored, sizeof stored);
    if (strcmp(stored, name) == 0) {
        return; /* nothing to wear the flash for */
    }
    nvs_handle_t h;
    if (nvs_open(CFG_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_str(h, KEY_LAST_APP, name) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

void settings_load_sources(char *sources, size_t sources_size)
{
    /* The official source first, whatever is stored; then what was added. */
    int used = snprintf(sources, sources_size, "%s\n", CATALOG_OFFICIAL_SOURCE);
    nvs_handle_t h;
    if (used < 0 || (size_t)used >= sources_size || nvs_open(CFG_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t length = sources_size - (size_t)used;
    /* An older firmware stored the official source with the rest. */
    static const char STORED_OFFICIAL[] = CATALOG_OFFICIAL_SOURCE "\n";
    char *added = sources + used;
    if (nvs_get_str(h, KEY_SOURCES, added, &length) != ESP_OK ||
        !catalog_sources_are_valid(sources)) {
        added[0] = '\0';
    } else if (strncmp(added, STORED_OFFICIAL, sizeof STORED_OFFICIAL - 1) == 0) {
        memmove(added, added + sizeof STORED_OFFICIAL - 1,
                strlen(added + sizeof STORED_OFFICIAL - 1) + 1);
    }
    nvs_close(h);
}

esp_err_t settings_save_sources(const char *sources)
{
    if (!catalog_sources_are_valid(sources) ||
        catalog_source_count(sources) > CATALOG_ADDED_SOURCES_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, KEY_SOURCES, sources);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

void settings_load_menu_order(char *order, size_t order_size)
{
    order[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(CFG_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t length = order_size;
    if (nvs_get_str(h, KEY_MENU_ORDER, order, &length) != ESP_OK) {
        order[0] = '\0';
    }
    nvs_close(h);
}

esp_err_t settings_save_menu_order(const char *order)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, KEY_MENU_ORDER, order);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* ---- what the host keeps for each installed application ----
 * A record a name, in a namespace of its own: the name is the key. */
#define APPS_NS "apps"

void settings_load_app_record(const char *app_name, settings_app_record_t *record)
{
    memset(record, 0, sizeof *record);
    nvs_handle_t h;
    if (nvs_open(APPS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t size = sizeof *record;
    if (nvs_get_blob(h, app_name, record, &size) != ESP_OK || size != sizeof *record) {
        memset(record, 0, sizeof *record);
    }
    nvs_close(h);
}

void settings_save_app_record(const char *app_name, const settings_app_record_t *record)
{
    settings_app_record_t stored;
    settings_load_app_record(app_name, &stored);
    if (memcmp(&stored, record, sizeof stored) == 0) {
        return; /* nothing to wear the flash for */
    }
    nvs_handle_t h;
    if (nvs_open(APPS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_blob(h, app_name, record, sizeof *record) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

void settings_forget_app_record(const char *app_name)
{
    nvs_handle_t h;
    if (nvs_open(APPS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_erase_key(h, app_name) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

/* ---- the settings in effect ---- */

static appconf_t         s_live;
static volatile uint32_t s_version; /* odd while s_live is being rewritten */
static char              s_device_id[APPCONF_DEVICE_ID_SIZE];

/* The compiler keeps the copy between the two looks at the version: one
 * core, so nothing else would move it. */
#define FENCE() __atomic_signal_fence(__ATOMIC_SEQ_CST)

static void publish(const appconf_t *settings)
{
    s_version++;
    FENCE();
    s_live = *settings;
    appconf_derive(&s_live, s_device_id);
    FENCE();
    s_version++;
}

void settings_start(const char device_id[APPCONF_DEVICE_ID_SIZE])
{
    memcpy(s_device_id, device_id, sizeof s_device_id);
    appconf_t stored;
    load(&stored, device_id);
    publish(&stored);
}

void settings_read(appconf_t *copy)
{
    uint32_t version;
    for (;;) {
        version = s_version;
        if ((version & 1) != 0) {
            /* Caught the owner in the middle of a change. It is the host
             * loop: a reader above it that spun here would keep it from ever
             * finishing, so a reader waits a tick whatever its tier. */
            vTaskDelay(1);
            continue;
        }
        FENCE();
        *copy = s_live;
        FENCE();
        if (version == s_version) {
            return;
        }
    }
}

/* Who follows the settings: told after every change, in the order they
 * asked. The store knows them by nothing else. */
#define WATCHERS_MAX 8
static settings_watch_fn s_watchers[WATCHERS_MAX];
static int               s_watcher_count;

void settings_watch(settings_watch_fn watcher)
{
    configASSERT(s_watcher_count < WATCHERS_MAX);
    s_watchers[s_watcher_count++] = watcher;
}

settings_outcome_t settings_change(const appconf_t *wanted, char *problem, size_t problem_size,
                                   bool *needs_restart)
{
    if (!appconf_validate(wanted, problem, problem_size)) {
        return SETTINGS_INVALID;
    }
    if (save(wanted) != ESP_OK) {
        return SETTINGS_NOT_STORED;
    }
    appconf_t before = s_live;
    publish(wanted);
    if (needs_restart != NULL) {
        *needs_restart = appconf_needs_restart(&before, &s_live);
    }
    for (int i = 0; i < s_watcher_count; i++) {
        s_watchers[i](&before, &s_live);
    }
    return SETTINGS_CHANGED;
}

settings_outcome_t settings_set(const char *key, size_t key_length, const char *value,
                                char *problem, size_t problem_size, bool *needs_restart)
{
    const appconf_field_t *field = appconf_field(key, key_length);
    appconf_t wanted;
    settings_read(&wanted);
    if (field == NULL || (field->flags & APPCONF_OWN_WAY) != 0 ||
        !appconf_put(&wanted, field, value)) {
        snprintf(problem, problem_size, "Invalid value.");
        return SETTINGS_INVALID;
    }
    return settings_change(&wanted, problem, problem_size, needs_restart);
}
