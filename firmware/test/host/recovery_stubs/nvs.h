// SPDX-License-Identifier: MIT
#pragma once
#include "esp_partition.h"

#define NVS_DEFAULT_PART_NAME "nvs"
typedef unsigned nvs_handle_t;
enum { NVS_READONLY, ESP_ERR_NVS_NOT_FOUND, ESP_ERR_NVS_INVALID_LENGTH };
esp_err_t nvs_open_from_partition(const char *partition, const char *name, int mode, nvs_handle_t *handle);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *value, size_t *length);
void nvs_close(nvs_handle_t handle);
