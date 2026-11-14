/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The glyph packs on the device: letters beyond ASCII for its fonts (see
 * gfx.h), in a partition of their own, used where they lie. A pack is
 * whole sectors of it; several packs stand side by side. The user adds and
 * removes them from the page. */

#define GLYPHS_PACKS_MAX     8
#define GLYPHS_PACK_MAX_BYTES (32 * 1024)

typedef struct {
    char   name[GFX_PACK_NAME_MAX + 1];
    size_t bytes;
} glyphs_pack_t;

/* Maps the partition and gives the fonts every pack in it. Once, before
 * anything is drawn. A device without the partition has no packs. */
void glyphs_start(void);

/* The packs there are; returns how many. Safe from any task. */
int glyphs_list(glyphs_pack_t packs[GLYPHS_PACKS_MAX]);

/* The room left for more, in bytes, in whole sectors. */
size_t glyphs_free_bytes(void);

/* Puts a pack in, in place of one of the same name. `pack` is the whole
 * pack in memory at an address that is a multiple of 4. In the host loop:
 * the fonts are its. False: not a pack, or no room. */
bool glyphs_install(const uint8_t *pack, size_t size);

/* Takes a pack out; false when there is none of the name. In the host loop. */
bool glyphs_remove(const char *name);

#ifdef __cplusplus
}
#endif
