/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Sets aside the memory of an application - one page, 64 KB - for as long
 * as the device runs. Call it early, while the heap is whole. */
void m3_reserve(void);

#ifdef __cplusplus
}
#endif
