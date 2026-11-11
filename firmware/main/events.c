/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "events.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "events";

#define QUEUE_LENGTH 64

static QueueHandle_t s_queue;
static volatile uint32_t s_dropped;
static uint32_t s_dropped_reported; /* by the host loop, which has the stack to say so */

void events_start(void)
{
    s_queue = xQueueCreate(QUEUE_LENGTH, sizeof(event_t));
    configASSERT(s_queue != NULL);
}

bool events_post(const event_t *event)
{
    if (xQueueSend(s_queue, event, 0) != pdTRUE) {
        /* Counted, not logged: this runs in whatever task has the event,
         * and the smallest of them has no stack for a log line. */
        s_dropped++;
        return false;
    }
    return true;
}

bool events_are_waiting(void)
{
    return uxQueueMessagesWaiting(s_queue) > 0;
}

void events_report_dropped(void)
{
    uint32_t dropped = s_dropped;
    if (dropped != s_dropped_reported) {
        ESP_LOGW(TAG, "queue full: %u events dropped", (unsigned)(dropped - s_dropped_reported));
        s_dropped_reported = dropped;
    }
}

void events_post_button(uint8_t index, event_press_t press)
{
    event_t event = { .kind = EVENT_BUTTON, .button = { .index = index, .press = press } };
    events_post(&event);
}

void events_post_apps_changed(void)
{
    event_t event = { .kind = EVENT_APPS_CHANGED };
    events_post(&event);
}

bool events_wait(event_t *event, uint32_t timeout_ms)
{
    TickType_t ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(s_queue, event, ticks) == pdTRUE;
}
