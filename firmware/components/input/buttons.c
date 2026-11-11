/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "buttons.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

/* It posts events and nothing else, but a log line from any task costs
 * about a kilobyte of its stack. */
#define BUTTONS_STACK_BYTES 3072

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

/* With every button up for a while there is nothing to sample: the task
 * waits for a level, which also wakes the chip from light sleep, and
 * samples again from there. */
#define QUIET_BEFORE_WAITING_MS 300

static SemaphoreHandle_t s_pressed;

static void set_press_interrupts(bool enabled)
{
    for (int i = 0; i < BUTTONS_COUNT; i++) {
        if (enabled) {
            gpio_intr_enable(s_gpio[i]);
        } else {
            gpio_intr_disable(s_gpio[i]);
        }
    }
}

/* A level, not an edge: it is there for as long as the button is down, so
 * it is turned off until the buttons are up again. */
static void IRAM_ATTR on_button_down(void *arg)
{
    (void)arg;
    set_press_interrupts(false);
    BaseType_t woke_higher = pdFALSE;
    xSemaphoreGiveFromISR(s_pressed, &woke_higher);
    portYIELD_FROM_ISR(woke_higher);
}

static void buttons_task(void *arg)
{
    (void)arg;
    btn_core_t core[BUTTONS_COUNT];
    for (int i = 0; i < BUTTONS_COUNT; i++) {
        btn_core_init(&core[i]);
    }
    uint32_t quiet_ms = 0;
    for (;;) {
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        bool     all_up = true;
        for (int i = 0; i < BUTTONS_COUNT; i++) {
            btn_event_t events[BTN_EVENTS_PER_SAMPLE];
            int level = gpio_get_level(s_gpio[i]);
            all_up = all_up && level != 0;
            int count = btn_core_feed(&core[i], level, now_ms, &s_cfg, events);
            for (int event = 0; event < count; event++) {
                s_sink((uint8_t)i, events[event]);
            }
        }
        quiet_ms = all_up ? quiet_ms + SAMPLE_MS : 0;
        if (quiet_ms >= QUIET_BEFORE_WAITING_MS) {
            xSemaphoreTake(s_pressed, 0); /* a press from before the quiet is done with */
            set_press_interrupts(true);
            xSemaphoreTake(s_pressed, portMAX_DELAY);
            quiet_ms = 0;
        } else {
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
        }
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
    s_pressed = xSemaphoreCreateBinary();
    configASSERT(s_pressed != NULL);
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    for (int i = 0; i < BUTTONS_COUNT; i++) {
        ESP_ERROR_CHECK(gpio_set_intr_type(s_gpio[i], GPIO_INTR_LOW_LEVEL));
        ESP_ERROR_CHECK(gpio_isr_handler_add(s_gpio[i], on_button_down, NULL));
        gpio_intr_disable(s_gpio[i]);
        /* Any button wakes the chip from light sleep. */
        ESP_ERROR_CHECK(gpio_wakeup_enable(s_gpio[i], GPIO_INTR_LOW_LEVEL));
    }
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    for (int i = 0; i < BUTTONS_COUNT; i++) {
        ESP_LOGI(TAG, "button %d on GPIO%d, idle level %d", i + 1, s_gpio[i],
                 gpio_get_level(s_gpio[i]));
    }

    BaseType_t ok = xTaskCreate(buttons_task, "buttons", BUTTONS_STACK_BYTES, NULL, 10, NULL);
    configASSERT(ok == pdPASS);
}
