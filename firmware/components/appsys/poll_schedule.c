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
