// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

/* Optional UI hint for recovery protocol 1. IDF keeps a 32-byte boot record
 * at the start of each 4096-byte otadata sector. This record is outside both;
 * selecting factory erases it, and it never participates in boot selection. */
enum { OTA_HANDOFF_OFFSET = 64, OTA_HANDOFF_BYTES = 8 };
#define OTA_HANDOFF_MAGIC UINT32_C(0x50494e4b)

typedef struct { uint32_t magic, check; } ota_handoff_t;
_Static_assert(sizeof(ota_handoff_t) == OTA_HANDOFF_BYTES, "handoff layout changed");
