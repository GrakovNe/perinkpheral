/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "refresh_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The one framebuffer, the panel and the refresh policy. Draw into
 * display_gfx(), then present with the reason; the policy decides
 * whether and how. A keep-alive task does the daily refresh. */
void display_start(void);

gfx_t *display_gfx(void);

/* True when the panel was refreshed. False: too soon for this reason;
 * keep the frame dirty and try again later. */
bool display_present(refresh_reason_t why);

#ifdef __cplusplus
}
#endif
