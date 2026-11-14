/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * What is promised of the flash, held against the partition table
 * (partitions.csv, as PARTITION_<NAME>_BYTES from the build): a number
 * changed on either side that breaks a promise does not compile. No code.
 */
#include "app_code.h"
#include "catalog.h"
#include "glyphs.h"
#include "host_api.h"
#include "manifest_limits.h"

#define BLOCK MANIFEST_FILE_BLOCK_BYTES

/* ---- firmware: two slots, so that an update has where to go ---- */
_Static_assert(PARTITION_OTA_0_BYTES == PARTITION_OTA_1_BYTES, "the two firmware slots are one size");

/* ---- appcode: a slot of code for every installed application ---- */
_Static_assert(CATALOG_INSTALLED_MAX * APP_CODE_SLOT_BYTES <= PARTITION_APPCODE_BYTES,
               "every installed application has a slot of code");
_Static_assert(MANIFEST_MODULE_MAX_BYTES <= APP_CODE_MODULE_BYTES,
               "the largest module the build allows fits a slot");

/* ---- apps (LittleFS): the files of all applications at once ----
 * An installed application: its files, its folder, its manifest. One more
 * on the way in over its old version: its files, its module until it is
 * moved to a slot, and what the step keeps. The rest is the file
 * system's to work in. */
#define BLOCKS_OF(bytes)        (((bytes) + BLOCK - 1) / BLOCK)
#define APP_FILES_BLOCKS        BLOCKS_OF(MANIFEST_FILES_MAX_BYTES)
#define APP_FOLDER_BLOCKS       2
#define APP_MANIFEST_BLOCKS     1
#define ROOT_BLOCKS             2
#define INSTALL_IN_HAND_BLOCKS  (APP_FILES_BLOCKS + BLOCKS_OF(MANIFEST_MODULE_MAX_BYTES) + 3)
#define FILE_SYSTEM_OWN_BLOCKS  13
#define PROMISED_BLOCKS                                                                   \
    (CATALOG_INSTALLED_MAX * (APP_FILES_BLOCKS + APP_FOLDER_BLOCKS + APP_MANIFEST_BLOCKS) + \
     ROOT_BLOCKS + INSTALL_IN_HAND_BLOCKS + FILE_SYSTEM_OWN_BLOCKS)
_Static_assert(PROMISED_BLOCKS <= PARTITION_APPS_BYTES / BLOCK,
               "the files promised to every application, and an update in hand, fit the partition");

/* ---- appkv (NVS): every application's store at its fullest ----
 * A page of the storage holds 126 entries of 32 bytes, and one page is
 * kept free for its own housekeeping. */
#define NVS_PAGE_BYTES        4096
#define NVS_PAGE_USABLE_BYTES (126 * 32)
_Static_assert(CATALOG_INSTALLED_MAX * HOST_KV_TOTAL_MAX <=
                   (PARTITION_APPKV_BYTES / NVS_PAGE_BYTES - 1) * NVS_PAGE_USABLE_BYTES,
               "the stores of all installed applications, full, fit the partition");

/* ---- glyphs: packs in whole sectors ---- */
_Static_assert(GLYPHS_PACK_MAX_BYTES <= PARTITION_GLYPHS_BYTES, "the largest pack fits the partition");
