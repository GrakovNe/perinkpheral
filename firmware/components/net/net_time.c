/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "net_time.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_sntp.h"

/* A clock that was never set counts from 1970; anything before this is
 * that, not the time. */
#define EARLIEST_REAL_TIME 1767225600 /* 2026-01-01 00:00:00 UTC */

#define SERVER_NAME_MAX 64

void net_time_set_zone(const char *time_zone)
{
    setenv("TZ", time_zone, 1);
    tzset();
}

void net_time_start(const char *ntp_server, const char *time_zone)
{
    /* SNTP keeps the pointer, not the name. */
    static char server[SERVER_NAME_MAX + 1];
    strlcpy(server, ntp_server, sizeof server);

    net_time_set_zone(time_zone);
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    esp_sntp_init();
}

void net_time_sync_now(void)
{
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
