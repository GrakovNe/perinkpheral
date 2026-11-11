/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_store.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MANIFEST_FILE      "manifest.json"
#define MODULE_FILE        "app.wasm"
#define MANIFEST_MAX_BYTES (8 * 1024)
#define PATH_MAX_BYTES     APP_STORE_PATH_MAX
#define INCOMING_SUFFIX    ".new"
#define FOLDER_MODE        0755

/* The whole file plus a terminator, in memory the caller frees. NULL when
 * it cannot be opened, is empty, is larger than max_bytes, or there is no
 * memory. */
static uint8_t *read_file(const char *path, size_t max_bytes, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    uint8_t *content = NULL;
    if (fseek(file, 0, SEEK_END) == 0) {
        long length = ftell(file);
        if (length > 0 && (size_t)length <= max_bytes && fseek(file, 0, SEEK_SET) == 0) {
            content = malloc((size_t)length + 1);
            if (content != NULL && fread(content, 1, (size_t)length, file) != (size_t)length) {
                free(content);
                content = NULL;
            }
            if (content != NULL) {
                content[length] = '\0';
                *size = (size_t)length;
            }
        }
    }
    fclose(file);
    return content;
}

static bool path_of(char *path, const char *root, const char *name, const char *file)
{
    int written = snprintf(path, PATH_MAX_BYTES, "%s/%s/%s", root, name, file);
    return written > 0 && written < PATH_MAX_BYTES;
}

static bool read_manifest(const char *root, const char *folder, app_manifest_t *manifest)
{
    char path[PATH_MAX_BYTES];
    size_t size;
    uint8_t *json = path_of(path, root, folder, MANIFEST_FILE)
                        ? read_file(path, MANIFEST_MAX_BYTES, &size)
                        : NULL;
    if (json == NULL) {
        return false;
    }
    bool valid = app_manifest_parse((const char *)json, manifest) == APP_MANIFEST_OK &&
                 /* Or two folders could claim one name and one set of settings. */
                 strcmp(manifest->name, folder) == 0;
    free(json);
    return valid;
}

static int by_title(const void *left, const void *right)
{
    return strcmp(((const app_manifest_t *)left)->title, ((const app_manifest_t *)right)->title);
}

int app_store_list(const char *root, app_manifest_t *manifests, int capacity)
{
    DIR *directory = opendir(root);
    if (directory == NULL) {
        return 0;
    }
    int count = 0;
    struct dirent *entry;
    while (count < capacity && (entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] != '.' && read_manifest(root, entry->d_name, &manifests[count])) {
            count++;
        }
    }
    closedir(directory);
    qsort(manifests, (size_t)count, sizeof manifests[0], by_title);
    return count;
}

uint8_t *app_store_read_module(const char *root, const char *name, size_t *size)
{
    char path[PATH_MAX_BYTES];
    return path_of(path, root, name, MODULE_FILE) ? read_file(path, APP_MODULE_MAX_BYTES, size)
                                                  : NULL;
}

char *app_store_read_manifest(const char *root, const char *name)
{
    char   path[PATH_MAX_BYTES];
    size_t size;
    if (!app_manifest_is_valid_name(name) || !path_of(path, root, name, MANIFEST_FILE)) {
        return NULL;
    }
    return (char *)read_file(path, MANIFEST_MAX_BYTES, &size);
}

size_t app_store_file_max_bytes(app_file_t file)
{
    return file == APP_FILE_MANIFEST ? MANIFEST_MAX_BYTES : APP_MODULE_MAX_BYTES;
}

static const char *file_name(app_file_t file)
{
    return file == APP_FILE_MANIFEST ? MANIFEST_FILE : MODULE_FILE;
}

static bool incoming_path_of(char *path, const char *root, const char *name, app_file_t file)
{
    int written = snprintf(path, PATH_MAX_BYTES, "%s/%s/%s" INCOMING_SUFFIX, root, name,
                           file_name(file));
    return written > 0 && written < PATH_MAX_BYTES;
}

bool app_store_begin_file(const char *root, const char *name, app_file_t file,
                          char path[APP_STORE_PATH_MAX])
{
    char folder[PATH_MAX_BYTES];
    if (!app_manifest_is_valid_name(name) ||
        snprintf(folder, sizeof folder, "%s/%s", root, name) >= (int)sizeof folder) {
        return false;
    }
    struct stat found;
    bool folder_is_there = stat(folder, &found) == 0 || mkdir(folder, FOLDER_MODE) == 0;
    return folder_is_there && incoming_path_of(path, root, name, file);
}

bool app_store_commit_file(const char *root, const char *name, app_file_t file)
{
    char incoming[PATH_MAX_BYTES], installed[PATH_MAX_BYTES];
    if (!app_manifest_is_valid_name(name) || !incoming_path_of(incoming, root, name, file) ||
        !path_of(installed, root, name, file_name(file))) {
        return false;
    }
    return rename(incoming, installed) == 0;
}
