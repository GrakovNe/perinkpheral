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
#include <unistd.h>

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

char *app_store_read_translation(const char *root, const char *name, const char *language)
{
    char   file[APP_FILE_NAME_MAX + 1];
    char   path[PATH_MAX_BYTES];
    size_t size;
    if (!app_manifest_is_valid_name(name) ||
        snprintf(file, sizeof file, "manifest.%s", language) >= (int)sizeof file ||
        !app_store_is_file_name(file) || !path_of(path, root, name, file)) {
        return NULL;
    }
    return (char *)read_file(path, MANIFEST_MAX_BYTES, &size);
}

bool app_store_is_file_name(const char *file)
{
    size_t length = strlen(file);
    if (length == 0 || length > APP_FILE_NAME_MAX || file[0] == '.') {
        return false;
    }
    for (const char *character = file; *character != '\0'; character++) {
        bool allowed = (*character >= 'a' && *character <= 'z') ||
                       (*character >= '0' && *character <= '9') || *character == '.' ||
                       *character == '-' || *character == '_';
        if (!allowed) {
            return false;
        }
    }
    /* A name of an upload in progress is not a file's. */
    return length < sizeof INCOMING_SUFFIX ||
           strcmp(file + length - (sizeof INCOMING_SUFFIX - 1), INCOMING_SUFFIX) != 0;
}

size_t app_store_file_max_bytes(const char *file)
{
    if (strcmp(file, APP_FILE_MANIFEST) == 0) {
        return MANIFEST_MAX_BYTES;
    }
    return strcmp(file, APP_FILE_MODULE) == 0 ? APP_MODULE_MAX_BYTES : APP_FILES_MAX_BYTES;
}

static bool file_path_of(char *path, const char *root, const char *name, const char *file,
                         const char *suffix)
{
    if (!app_manifest_is_valid_name(name) || !app_store_is_file_name(file)) {
        return false;
    }
    int written = snprintf(path, PATH_MAX_BYTES, "%s/%s/%s%s", root, name, file, suffix);
    return written > 0 && written < PATH_MAX_BYTES;
}

bool app_store_begin_file(const char *root, const char *name, const char *file,
                          char path[APP_STORE_PATH_MAX])
{
    char folder[PATH_MAX_BYTES];
    if (!file_path_of(path, root, name, file, INCOMING_SUFFIX) ||
        snprintf(folder, sizeof folder, "%s/%s", root, name) >= (int)sizeof folder) {
        return false;
    }
    struct stat found;
    return stat(folder, &found) == 0 || mkdir(folder, FOLDER_MODE) == 0;
}

#define COMMIT_ATTEMPTS 10
#define COMMIT_RETRY_US 20000

static bool replace_with(const char *incoming, const char *installed);

bool app_store_commit_file(const char *root, const char *name, const char *file)
{
    char incoming[PATH_MAX_BYTES], installed[PATH_MAX_BYTES];
    if (!file_path_of(incoming, root, name, file, INCOMING_SUFFIX) ||
        !file_path_of(installed, root, name, file, "")) {
        return false;
    }
    /* The file it replaces may be open this very moment - the poller reads
     * a manifest for every request it makes - and a file that is open is
     * not replaced. It is read in a moment: tried again a few times. */
    return replace_with(incoming, installed);
}

/* ---- several files at once ----
 *
 * An application is several files, and putting them in place one by one
 * can stop half-way: a failure, the power. A mark is left before the first
 * and taken away after the last; a folder found with the mark at start is
 * one whose new files were all here and checked, and the rest of them are
 * put in place then. Without the mark, new files are what a download left
 * unfinished, and go. The mark's name is none a file may have. */
#define COMMIT_MARK ".commit"

static bool is_incoming(const char *file)
{
    size_t length = strlen(file);
    return length > sizeof INCOMING_SUFFIX - 1 &&
           strcmp(file + length - (sizeof INCOMING_SUFFIX - 1), INCOMING_SUFFIX) == 0;
}

/* The name of one new file in the folder; false when there is none. */
static bool find_incoming(const char *folder, char file[APP_STORE_PATH_MAX])
{
    DIR *directory = opendir(folder);
    if (directory == NULL) {
        return false;
    }
    bool           found = false;
    struct dirent *entry;
    while (!found && (entry = readdir(directory)) != NULL) {
        if (is_incoming(entry->d_name) && strlen(entry->d_name) < APP_STORE_PATH_MAX) {
            strcpy(file, entry->d_name);
            found = true;
        }
    }
    closedir(directory);
    return found;
}

static bool replace_with(const char *incoming, const char *installed)
{
    for (int attempt = 0; attempt < COMMIT_ATTEMPTS; attempt++) {
        if (rename(incoming, installed) == 0) {
            return true;
        }
        usleep(COMMIT_RETRY_US);
    }
    return false;
}

/* Every new file of the folder into its place, or - `keeps` false - away. */
static bool settle_incoming(const char *folder, bool keeps)
{
    char file[APP_STORE_PATH_MAX];
    while (find_incoming(folder, file)) {
        char incoming[2 * PATH_MAX_BYTES], installed[2 * PATH_MAX_BYTES];
        snprintf(incoming, sizeof incoming, "%s/%s", folder, file);
        snprintf(installed, sizeof installed, "%s/%.*s", folder,
                 (int)(strlen(file) - (sizeof INCOMING_SUFFIX - 1)), file);
        if (!(keeps ? replace_with(incoming, installed) : remove(incoming) == 0)) {
            return false;
        }
    }
    return true;
}

