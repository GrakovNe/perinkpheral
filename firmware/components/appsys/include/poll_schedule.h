/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The polls of the installed applications: what pace each keeps, and
 * which are waiting to be asked now - one request at a time, the one
 * waiting longest first. When a poll's time comes is not kept here: that
 * is a deadline like any other, and this says only how far the next one
 * is. Pure, host-tested. */

#define POLL_SCHEDULE_MAX 32

typedef struct {
    int      app;      /* whose poll: the application's id, as the caller has it */
    int      poll;     /* its number in that application's manifest */
    uint32_t every_ms;
    bool     is_wanted;    /* its time has come, or somebody asked for it */
    uint32_t wanted_order; /* of several that are wanted the first to be goes first */
} poll_entry_t;

typedef struct {
    poll_entry_t entries[POLL_SCHEDULE_MAX];
    int          count;
    uint32_t     floor_ms; /* no poll comes closer to its last than this; 0: no floor */
    uint32_t     next_order;
} poll_schedule_t;

void poll_schedule_init(poll_schedule_t *schedule);

/* A new poll. `first_in_ms` is how long until its first run: they are
 * spread out, so that a device with many applications does not ask for
 * everything at once. False when the schedule is full. */
bool poll_schedule_add(poll_schedule_t *schedule, int app, int poll, uint32_t every_s,
                       uint32_t *first_in_ms);

/* Whether the poll is scheduled. */
bool poll_schedule_has(const poll_schedule_t *schedule, int app, int poll);

/* Takes out the polls of every application that is not among `apps`:
 * they were removed. The others stay as they are. */
void poll_schedule_keep_only(poll_schedule_t *schedule, const int *apps, int app_count);

/* How far the poll's next run is from its last: what it asked for, or
 * the floor when that is more. Counted from when it was answered, not
 * from when it should have run - a device that slept through several
 * periods does not catch up with a burst. 0: there is no such poll. */
uint32_t poll_schedule_pace_ms(const poll_schedule_t *schedule, int app, int poll);

/* The least any poll may be from its last, whatever it asks for - the
 * device is saving power; 0 takes the floor away. The polls keep the pace
 * they asked for and get it back with the floor gone. */
void poll_schedule_set_floor(poll_schedule_t *schedule, uint32_t floor_s);

/* The application wants its poll at another pace than its manifest says
 * - the server told it when to come again. The limits of a manifest hold
 * (APP_POLL_EVERY_MIN_S, APP_POLL_EVERY_MAX_S). False when there is no
 * such poll; `is_another` says whether the pace changed - asking for the
 * one it has moves nothing, an application says it every time it is
 * loaded. */
bool poll_schedule_set_interval(poll_schedule_t *schedule, int app, int poll, uint32_t every_s,
                                bool *is_another);

/* The poll is to be asked now: its time has come, or the application
 * asked. One that is wanted already keeps its place. False when there is
 * no such poll. */
bool poll_schedule_want(poll_schedule_t *schedule, int app, int poll);

/* Every poll of that application: its settings changed, and what it shows
 * was fetched with the old ones. */
void poll_schedule_want_of(poll_schedule_t *schedule, int app);

/* Every poll: the network is back, and what was not asked while it was
 * away is asked at once. */
void poll_schedule_want_all(poll_schedule_t *schedule);

/* The poll to ask next, or NULL when none is wanted. */
const poll_entry_t *poll_schedule_next_wanted(const poll_schedule_t *schedule);

/* The poll was fetched, or failed: it is not wanted any more. */
void poll_schedule_answered(poll_schedule_t *schedule, int app, int poll);

#ifdef __cplusplus
}
#endif
