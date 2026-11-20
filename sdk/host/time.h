/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Time: the clock, the zone, local time taken apart, time in words, alarms
 * on or off the screen, and timers on it.
 */
#pragma once

#include "base.h"

/* Moments accepted by time_local_at(), time_text(), time_from_local() and
 * alarm_set_at(): 2026-01-01 through 2100-01-01, inclusive, in UTC.
 * Outside this range: HOST_ERR_ARGUMENT, with destinations unchanged.
 * time_from_local() checks the moment after normalizing the local fields. */
#define HOST_TIME_MIN 1767225600LL
#define HOST_TIME_MAX 4102444800LL

/* ---- Alarms ---- */

/* An alarm calls app_on_alarm(id) once at its time, on or off the screen.
 * Off the screen the application is loaded for the call and can request
 * the screen with screen_request(). An application has HOST_ALARM_COUNT
 * alarms, numbered from 0. Setting an alarm again moves it.
 * timer_set() works only on the screen; alarms work everywhere.
 *
 * alarm_set():    relative, `seconds` from now, at least 1. Independent
 *                 of clock changes. 0 cancels the alarm.
 * alarm_set_at(): absolute, at `unix_time` as counted by time_now().
 *                 Accepts HOST_TIME_MIN..HOST_TIME_MAX. Follows clock changes.
 * Both return HOST_OK; HOST_ERR_ARGUMENT for an invalid number or time;
 * HOST_ERR_NO_SPACE if the device cannot hold another alarm.
 *
 * A device restart clears all alarms. To survive a restart, keep the time
 * in the store and set the alarm again in the "started" intent. A new
 * version of an application does not inherit the alarms of the old one. */
#define HOST_ALARM_COUNT 4
HOST_IMPORT(alarm_set, int32_t alarm_set(int32_t id, int32_t seconds));
HOST_IMPORT(alarm_set_at, int32_t alarm_set_at(int32_t id, int64_t unix_time));

/* Repeating alarm: app_on_alarm(id) every `seconds`, until alarm_set(id, 0)
 * cancels it or the alarm is set another way. The first call comes
 * `first_in` seconds from now; 0 means one full period. To align with the
 * minute, pass the seconds left in the current minute.
 * Period: HOST_ALARM_EVERY_MIN to HOST_ALARM_EVERY_MAX seconds. Every
 * call loads the application when it is not on the screen. first_in is
 * 0..86400 independently of the period. A late alarm does not replay every
 * missed period: if the following occurrence is already overdue, the next
 * is scheduled one period from processing time.
 * Returns HOST_OK; HOST_ERR_ARGUMENT for an invalid number; HOST_ERR_NO_SPACE
 * when the device's shared pool of HOST_ALARM_REPEATING_MAX alarms is full. One
 * application can use several slots; none are reserved per application.
 * Say so in the log: a line in the header that
 * stops changing is otherwise taken for a hang.
 * Typical use: keeping a header_set_status() text current while the
 * application is off the screen. */
#define HOST_ALARM_EVERY_MIN 60
#define HOST_ALARM_EVERY_MAX 86400
#define HOST_ALARM_REPEATING_MAX 24 /* shared by all applications, not reserved per application */
HOST_IMPORT(alarm_set_every, int32_t alarm_set_every(int32_t id, int32_t seconds,
                                                     int32_t first_in));
APP_EXPORT(app_on_alarm, void app_on_alarm(int32_t id));

/* One-shot timer: app_on_timer(id) after `milliseconds`. 0 cancels.
 * Setting a running timer restarts it. id: 0..HOST_TIMER_COUNT-1.
 * Timers stop when the application leaves the screen. */
#define HOST_TIMER_COUNT 4
HOST_IMPORT(timer_set, int32_t timer_set(int32_t id, int32_t milliseconds));

/* A timer set with timer_set() has expired. */
APP_EXPORT(app_on_timer, void app_on_timer(int32_t id));

/* ---- Time ---- */

