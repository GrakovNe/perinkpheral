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

/* When each poll of each application is due. One request at a time: the
 * schedule says which is next and how long until then. Pure, host-tested. */

#define POLL_SCHEDULE_MAX 32

typedef struct {
    int      app;      /* whose poll: the application's id, as the caller has it */
    int      poll;     /* its number in that application's manifest */
    uint32_t every_ms;
    int64_t  due_ms;
} poll_entry_t;

typedef struct {
    poll_entry_t entries[POLL_SCHEDULE_MAX];
    int          count;
    uint32_t     floor_ms; /* no poll comes closer to its last than this; 0: no floor */
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

/* The application wants its poll at another pace than its manifest says
 * - the server told it when to come again. Counted from now; the limits
 * of a manifest hold (APP_POLL_EVERY_MIN_S, APP_POLL_EVERY_MAX_S). False
 * when there is no such poll. */
bool poll_schedule_set_interval(poll_schedule_t *schedule, int app, int poll, uint32_t every_s,
                                int64_t now_ms);

/* An application's alarm: it asked to be called once, some time from now,
 * whether it is on the screen then or not. One for each application. */
/* Every poll is due now: the network is back, and what was not asked while
 * it was away is asked at once. */
void poll_schedule_hurry_all(poll_schedule_t *schedule, int64_t now_ms);

/* The least any poll may be from its last, whatever it asks for - the
 * device is saving power; 0 takes the floor away. The polls keep the pace
 * they asked for and get it back with the floor gone. */
void poll_schedule_set_floor(poll_schedule_t *schedule, uint32_t floor_s);

/* Whether the poll is scheduled. */
bool poll_schedule_has(const poll_schedule_t *schedule, int app, int poll);

/* Takes out the polls of every application that is not among `apps`:
 * they were removed. The others stay as they are. */
void poll_schedule_keep_only(poll_schedule_t *schedule, const int *apps, int app_count);

/* ---- alarms ----
 *
 * An application has a few alarms, each under a number of its own. One is
 * a moment on the caller's clock - a clock that only goes forward; an
 * alarm set for a time of day says so, and moves when the wall clock is
 * set to another time. */
#define APP_ALARMS_MAX 16

typedef struct {
    bool    is_set;
    bool    follows_wall_clock; /* set "at such a time", not "in so long" */
    int     app;
    int     alarm;
    int64_t due_ms;
} app_alarm_t;

typedef struct {
    app_alarm_t alarms[APP_ALARMS_MAX];
} app_alarms_t;

void app_alarms_init(app_alarms_t *alarms);

/* Sets the alarm, or sets it anew. False when the table is full. */
bool app_alarms_set(app_alarms_t *alarms, int app, int alarm, int64_t due_ms,
                    bool follows_wall_clock);

/* Takes the alarm back; one that was not set is gone already. */
void app_alarms_cancel(app_alarms_t *alarms, int app, int alarm);

/* An alarm that has come, taken out of the table; false when none has. */
bool app_alarms_take_due(app_alarms_t *alarms, int64_t now_ms, int *app, int *alarm);

/* How long until the nearest; UINT32_MAX when none is set, 0 when one is due. */
uint32_t app_alarms_wait_ms(const app_alarms_t *alarms, int64_t now_ms);

/* The alarms of applications that are not among `apps` go. */
void app_alarms_keep_only(app_alarms_t *alarms, const int *apps, int app_count);

/* The wall clock was set `delta_ms` ahead (or back, negative): an alarm
 * for a time of day comes that much sooner, one "in so long" not at all. */
void app_alarms_clock_moved(app_alarms_t *alarms, int64_t delta_ms);

/* The same as poll_schedule_done(), for a caller that kept the numbers
 * and not the entry. */
void poll_schedule_finished(poll_schedule_t *schedule, int app, int poll, int64_t now_ms);

/* One poll is due now: the application asked for it. False when there is
 * no such poll. */
bool poll_schedule_hurry_one(poll_schedule_t *schedule, int app, int poll, int64_t now_ms);

/* Every poll of that application is due now: its settings changed, and
 * what it shows was fetched with the old ones. */
void poll_schedule_hurry(poll_schedule_t *schedule, int app, int64_t now_ms);

#ifdef __cplusplus
}
#endif
