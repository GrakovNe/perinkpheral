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

/* The two files an application is made of. */
typedef enum {
    APP_FILE_MANIFEST,
    APP_FILE_MODULE,
} app_file_t;

/* The most bytes a file of that kind may have. */
size_t app_store_file_max_bytes(app_file_t file);

#define APP_STORE_PATH_MAX 128

/* Installing a file is two steps, so that an upload cut short leaves the
 * file that was there: write the new content to the path that
 * app_store_begin_file() gives, then app_store_commit_file() puts it in
 * place. Both are false for a name that is not an application's, and when
 * the file system refuses. The application's folder is made if it is not
 * there. */
bool app_store_begin_file(const char *root, const char *name, app_file_t file,
                          char path[APP_STORE_PATH_MAX]);
bool app_store_commit_file(const char *root, const char *name, app_file_t file);

#ifdef __cplusplus
}
#endif
