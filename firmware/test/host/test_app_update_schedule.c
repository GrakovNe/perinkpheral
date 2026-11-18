/* SPDX-License-Identifier: MIT */
#include "app_update_schedule.h"
#include "tinytest.h"

int main(void)
{
    uint32_t now = APP_UPDATE_CLOCK_FIRST + 3 * 86400;
    CHECK(app_update_wait_ms(now, 0) == APP_UPDATE_FIRST_MS);
    CHECK(app_update_wait_ms(now, now) == APP_UPDATE_DAY_MS);
    CHECK(app_update_wait_ms(now, now - 3600) == 23LL * 3600 * 1000);
    CHECK(app_update_wait_ms(now, now - 86400) == APP_UPDATE_FIRST_MS);
    CHECK(app_update_wait_ms(now, now - 2 * 86400) == APP_UPDATE_FIRST_MS);
    CHECK(app_update_wait_ms(now, now + 1) == APP_UPDATE_DAY_MS);
    CHECK(app_update_wait_ms(0, now) == APP_UPDATE_DAY_MS);
    CHECK(app_update_wait_ms(0, 1) == APP_UPDATE_DAY_MS);
    CHECK(app_update_wait_ms(0, 0) == APP_UPDATE_FIRST_MS);
    REPORT();
}
