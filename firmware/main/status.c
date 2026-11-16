/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "status.h"

#include <stdio.h>

#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "deadlines.h"
#include "events.h"
#include "settings.h"
#include "net_time.h"
#include "poller.h"
#include "wifi.h"
#include "units.h"

static SemaphoreHandle_t s_lock;
static status_t          s_last_reported; /* what the host was last told about */

/* The user may have the time where the date stands. */
static bool shows_clock(void)
{
    appconf_t settings;
    settings_read(&settings);
    return settings.header_clock != 0;
}

void status_read(status_t *status)
{
    memset(status, 0, sizeof *status);
    /* With the radio off between polls the icon says how the last attempt
     * went: that is all there is to know. */
    wifi_status_t wifi;
    wifi_get_status(&wifi);
    bool has_network = wifi.is_radio_off ? wifi.was_reached : wifi.phase == WIFI_PHASE_ONLINE;
    /* And being on a network is not yet having one that answers: the icon
     * is about the last time the network was needed. */
    status->wifi_connected = has_network && !poller_last_use_failed();
    status->date_known = net_time_is_known();
    status->shows_clock = shows_clock();
    if (status->date_known) {
        time_t now = (time_t)net_time_now();
        localtime_r(&now, &status->local);
    }
}

/* Whether the header says another thing: the network, the time known or
 * not, the clock or the date, and the minute or the day it shows. */
static bool header_differs(const status_t *one, const status_t *other)
{
    if (one->wifi_connected != other->wifi_connected || one->date_known != other->date_known ||
        one->shows_clock != other->shows_clock) {
        return true;
    }
    if (!one->date_known) {
        return false;
    }
    const struct tm *a = &one->local, *b = &other->local;
    bool same_day = a->tm_year == b->tm_year && a->tm_yday == b->tm_yday;
    return one->shows_clock ? !same_day || a->tm_hour != b->tm_hour || a->tm_min != b->tm_min
                            : !same_day;
}

/* The date turns at the next midnight of the user's zone: a moment of
 * the clock, which stays where it is when the clock is set. */
static void expect_next_day(void)
{
    time_t    now = time(NULL);
    struct tm local;
    if (shows_clock()) {
        /* The header is another at the turn of every minute. */
        deadlines_set_at(DEADLINE_DATE, 0, 0,
                         (int64_t)(now - now % SECONDS_PER_MINUTE + SECONDS_PER_MINUTE) *
                             MS_PER_SECOND);
        return;
    }
    localtime_r(&now, &local);
    local.tm_mday += 1;
    local.tm_hour = local.tm_min = local.tm_sec = 0;
    local.tm_isdst = -1; /* whatever it is then */
    deadlines_set_at(DEADLINE_DATE, 0, 0, (int64_t)mktime(&local) * MS_PER_SECOND);
}

void status_check(void)
{
    status_t now;
    status_read(&now);
    /* Whoever calls this may have set the clock or the zone. */
    expect_next_day();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = header_differs(&now, &s_last_reported);
    s_last_reported = now;
    xSemaphoreGive(s_lock);
    if (changed) {
        event_t event = { .kind = EVENT_STATUS_CHANGED };
        events_post(&event);
    }
}

void status_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);
    status_read(&s_last_reported);
    expect_next_day();
}
