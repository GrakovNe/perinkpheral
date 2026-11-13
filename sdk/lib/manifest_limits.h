/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * How large the things of a manifest may be. The one place they are
 * written: the host checks a manifest against these, and tools/mkapp
 * reads this file to check it the same way before the device sees it.
 * Plain numbers and products of them, one a line, for that reason.
 */
#pragma once

/* The name is the application's folder, its key in the catalog and, after
 * a prefix, the namespace of its store - which is what limits it. */
#define MANIFEST_NAME_MAX    13
/* Texts a person reads are counted in characters; in bytes they may be twice
 * that - every letter of Europe's alphabets is two bytes of UTF-8 at most. */
#define MANIFEST_TEXT_BYTES_PER_CHARACTER 2
#define MANIFEST_TITLE_MAX   23
#define MANIFEST_VERSION_MAX 15

/* Polls: what the host fetches for the application, and how often. */
#define MANIFEST_POLLS_MAX        4
#define MANIFEST_POLL_EVERY_MIN_S 10
#define MANIFEST_POLL_EVERY_MAX_S (24 * 60 * 60)
#define MANIFEST_URL_MAX          511 /* a calendar's secret address is long */
#define MANIFEST_HEADERS_MAX      4
#define MANIFEST_HEADER_NAME_MAX  31
#define MANIFEST_HEADER_VALUE_MAX 255 /* "Bearer " and a token of two hundred characters */
#define MANIFEST_PICK_PATHS_MAX   16
#define MANIFEST_PICK_PATH_MAX    63

/* The icon: so many rows of so many characters. */
#define MANIFEST_ICON_CELLS 11

/* The servers an application may ask itself. */
#define MANIFEST_HOSTS_MAX 4
#define MANIFEST_HOST_MAX  63

/* Settings, as the page draws them and the store keeps them. */
#define MANIFEST_SETTINGS_MAX        8
#define MANIFEST_SETTING_KEY_MAX     15 /* a key of the store */
#define MANIFEST_SETTING_TITLE_MAX   31
#define MANIFEST_SETTING_HELP_MAX    95
#define MANIFEST_SETTING_VALUE_MAX   255
#define MANIFEST_SETTING_OPTIONS_MAX 8
#define MANIFEST_SETTING_OPTION_MAX  23

/* The module: what fits when it runs is less than its slot of 52 KB - the
 * interpreter needs about two bytes of memory for a byte of code it runs. */
#define MANIFEST_MODULE_MAX_BYTES (48 * 1024)

/* The names of the files an application brings, and all of them together -
 * as they lie on the device, where a file takes whole blocks: each is
 * counted rounded up to one. That is what makes the room a promise: six
 * applications full of files, and one of them being installed anew over
 * itself, fit the files' partition whatever the files are. */
#define MANIFEST_FILE_NAME_MAX  23
#define MANIFEST_FILES_MAX_BYTES (64 * 1024)
#define MANIFEST_FILE_BLOCK_BYTES 4096
