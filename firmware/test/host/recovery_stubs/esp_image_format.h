// SPDX-License-Identifier: MIT
#pragma once
#include "esp_ota_ops.h"
enum { ESP_IMAGE_VERIFY_SILENT = 1 };
typedef struct { uint32_t offset, size; } esp_partition_pos_t;
typedef struct { uint32_t image_len; } esp_image_metadata_t;
esp_err_t esp_image_verify(int mode, const esp_partition_pos_t *position, esp_image_metadata_t *metadata);
