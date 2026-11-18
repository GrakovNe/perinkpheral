// SPDX-License-Identifier: MIT
#pragma once
#include "esp_partition.h"

typedef unsigned esp_ota_handle_t;
typedef struct __attribute__((packed)) {
    uint8_t prefix[12];
    uint16_t chip_id;
    uint8_t suffix[10];
} esp_image_header_t;
typedef struct { uint32_t load_addr, data_len; } esp_image_segment_header_t;

const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t size, esp_ota_handle_t *handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *bytes, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
