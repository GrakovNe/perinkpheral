// SPDX-License-Identifier: MIT
#include "tinytest.h"
#include "../../../recovery/main/screen.h"

enum { WIDTH = 400, HEIGHT = 300, FRAME_BYTES = WIDTH * HEIGHT / 8 };
static uint8_t prepared[FRAME_BYTES], actual[FRAME_BYTES];

static void test_entry_reason_and_network(void)
{
    CHECK(recovery_screen_wait(true, true, false, 0) == RECOVERY_WAIT_PREPARING);
    CHECK(recovery_screen_wait(true, true, true, 5) == RECOVERY_WAIT_PREPARING);
    CHECK(recovery_screen_wait(true, true, true, RECOVERY_NETWORK_HELP_SECONDS - 1) ==
          RECOVERY_WAIT_PREPARING);
    CHECK(recovery_screen_wait(true, true, true, RECOVERY_NETWORK_HELP_SECONDS) ==
          RECOVERY_WAIT_NETWORK);
    CHECK(recovery_screen_wait(true, true, false, RECOVERY_NETWORK_HELP_SECONDS) ==
          RECOVERY_WAIT_NETWORK);
    CHECK(recovery_screen_wait(true, false, false, 0) == RECOVERY_WAIT_NETWORK);

    /* A failed update or manual entry never pretends another update has begun. */
    CHECK(recovery_screen_wait(false, true, false, 0) == RECOVERY_WAIT_CONNECTING);
    CHECK(recovery_screen_wait(false, true, true, 2) == RECOVERY_WAIT_NETWORK);
    CHECK(recovery_screen_wait(false, false, false, 0) == RECOVERY_WAIT_NETWORK);
    CHECK(recovery_screen_wait(false, true, false, RECOVERY_NETWORK_HELP_SECONDS) ==
          RECOVERY_WAIT_NETWORK);
}

static void test_update_does_not_flash_network_instructions(void)
{
    gfx_t graphics;
    gfx_init(&graphics, prepared, WIDTH, HEIGHT);
    recovery_screen_draw(&graphics, WAITING, "", "", "perink-recovery", RECOVERY_WAIT_PREPARING);
    gfx_init(&graphics, actual, WIDTH, HEIGHT);
    recovery_screen_draw(&graphics, WAITING, "", "192.168.1.70", "perink-recovery",
                         RECOVERY_WAIT_PREPARING);
    CHECK(memcmp(prepared, actual, FRAME_BYTES) == 0);
    recovery_screen_draw(&graphics, RECEIVING, "", "192.168.1.70", "perink-recovery",
                         RECOVERY_WAIT_NETWORK);
    CHECK(memcmp(prepared, actual, FRAME_BYTES) == 0);
    recovery_screen_draw(&graphics, CONFIRM, "0.1.38", "192.168.1.70", "perink-recovery",
                         RECOVERY_WAIT_PREPARING);
    CHECK(memcmp(prepared, actual, FRAME_BYTES) != 0);
    recovery_screen_draw(&graphics, FAILED, "", "192.168.1.70", "perink-recovery",
                         RECOVERY_WAIT_PREPARING);
    CHECK(memcmp(prepared, actual, FRAME_BYTES) != 0);
    recovery_screen_draw(&graphics, WAITING, "", "", "perink-recovery", RECOVERY_WAIT_CONNECTING);
    CHECK(memcmp(prepared, actual, FRAME_BYTES) != 0);
}

int main(void)
{
    test_entry_reason_and_network();
    test_update_does_not_flash_network_instructions();
    REPORT();
}
