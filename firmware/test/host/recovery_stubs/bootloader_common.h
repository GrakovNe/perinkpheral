// SPDX-License-Identifier: MIT
#pragma once
#include "esp_ota_ops.h"
enum { ESP_IMAGE_APPLICATION = 1 };
esp_err_t bootloader_common_check_chip_validity(const esp_image_header_t *header, int kind);
