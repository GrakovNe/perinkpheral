/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "events.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "events";

#define QUEUE_LENGTH 32

static QueueHandle_t s_queue;

void events_start(void)
{
    s_queue = xQueueCreate(QUEUE_LENGTH, sizeof(event_t));
    configASSERT(s_queue != NULL);
}

bool events_post(const event_t *event)
{
    if (xQueueSend(s_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, event %d dropped", (int)event->kind);
        return false;
    }
    return true;
}

void events_post_button(uint8_t index, event_press_t press)
{
    event_t event = { .kind = EVENT_BUTTON, .button = { .index = index, .press = press } };
    events_post(&event);
}

bool events_wait(event_t *event, uint32_t timeout_ms)
{
    TickType_t ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(s_queue, event, ticks) == pdTRUE;
}
