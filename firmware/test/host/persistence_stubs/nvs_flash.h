/* SPDX-License-Identifier: MIT */
#pragma once
#include "esp_err.h"
esp_err_t nvs_flash_init_partition(const char *partition);
esp_err_t nvs_flash_erase_partition(const char *partition);
