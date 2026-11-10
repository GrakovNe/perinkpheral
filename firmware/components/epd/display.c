/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "display.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "epd.h"

static const char *TAG = "display";

#define KEEPALIVE_CHECK_MS (60 * 1000)

static uint8_t           s_fb[EPD_FB_SIZE];
static gfx_t             s_gfx;
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

static void keepalive_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(KEEPALIVE_CHECK_MS));
        display_present(REFRESH_FOR_KEEPALIVE);
    }
}

void display_start(void)
{
    gfx_init(&s_gfx, s_fb, EPD_W, EPD_H);
    gfx_clear(&s_gfx, false);
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

gfx_t *display_gfx(void)
{
    return &s_gfx;
}

bool display_present(refresh_reason_t why)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t t0 = now_ms();
    refresh_kind_t kind = refresh_policy_decide(&s_policy, &s_cfg, why, t0);
    if (kind == REFRESH_NONE) {
        xSemaphoreGive(s_lock);
        return false;
    }
    esp_err_t err = kind == REFRESH_FULL ? epd_show_full(s_fb) : epd_show_partial(s_fb);
    int64_t t1 = now_ms();
    refresh_policy_note(&s_policy, kind, t1);
    xSemaphoreGive(s_lock);

    const char *name = kind == REFRESH_FULL ? "full" : "partial";
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s refresh, %lld ms", name, (long long)(t1 - t0));
    } else {
        ESP_LOGW(TAG, "%s refresh: %s after %lld ms", name, esp_err_to_name(err),
                 (long long)(t1 - t0));
    }
    return true;
}
