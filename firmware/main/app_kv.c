/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_kv.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "app_manifest.h"
#include "host_api.h"

static const char *TAG = "app_kv";

#define NAMESPACE_PREFIX "a."

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
    return nvs_open(namespace, mode, store);
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
    esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, namespace, NVS_TYPE_BLOB, &entry);
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

int32_t app_kv_set(const char *app_name, const char *key, const void *value, int32_t value_length)
{
    nvs_handle_t store;
    esp_err_t err = open_store(app_name, NVS_READWRITE, &store);
    if (err == ESP_OK) {
        /* The store is shared by every application there is and ever was:
         * one of them does not get to fill it. */
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
    }
    if (err == ESP_ERR_NVS_NOT_ENOUGH_SPACE || err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGW(TAG, "%s: no room for \"%s\"", app_name, key);
        return HOST_ERR_NO_SPACE;
    }
    return err == ESP_OK ? HOST_OK : storage_failed(app_name, key, err);
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
