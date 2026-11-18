/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

#define APP_UPDATE_DAY_MS (24LL * 60 * 60 * 1000)
#define APP_UPDATE_FIRST_MS 30000
#define APP_UPDATE_CLOCK_FIRST 1577836800u /* 2020-01-01 */

/* Unknown time after an earlier attempt waits a full day; a first enable
 * checks soon. Clock rollback never turns into a tight retry loop. */
int64_t app_update_wait_ms(uint32_t now_seconds, uint32_t last_seconds);
