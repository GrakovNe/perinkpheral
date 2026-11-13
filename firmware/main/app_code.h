/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Where the code of installed applications is: a partition of slots, one
 * an application, mapped into memory read-only. A module is run from
 * there and never copied into RAM - as the system applications, which are
 * part of the firmware, already are. The host writes a slot when an
 * application is installed and forgets it when one is removed; nothing
 * else ever goes there. An application's manifest, files and store are
 * where they were. */

/* A slot is whole sectors of flash - it is erased alone - and as small as
 * holds the largest module (48 KB) after a few bytes that say whose it
 * is: 52 KB, six of them in the partition's 320 KB. They were 64 KB, one
 * page of the chip's memory mapping, and five, until 2026-11-13; the
 * partition is mapped whole, so a slot need not be a page. */
#define APP_CODE_SLOT_BYTES   0xD000
#define APP_CODE_MODULE_BYTES (APP_CODE_SLOT_BYTES - 32)

/* Once, before anything asks. Without the partition - a device flashed
 * for an older layout - nothing is ever found here, and applications run
 * from their files as they used to. */
void app_code_start(void);

/* The module of the application, where it is mapped; NULL when it has no
 * slot. The pointer is good until the application is stored again or
 * forgotten. */
const uint8_t *app_code_find(const char *app_name, size_t *size);

/* Takes into slots the modules that lie as files in the applications'
 * folders - a new installation, an upload, what the firmware's image
 * came with - and removes the files. One that finds no slot stays a file.
 * Only in the host loop: it erases and writes flash. */
void app_code_adopt(const char *apps_root);

/* A slot is erased when its application has no folder any more, the next
 * time app_code_adopt() runs - not while an instance may still be running
 * from it. This erases every slot: the applications' partition was
 * formatted. */
void app_code_erase_all(void);

#ifdef __cplusplus
}
#endif
