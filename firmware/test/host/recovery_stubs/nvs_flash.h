// SPDX-License-Identifier: MIT
#pragma once
#include "esp_partition.h"

esp_err_t nvs_flash_init_partition_ptr(const esp_partition_t *partition);
