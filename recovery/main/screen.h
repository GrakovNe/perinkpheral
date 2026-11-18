// SPDX-License-Identifier: MIT
#pragma once
#include "control.h"
#include "gfx.h"

void recovery_screen_draw(gfx_t *graphics, recovery_state_t state, const char *version,
                          const char *address, const char *access_point);
