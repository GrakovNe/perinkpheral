/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tinytest.h"

/* The store over a real folder, made fresh for every test. */

static char root[64];

static void fresh_root(void)
{
    strcpy(root, "/tmp/perinkpheral-store-XXXXXX");
    if (mkdtemp(root) == NULL) {
        perror("mkdtemp");
        exit(1);
    }
}

static void write_file(const char *folder, const char *file, const void *content, size_t size)
{
    char path[160];
    snprintf(path, sizeof path, "%s/%s", root, folder);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/%s/%s", root, folder, file);
    FILE *out = fopen(path, "wb");
    fwrite(content, 1, size, out);
    fclose(out);
}

static void install(const char *folder, const char *name, const char *title)
{
    char manifest[160];
    snprintf(manifest, sizeof manifest, "{\"name\":\"%s\",\"title\":\"%s\",\"abi\":1}", name, title);
    write_file(folder, "manifest.json", manifest, strlen(manifest));
    write_file(folder, "app.wasm", "\0asm", 4);
}

static void test_list_is_sorted_by_title(void)
{
    fresh_root();
    install("plex", "plex", "Now Playing");
    install("clock", "clock", "Clock");
    install("hello", "hello", "Hello");

    app_manifest_t apps[8];
    CHECK(app_store_list(root, apps, 8) == 3);
    CHECK_STR(apps[0].name, "clock");
    CHECK_STR(apps[1].name, "hello");
    CHECK_STR(apps[2].name, "plex");
    CHECK_STR(apps[2].title, "Now Playing");
}

static void test_broken_ones_are_passed_over(void)
{
    fresh_root();
    install("good", "good", "Good");
    write_file("no-json", "manifest.json", "{oops", 5);
    write_file("no-manifest", "app.wasm", "\0asm", 4);
    /* a manifest that claims another application's name */
    install("impostor", "good", "Impostor");
    /* a stray file in the root is not an application */
    char stray_path[96];
    snprintf(stray_path, sizeof stray_path, "%s/README.txt", root);
    FILE *stray = fopen(stray_path, "w");
    fputs("not an application", stray);
    fclose(stray);

    app_manifest_t apps[8];
    CHECK(app_store_list(root, apps, 8) == 1);
    CHECK_STR(apps[0].name, "good");
}

static void test_capacity_and_missing_root(void)
{
    fresh_root();
    install("a", "a", "A");
    install("b", "b", "B");
    install("c", "c", "C");
    app_manifest_t apps[2];
    CHECK(app_store_list(root, apps, 2) == 2);

    CHECK(app_store_list("/nonexistent/perinkpheral", apps, 2) == 0);
}

static void test_read_module(void)
{
    fresh_root();
    install("hello", "hello", "Hello");
    size_t size = 0;
    uint8_t *module = app_store_read_module(root, "hello", &size);
    CHECK(module != NULL && size == 4);
    CHECK(module != NULL && memcmp(module, "\0asm", 4) == 0);
    free(module);

    CHECK(app_store_read_module(root, "absent", &size) == NULL);

    write_file("empty", "app.wasm", "", 0);
    CHECK(app_store_read_module(root, "empty", &size) == NULL);

    static uint8_t too_large[APP_MODULE_MAX_BYTES + 1];
    write_file("huge", "app.wasm", too_large, sizeof too_large);
    CHECK(app_store_read_module(root, "huge", &size) == NULL);
    write_file("limit", "app.wasm", too_large, APP_MODULE_MAX_BYTES);
    module = app_store_read_module(root, "limit", &size);
    CHECK(module != NULL && size == APP_MODULE_MAX_BYTES);
    free(module);
}

static bool put_file(const char *name, const char *file, const char *content)
{
    char path[APP_STORE_PATH_MAX];
    if (!app_store_begin_file(root, name, file, path)) {
        return false;
    }
    FILE *incoming = fopen(path, "wb");
    if (incoming == NULL) {
        return false;
    }
    fwrite(content, 1, strlen(content), incoming);
    fclose(incoming);
    return app_store_commit_file(root, name, file);
}

