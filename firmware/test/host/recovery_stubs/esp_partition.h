// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
enum { ESP_OK = 0, ESP_FAIL = -1, ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1,
       ESP_PARTITION_SUBTYPE_APP_FACTORY = 0, ESP_PARTITION_SUBTYPE_APP_OTA_0 = 16,
       ESP_PARTITION_SUBTYPE_DATA_NVS = 2 };
typedef struct {
    unsigned subtype;
    size_t size;
    uint32_t address;
    char label[17];
    bool readonly;
} esp_partition_t;

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset, void *data, size_t length);
