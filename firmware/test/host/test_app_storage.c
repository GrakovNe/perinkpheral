/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_partition.h"
#include "tinytest.h"

_Alignas(uint32_t) static uint8_t flash[4 * 4096];
static const esp_partition_t partition = { sizeof flash };
static int erases;
static size_t last_erased;
static const char *stat_failure, *rename_failure, *open_failure;
static bool read_failure, erase_failure;
/* The power goes as a module's mark is about to be written: nothing more reaches the flash. */
static bool cuts_before_mark, is_cut;

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{
    (void)type; (void)subtype; (void)label;
    return &partition;
}

esp_err_t esp_partition_mmap(const esp_partition_t *part, size_t offset, size_t size,
                             int memory, const void **mapped, esp_partition_mmap_handle_t *handle)
{
    (void)part; (void)offset; (void)size; (void)memory; (void)handle;
    *mapped = flash;
    return ESP_OK;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *part, size_t offset, size_t size)
{
    (void)part;
    CHECK(offset + size <= sizeof flash);
    if (erase_failure || is_cut) {
        return ESP_FAIL;
    }
    memset(flash + offset, 0xff, size);
    erases++;
    last_erased = offset;
    return ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t *part, size_t offset, const void *bytes, size_t size)
{
    (void)part;
    CHECK(offset + size <= sizeof flash);
    is_cut = is_cut || (cuts_before_mark && offset % 4096 == 0); /* a module's mark begins a sector */
    if (is_cut) {
        return ESP_FAIL;
    }
    memcpy(flash + offset, bytes, size);
    return ESP_OK;
}

static int checked_stat(const char *path, struct stat *info)
{
    if (stat_failure != NULL && strcmp(path, stat_failure) == 0) {
        errno = EIO;
        return -1;
    }
    return stat(path, info);
}

static DIR *checked_opendir(const char *path)
{
    if (open_failure != NULL && strcmp(path, open_failure) == 0) {
        errno = EACCES;
        return NULL;
    }
    return opendir(path);
}

static struct dirent *checked_readdir(DIR *directory)
{
    if (read_failure) {
        errno = EIO;
        return NULL;
    }
    return readdir(directory);
}

static int checked_rename(const char *from, const char *to)
{
    if (rename_failure != NULL && strcmp(to, rename_failure) == 0) {
        errno = EIO;
        return -1;
    }
    return rename(from, to);
}

#define stat(path, info) checked_stat(path, info)
#define opendir checked_opendir
#define readdir checked_readdir
#define rename checked_rename
#include "../../main/app_code.c"
#include "../../components/appstore/app_store.c"
#undef stat
#undef opendir
#undef readdir
#undef rename

static void seed_code(int sector)
{
    memset(flash, 0xff, sizeof flash);
    slot_header_t *header = (slot_header_t *)(flash + sector * SECTOR_BYTES);
    header->magic = SLOT_MAGIC;
    header->size = 4;
    strcpy(header->name, "clock");
    memcpy((uint8_t *)header + sizeof *header, "old!", 4);
    erases = 0;
    app_code_start();
}

static void write_bytes(const char *path, const char *content)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fwrite(content, 1, strlen(content), file) == strlen(content));
        CHECK(fclose(file) == 0);
    }
}

