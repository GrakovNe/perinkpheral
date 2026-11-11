/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "events.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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

typedef struct {
    events_call_fn    run;
    void             *argument;
    SemaphoreHandle_t done;
} call_t;

static TaskHandle_t s_host_loop;

void events_this_is_the_host_loop(void)
{
    s_host_loop = xTaskGetCurrentTaskHandle();
}

bool events_call(events_call_fn run, void *argument)
{
    if (xTaskGetCurrentTaskHandle() == s_host_loop) {
        run(argument);
        return true;
    }
    call_t call = { .run = run, .argument = argument, .done = xSemaphoreCreateBinary() };
    if (call.done == NULL) {
        return false;
    }
    event_t event = { .kind = EVENT_CALL, .call = &call };
    bool    is_asked = events_post(&event);
    if (is_asked) {
        /* For as long as it takes: the call is on this task's stack. */
        xSemaphoreTake(call.done, portMAX_DELAY);
    }
    vSemaphoreDelete(call.done);
    return is_asked;
}

void events_answer_call(void *asked)
{
    call_t *call = asked;
    call->run(call->argument);
    xSemaphoreGive(call->done);
}

bool events_post_apps_changed(void)
{
    event_t event = { .kind = EVENT_APPS_CHANGED };
    return events_post(&event);
}

bool events_wait(event_t *event, uint32_t timeout_ms)
{
    TickType_t ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(s_queue, event, ticks) == pdTRUE;
}
