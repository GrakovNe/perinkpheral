/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_kv.h"

#include <stdio.h>

#include "esp_log.h"
#include "nvs.h"

#include "app_manifest.h"
#include "host_api.h"

static const char *TAG = "app_kv";

#define NAMESPACE_PREFIX "a."

_Static_assert(sizeof NAMESPACE_PREFIX - 1 + APP_NAME_MAX <= NVS_NS_NAME_MAX_SIZE - 1,
               "an application's namespace has to fit NVS");
_Static_assert(HOST_KV_KEY_MAX <= NVS_KEY_NAME_MAX_SIZE - 1, "a key has to fit NVS");

static esp_err_t open_store(const char *app_name, nvs_open_mode_t mode, nvs_handle_t *store)
{
    char namespace[NVS_NS_NAME_MAX_SIZE];
    snprintf(namespace, sizeof namespace, NAMESPACE_PREFIX "%s", app_name);
    return nvs_open(namespace, mode, store);
}

int32_t app_kv_get(const char *app_name, const char *key, void *value, int32_t capacity)
{
    nvs_handle_t store;
    /* A namespace nobody wrote to yet does not exist. */
    if (open_store(app_name, NVS_READONLY, &store) != ESP_OK) {
        return HOST_ERR_NOT_FOUND;
    }
    size_t stored_length = 0;
    esp_err_t err = nvs_get_blob(store, key, NULL, &stored_length);
    int32_t result;
    if (err != ESP_OK) {
        result = HOST_ERR_NOT_FOUND;
    } else if (stored_length > (size_t)capacity) {
        result = HOST_ERR_NO_SPACE;
    } else if (nvs_get_blob(store, key, value, &stored_length) != ESP_OK) {
        result = HOST_ERR_NOT_FOUND;
    } else {
        result = (int32_t)stored_length;
    }
    nvs_close(store);
    return result;
}

int32_t app_kv_set(const char *app_name, const char *key, const void *value, int32_t value_length)
{
    nvs_handle_t store;
    esp_err_t err = open_store(app_name, NVS_READWRITE, &store);
    if (err == ESP_OK) {
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
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: cannot store \"%s\": %s", app_name, key, esp_err_to_name(err));
        return HOST_ERR_NO_SPACE;
    }
    return HOST_OK;
}