static void test_install_files(void)
{
    fresh_root();
    app_manifest_t apps[4];
    const char *manifest = "{\"name\":\"fresh\",\"title\":\"Fresh\",\"version\":\"1\",\"abi\":1}";

    /* a new application: its folder is made, and it is listed once both files are in */
    CHECK(put_file("fresh", APP_FILE_MODULE, "first"));
    CHECK(app_store_list(root, apps, 4) == 0);
    CHECK(put_file("fresh", APP_FILE_MANIFEST, manifest));
    CHECK(app_store_list(root, apps, 4) == 1);

    /* a newer file takes the place of the one that was there */
    CHECK(put_file("fresh", APP_FILE_MODULE, "second!"));
    size_t size = 0;
    uint8_t *module = app_store_read_module(root, "fresh", &size);
    CHECK(module != NULL && size == 7 && memcmp(module, "second!", 7) == 0);
    free(module);

    /* an upload that never finished leaves what was installed */
    char path[APP_STORE_PATH_MAX];
    CHECK(app_store_begin_file(root, "fresh", APP_FILE_MODULE, path));
    FILE *cut_short = fopen(path, "wb");
    fputs("hal", cut_short);
    fclose(cut_short);
    module = app_store_read_module(root, "fresh", &size);
    CHECK(module != NULL && size == 7);
    free(module);

    /* a name is not a path */
    CHECK(!app_store_begin_file(root, "../escape", APP_FILE_MODULE, path));
    CHECK(!app_store_begin_file(root, "", APP_FILE_MODULE, path));
    CHECK(!app_store_begin_file(root, "Capital", APP_FILE_MODULE, path));
    CHECK(!app_store_commit_file(root, "a/b", APP_FILE_MODULE));
    /* nothing was begun */
    CHECK(!app_store_commit_file(root, "absent", APP_FILE_MODULE));

    /* a data file beside them, read back in pieces */
    CHECK(put_file("fresh", "frames.bin", "0123456789"));
    char piece[8] = { 0 };
    CHECK(app_store_file_size(root, "fresh", "frames.bin") == 10);
    CHECK(app_store_read_file(root, "fresh", "frames.bin", 4, piece, 3) == 3 && piece[0] == '4');
    CHECK(app_store_read_file(root, "fresh", "frames.bin", 8, piece, 8) == 2);
    CHECK(app_store_read_file(root, "fresh", "frames.bin", 10, piece, 8) == 0);
    CHECK(app_store_file_size(root, "fresh", "absent.bin") < 0);
    CHECK(app_store_read_file(root, "fresh", "absent.bin", 0, piece, 8) < 0);
    CHECK(app_store_read_file(root, "fresh", "frames.bin", -1, piece, 8) < 0);
    /* a file's name is not a path either, and not an upload in progress */
    CHECK(!app_store_is_file_name("../frames.bin") && !app_store_is_file_name("a/b"));
    CHECK(!app_store_is_file_name(".hidden") && !app_store_is_file_name(""));
    CHECK(!app_store_is_file_name("Frames.bin") && !app_store_is_file_name("app.wasm.new"));
    CHECK(!app_store_is_file_name("012345678901234567890123"));
    CHECK(app_store_is_file_name("01234567890123456789012") && app_store_is_file_name("a"));
    CHECK(!app_store_begin_file(root, "fresh", "../x", path));
    CHECK(app_store_file_size(root, "fresh", "../fresh/frames.bin") < 0);
    CHECK(app_store_file_max_bytes("frames.bin") == APP_FILES_MAX_BYTES);

    CHECK(app_store_file_max_bytes(APP_FILE_MODULE) == APP_MODULE_MAX_BYTES);
    CHECK(app_store_file_max_bytes(APP_FILE_MANIFEST) < APP_MODULE_MAX_BYTES);
}

int main(void)
{
    test_list_is_sorted_by_title();
    test_broken_ones_are_passed_over();
    test_capacity_and_missing_root();
    test_read_module();
    test_install_files();
    REPORT();
}
