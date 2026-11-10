/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* The host's built-in screens until there are applications: the splash,
 * then a menu (1 up, 3 down, 2 open; a long press on 4 from anywhere)
 * over the device page and the splash. Never
 * returns. */
void demo_run(const char *device_name) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
