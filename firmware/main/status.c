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
#include "words.h"
#include "settings.h"
#include "net_time.h"
#include "poller.h"
#include "wifi.h"

#define MS_PER_SECOND        1000
#define DATE_FORMAT          "%a %d %b"
#define CLOCK_FORMAT         "%H:%M"
#define SECONDS_PER_MINUTE   60

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
    if (status->date_known) {
        time_t now = (time_t)net_time_now();
        struct tm local;
        localtime_r(&now, &local);
        if (shows_clock()) {
            strftime(status->date, sizeof status->date, CLOCK_FORMAT, &local);
        } else {
            /* "Fri 13 Nov", in the device's language. */
            snprintf(status->date, sizeof status->date, "%s %d %s",
                     word((word_t)(WORD_SUN + local.tm_wday)), local.tm_mday,
                     word((word_t)(WORD_JAN + local.tm_mon)));
        }
    }
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
    bool changed = memcmp(&now, &s_last_reported, sizeof now) != 0;
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
