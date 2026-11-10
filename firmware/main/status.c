/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "status.h"

#include <string.h>
#include <time.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "events.h"
#include "net_time.h"
#include "wifi.h"

/* The date turns once a day, but nobody says when the clock gets set. */
#define DATE_CHECK_PERIOD_US (60LL * 1000 * 1000)
#define DATE_FORMAT          "%a %d %b"

static SemaphoreHandle_t s_lock;
static status_t          s_last_told; /* what the host was last told about */

void status_read(status_t *status)
{
    memset(status, 0, sizeof *status);
    status->wifi_connected = wifi_is_online();
    status->date_known = net_time_is_known();
    if (status->date_known) {
        time_t now = (time_t)net_time_now();
        struct tm local;
        localtime_r(&now, &local);
        strftime(status->date, sizeof status->date, DATE_FORMAT, &local);
    }
}

void status_check(void)
{
    status_t now;
    status_read(&now);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = memcmp(&now, &s_last_told, sizeof now) != 0;
    s_last_told = now;
    xSemaphoreGive(s_lock);
    if (changed) {
        event_t event = { .kind = EVENT_STATUS_CHANGED };
        events_post(&event);
    }
}

static void on_date_check(void *arg)
{
    (void)arg;
    status_check();
}

void status_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);
    status_read(&s_last_told);

    esp_timer_handle_t timer;
    esp_timer_create_args_t args = { .callback = on_date_check, .name = "status" };
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, DATE_CHECK_PERIOD_US));
}
