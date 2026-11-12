/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "deadlines.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MICROSECONDS_PER_MS 1000
#define MS_PER_SECOND       1000
/* The news of a deadline was not taken - the host's queue was full: it is
 * offered again this soon. */
#define TELL_AGAIN_MS       50

static deadline_table_t s_table;
static SemaphoreHandle_t  s_lock;
static esp_timer_handle_t s_timer; /* the one timer: set for the nearest deadline */
static bool (*s_some_are_due)(void);

int64_t deadlines_now_ms(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (int64_t)now.tv_sec * MS_PER_SECOND + now.tv_usec / MICROSECONDS_PER_MS;
}

/* Sets the timer for the nearest deadline. With the lock held. */
static void aim_timer(void)
{
    esp_timer_stop(s_timer); /* not running is fine */
    int64_t nearest_ms = deadline_table_nearest(&s_table);
    if (nearest_ms == DEADLINE_NONE) {
        return;
    }
    int64_t wait_ms = nearest_ms - deadlines_now_ms();
    esp_timer_start_once(s_timer, (uint64_t)(wait_ms > 0 ? wait_ms : 0) * MICROSECONDS_PER_MS);
}

/* In the timer's task. The deadline stays in the table until somebody
 * takes it, so news that was not taken loses nothing. */
static void on_timer(void *arg)
{
    (void)arg;
    if (!s_some_are_due()) {
        esp_timer_start_once(s_timer, (uint64_t)TELL_AGAIN_MS * MICROSECONDS_PER_MS);
    }
}

void deadlines_start(bool (*some_are_due)(void))
{
    s_some_are_due = some_are_due;
    deadline_table_init(&s_table);
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);
    esp_timer_create_args_t timer = { .callback = on_timer, .name = "deadlines" };
    ESP_ERROR_CHECK(esp_timer_create(&timer, &s_timer));
    aim_timer();
}

static bool set(deadline_kind_t kind, uint32_t owner, int number, int64_t when_ms,
                bool is_from_now)
{
    deadline_id_t id = { kind, owner, number };
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* Now is read with the lock held: the clock is not set in between. */
    int64_t at_ms = is_from_now ? deadlines_now_ms() + when_ms : when_ms;
    bool    is_set = deadline_table_set(&s_table, id, at_ms, is_from_now);
    aim_timer();
    xSemaphoreGive(s_lock);
    return is_set;
}

bool deadlines_set_in(deadline_kind_t kind, uint32_t owner, int number, int64_t in_ms)
{
    return set(kind, owner, number, in_ms, true);
}

bool deadlines_set_at(deadline_kind_t kind, uint32_t owner, int number, int64_t at_ms)
{
    return set(kind, owner, number, at_ms, false);
}

void deadlines_cancel(deadline_kind_t kind, uint32_t owner, int number)
{
    deadline_id_t id = { kind, owner, number };
    xSemaphoreTake(s_lock, portMAX_DELAY);
    deadline_table_cancel(&s_table, id);
    aim_timer();
    xSemaphoreGive(s_lock);
}

void deadlines_keep_only(deadline_kind_t kind, const uint32_t *owners, int owner_count)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    deadline_table_keep_only(&s_table, kind, owners, owner_count);
    aim_timer();
    xSemaphoreGive(s_lock);
}

bool deadlines_has(deadline_kind_t kind, uint32_t owner, int number)
{
    deadline_id_t id = { kind, owner, number };
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool has = deadline_table_when(&s_table, id) != DEADLINE_NONE;
    xSemaphoreGive(s_lock);
    return has;
}

bool deadlines_take_due(deadline_id_t *id)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool is_due = deadline_table_take_due(&s_table, deadlines_now_ms(), id);
    if (!is_due) {
        /* The last of them was taken: the timer goes back to waiting for
         * the next. While some are due it stays as it is - whoever takes
         * them is at it. */
        aim_timer();
    }
    xSemaphoreGive(s_lock);
    return is_due;
}

void deadlines_look_again(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    aim_timer();
    xSemaphoreGive(s_lock);
}

bool deadlines_set_clock(const struct timeval *to, int64_t *moved_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t before_ms = deadlines_now_ms();
    bool    is_set = settimeofday(to, NULL) == 0;
    int64_t delta_ms = 0;
    if (is_set) {
        delta_ms = (int64_t)to->tv_sec * MS_PER_SECOND + to->tv_usec / MICROSECONDS_PER_MS -
                   before_ms;
        deadline_table_clock_moved(&s_table, delta_ms);
        aim_timer();
    }
    xSemaphoreGive(s_lock);
    if (moved_ms != NULL) {
        *moved_ms = delta_ms;
    }
    return is_set;
}
