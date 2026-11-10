/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "timers.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "events.h"

#define MICROSECONDS_PER_MILLISECOND 1000

typedef struct {
    esp_timer_handle_t handle;
    int                id;
    /* Written by the host task, read by the timer task. */
    volatile uint32_t  generation;
} host_timer_t;

static host_timer_t s_timers[TIMERS_COUNT];

static void on_timer_ran_out(void *arg)
{
    const host_timer_t *timer = arg;
    event_t event = {
        .kind = EVENT_TIMER,
        .timer = { .id = (uint8_t)timer->id, .generation = timer->generation },
    };
    events_post(&event);
}

void timers_start(void)
{
    for (int id = 0; id < TIMERS_COUNT; id++) {
        s_timers[id].id = id;
        esp_timer_create_args_t args = {
            .callback = on_timer_ran_out,
            .arg = &s_timers[id],
            .name = "scene",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_timers[id].handle));
    }
}

void timers_set(int id, int32_t milliseconds)
{
    host_timer_t *timer = &s_timers[id];
    /* Not running is not an error here. */
    esp_timer_stop(timer->handle);
    timer->generation++;
    if (milliseconds > 0) {
        ESP_ERROR_CHECK(esp_timer_start_once(
            timer->handle, (uint64_t)milliseconds * MICROSECONDS_PER_MILLISECOND));
    }
}

bool timers_is_current(int id, uint32_t generation)
{
    return s_timers[id].generation == generation;
}
