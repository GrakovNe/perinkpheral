/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A handler declared with more arguments than the host passes: the
 * interpreter would read the others from wherever they are not.
 */
#include <stdint.h>

static volatile int32_t seen;

__attribute__((export_name("app_on_timer"))) void app_on_timer(int32_t id, int32_t extra, int32_t more)
{
    seen = id + extra + more;
}
