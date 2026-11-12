/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_kv.h"

#include <stdio.h>
#include <string.h>

#include <sys/stat.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "storage.h"

#include "app_manifest.h"
#include "host_api.h"

static const char *TAG = "app_kv";

#define PARTITION        "appkv"
#define NAMESPACE_PREFIX "a."
#define SYSTEM_PREFIX    NAMESPACE_PREFIX "sys."

_Static_assert(sizeof NAMESPACE_PREFIX - 1 + APP_NAME_MAX <= NVS_NS_NAME_MAX_SIZE - 1,
               "an application's namespace has to fit NVS");
_Static_assert(HOST_KV_KEY_MAX <= NVS_KEY_NAME_MAX_SIZE - 1, "a key has to fit NVS");

static int32_t storage_failed(const char *app_name, const char *key, esp_err_t err)
{
    ESP_LOGW(TAG, "%s: \"%s\": %s", app_name, key, esp_err_to_name(err));
    return HOST_ERR_FAILED;
}

static esp_err_t open_store(const char *app_name, nvs_open_mode_t mode, nvs_handle_t *store)
{
    char namespace[NVS_NS_NAME_MAX_SIZE];
    snprintf(namespace, sizeof namespace, NAMESPACE_PREFIX "%s", app_name);
    return nvs_open_from_partition(PARTITION, namespace, mode, store);
}

/* Everything of one namespace of one partition, gone. */
static void erase_namespace(const char *partition, const char *namespace)
{
    nvs_handle_t store;
    if (nvs_open_from_partition(partition, namespace, NVS_READWRITE, &store) != ESP_OK) {
        return;
    }
    if (nvs_erase_all(store) == ESP_OK) {
        nvs_commit(store);
    }
    nvs_close(store);
}

/* The namespace of the first value found in the partition for which
 * `is_wanted` says yes. */
static bool find_namespace(const char *partition, bool (*is_wanted)(const char *namespace),
                           char found[NVS_NS_NAME_MAX_SIZE])
{
    bool is_found = false;
    nvs_iterator_t entry = NULL;
    esp_err_t err = nvs_entry_find(partition, NULL, NVS_TYPE_BLOB, &entry);
    while (err == ESP_OK && !is_found) {
        nvs_entry_info_t info;
        nvs_entry_info(entry, &info);
        if (is_wanted(info.namespace_name)) {
            strlcpy(found, info.namespace_name, NVS_NS_NAME_MAX_SIZE);
            is_found = true;
        }
        err = nvs_entry_next(&entry);
    }
    nvs_release_iterator(entry);
    return is_found;
}

static bool is_an_applications(const char *namespace)
{
    return strncmp(namespace, NAMESPACE_PREFIX, sizeof NAMESPACE_PREFIX - 1) == 0;
}

/* A store left by an application that is not on the device any more. */
static bool is_left_behind(const char *namespace)
{
    if (!is_an_applications(namespace) ||
        strncmp(namespace, SYSTEM_PREFIX, sizeof SYSTEM_PREFIX - 1) == 0) {
        return false;
    }
    /* A storage that cannot be read says nothing about who is installed. */
    struct stat root;
    if (stat(STORAGE_APPS_ROOT, &root) != 0) {
        return false;
    }
    char folder[sizeof STORAGE_APPS_ROOT + NVS_NS_NAME_MAX_SIZE];
    snprintf(folder, sizeof folder, STORAGE_APPS_ROOT "/%s",
             namespace + sizeof NAMESPACE_PREFIX - 1);
    struct stat entry;
    return stat(folder, &entry) != 0;
}

/* The stores used to live among the host's settings: what is still there
 * is brought over, a value at a time, and removed. */
static void bring_over_from_the_settings(void)
{
    static uint8_t value[HOST_KV_VALUE_MAX];
    char namespace[NVS_NS_NAME_MAX_SIZE];
    while (find_namespace(NVS_DEFAULT_PART_NAME, is_an_applications, namespace)) {
        nvs_handle_t from, to;
        if (nvs_open(namespace, NVS_READONLY, &from) == ESP_OK) {
            bool is_open = nvs_open_from_partition(PARTITION, namespace, NVS_READWRITE, &to) == ESP_OK;
            nvs_iterator_t entry = NULL;
            esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, namespace, NVS_TYPE_BLOB, &entry);
            while (err == ESP_OK && is_open) {
                nvs_entry_info_t info;
                nvs_entry_info(entry, &info);
                size_t length = sizeof value;
                if (nvs_get_blob(from, info.key, value, &length) == ESP_OK) {
                    nvs_set_blob(to, info.key, value, length);
                }
                err = nvs_entry_next(&entry);
            }
            nvs_release_iterator(entry);
            if (is_open) {
                nvs_commit(to);
                nvs_close(to);
            }
            nvs_close(from);
        }
        ESP_LOGI(TAG, "%s: brought over from the settings", namespace);
        erase_namespace(NVS_DEFAULT_PART_NAME, namespace);
    }
}

void app_kv_start(void)
{
    esp_err_t err = nvs_flash_init_partition(PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "%s, erasing once", esp_err_to_name(err));
        nvs_flash_erase_partition(PARTITION);
        err = nvs_flash_init_partition(PARTITION);
    }
    if (err != ESP_OK) {
        /* The device runs without: every kv call answers that it failed. */
        ESP_LOGE(TAG, "the stores cannot be opened: %s", esp_err_to_name(err));
        return;
    }
    bring_over_from_the_settings();
}

