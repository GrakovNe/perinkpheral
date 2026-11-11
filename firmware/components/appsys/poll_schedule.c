/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "poll_schedule.h"

#include <stddef.h>

#define MS_PER_SECOND      1000
#define FIRST_RUN_SPREAD_MS 2000 /* between the first runs of two polls */

void poll_schedule_init(poll_schedule_t *schedule)
{
    schedule->count = 0;
    schedule->floor_ms = 0;
}

/* How far the poll's next run is from its last: what it asked for, or the
 * floor when that is more. */
static uint32_t pace_ms(const poll_schedule_t *schedule, const poll_entry_t *entry)
{
    return entry->every_ms > schedule->floor_ms ? entry->every_ms : schedule->floor_ms;
}

void poll_schedule_set_floor(poll_schedule_t *schedule, uint32_t floor_s)
{
    schedule->floor_ms = floor_s * MS_PER_SECOND;
}

bool poll_schedule_has(const poll_schedule_t *schedule, int app, int poll)
{
    for (int i = 0; i < schedule->count; i++) {
        if (schedule->entries[i].app == app && schedule->entries[i].poll == poll) {
            return true;
        }
    }
    return false;
}

static bool is_among(int app, const int *apps, int app_count)
{
    for (int i = 0; i < app_count; i++) {
        if (apps[i] == app) {
            return true;
        }
    }
    return false;
}

void poll_schedule_keep_only(poll_schedule_t *schedule, const int *apps, int app_count)
{
    int kept = 0;
    for (int i = 0; i < schedule->count; i++) {
        if (is_among(schedule->entries[i].app, apps, app_count)) {
            schedule->entries[kept++] = schedule->entries[i];
        }
    }
    schedule->count = kept;
}

bool poll_schedule_add(poll_schedule_t *schedule, int app, int poll, uint32_t every_s,
                       int64_t now_ms)
{
    if (schedule->count >= POLL_SCHEDULE_MAX) {
        return false;
    }
    poll_entry_t *entry = &schedule->entries[schedule->count];
    entry->app = app;
    entry->poll = poll;
    entry->every_ms = every_s * MS_PER_SECOND;
    entry->due_ms = now_ms + (int64_t)schedule->count * FIRST_RUN_SPREAD_MS;
    schedule->count++;
    return true;
}

const poll_entry_t *poll_schedule_due(const poll_schedule_t *schedule, int64_t now_ms,
                                      uint32_t *wait_ms)
{
    const poll_entry_t *earliest = NULL;
    for (int i = 0; i < schedule->count; i++) {
        if (earliest == NULL || schedule->entries[i].due_ms < earliest->due_ms) {
            earliest = &schedule->entries[i];
        }
    }
    if (earliest == NULL) {
        *wait_ms = UINT32_MAX;
        return NULL;
    }
    if (earliest->due_ms <= now_ms) {
        *wait_ms = 0;
        return earliest;
    }
    *wait_ms = (uint32_t)(earliest->due_ms - now_ms);
    return NULL;
}

void poll_schedule_done(poll_schedule_t *schedule, const poll_entry_t *entry, int64_t now_ms)
{
    poll_entry_t *done = &schedule->entries[entry - schedule->entries];
    done->due_ms = now_ms + pace_ms(schedule, done);
}

void poll_schedule_hurry_all(poll_schedule_t *schedule, int64_t now_ms)
{
    for (int i = 0; i < schedule->count; i++) {
        if (schedule->entries[i].due_ms > now_ms) {
            schedule->entries[i].due_ms = now_ms;
        }
    }
}

void poll_schedule_hurry(poll_schedule_t *schedule, int app, int64_t now_ms)
{
    for (int i = 0; i < schedule->count; i++) {
        if (schedule->entries[i].app == app && schedule->entries[i].due_ms > now_ms) {
            schedule->entries[i].due_ms = now_ms;
        }
    }
}

bool poll_schedule_hurry_one(poll_schedule_t *schedule, int app, int poll, int64_t now_ms)
{
    for (int i = 0; i < schedule->count; i++) {
        poll_entry_t *entry = &schedule->entries[i];
        if (entry->app == app && entry->poll == poll) {
            if (entry->due_ms > now_ms) {
                entry->due_ms = now_ms;
            }
            return true;
        }
    }
    return false;
}