int main(void)
{
    char root[] = "/tmp/perink-storage-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char folder[128], module[128], manifest[128], incoming[128], mark[128];
    snprintf(folder, sizeof folder, "%s/clock", root);
    snprintf(module, sizeof module, "%s/clock/app.wasm", root);
    snprintf(manifest, sizeof manifest, "%s/clock/manifest.json", root);
    snprintf(incoming, sizeof incoming, "%s/clock/manifest.json.new", root);
    snprintf(mark, sizeof mark, "%s/clock/.commit", root);
    size_t size;
    seed_code(0);
    app_code_adopt("/nonexistent/perinkpheral-apps");
    CHECK(erases == 0 && app_code_find(root, "clock", &size) != NULL);
    open_failure = root;
    app_code_adopt(root);
    CHECK(erases == 0 && app_code_find(root, "clock", &size) != NULL);
    open_failure = NULL;
    read_failure = true;
    app_code_adopt(root);
    CHECK(erases == 0 && app_code_find(root, "clock", &size) != NULL);
    read_failure = false;
    stat_failure = folder;
    app_code_adopt(root);
    CHECK(erases == 0 && app_code_find(root, "clock", &size) != NULL);
    stat_failure = NULL;
    app_code_adopt(root);
    CHECK(erases == 1 && app_code_find(root, "clock", &size) == NULL);

    CHECK(mkdir(folder, 0700) == 0);
    write_bytes(manifest, "{\"name\":\"clock\",\"title\":\"Old\",\"abi\":3}");
    write_bytes(module, "new!");
    write_bytes(incoming, "{\"name\":\"clock\",\"title\":\"New\",\"abi\":3}");
    rename_failure = manifest;
    CHECK(!app_store_commit_all(root, "clock"));
    app_manifest_t apps[2];
    CHECK(app_store_list(root, apps, 2) == 0);
    CHECK(app_store_read_module(root, "clock", &size) == NULL);
    CHECK(app_store_read_manifest(root, "clock") == NULL);
    seed_code(1);
    app_code_adopt(root);
    CHECK(erases == 0 && header_at(1)->magic == SLOT_MAGIC);
    CHECK(app_code_find(root, "clock", &size) == NULL);
    pack(root);
    CHECK(erases == 0 && app_store_file_size(root, "clock", "app.wasm") == 4);
    CHECK(!app_store_drop_incoming(root, "clock"));
    CHECK(!app_store_is_ready(root, "clock"));
    rename_failure = NULL;
    read_failure = true;
    CHECK(!app_store_drop_incoming(root, "clock"));
    CHECK(!app_store_is_ready(root, "clock"));
    read_failure = false;
    open_failure = folder;
    CHECK(!app_store_drop_incoming(root, "clock"));
    CHECK(!app_store_is_ready(root, "clock"));
    open_failure = NULL;
    CHECK(app_store_drop_incoming(root, "clock"));
    CHECK(app_store_list(root, apps, 2) == 1);
    CHECK_STR(apps[0].title, "New");
    pack(root); /* A complete newer module must not be overwritten during compaction either. */
    CHECK(erases == 0);
    CHECK(app_code_find(root, "clock", &size) == NULL);
    stat_failure = module;
    CHECK(app_code_find(root, "clock", &size) == NULL);
    stat_failure = NULL;
    erase_failure = true;
    app_code_adopt(root);
    CHECK(header_at(1)->magic == SLOT_MAGIC);
    CHECK(app_code_find(root, "clock", &size) == NULL);
    uint8_t *fallback = app_store_read_module(root, "clock", &size);
    CHECK(fallback != NULL && size == 4 && memcmp(fallback, "new!", 4) == 0);
    free(fallback);
    erase_failure = false;
    app_code_adopt(root);
    CHECK(memcmp(app_code_find(root, "clock", &size), "new!", 4) == 0);
    CHECK(access(module, F_OK) != 0);
    write_bytes(mark, "");
    stat_failure = mark;
    CHECK(!app_store_drop_incoming(root, "clock"));
    CHECK(!app_store_is_ready(root, "clock"));
    stat_failure = NULL;
    CHECK(app_store_remove(root, "clock"));

    /* A module cut short before its mark carries, where its second sector
     * begins, the header of a module named as another application. It is
     * never taken for that application's, and the next adoption erases it. */
    seed_code(3);
    CHECK(mkdir(folder, 0700) == 0);
    char donor[128], donor_module[128];
    snprintf(donor, sizeof donor, "%s/donor", root);
    snprintf(donor_module, sizeof donor_module, "%s/donor/app.wasm", root);
    CHECK(mkdir(donor, 0700) == 0);
    static uint8_t body[4100];
    memset(body, 'd', sizeof body);
    slot_header_t fake = { .magic = SLOT_MAGIC, .size = 4 };
    strcpy(fake.name, "clock");
    size_t at_second = SECTOR_BYTES - sizeof(slot_header_t);
    memcpy(body + at_second, &fake, sizeof fake);
    memcpy(body + at_second + sizeof fake, "evil", 4);
    FILE *output = fopen(donor_module, "wb");
    CHECK(output != NULL && fwrite(body, 1, sizeof body, output) == sizeof body && fclose(output) == 0);
    cuts_before_mark = true;
    app_code_adopt(root);
    cuts_before_mark = is_cut = false;
    CHECK(header_at(0)->magic != SLOT_MAGIC && header_at(1)->magic == SLOT_MAGIC);
    app_code_start(); /* the next start */
    const uint8_t *code = app_code_find(root, "clock", &size);
    CHECK(code != NULL && size == 4 && memcmp(code, "old!", 4) == 0);
    CHECK(app_code_free_bytes() == SECTOR_BYTES); /* the cut piece's two sectors are not free yet */
    erases = 0;
    app_code_adopt(root);
    code = app_code_find(root, "clock", &size);
    CHECK(code != NULL && size == 4 && memcmp(code, "old!", 4) == 0);
    code = app_code_find(root, "donor", &size);
    CHECK(code != NULL && size == sizeof body && memcmp(code, body, sizeof body) == 0);
    CHECK(access(donor_module, F_OK) != 0);
    /* Forgotten, a piece loses its first sector last: its header covers the rest until then. */
    CHECK(rmdir(donor) == 0);
    erases = 0;
    app_code_adopt(root);
    CHECK(erases == 2 && last_erased == 0 && app_code_find(root, "donor", &size) == NULL);
    CHECK(header_at(1)->magic != SLOT_MAGIC);
    CHECK(memcmp(app_code_find(root, "clock", &size), "old!", 4) == 0);
    CHECK(rmdir(folder) == 0);
    CHECK(rmdir(root) == 0);
    REPORT();
}
