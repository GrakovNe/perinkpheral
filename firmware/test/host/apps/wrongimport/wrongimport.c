/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A known host name with a wrong wasm result type.
 */
#include <stdint.h>

__attribute__((import_module("host"), import_name("gfx_width"))) int64_t wrong_width(void);
static volatile int64_t width;

__attribute__((export_name("app_on_draw"))) void app_on_draw(void)
{
    width = wrong_width();
}
