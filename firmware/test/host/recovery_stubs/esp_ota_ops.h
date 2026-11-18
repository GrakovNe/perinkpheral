// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
typedef unsigned esp_ota_handle_t;
enum { ESP_OK = 0, ESP_FAIL = -1, ESP_PARTITION_TYPE_APP = 0,
       ESP_PARTITION_SUBTYPE_APP_FACTORY = 0, ESP_PARTITION_SUBTYPE_APP_OTA_0 = 16 };
typedef struct { unsigned subtype; size_t size; uint32_t address; } esp_partition_t;
typedef struct __attribute__((packed)) {
    uint8_t prefix[12];
    uint16_t chip_id;
    uint8_t suffix[10];
} esp_image_header_t;
typedef struct { uint32_t load_addr, data_len; } esp_image_segment_header_t;

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset, void *data, size_t length);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t size, esp_ota_handle_t *handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *bytes, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
