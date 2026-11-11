/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "buttons.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "buttons";

#define SAMPLE_MS 5

/* Active low; the module has its own pull-ups. */
static const int s_gpio[BUTTONS_COUNT] = {
    CONFIG_APP_BTN1_GPIO, CONFIG_APP_BTN2_GPIO,
    CONFIG_APP_BTN3_GPIO, CONFIG_APP_BTN4_GPIO,
};

static const btn_core_cfg_t s_cfg = {
    .debounce = CONFIG_APP_BTN_DEBOUNCE_MS / SAMPLE_MS,
    .long_ms = CONFIG_APP_BTN_LONG_MS,
};

static buttons_sink_fn s_sink;

static void buttons_task(void *arg)
{
    (void)arg;
    btn_core_t core[BUTTONS_COUNT];
    for (int i = 0; i < BUTTONS_COUNT; i++) {
        btn_core_init(&core[i]);
    }
    for (;;) {
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        for (int i = 0; i < BUTTONS_COUNT; i++) {
            btn_event_t events[BTN_EVENTS_PER_SAMPLE];
            int count =
                btn_core_feed(&core[i], gpio_get_level(s_gpio[i]), now_ms, &s_cfg, events);
            for (int event = 0; event < count; event++) {
                s_sink((uint8_t)i, events[event]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}

void buttons_start(buttons_sink_fn sink)
{
    s_sink = sink;
    uint64_t mask = 0;
    for (int i = 0; i < BUTTONS_COUNT; i++) {
        mask |= 1ULL << s_gpio[i];
    }
    gpio_config_t in = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));
    for (int i = 0; i < BUTTONS_COUNT; i++) {
        ESP_LOGI(TAG, "button %d on GPIO%d, idle level %d", i + 1, s_gpio[i],
                 gpio_get_level(s_gpio[i]));
    }

    BaseType_t ok = xTaskCreate(buttons_task, "buttons", 2048, NULL, 10, NULL);
    configASSERT(ok == pdPASS);
}
