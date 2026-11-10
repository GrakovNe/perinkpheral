/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_host.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "host_api.h"
#include "timers.h"

static const char *TAG = "app";

_Static_assert(HOST_TIMER_COUNT == TIMERS_COUNT, "an application gets every timer of the host");

/* There is no clock to ask until the network milestone. */
static int64_t unix_time(void)
{
    return 0;
}

static int64_t uptime_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void log_from_app(int level, const char *text, int length)
{
    switch (level) {
    case HOST_LOG_ERROR:
        ESP_LOGE(TAG, "%.*s", length, text);
        break;
    case HOST_LOG_WARN:
        ESP_LOGW(TAG, "%.*s", length, text);
        break;
    case HOST_LOG_DEBUG:
        ESP_LOGD(TAG, "%.*s", length, text);
        break;
    default:
        ESP_LOGI(TAG, "%.*s", length, text);
        break;
    }
}

const app_services_t *app_host_services(void)
{
    static const app_services_t SERVICES = {
        .unix_time = unix_time,
        .uptime_ms = uptime_ms,
        .log = log_from_app,
        .timer_set = timers_set,
    };
    return &SERVICES;
}
