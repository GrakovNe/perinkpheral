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
    done->due_ms = now_ms + done->every_ms;
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
            entry->every_ms = every_s * MS_PER_SECOND;
            entry->due_ms = now_ms + entry->every_ms;
            return true;
        }
    }
    return false;
}

void app_alarms_init(app_alarms_t *alarms)
{
    for (int app = 0; app < APP_ALARMS_MAX; app++) {
        alarms->due_ms[app] = 0;
    }
}

bool app_alarms_set(app_alarms_t *alarms, int app, uint32_t seconds, int64_t now_ms)
{
    if (app < 0 || app >= APP_ALARMS_MAX) {
        return false;
    }
    /* 0 means "not set", so an alarm never falls on the very first millisecond. */
    int64_t due_ms = now_ms + (int64_t)seconds * MS_PER_SECOND;
    alarms->due_ms[app] = seconds == 0 ? 0 : due_ms > 0 ? due_ms : 1;
    return true;
}

int app_alarms_take_due(app_alarms_t *alarms, int64_t now_ms)
{
    for (int app = 0; app < APP_ALARMS_MAX; app++) {
        if (alarms->due_ms[app] != 0 && alarms->due_ms[app] <= now_ms) {
            alarms->due_ms[app] = 0;
            return app;
        }
    }
    return -1;
}

void poll_schedule_finished(poll_schedule_t *schedule, int app, int poll, int64_t now_ms)
{
    for (int i = 0; i < schedule->count; i++) {
        poll_entry_t *entry = &schedule->entries[i];
        if (entry->app == app && entry->poll == poll) {
            entry->due_ms = now_ms + entry->every_ms;
            return;
        }
    }
}
