/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "net_time.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_sntp.h"

#include "deadlines.h"

/* A clock that was never set counts from 1970; anything before this is
 * that, not the time. */
#define EARLIEST_REAL_TIME 1767225600 /* 2026-01-01 00:00:00 UTC */
#define LATEST_TIME        4102444800LL /* 2100-01-01 00:00:00 UTC: by hand, no later */

#define SERVER_NAME_MAX 64

void net_time_set_zone(const char *time_zone)
{
    setenv("TZ", time_zone, 1);
    tzset();
}

static void (*s_moved)(int64_t delta_ms);

void net_time_on_change(void (*moved)(int64_t delta_ms))
{
    s_moved = moved;
}

/* The one place the wall clock is set: whoever follows it is told how far
 * it went. */
static bool set_clock(const struct timeval *to)
{
    /* The deadlines live on this clock: they are moved with it. */
    int64_t moved_ms;
    if (!deadlines_set_clock(to, &moved_ms)) {
        return false;
    }
    if (s_moved != NULL) {
        s_moved(moved_ms);
    }
    return true;
}

/* What the SNTP client calls with the time it got: ours instead of the
 * library's, which sets the clock and tells nobody by how much. */
void sntp_sync_time(struct timeval *tv)
{
    set_clock(tv);
    sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
}

static bool s_automatic = true;

void net_time_set_automatic(bool automatic)
{
    if (automatic == s_automatic) {
        return;
    }
    s_automatic = automatic;
    if (automatic) {
        esp_sntp_init();
    } else {
        esp_sntp_stop();
    }
}

bool net_time_set(int64_t unix_seconds)
{
    if (s_automatic || unix_seconds < EARLIEST_REAL_TIME || unix_seconds > LATEST_TIME) {
        return false;
    }
    struct timeval now = { .tv_sec = (time_t)unix_seconds };
    return set_clock(&now);
}

void net_time_start(const char *ntp_server, const char *time_zone, bool automatic)
{
    /* SNTP keeps the pointer, not the name. */
    static char server[SERVER_NAME_MAX + 1];
    strlcpy(server, ntp_server, sizeof server);

    net_time_set_zone(time_zone);
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    s_automatic = automatic;
    if (automatic) {
        esp_sntp_init();
    }
}

void net_time_sync_now(void)
{
    if (!s_automatic) {
        return;
    }
    esp_sntp_restart();
}

int64_t net_time_now(void)
{
    time_t now = time(NULL);
    return now >= EARLIEST_REAL_TIME ? (int64_t)now : 0;
}

bool net_time_is_known(void)
{
    return net_time_now() != 0;
}

int32_t net_time_zone_offset(void)
{
    time_t now = (time_t)net_time_now();
    if (now == 0) {
        return 0;
    }
    /* The same instant read as local time and as UTC: the difference of
     * the two as calendar times is the offset. */
    struct tm local, utc;
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
    utc.tm_isdst = local.tm_isdst;
    return (int32_t)(mktime(&local) - mktime(&utc));
}
