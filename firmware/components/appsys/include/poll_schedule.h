/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* When each poll of each application is due. One request at a time: the
 * schedule says which is next and how long until then. Pure, host-tested. */

#define POLL_SCHEDULE_MAX 32

typedef struct {
    int      app;      /* whose poll, as the caller numbers its applications */
    int      poll;     /* its number in that application's manifest */
    uint32_t every_ms;
    int64_t  due_ms;
} poll_entry_t;

typedef struct {
    poll_entry_t entries[POLL_SCHEDULE_MAX];
    int          count;
} poll_schedule_t;

void poll_schedule_init(poll_schedule_t *schedule);

/* The first run of every poll is spread out from `now_ms`, so that a
 * device with many applications does not ask for everything at once.
 * False when the schedule is full. */
bool poll_schedule_add(poll_schedule_t *schedule, int app, int poll, uint32_t every_s,
                       int64_t now_ms);

/* The entry that is due, or NULL; `wait_ms` is then how long until one
 * is, or UINT32_MAX when the schedule is empty. Of several that are due
 * the one waiting longest comes first. */
const poll_entry_t *poll_schedule_due(const poll_schedule_t *schedule, int64_t now_ms,
                                      uint32_t *wait_ms);

/* The entry was just fetched, or failed: it is due again one period from
 * now, not from when it should have run - a device that slept through
 * several periods does not catch up with a burst. */
void poll_schedule_done(poll_schedule_t *schedule, const poll_entry_t *entry, int64_t now_ms);

#ifdef __cplusplus
}
#endif
