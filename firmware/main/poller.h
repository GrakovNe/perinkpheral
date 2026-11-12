/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app.h"
#include "app_delivery.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The background mechanism: the host itself fetches every poll of every
 * installed application when it is due, one request at a time, and hands
 * the answer to the host loop for the application.
 *
 * A poll that names what it wants ("pick") is read to the end, the
 * connection is closed, and the application gets the values in one call.
 * Any other answer goes to the application as it comes, a kilobyte at a
 * time, while the connection is open: the host never holds it whole. */

/* Reads the polls of the installed applications and starts the task.
 * After launcher_start(). */
void poller_start(void);

/* Whether the last time the network was needed it was not there; and the
 * network saying it is there again, which also makes every poll due. For
 * the header's icon. */
bool poller_last_use_failed(void);
void poller_network_is_back(void);

/* The installed applications are about to change: nothing of the poller
 * looks at them between these two. The second takes the new list:
 * what was scheduled, asked or set for an application that is still
 * installed stays, what was for one that is gone goes, and the polls of a
 * new one are scheduled. */
void poller_hold(void);
void poller_release_reloaded(void);

/* The power profile changed: the least distance between polls is another.
 * Nothing else of the schedule is touched. */
void poller_reschedule(void);

/* The application's settings changed: fetch its polls again now, not
 * when their time comes. From any task. */
void poller_hurry(const char *app_name);

/* One poll of an application now; false when it has no such poll. From
 * any task. */
bool poller_poll_now(const char *app_name, int poll);

/* Queues a request an application makes itself, to be answered under the
 * tag it carries: HOST_OK or a HOST_ERR_*. */
int32_t poller_request(const char *app_name, const app_request_t *request);

/* Another pace for a poll than its manifest says, until the device
 * restarts. False when there is no such poll. */
bool poller_set_interval(const char *app_name, int poll, uint32_t every_s);

/* The application's alarm numbered `alarm`: it is called once when the
 * time comes, on the screen or off it. `when` is seconds from now, 0
 * taking the alarm back, or - `is_a_time` - a moment in unix seconds.
 * False when there is no room for one more. Kept in memory: a restart
 * forgets it. */
bool poller_alarm_set(const char *app_name, int alarm, int64_t when, bool is_a_time);

/* The wall clock was set `delta_ms` ahead: alarms for a time of day follow. */
void poller_clock_moved(int64_t delta_ms);

/* Fetches `url` on the poller's task - the one with the stack for TLS -
 * and hands the answer to `take` piece by piece, there. Waits until it is
 * done. `take` returns false to stop. False when the request could not be
 * made, broke off, was stopped, or the answer was longer than max_bytes;
 * *http_status is 0 unless the server answered. From any task but the
 * poller's own, one caller at a time. */
typedef bool (*poller_take_fn)(void *context, const uint8_t *piece, size_t length);
bool poller_fetch(const char *url, size_t max_bytes, poller_take_fn take, void *context,
                  int *http_status);

#ifdef __cplusplus
}
#endif
