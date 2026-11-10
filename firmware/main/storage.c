/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "storage.h"

#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "storage";

#define APPS_PARTITION "apps"

bool storage_start(void)
{
    esp_vfs_littlefs_conf_t config = {
        .base_path = STORAGE_APPS_ROOT,
        .partition_label = APPS_PARTITION,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_littlefs_register(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot mount %s: %s", APPS_PARTITION, esp_err_to_name(err));
        return false;
    }
    size_t total_bytes = 0, used_bytes = 0;
    esp_littlefs_info(APPS_PARTITION, &total_bytes, &used_bytes);
    ESP_LOGI(TAG, "%s mounted, %u of %u KB used", STORAGE_APPS_ROOT, (unsigned)(used_bytes / 1024),
             (unsigned)(total_bytes / 1024));
    return true;
}
