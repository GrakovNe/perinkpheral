/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * What is promised of the flash, held against the partition table
 * (partitions.csv, as PARTITION_<NAME>_BYTES from the build): a number
 * changed on either side that breaks a promise does not compile. No code.
 */
#include "app_code.h"
#include "app_room.h"
#include "catalog.h"
#include "glyphs.h"
#include "host_api.h"
#include "manifest_limits.h"

#define BLOCK MANIFEST_FILE_BLOCK_BYTES

/* ---- firmware: two slots, so that an update has where to go ---- */
_Static_assert(PARTITION_OTA_0_BYTES == 2 * 1024 * 1024, "main firmware has two MiB");

/* ---- appcode: modules in whole sectors, one piece each ----
 * No module is promised a place: an install looks for room. What is held
 * here is that the largest one the build allows fits at all. */
#define SECTOR_BYTES 4096
_Static_assert(APP_CODE_HEADER_BYTES + MANIFEST_MODULE_MAX_BYTES <= PARTITION_APPCODE_BYTES,
               "the largest module the build allows fits the partition");
_Static_assert(PARTITION_APPCODE_BYTES % SECTOR_BYTES == 0, "the partition is whole sectors");

/* ---- apps (LittleFS): the files ----
 * Only the room of an application that writes is promised, and an install
 * looks at it with the files on the device (web_apps.c): what is held here
 * is that the file system's spare and the largest package with a module
 * on its way to the code partition leave room for some. */
#define BLOCKS_OF(bytes) (((bytes) + BLOCK - 1) / BLOCK)
_Static_assert(BLOCKS_OF(APP_ROOM_FILES_SPARE_BYTES) + BLOCKS_OF(MANIFEST_FILES_MAX_BYTES) +
                       BLOCKS_OF(MANIFEST_MODULE_MAX_BYTES) + BLOCKS_OF(MANIFEST_FILES_WRITER_MAX_BYTES) <
                   PARTITION_APPS_BYTES / BLOCK,
               "the files partition holds an install in hand beside its spare and one writer's share");

/* ---- appkv (NVS): every application's store at its fullest ----
 * Counted in the storage's own entries of 32 bytes, as a store is charged
 * (app_kv.c): a page holds 126, and one page is kept free for its own
 * housekeeping. An application's store is its namespace's entry and its
 * values; the system applications keep a value or two beside them, under
 * namespaces of their own. What the host keeps for an application is in
 * the host's NVS, not here (settings.c). */
#define NVS_PAGE_BYTES        4096
#define NVS_ENTRY_BYTES       32
#define NVS_PAGE_ENTRIES      126
#define SYSTEM_APPS           3
#define SYSTEM_APPS_KV_BYTES  1024
_Static_assert(CATALOG_INSTALLED_MAX * (1 + HOST_KV_TOTAL_MAX / NVS_ENTRY_BYTES) + SYSTEM_APPS +
                       SYSTEM_APPS_KV_BYTES / NVS_ENTRY_BYTES <=
                   (PARTITION_APPKV_BYTES / NVS_PAGE_BYTES - 1) * NVS_PAGE_ENTRIES,
               "the stores of all installed applications, full, fit the partition");

/* ---- glyphs: packs in whole sectors ---- */
_Static_assert(GLYPHS_PACK_MAX_BYTES <= PARTITION_GLYPHS_BYTES, "the largest pack fits the partition");
