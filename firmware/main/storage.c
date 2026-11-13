/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "storage.h"

#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "storage";

#define APPS_PARTITION "apps"

static bool s_is_mounted;

bool storage_is_mounted(void)
{
    return s_is_mounted;
}

bool storage_format(void)
{
    esp_err_t err = esp_littlefs_format(APPS_PARTITION);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot format %s: %s", APPS_PARTITION, esp_err_to_name(err));
    }
    return err == ESP_OK;
}

bool storage_start(void)
{
    esp_vfs_littlefs_conf_t config = {
        .base_path = STORAGE_APPS_ROOT,
        .partition_label = APPS_PARTITION,
        /* What is there may be all the user has of an application's files:
         * erasing it is theirs to decide. */
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot mount %s: %s", APPS_PARTITION, esp_err_to_name(err));
        return false;
    }
    s_is_mounted = true;
    size_t total_bytes = 0, used_bytes = 0;
    esp_littlefs_info(APPS_PARTITION, &total_bytes, &used_bytes);
    ESP_LOGI(TAG, "%s mounted, %u of %u KB used", STORAGE_APPS_ROOT, (unsigned)(used_bytes / 1024),
             (unsigned)(total_bytes / 1024));
    return true;
}

void storage_usage(size_t *used_bytes, size_t *total_bytes)
{
    *used_bytes = 0;
    *total_bytes = 0;
    if (s_is_mounted) {
        esp_littlefs_info(APPS_PARTITION, total_bytes, used_bytes);
    }
}
