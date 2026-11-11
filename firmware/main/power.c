/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "power.h"

#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "wifi.h"

static const char *TAG = "power";

#define CPU_MHZ             160
#define MICROSECONDS_PER_MS 1000

static SemaphoreHandle_t  s_lock;
static esp_timer_handle_t s_awake_timer;
static bool               s_is_saving;
static int                s_radio_holds;
static bool               s_is_awake_for_user; /* the while after a press */

/* With the lock held: the radio is on unless the device is saving and
 * nobody needs it. */
static void apply_radio(void)
{
    wifi_set_radio(!s_is_saving || s_radio_holds > 0 || s_is_awake_for_user);
}

/* Lets the chip sleep whenever nothing runs, or not at all. */
static void apply_sleep(void)
{
    esp_pm_config_t config = {
        .max_freq_mhz = CPU_MHZ,
        .min_freq_mhz = CPU_MHZ,
        .light_sleep_enable = s_is_saving,
    };
    esp_err_t result = esp_pm_configure(&config);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "sleep not %s: %s", s_is_saving ? "allowed" : "forbidden",
                 esp_err_to_name(result));
    }
}

static void on_awake_timer(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_is_awake_for_user = false;
    apply_radio();
    xSemaphoreGive(s_lock);
}

/* With the lock held. */
static void stay_awake_for_user(void)
{
    s_is_awake_for_user = true;
    esp_timer_stop(s_awake_timer); /* not running is fine */
    esp_timer_start_once(s_awake_timer, (uint64_t)POWER_AWAKE_AFTER_PRESS_MS * MICROSECONDS_PER_MS);
}

void power_start(bool is_saving)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);
    esp_timer_create_args_t awake = { .callback = on_awake_timer, .name = "power-awake" };
    ESP_ERROR_CHECK(esp_timer_create(&awake, &s_awake_timer));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_is_saving = is_saving;
    /* Just powered up: whoever did it may want the page. */
    stay_awake_for_user();
    apply_radio();
    xSemaphoreGive(s_lock);
    apply_sleep();
    ESP_LOGI(TAG, "%s", is_saving ? "saving power" : "plugged");
}

void power_set_saving(bool is_saving)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = s_is_saving != is_saving;
    s_is_saving = is_saving;
    if (changed) {
        /* Whoever changed it is here. */
        stay_awake_for_user();
        apply_radio();
    }
    xSemaphoreGive(s_lock);
    if (changed) {
        apply_sleep();
        ESP_LOGI(TAG, "%s", is_saving ? "saving power" : "plugged");
    }
}

bool power_is_saving(void)
{
    return s_is_saving;
}

void power_hold_radio(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_radio_holds++;
    apply_radio();
    xSemaphoreGive(s_lock);
}

void power_release_radio(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_radio_holds > 0) {
        s_radio_holds--;
    }
    apply_radio();
    xSemaphoreGive(s_lock);
}

void power_note_press(void)
{
    if (s_lock == NULL || !s_is_saving) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    stay_awake_for_user();
    apply_radio();
    xSemaphoreGive(s_lock);
}
