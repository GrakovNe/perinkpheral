/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "events.h"

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
/* The input queue's last places are kept for releases. A release is posted only for a button
 * whose press the queue took, so each button has at most one release to come: as many places as
 * buttons are enough, and no release is ever dropped. Losing one would leave the button held for
 * the host loop - BACK's long press never coming, UP and DOWN switching applications. */
#define RELEASE_PLACES 4
static SemaphoreHandle_t s_button_lock; /* one producer at a time between counting and posting */
static bool              s_press_taken[RELEASE_PLACES];
_Static_assert(INPUT_QUEUE_LENGTH > RELEASE_PLACES, "the queue must leave room for presses");
static uint32_t s_dropped_reported; /* by the host loop, which has the stack to say so */

void events_start(void)
{
    s_queue = xQueueCreate(QUEUE_LENGTH, sizeof(event_t));
    s_input = xQueueCreate(INPUT_QUEUE_LENGTH, sizeof(event_t));
    s_either = xQueueCreateSet(QUEUE_LENGTH + INPUT_QUEUE_LENGTH);
    s_button_lock = xSemaphoreCreateMutex();
    configASSERT(s_queue != NULL && s_input != NULL && s_either != NULL && s_button_lock != NULL);
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
    bool posted = false;
    xSemaphoreTake(s_button_lock, portMAX_DELAY);
    UBaseType_t places = uxQueueSpacesAvailable(s_input);
    if (press == EVENT_PRESS_UP) {
        /* The release of a press the host loop never saw is not news to it either. */
        bool was_taken = index >= RELEASE_PLACES || s_press_taken[index];
        posted = was_taken && places > 0 && xQueueSend(s_input, &event, 0) == pdTRUE;
        if (index < RELEASE_PLACES) {
            s_press_taken[index] = false;
        }
    } else {
        posted = places > RELEASE_PLACES && xQueueSend(s_input, &event, 0) == pdTRUE;
        if (press == EVENT_PRESS_DOWN && index < RELEASE_PLACES && posted) {
            /* A press refused leaves one taken before it waiting for its release. */
            s_press_taken[index] = true;
        }
    }
    xSemaphoreGive(s_button_lock);
    if (!posted) {
        s_dropped++;
    }
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
