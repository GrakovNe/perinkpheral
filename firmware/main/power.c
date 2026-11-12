/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "power.h"

#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "deadlines.h"
#include "wifi.h"

static const char *TAG = "power";

#define CPU_MHZ             160

static SemaphoreHandle_t  s_lock;
static bool               s_is_saving;
static bool               s_sleeps_deeply; /* saving, and asleep for good between the polls */
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

void power_awake_is_over(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_is_awake_for_user = false;
    apply_radio();
    xSemaphoreGive(s_lock);
}

/* With the lock held. */
static void stay_awake_for_user(void)
{
    s_is_awake_for_user = true;
    deadlines_set_in(DEADLINE_AWAKE_WINDOW, 0, 0, POWER_AWAKE_AFTER_PRESS_MS);
}

void power_start(bool is_saving, bool sleeps_deeply, bool is_user_here)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_is_saving = is_saving;
    s_sleeps_deeply = is_saving && sleeps_deeply;
    /* Powered up, or woken by a button: whoever did it may want the page.
     * Woken by its own clock for a poll, nobody is there. */
    if (is_user_here) {
        stay_awake_for_user();
    }
    apply_radio();
    xSemaphoreGive(s_lock);
    apply_sleep();
    ESP_LOGI(TAG, "%s", is_saving ? "saving power" : "plugged");
}

void power_set_saving(bool is_saving, bool sleeps_deeply)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = s_is_saving != is_saving || s_sleeps_deeply != (is_saving && sleeps_deeply);
    s_is_saving = is_saving;
    s_sleeps_deeply = is_saving && sleeps_deeply;
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

bool power_may_sleep_deeply(void)
{
    if (s_lock == NULL) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool may = s_sleeps_deeply && s_radio_holds == 0 && !s_is_awake_for_user;
    xSemaphoreGive(s_lock);
    return may;
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