bool poll_schedule_set_interval(poll_schedule_t *schedule, int app, int poll, uint32_t every_s,
                                int64_t now_ms)
{
    if (every_s < APP_POLL_EVERY_MIN_S) {
        every_s = APP_POLL_EVERY_MIN_S;
    } else if (every_s > APP_POLL_EVERY_MAX_S) {
        every_s = APP_POLL_EVERY_MAX_S;
    }
    for (int i = 0; i < schedule->count; i++) {
        poll_entry_t *entry = &schedule->entries[i];
        if (entry->app == app && entry->poll == poll) {
            /* Asking for the pace it already has moves nothing: an
             * application says it every time it is loaded. */
            if (entry->every_ms != every_s * MS_PER_SECOND) {
                entry->every_ms = every_s * MS_PER_SECOND;
                entry->due_ms = now_ms + pace_ms(schedule, entry);
            }
            return true;
        }
    }
    return false;
}

void app_alarms_init(app_alarms_t *alarms)
{
    for (int i = 0; i < APP_ALARMS_MAX; i++) {
        alarms->alarms[i].is_set = false;
    }
}

static app_alarm_t *find_alarm(app_alarms_t *alarms, int app, int alarm)
{
    for (int i = 0; i < APP_ALARMS_MAX; i++) {
        app_alarm_t *entry = &alarms->alarms[i];
        if (entry->is_set && entry->app == app && entry->alarm == alarm) {
            return entry;
        }
    }
    return NULL;
}

bool app_alarms_set(app_alarms_t *alarms, int app, int alarm, int64_t due_ms,
                    bool follows_wall_clock)
{
    app_alarm_t *entry = find_alarm(alarms, app, alarm);
    for (int i = 0; entry == NULL && i < APP_ALARMS_MAX; i++) {
        if (!alarms->alarms[i].is_set) {
            entry = &alarms->alarms[i];
        }
    }
    if (entry == NULL) {
        return false;
    }
    *entry = (app_alarm_t){ .is_set = true, .follows_wall_clock = follows_wall_clock,
                            .app = app, .alarm = alarm, .due_ms = due_ms };
    return true;
}

void app_alarms_cancel(app_alarms_t *alarms, int app, int alarm)
{
    app_alarm_t *entry = find_alarm(alarms, app, alarm);
    if (entry != NULL) {
        entry->is_set = false;
    }
}

bool app_alarms_take_due(app_alarms_t *alarms, int64_t now_ms, int *app, int *alarm)
{
    /* The one that has waited longest first. */
    app_alarm_t *earliest = NULL;
    for (int i = 0; i < APP_ALARMS_MAX; i++) {
        app_alarm_t *entry = &alarms->alarms[i];
        if (entry->is_set && entry->due_ms <= now_ms &&
            (earliest == NULL || entry->due_ms < earliest->due_ms)) {
            earliest = entry;
        }
    }
    if (earliest == NULL) {
        return false;
    }
    earliest->is_set = false;
    *app = earliest->app;
    *alarm = earliest->alarm;
    return true;
}

uint32_t app_alarms_wait_ms(const app_alarms_t *alarms, int64_t now_ms)
{
    uint32_t wait_ms = UINT32_MAX;
    for (int i = 0; i < APP_ALARMS_MAX; i++) {
        const app_alarm_t *entry = &alarms->alarms[i];
        if (!entry->is_set) {
            continue;
        }
        int64_t left_ms = entry->due_ms - now_ms;
        uint32_t entry_wait_ms = left_ms <= 0 ? 0 : left_ms > UINT32_MAX - 1 ? UINT32_MAX - 1
                                                                             : (uint32_t)left_ms;
        if (entry_wait_ms < wait_ms) {
            wait_ms = entry_wait_ms;
        }
    }
    return wait_ms;
}

void app_alarms_keep_only(app_alarms_t *alarms, const int *apps, int app_count)
{
    for (int i = 0; i < APP_ALARMS_MAX; i++) {
        if (alarms->alarms[i].is_set && !is_among(alarms->alarms[i].app, apps, app_count)) {
            alarms->alarms[i].is_set = false;
        }
    }
}

void app_alarms_clock_moved(app_alarms_t *alarms, int64_t delta_ms)
{
    for (int i = 0; i < APP_ALARMS_MAX; i++) {
        if (alarms->alarms[i].is_set && alarms->alarms[i].follows_wall_clock) {
            alarms->alarms[i].due_ms -= delta_ms;
        }
    }
}

void poll_schedule_finished(poll_schedule_t *schedule, int app, int poll, int64_t now_ms)
{
    for (int i = 0; i < schedule->count; i++) {
        poll_entry_t *entry = &schedule->entries[i];
        if (entry->app == app && entry->poll == poll) {
            entry->due_ms = now_ms + pace_ms(schedule, entry);
            return;
        }
    }
}
