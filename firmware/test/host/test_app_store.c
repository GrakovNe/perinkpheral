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

int main(void)
{
    test_list_is_sorted_by_title();
    test_broken_ones_are_passed_over();
    test_capacity_and_missing_root();
    test_read_module();
    REPORT();
}