void app_kv_erase_all(void)
{
    nvs_flash_erase_partition(PARTITION);
}

int32_t app_kv_get(const char *app_name, const char *key, void *value, int32_t capacity)
{
    nvs_handle_t store;
    esp_err_t err = open_store(app_name, NVS_READONLY, &store);
    if (err != ESP_OK) {
        /* A namespace nobody wrote to yet does not exist. Anything else
         * is the storage failing, and an application must not take that
         * for "nothing stored" and write a blank state over its own. */
        return err == ESP_ERR_NVS_NOT_FOUND ? HOST_ERR_NOT_FOUND : storage_failed(app_name, key, err);
    }
    size_t stored_length = 0;
    err = nvs_get_blob(store, key, NULL, &stored_length);
    int32_t result;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        result = HOST_ERR_NOT_FOUND;
    } else if (err != ESP_OK) {
        result = storage_failed(app_name, key, err);
    } else if (stored_length > (size_t)capacity) {
        result = HOST_ERR_NO_SPACE;
    } else {
        err = nvs_get_blob(store, key, value, &stored_length);
        result = err == ESP_OK ? (int32_t)stored_length : storage_failed(app_name, key, err);
    }
    nvs_close(store);
    return result;
}

/* What the application keeps now, in bytes of its values, the one under
 * `except_key` left out. */
static size_t stored_bytes(nvs_handle_t store, const char *app_name, const char *except_key)
{
    char namespace[NVS_NS_NAME_MAX_SIZE];
    snprintf(namespace, sizeof namespace, NAMESPACE_PREFIX "%s", app_name);
    size_t total = 0;
    nvs_iterator_t entry = NULL;
    esp_err_t err = nvs_entry_find(PARTITION, namespace, NVS_TYPE_BLOB, &entry);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(entry, &info);
        size_t length = 0;
        if (strcmp(info.key, except_key) != 0 &&
            nvs_get_blob(store, info.key, NULL, &length) == ESP_OK) {
            total += length;
        }
        err = nvs_entry_next(&entry);
    }
    nvs_release_iterator(entry);
    return total;
}

static bool is_full(esp_err_t err)
{
    return err == ESP_ERR_NVS_NOT_ENOUGH_SPACE || err == ESP_ERR_NVS_NO_FREE_PAGES;
}

/* HOST_OK, HOST_ERR_NO_SPACE for a value over what the application may
 * keep, or the storage's own error in *storage_error. */
static int32_t write_value(const char *app_name, const char *key, const void *value,
                           int32_t value_length, esp_err_t *storage_error)
{
    *storage_error = ESP_OK;
    nvs_handle_t store;
    esp_err_t err = open_store(app_name, NVS_READWRITE, &store);
    if (err != ESP_OK) {
        *storage_error = err;
        return HOST_ERR_FAILED;
    }
    if (value_length > 0 &&
        stored_bytes(store, app_name, key) + (size_t)value_length > HOST_KV_TOTAL_MAX) {
        nvs_close(store);
        ESP_LOGW(TAG, "%s: \"%s\" is over what an application may keep", app_name, key);
        return HOST_ERR_NO_SPACE;
    }
    err = value_length > 0 ? nvs_set_blob(store, key, value, (size_t)value_length)
                           : nvs_erase_key(store, key);
    if (err == ESP_ERR_NVS_NOT_FOUND && value_length == 0) {
        err = ESP_OK; /* removing what is not there is done already */
    }
    if (err == ESP_OK) {
        err = nvs_commit(store);
    }
    nvs_close(store);
    *storage_error = err;
    return err == ESP_OK ? HOST_OK : HOST_ERR_FAILED;
}

int32_t app_kv_set(const char *app_name, const char *key, const void *value, int32_t value_length)
{
    esp_err_t err;
    int32_t result = write_value(app_name, key, value, value_length, &err);
    /* Full: what the applications that were removed left behind goes
     * first, one at a time, for as long as that helps. */
    char left_behind[NVS_NS_NAME_MAX_SIZE];
    while (is_full(err) && find_namespace(PARTITION, is_left_behind, left_behind)) {
        ESP_LOGW(TAG, "full: forgetting what %s kept", left_behind + sizeof NAMESPACE_PREFIX - 1);
        erase_namespace(PARTITION, left_behind);
        result = write_value(app_name, key, value, value_length, &err);
    }
    if (is_full(err)) {
        ESP_LOGW(TAG, "%s: no room for \"%s\"", app_name, key);
        return HOST_ERR_NO_SPACE;
    }
    return err == ESP_OK ? result : storage_failed(app_name, key, err);
}

void app_kv_erase(const char *app_name)
{
    nvs_handle_t store;
    if (open_store(app_name, NVS_READWRITE, &store) != ESP_OK) {
        return;
    }
    esp_err_t err = nvs_erase_all(store);
    if (err == ESP_OK) {
        err = nvs_commit(store);
    }
    nvs_close(store);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: the store could not be emptied: %s", app_name, esp_err_to_name(err));
    }
}
