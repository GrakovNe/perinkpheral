/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "display.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "epd.h"

_Static_assert(DISPLAY_WIDTH == EPD_W && DISPLAY_HEIGHT == EPD_H,
               "the display service is sized for this panel");

static const char *TAG = "display";

#define KEEPALIVE_CHECK_MS (60 * 1000)
#define WHITE_BYTE         0xFF

static uint8_t           s_shown[DISPLAY_FRAME_SIZE];
/* False after a reset and after a failed refresh: the panel holds
 * something else than s_shown, so the next frame goes out even when it
 * looks unchanged. */
static bool              s_shown_is_on_panel;
static refresh_policy_t  s_policy;
static SemaphoreHandle_t s_lock;

static const refresh_policy_cfg_t s_cfg = {
    .min_data_ms = CONFIG_APP_EPD_MIN_DATA_S * 1000u,
    .min_button_ms = CONFIG_APP_EPD_MIN_BUTTON_MS,
    .full_every = CONFIG_APP_EPD_FULL_EVERY_N,
    .keepalive_ms = CONFIG_APP_EPD_KEEPALIVE_H * 3600u * 1000u,
};

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* Asks the policy and drives the panel. REFRESH_NONE: the policy said
 * not now. Call with the lock held. */
static refresh_kind_t refresh_panel(const uint8_t *frame, refresh_reason_t why, esp_err_t *err)
{
    int64_t started_ms = now_ms();
    refresh_kind_t kind = refresh_policy_decide(&s_policy, &s_cfg, why, started_ms);
    if (kind == REFRESH_NONE) {
        return kind;
    }
    *err = kind == REFRESH_FULL ? epd_show_full(frame) : epd_show_partial(frame);
    int64_t finished_ms = now_ms();
    refresh_policy_note(&s_policy, kind, finished_ms);

    const char *kind_name = kind == REFRESH_FULL ? "full" : "partial";
    long long took_ms = (long long)(finished_ms - started_ms);
    if (*err == ESP_OK) {
        ESP_LOGI(TAG, "%s refresh, %lld ms", kind_name, took_ms);
    } else {
        ESP_LOGW(TAG, "%s refresh: %s after %lld ms", kind_name, esp_err_to_name(*err), took_ms);
    }
    return kind;
}

static void keepalive_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(KEEPALIVE_CHECK_MS));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        esp_err_t err = ESP_OK;
        if (refresh_panel(s_shown, REFRESH_FOR_KEEPALIVE, &err) != REFRESH_NONE) {
            s_shown_is_on_panel = err == ESP_OK;
        }
        xSemaphoreGive(s_lock);
    }
}

void display_start(void)
{
    memset(s_shown, WHITE_BYTE, sizeof s_shown);
    s_shown_is_on_panel = false;
    refresh_policy_init(&s_policy);
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);

    epd_config_t cfg = {
        .mosi_gpio = CONFIG_APP_EPD_MOSI_GPIO,
        .sclk_gpio = CONFIG_APP_EPD_SCLK_GPIO,
        .cs_gpio = CONFIG_APP_EPD_CS_GPIO,
        .dc_gpio = CONFIG_APP_EPD_DC_GPIO,
        .rst_gpio = CONFIG_APP_EPD_RST_GPIO,
        .busy_gpio = CONFIG_APP_EPD_BUSY_GPIO,
        .spi_hz = CONFIG_APP_EPD_SPI_HZ,
    };
    ESP_ERROR_CHECK(epd_init(&cfg));

    BaseType_t ok = xTaskCreate(keepalive_task, "epd-keepalive", 2048, NULL, 1, NULL);
    configASSERT(ok == pdPASS);
}

display_result_t display_show(const uint8_t *frame, refresh_reason_t why)
{
    display_result_t result;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_shown_is_on_panel && memcmp(s_shown, frame, sizeof s_shown) == 0) {
        result = DISPLAY_UNCHANGED;
    } else {
        esp_err_t err = ESP_OK;
        if (refresh_panel(frame, why, &err) == REFRESH_NONE) {
            result = DISPLAY_DEFERRED;
        } else if (err != ESP_OK) {
            s_shown_is_on_panel = false;
            result = DISPLAY_FAILED;
        } else {
            memcpy(s_shown, frame, sizeof s_shown);
            s_shown_is_on_panel = true;
            result = DISPLAY_SHOWN;
        }
    }
    xSemaphoreGive(s_lock);
    return result;
}

uint32_t display_wait_ms(refresh_reason_t why)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t wait_ms = refresh_policy_wait_ms(&s_policy, &s_cfg, why, now_ms());
    xSemaphoreGive(s_lock);
    return wait_ms;
}

const uint8_t *display_shown_frame(void)
{
    return s_shown;
}
