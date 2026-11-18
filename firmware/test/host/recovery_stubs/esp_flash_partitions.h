// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
typedef struct {
    uint32_t ota_seq;
    uint8_t seq_label[20];
    uint32_t ota_state, crc;
} esp_ota_select_entry_t;
