/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "poll_schedule.h"

#include <stddef.h>

#define MS_PER_SECOND       1000
#define FIRST_RUN_SPREAD_MS 2000 /* between the first runs of two polls */

void poll_schedule_init(poll_schedule_t *schedule)
{
    schedule->count = 0;
    schedule->floor_ms = 0;
    schedule->next_order = 0;
}

static poll_entry_t *find(poll_schedule_t *schedule, int app, int poll)
{
    for (int i = 0; i < schedule->count; i++) {
        if (schedule->entries[i].app == app && schedule->entries[i].poll == poll) {
            return &schedule->entries[i];
        }
    }
    return NULL;
}

void poll_schedule_set_floor(poll_schedule_t *schedule, uint32_t floor_s)
{
    schedule->floor_ms = floor_s * MS_PER_SECOND;
}

bool poll_schedule_has(const poll_schedule_t *schedule, int app, int poll)
{
    return find((poll_schedule_t *)schedule, app, poll) != NULL;
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
                       uint32_t *first_in_ms)
{
    if (schedule->count >= POLL_SCHEDULE_MAX) {
        return false;
    }
    *first_in_ms = (uint32_t)schedule->count * FIRST_RUN_SPREAD_MS;
    poll_entry_t *entry = &schedule->entries[schedule->count++];
    entry->app = app;
    entry->poll = poll;
    entry->every_ms = every_s * MS_PER_SECOND;
    entry->is_wanted = false;
    return true;
}

uint32_t poll_schedule_pace_ms(const poll_schedule_t *schedule, int app, int poll)
{
    const poll_entry_t *entry = find((poll_schedule_t *)schedule, app, poll);
    if (entry == NULL) {
        return 0;
    }
    return entry->every_ms > schedule->floor_ms ? entry->every_ms : schedule->floor_ms;
}

bool poll_schedule_set_interval(poll_schedule_t *schedule, int app, int poll, uint32_t every_s,
                                bool *is_another)
{
    if (every_s < APP_POLL_EVERY_MIN_S) {
        every_s = APP_POLL_EVERY_MIN_S;
    } else if (every_s > APP_POLL_EVERY_MAX_S) {
        every_s = APP_POLL_EVERY_MAX_S;
    }
    poll_entry_t *entry = find(schedule, app, poll);
    if (entry == NULL) {
        return false;
    }
    *is_another = entry->every_ms != every_s * MS_PER_SECOND;
    entry->every_ms = every_s * MS_PER_SECOND;
    return true;
}

static void want(poll_schedule_t *schedule, poll_entry_t *entry)
{
    if (!entry->is_wanted) {
        entry->is_wanted = true;
        entry->wanted_order = schedule->next_order++;
    }
}

bool poll_schedule_want(poll_schedule_t *schedule, int app, int poll)
{
    poll_entry_t *entry = find(schedule, app, poll);
    if (entry == NULL) {
        return false;
    }
    want(schedule, entry);
    return true;
}

void poll_schedule_want_of(poll_schedule_t *schedule, int app)
{
    for (int i = 0; i < schedule->count; i++) {
        if (schedule->entries[i].app == app) {
            want(schedule, &schedule->entries[i]);
        }
    }
}

void poll_schedule_want_all(poll_schedule_t *schedule)
{
    for (int i = 0; i < schedule->count; i++) {
        want(schedule, &schedule->entries[i]);
    }
}

const poll_entry_t *poll_schedule_next_wanted(const poll_schedule_t *schedule)
{
    const poll_entry_t *first = NULL;
    for (int i = 0; i < schedule->count; i++) {
        const poll_entry_t *entry = &schedule->entries[i];
        /* The difference, not the numbers: the count may wrap. */
        if (entry->is_wanted &&
            (first == NULL || (int32_t)(entry->wanted_order - first->wanted_order) < 0)) {
            first = entry;
        }
    }
    return first;
}

void poll_schedule_answered(poll_schedule_t *schedule, int app, int poll)
{
    poll_entry_t *entry = find(schedule, app, poll);
    if (entry != NULL) {
        entry->is_wanted = false;
    }
}
