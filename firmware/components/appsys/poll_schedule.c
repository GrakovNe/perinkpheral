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
