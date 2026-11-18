// SPDX-License-Identifier: MIT
#pragma once
#include "control.h"
#include "gfx.h"

enum { RECOVERY_NETWORK_HELP_SECONDS = 30 };

typedef enum {
    RECOVERY_WAIT_PREPARING, RECOVERY_WAIT_CONNECTING, RECOVERY_WAIT_NETWORK
} recovery_wait_t;

recovery_wait_t recovery_screen_wait(bool update_requested, bool saved_network, bool online,
                                    uint32_t uptime);

void recovery_screen_draw(gfx_t *graphics, recovery_state_t state, const char *version,
                          const char *address, const char *access_point, recovery_wait_t waiting);
