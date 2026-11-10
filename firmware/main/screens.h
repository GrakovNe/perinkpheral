/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The host's own screens, drawn into a framebuffer. Pure: no ESP-IDF, so
 * tools/preview can render them on the host. */

/* The brand splash. */
void screens_splash(gfx_t *g);

/* The bench demo: four counters, one per button. */
void screens_counters(gfx_t *g, const uint32_t counts[4]);

#ifdef __cplusplus
}
#endif
