/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Manifest limits. The single definition: the host validates manifests
 * against these values, and tools/mkapp parses this file to apply the
 * same checks at build time. For that reason every value is a plain
 * number or a product of numbers, one definition per line.
 */
#pragma once

/* The name is the application folder, the catalog key and, with a prefix,
 * the namespace of the store. The store namespace sets the limit. */
#define MANIFEST_NAME_MAX    13
/* User-visible text is limited in characters. The byte limit is twice the
 * character limit: European alphabets use at most two bytes per character
 * in UTF-8. */
#define MANIFEST_TEXT_BYTES_PER_CHARACTER 2
#define MANIFEST_TITLE_MAX   23
#define MANIFEST_VERSION_MAX 15

/* Polls. */
#define MANIFEST_POLLS_MAX        4
#define MANIFEST_POLL_EVERY_MIN_S 10
#define MANIFEST_POLL_EVERY_MAX_S (24 * 60 * 60)
#define MANIFEST_URL_MAX          511 /* fits a secret calendar address */
#define MANIFEST_HEADERS_MAX      4
#define MANIFEST_HEADER_NAME_MAX  31
#define MANIFEST_HEADER_VALUE_MAX 255 /* fits "Bearer " and a 200-character token */
#define MANIFEST_PICK_PATHS_MAX   16
#define MANIFEST_PICK_PATH_MAX    63

/* Icon: number of rows, and of characters per row. */
#define MANIFEST_ICON_CELLS 11

/* Hosts allowed for requests from code. */
#define MANIFEST_HOSTS_MAX 4
#define MANIFEST_HOST_MAX  63

/* Settings. */
#define MANIFEST_SETTINGS_MAX        8
#define MANIFEST_SETTING_KEY_MAX     15 /* a store key */
#define MANIFEST_SETTING_TITLE_MAX   31
#define MANIFEST_SETTING_HELP_MAX    95
#define MANIFEST_SETTING_VALUE_MAX   255
#define MANIFEST_SETTING_OPTIONS_MAX 8
#define MANIFEST_SETTING_OPTION_MAX  23

/* Module size. The code slot is 52 KB. Code that actually runs is limited
 * further by the interpreter memory: about four bytes per byte of code. */
#define MANIFEST_MODULE_MAX_BYTES (48 * 1024)

/* File name length, and the total size of all files of one application.
 * The total is counted as stored on the device: every file is rounded up
 * to whole blocks. With this accounting, six applications at the limit
 * plus one update in progress always fit the files partition. */
#define MANIFEST_FILE_NAME_MAX  23
#define MANIFEST_FILES_MAX_BYTES (64 * 1024)
#define MANIFEST_FILE_BLOCK_BYTES 4096
