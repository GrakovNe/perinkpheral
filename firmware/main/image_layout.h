/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The partition table this firmware was built for, as a number: another
 * table, another number. Every image carries it (ota_layout_t), and an
 * update that carries another is refused. */
uint32_t image_layout_partition_table(void);

#ifdef __cplusplus
}
#endif
