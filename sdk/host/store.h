/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * What an application keeps: the key-value store and its files.
 */
#pragma once

#include "base.h"

/* ---- Store ---- */

/* Key-value store for small persistent values. Each application has a
 * private store. Writing wears the flash memory: write when data has
 * changed, for example in app_on_terminate(), not on every timer tick.
 * Removal normally keeps the store for reinstall. The host may reclaim
 * removed applications' stores when the partition is full. Removal with
 * forget erases the store immediately. */
#define HOST_KV_KEY_MAX   15  /* 1..15 characters from '!' to '~' */
#define HOST_KV_VALUE_MAX 1024
#define HOST_KV_TOTAL_MAX 2048 /* all values of one application, its settings among them, as
                                  stored: 64 bytes for a value plus its length rounded up to a multiple of 32 bytes;
                                  guaranteed to every installed application */

/* Reads a value into `value`. Returns its length. A setting of the manifest
 * with nothing stored reads as its "default", a list's items with line
 * breaks between them. */
HOST_IMPORT(kv_get, int32_t kv_get(const char *key, int32_t key_length, void *value,
                                   int32_t capacity));

/* Writes a value. Length 0 deletes the key. Keys declared as "url" or
 * "link" in the manifest belong to the user: kv_set() returns
 * HOST_ERR_ARGUMENT for them, including deletion. Other keys need not be
 * declared in the manifest. kv_get() copies bytes without a terminator;
 * a missing key returns HOST_ERR_NOT_FOUND. Insufficient capacity returns
 * HOST_ERR_NO_SPACE without copying; capacity 0 can read an empty value. */
HOST_IMPORT(kv_set, int32_t kv_set(const char *key, int32_t key_length, const void *value,
                                   int32_t length));

/* ---- Files ---- */

/* Files hold data that does not fit in a store value: a picture, a table,
 * a cache. An application can read the files of its package (the `files`
 * folder at build time) and, with "writes_files": true in its manifest,
 * the files it has written.
 * Limits, counted as stored on the device, every file in whole 4 KB
 * blocks: HOST_FILES_MAX for the files of a package; HOST_FILES_WRITER_MAX
 * for all files of an application that writes, its package's included.
 * The room of an application that writes is guaranteed: the device keeps
 * it free for it from the moment it is installed.
 * Writing wears the flash memory, like kv_set().
 * File name: 1-23 characters of a-z, 0-9, '.', '-' and '_'; the first
 * character is not a dot. Names ending in .new are reserved.
 * file_write() also refuses app.wasm and manifest.json. A file supplied by
 * an update replaces an existing file of the same name, including one
 * written by the application. At catalog installation the writer quota
 * counts the manifest too; runtime writes count data files only. */
#define HOST_FILE_NAME_MAX    23
#define HOST_FILES_MAX        (64 * 1024)
#define HOST_FILES_WRITER_MAX (32 * 1024)

/* Returns the file size, or HOST_ERR_NOT_FOUND. */
HOST_IMPORT(file_size, int32_t file_size(const char *name, int32_t name_length));

/* Reads up to `capacity` bytes starting at `offset`. Returns the number
 * of bytes read: fewer at the end of the file, 0 past it. */
HOST_IMPORT(file_read, int32_t file_read(const char *name, int32_t name_length, int32_t offset,
                                         void *bytes, int32_t capacity));

/* Writes `length` bytes at `offset`. The offset must be inside the file
 * or at its end: a file is extended piece by piece, and existing data can
 * be overwritten. Length 0 at offset 0 deletes the file.
 * Only for an application whose manifest says "writes_files": true; the
 * build and the host refuse a module that imports this without it.
 * Returns HOST_ERR_NO_SPACE if the files of the application would exceed
 * HOST_FILES_WRITER_MAX. */
HOST_IMPORT(file_write, int32_t file_write(const char *name, int32_t name_length, int32_t offset,
                                           const void *bytes, int32_t length));
