/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "events.h"

#include <stdatomic.h>

#include "esp_timer.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "units.h"

static const char *TAG = "events";

#define QUEUE_LENGTH 64
/* Presses have a queue of their own, taken first: what the user did comes
 * before what other tasks ask of the host loop. Four events a press. */
#define INPUT_QUEUE_LENGTH 16

static QueueHandle_t    s_queue;
static QueueHandle_t    s_input;
static QueueSetHandle_t s_either;
static volatile uint32_t s_dropped;
static uint32_t s_dropped_reported; /* by the host loop, which has the stack to say so */
/* A release the full queue could not take, a bit a button: the host loop ends the hold it ended.
 * Only a dropped release counts - one still waiting in the queue is not lost. */
static atomic_uint s_releases_dropped;

void events_start(void)
{
    s_queue = xQueueCreate(QUEUE_LENGTH, sizeof(event_t));
    s_input = xQueueCreate(INPUT_QUEUE_LENGTH, sizeof(event_t));
    s_either = xQueueCreateSet(QUEUE_LENGTH + INPUT_QUEUE_LENGTH);
    configASSERT(s_queue != NULL && s_input != NULL && s_either != NULL);
    xQueueAddToSet(s_queue, s_either);
    xQueueAddToSet(s_input, s_either);
}

bool events_post(const event_t *event)
{
    if (xQueueSend(event->kind == EVENT_BUTTON ? s_input : s_queue, event, 0) != pdTRUE) {
        /* Counted, not logged: this runs in whatever task has the event,
         * and the smallest of them has no stack for a log line. */
        s_dropped++;
        return false;
    }
    return true;
}

bool events_are_waiting(void)
{
    return uxQueueMessagesWaiting(s_queue) > 0 || uxQueueMessagesWaiting(s_input) > 0;
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
    event_t event = { .kind = EVENT_BUTTON,
                      .button = { .index = index, .press = press,
                                  .at_ms = esp_timer_get_time() / US_PER_MS } };
    if (!events_post(&event) && press == EVENT_PRESS_UP && index < 32) {
        atomic_fetch_or(&s_releases_dropped, 1u << index);
    }
}

bool events_take_release_dropped(uint8_t index)
{
    if (index >= 32) {
        return false;
    }
    return (atomic_fetch_and(&s_releases_dropped, ~(1u << index)) & (1u << index)) != 0;
}

typedef struct {
    events_call_fn    run;
    void             *argument;
    SemaphoreHandle_t done;
} call_t;

static TaskHandle_t s_host_loop;

void events_set_host_loop_task(void)
{
    s_host_loop = xTaskGetCurrentTaskHandle();
}

bool events_is_host_loop(void)
{
    return xTaskGetCurrentTaskHandle() == s_host_loop;
}

bool events_call(events_call_fn run, void *argument)
{
    if (events_is_host_loop()) {
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

#define QUEUE_FULL_RETRY_MS 50 /* the host loop is busy with the panel: not for long */

void events_call_patiently(events_call_fn run, void *argument)
{
    while (!events_call(run, argument)) {
        vTaskDelay(pdMS_TO_TICKS(QUEUE_FULL_RETRY_MS));
    }
}

void events_answer_call(void *asked)
{
    call_t *call = asked;
    call->run(call->argument);
    xSemaphoreGive(call->done);
}

bool events_post_intent(int intent)
{
    event_t event = { .kind = EVENT_INTENT, .intent = intent };
    return events_post(&event);
}

bool events_wait(event_t *event, uint32_t timeout_ms)
{
    TickType_t ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    /* Whichever queue the set names, input is looked at first. */
    if (xQueueSelectFromSet(s_either, ticks) == NULL) {
        return false;
    }
    return xQueueReceive(s_input, event, 0) == pdTRUE || xQueueReceive(s_queue, event, 0) == pdTRUE;
}
