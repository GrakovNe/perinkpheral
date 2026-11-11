/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The applications installed on the file system:
 *
 *   <root>/<name>/manifest.json
 *   <root>/<name>/app.wasm
 *
 * Plain files through the C library, so the same code reads the device's
 * LittleFS and a folder on the development machine under the host
 * tests. */

/* A module larger than this is not an application for a 64 KB device. */
#define APP_MODULE_MAX_BYTES (48 * 1024)

/* Fills `manifests` with every application under root that has a valid
 * manifest named like its folder, sorted by title, and returns how many;
 * the rest are passed over. Never more than `capacity`. */
int app_store_list(const char *root, app_manifest_t *manifests, int capacity);

/* The module of an installed application, in memory the caller frees.
 * NULL when it is missing, empty, too large, or there is no memory. */
uint8_t *app_store_read_module(const char *root, const char *name, size_t *size);

/* The manifest of an installed application as text, terminated, in
 * memory the caller frees; NULL when it cannot be read. For what the
 * parsed manifest does not keep: a poll in full, the settings. */
char *app_store_read_manifest(const char *root, const char *name);

/* The files of an application: the two it is made of, and whatever data
 * it brings along or keeps - pictures, a cache. */
#define APP_FILE_MANIFEST "manifest.json"
#define APP_FILE_MODULE   "app.wasm"

/* A data file is named with 1-23 of a-z, 0-9, '.', '-' and '_', not
 * starting with a dot. */
#define APP_FILE_NAME_MAX 23
/* All the data files of one application may take this much. */
#define APP_FILES_MAX_BYTES (100 * 1024)

/* True for the manifest, the module and any well-named data file. */
bool app_store_is_file_name(const char *file);

/* The most bytes a file of that name may have. */
size_t app_store_file_max_bytes(const char *file);

#define APP_STORE_PATH_MAX 128

/* Installing a file is two steps, so that an upload cut short leaves the
 * file that was there: write the new content to the path that
 * app_store_begin_file() gives, then app_store_commit_file() puts it in
 * place. Both are false for a name that is not an application's or not a
 * file's, and when the file system refuses. The application's folder is
 * made if it is not there. */
bool app_store_begin_file(const char *root, const char *name, const char *file,
                          char path[APP_STORE_PATH_MAX]);
bool app_store_commit_file(const char *root, const char *name, const char *file);

typedef enum {
    APP_WRITE_OK,
    APP_WRITE_REFUSED,  /* not a data file's name, or an offset past its end */
    APP_WRITE_NO_SPACE, /* the application's data files would take more than their share */
    APP_WRITE_FAILED,   /* the file system refused */
} app_write_result_t;

/* Writes into a data file of an application - not its manifest and not
 * its module - at `offset`, which is inside the file or at its end.
 * length 0 at offset 0 removes the file. */
app_write_result_t app_store_write_file(const char *root, const char *name, const char *file,
                                        long offset, const void *bytes, size_t length);

/* Takes an application off the device: its files and its folder. False
 * when the name is not an application's or something could not go. */
bool app_store_remove(const char *root, const char *name);

/* How long a file of an application is; negative when there is none. */
long app_store_file_size(const char *root, const char *name, const char *file);

/* Up to `capacity` bytes of it from `offset` on; fewer at its end, none
 * past it, negative when it cannot be read. */
long app_store_read_file(const char *root, const char *name, const char *file, long offset,
                         void *bytes, size_t capacity);

#ifdef __cplusplus
}
#endif