static bool folder_of(char folder[PATH_MAX_BYTES], char mark[PATH_MAX_BYTES + sizeof COMMIT_MARK],
                      const char *root, const char *name)
{
    if (!app_manifest_is_valid_name(name) ||
        snprintf(folder, PATH_MAX_BYTES, "%s/%s", root, name) >= PATH_MAX_BYTES) {
        return false;
    }
    snprintf(mark, PATH_MAX_BYTES + sizeof COMMIT_MARK, "%s/%s", folder, COMMIT_MARK);
    return true;
}

bool app_store_commit_all(const char *root, const char *name)
{
    char folder[PATH_MAX_BYTES], mark[PATH_MAX_BYTES + sizeof COMMIT_MARK];
    if (!folder_of(folder, mark, root, name)) {
        return false;
    }
    FILE *marked = fopen(mark, "wb");
    if (marked == NULL || fclose(marked) != 0) {
        return false;
    }
    /* Half-way and failed, the mark stays: the rest is done at the next start. */
    return settle_incoming(folder, true) && remove(mark) == 0;
}

bool app_store_drop_incoming(const char *root, const char *name)
{
    char folder[PATH_MAX_BYTES], mark[PATH_MAX_BYTES + sizeof COMMIT_MARK];
    return folder_of(folder, mark, root, name) && settle_incoming(folder, false);
}

void app_store_recover(const char *root)
{
    DIR *directory = opendir(root);
    if (directory == NULL) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        char folder[PATH_MAX_BYTES], mark[PATH_MAX_BYTES + sizeof COMMIT_MARK];
        if (!folder_of(folder, mark, root, entry->d_name)) {
            continue;
        }
        struct stat found;
        bool        was_being_put_in_place = stat(mark, &found) == 0;
        if (settle_incoming(folder, was_being_put_in_place) && was_being_put_in_place) {
            remove(mark);
        }
    }
    closedir(directory);
}

long app_store_file_size(const char *root, const char *name, const char *file)
{
    char        path[PATH_MAX_BYTES];
    struct stat found;
    if (!file_path_of(path, root, name, file, "") || stat(path, &found) != 0) {
        return -1;
    }
    return (long)found.st_size;
}

long app_store_read_file(const char *root, const char *name, const char *file, long offset,
                         void *bytes, size_t capacity)
{
    char path[PATH_MAX_BYTES];
    if (offset < 0 || !file_path_of(path, root, name, file, "")) {
        return -1;
    }
    FILE *opened = fopen(path, "rb");
    if (opened == NULL) {
        return -1;
    }
    long count = fseek(opened, offset, SEEK_SET) == 0 ? (long)fread(bytes, 1, capacity, opened)
                                                      : -1;
    fclose(opened);
    return count;
}

static bool is_data_file(const char *file)
{
    size_t length = strlen(file);
    bool is_incoming = length >= sizeof INCOMING_SUFFIX &&
                       strcmp(file + length - (sizeof INCOMING_SUFFIX - 1), INCOMING_SUFFIX) == 0;
    return strcmp(file, APP_FILE_MANIFEST) != 0 && strcmp(file, APP_FILE_MODULE) != 0 &&
           !is_incoming;
}

/* How much the application's data files take, the one named `except` not counted. */
static long data_bytes(const char *root, const char *name, const char *except)
{
    char folder[PATH_MAX_BYTES];
    snprintf(folder, sizeof folder, "%s/%s", root, name);
    DIR *directory = opendir(folder);
    if (directory == NULL) {
        return 0;
    }
    long total = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.' || !is_data_file(entry->d_name) ||
            strcmp(entry->d_name, except) == 0) {
            continue;
        }
        long size = app_store_file_size(root, name, entry->d_name);
        total += size > 0 ? size : 0;
    }
    closedir(directory);
    return total;
}

app_write_result_t app_store_write_file(const char *root, const char *name, const char *file,
                                        long offset, const void *bytes, size_t length)
{
    char path[PATH_MAX_BYTES];
    if (offset < 0 || !is_data_file(file) || !file_path_of(path, root, name, file, "")) {
        return APP_WRITE_REFUSED;
    }
    long size = app_store_file_size(root, name, file);
    if (length == 0) {
        if (offset != 0) {
            return APP_WRITE_REFUSED;
        }
        return size < 0 || remove(path) == 0 ? APP_WRITE_OK : APP_WRITE_FAILED;
    }
    long size_before = size > 0 ? size : 0;
    if (offset > size_before) {
        return APP_WRITE_REFUSED; /* a file has no holes */
    }
    long end = offset + (long)length;
    long size_after = end > size_before ? end : size_before;
    if (data_bytes(root, name, file) + size_after > APP_FILES_MAX_BYTES) {
        return APP_WRITE_NO_SPACE;
    }
    FILE *opened = fopen(path, size < 0 ? "wb" : "r+b");
    if (opened == NULL) {
        return APP_WRITE_FAILED;
    }
    bool written = fseek(opened, offset, SEEK_SET) == 0 &&
                   fwrite(bytes, 1, length, opened) == length;
    return fclose(opened) == 0 && written ? APP_WRITE_OK : APP_WRITE_FAILED;
}

bool app_store_remove(const char *root, const char *name)
{
    char folder[PATH_MAX_BYTES];
    if (!app_manifest_is_valid_name(name) ||
        snprintf(folder, sizeof folder, "%s/%s", root, name) >= (int)sizeof folder) {
        return false;
    }
    DIR *directory = opendir(folder);
    if (directory == NULL) {
        return false;
    }
    bool emptied = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char path[PATH_MAX_BYTES + sizeof entry->d_name + 1];
        snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
        emptied = remove(path) == 0 && emptied;
    }
    closedir(directory);
    return emptied && rmdir(folder) == 0;
}
