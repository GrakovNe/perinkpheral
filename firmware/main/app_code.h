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

/* Where the code of installed applications is: a partition mapped into
 * memory read-only, a module in one piece of whole sectors after a few
 * bytes that say whose it is. A module is run from there and never copied
 * into RAM - as the system applications, which are part of the firmware,
 * already are. The host writes a module when an application is installed
 * and forgets it when one is removed; nothing else ever goes there. An
 * application's manifest, files and store are where they were.
 *
 * Until 2026-11-16 the partition was six slots of 52 KB, one an
 * application; a slot began at a sector, so what they hold is found the
 * same way. */
#define APP_CODE_HEADER_BYTES 32 /* the mark, the module's size and name */

/* Once, before anything asks. Without the partition - a device flashed
 * for an older layout - nothing is ever found here, and applications run
 * from their files as they used to. */
void app_code_start(void);

/* The module of the application, where it is mapped; NULL when it has
 * none here. The pointer is good until the list of applications is read
 * anew: app_code_adopt() may move it. */
const uint8_t *app_code_find(const char *app_name, size_t *size);

/* Whether a module of so many bytes would find room, the one the
 * application has now given up for it: in one piece, or after the rest
 * were moved together. */
bool app_code_has_room(const char *app_name, size_t module_bytes);

/* Takes into the partition the modules that lie as files in the
 * applications' folders - a new installation, an upload, what the
 * firmware's image came with - and removes the files. One that finds no
 * room stays a file. Moves modules together when the room there is lies
 * in pieces. Only in the host loop, with no application loaded: it erases
 * and writes flash, and moves code. */
void app_code_adopt(const char *apps_root);

/* A module is forgotten when its application has no folder any more, the
 * next time app_code_adopt() runs - not while an instance may still be
 * running from it. This forgets every one: the applications' partition
 * was formatted. */
void app_code_erase_all(void);

#ifdef __cplusplus
}
#endif