/* Returns Unix time in seconds; 0 while the device does not know the time.
 *
 * To measure time (a countdown, a stopwatch), keep the start time in the
 * store and compute the rest on demand. The application is not loaded
 * most of the time and cannot count ticks.
 * The clock runs through device restarts and sleep, but not through a
 * power loss. It is 0 until the network or the user sets it for the first
 * time. Until then use time_uptime_ms(), which also runs through sleep
 * and starts from 0 at every restart. */
HOST_IMPORT(time_now, int64_t time_now(void));

/* Returns the offset of local time from UTC in seconds, daylight saving
 * included; 0 while the time is unknown. */
HOST_IMPORT(time_zone_offset, int32_t time_zone_offset(void));

/* Fills the current local date and time. Returns HOST_ERR_NOT_FOUND while
 * the time is unknown. */
typedef struct {
    int32_t year;    /* 2026 */
    int32_t month;   /* 1..12 */
    int32_t day;     /* 1..31 */
    int32_t weekday; /* 0 is Sunday */
    int32_t hour;    /* 0..23 */
    int32_t minute;
    int32_t second;
} host_local_time_t;
HOST_LAYOUT(host_local_time_t, 28);
HOST_IMPORT(time_local, int32_t time_local(host_local_time_t *now));

/* Converts a moment (Unix seconds) to local date and time, using the zone
 * offset in effect at that moment, daylight saving included.
 * Events with their own time zone must be converted before they reach the
 * application. */
HOST_IMPORT(time_local_at, int32_t time_local_at(int64_t unix_time, host_local_time_t *parts));

/* Formats a moment as text in the device language and local time. Writes
 * up to `capacity` bytes without a terminator, cut between UTF-8
 * characters. Returns the length copied. Capacity must be positive:
 * zero returns HOST_ERR_ARGUMENT. Errors leave text untouched.
 *   HOST_TIME_CLOCK      "15:04"
 *   HOST_TIME_DATE       "Fri 13 Nov"
 *   HOST_TIME_WEEKDAY    "Friday"
 *   HOST_TIME_LONG_DATE  "13 November"
 *   HOST_TIME_NEAR       relative: "15:04" today, "Tmrw 15:04",
 *                        "Sat 15:04" within a week, "21 Nov" beyond
 *   HOST_TIME_NEAR_DAY   relative, date only: "Today", "Tmrw", "Sat", "21 Nov"
 *   HOST_TIME_WEEKDAY_SHORT "Fri" */
#define HOST_TIME_CLOCK     0
#define HOST_TIME_DATE      1
#define HOST_TIME_WEEKDAY   2
#define HOST_TIME_LONG_DATE 3
#define HOST_TIME_NEAR      4
#define HOST_TIME_NEAR_DAY  5
#define HOST_TIME_WEEKDAY_SHORT 6
HOST_IMPORT(time_text, int32_t time_text(int64_t unix_time, int32_t style, char *text,
                                         int32_t capacity));

/* Converts a local date and time to Unix time, for example for
 * alarm_set_at(). The weekday field is ignored. Out-of-range values carry
 * over: day 32 of January is 1 February. A nonexistent local time in a
 * forward clock change moves forward by the gap: 02:30 becomes 03:30
 * for a one-hour change. A repeated local time uses its earlier occurrence.
 * Returns HOST_OK and writes the moment to unix_time, or HOST_ERR_ARGUMENT
 * for an invalid pointer or a normalized moment outside the supported range.
 * On error the output is untouched. */
HOST_IMPORT(time_from_local, int32_t time_from_local(const host_local_time_t *parts,
                                                    int64_t *unix_time));

/* Returns milliseconds since the device started. Always available. */
HOST_IMPORT(time_uptime_ms, int64_t time_uptime_ms(void));

/* Returns a randomly generated nonzero 32-bit identifier, generated once
 * per boot. Equality is a practical check, not a guarantee that no restart
 * occurred. A stored time_uptime_ms() value is valid only within
 * the boot it was taken in. Store this identifier with it and discard the
 * value when the identifier differs. */
HOST_IMPORT(time_boot_id, uint32_t time_boot_id(void));
