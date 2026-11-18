/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef int esp_partition_mmap_handle_t;
typedef struct { size_t size; } esp_partition_t;
enum { ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_MMAP_DATA = 0 };
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
esp_err_t esp_partition_mmap(const esp_partition_t *partition, size_t offset, size_t size,
                             int memory, const void **mapped, esp_partition_mmap_handle_t *handle);
esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size);
esp_err_t esp_partition_write(const esp_partition_t *partition, size_t offset, const void *bytes,
                              size_t size);
