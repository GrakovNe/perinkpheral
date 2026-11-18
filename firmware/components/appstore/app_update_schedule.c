/* SPDX-License-Identifier: MIT */
#include "app_update_schedule.h"

int64_t app_update_wait_ms(uint32_t now_seconds, uint32_t last_seconds)
{
    if (last_seconds == 0) return APP_UPDATE_FIRST_MS;
    if (now_seconds < APP_UPDATE_CLOCK_FIRST || last_seconds < APP_UPDATE_CLOCK_FIRST || now_seconds < last_seconds)
        return APP_UPDATE_DAY_MS;
    int64_t remaining = APP_UPDATE_DAY_MS - (int64_t)(now_seconds - last_seconds) * 1000;
    return remaining > APP_UPDATE_FIRST_MS ? remaining : APP_UPDATE_FIRST_MS;
}
