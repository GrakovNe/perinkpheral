/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Bench demo: a splash, then four counters, one per button. Never
 * returns. Goes away once there are real applications. */
void demo_run(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
