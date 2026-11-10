/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "board";

void board_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_RF_SWITCH) | (1ULL << BOARD_GPIO_RF_SELECT),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(BOARD_GPIO_RF_SWITCH, 0);
#if CONFIG_APP_ANTENNA_EXTERNAL
    gpio_set_level(BOARD_GPIO_RF_SELECT, 1);
    ESP_LOGI(TAG, "antenna: external (U.FL)");
#else
    gpio_set_level(BOARD_GPIO_RF_SELECT, 0);
    ESP_LOGI(TAG, "antenna: ceramic");
#endif
}
