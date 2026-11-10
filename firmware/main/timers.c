/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "timers.h"

#include "esp_timer.h"

#include "events.h"

#define MICROSECONDS_PER_MILLISECOND 1000

typedef struct {
    esp_timer_handle_t handle;
    bool               running;
    int64_t            due_us;
} host_timer_t;

static host_timer_t s_timers[TIMERS_COUNT];

/* Runs in the esp_timer task: it only says which timer, and touches no
 * state the host task owns. */
static void on_timer_ran_out(void *arg)
{
    event_t event = { .kind = EVENT_TIMER, .timer = { .id = (uint8_t)(uintptr_t)arg } };
    events_post(&event);
}

void timers_start(void)
{
    for (int id = 0; id < TIMERS_COUNT; id++) {
        esp_timer_create_args_t args = {
            .callback = on_timer_ran_out,
            .arg = (void *)(uintptr_t)id,
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
    timer->running = milliseconds > 0;
    if (timer->running) {
        uint64_t delay_us = (uint64_t)milliseconds * MICROSECONDS_PER_MILLISECOND;
        timer->due_us = esp_timer_get_time() + (int64_t)delay_us;
        ESP_ERROR_CHECK(esp_timer_start_once(timer->handle, delay_us));
    }
}

bool timers_take_due(int id)
{
    host_timer_t *timer = &s_timers[id];
    if (!timer->running || esp_timer_get_time() < timer->due_us) {
        return false;
    }
    timer->running = false;
    return true;
}
