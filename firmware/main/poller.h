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

/* What the poller posts to the host loop (EVENT_APP_DELIVERY). It waits
 * until the host has handed it over: the data is the poller's and stays
 * where it is only that long. */
typedef struct {
    const char       *app_name;
    app_delivery_t    delivery;
    SemaphoreHandle_t handed_over;
} poller_delivery_t;

/* Reads the polls of the installed applications and starts the task.
 * After launcher_start(). */
void poller_start(void);

/* The application's settings changed: fetch its polls again now, not
 * when their time comes. From any task. */
void poller_hurry(const char *app_name);

/* One poll of an application now; false when it has no such poll. From
 * any task. */
bool poller_poll_now(const char *app_name, int poll);

/* Queues a request an application makes itself; the number its answer
 * will be delivered under, or a HOST_ERR_*. */
int32_t poller_request(const char *app_name, const app_request_t *request);

/* Another pace for a poll than its manifest says, until the device
 * restarts. False when there is no such poll. */
bool poller_set_interval(const char *app_name, int poll, uint32_t every_s);

/* Calls the application once, `seconds` from now, on the screen or off it;
 * 0 takes the alarm back. Kept in memory: a restart forgets it. */
bool poller_alarm_set(const char *app_name, uint32_t seconds);

/* Gives something to the host loop for an application and waits until
 * the application has had it. From any task but the host's own. */
void poller_hand_over(const char *app_name, const app_delivery_t *delivery);

#ifdef __cplusplus
}
#endif
