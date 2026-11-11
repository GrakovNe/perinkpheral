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

/* The wall clock: set from the network (SNTP) whenever there is one, in
 * the time zone the user chose. Until the first answer the device does
 * not know the time and says so rather than show 1970. */

/* time_zone is a POSIX TZ string, "CET-1CEST,M3.5.0,M10.5.0/3". */
void net_time_start(const char *ntp_server, const char *time_zone);

/* From now on local time is read in this zone. */
void net_time_set_zone(const char *time_zone);

/* The network just came up: ask now, not when the next poll is due. */
void net_time_sync_now(void);

bool net_time_is_known(void);

/* Unix seconds; 0 while the time is not known. */
int64_t net_time_now(void);

/* Seconds to add to UTC for local time now; 0 while the time is not known. */
int32_t net_time_zone_offset(void);

#ifdef __cplusplus
}
#endif
