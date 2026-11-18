// SPDX-License-Identifier: MIT
#pragma once
#include <assert.h>
#include "esp_partition.h"

#define ESP_ERROR_CHECK(call) assert((call) == ESP_OK)
enum { ESP_MAC_WIFI_STA };
esp_err_t esp_read_mac(uint8_t *mac, int interface);
