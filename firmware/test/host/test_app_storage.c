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
/* The power goes while the first sector is erased: some bits are back to 1, the mark's
 * all of them, and the size's (TEARS_SIZE) or the name's (TEARS_NAME) some. */
enum { TEARS_NOTHING, TEARS_SIZE, TEARS_NAME };
static int tears_first_erase;

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
    if (tears_first_erase && offset == 0 && size == 4096) {
        uint32_t *words = (uint32_t *)flash;
        words[0] |= 0x50414b50u; /* the mark as it was */
        if (tears_first_erase == TEARS_SIZE) {
            words[1] |= 0x2000; /* two sectors claim four */
        } else {
            flash[8] |= 0x02;   /* "a" reads "c" */
        }
        is_cut = true;
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
    /* Its mark is cleared before the erase that was refused: the file stays what runs. */
    CHECK(header_at(1)->magic == 0);
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

    /* An erase of a first sector cut short grows its size: that header does not take the
     * module after it along, now or when the next start forgets what is unfinished. */
    memset(flash, 0xff, sizeof flash);
    app_code_start();
    char alpha[128], alpha_module[128], beta[128], beta_module[128];
    snprintf(alpha, sizeof alpha, "%s/alpha", root);
    snprintf(alpha_module, sizeof alpha_module, "%s/alpha/app.wasm", root);
    snprintf(beta, sizeof beta, "%s/beta", root);
    snprintf(beta_module, sizeof beta_module, "%s/beta/app.wasm", root);
    CHECK(mkdir(alpha, 0700) == 0);
    memset(body, 'a', sizeof body);
    output = fopen(alpha_module, "wb");
    CHECK(output != NULL && fwrite(body, 1, sizeof body, output) == sizeof body && fclose(output) == 0);
    app_code_adopt(root);
    CHECK(mkdir(beta, 0700) == 0);
    memset(body, 'b', sizeof body);
    output = fopen(beta_module, "wb");
    CHECK(output != NULL && fwrite(body, 1, sizeof body, output) == sizeof body && fclose(output) == 0);
    app_code_adopt(root);
    CHECK(header_at(0)->magic == SLOT_MAGIC && header_at(2)->magic == SLOT_MAGIC);
    CHECK(rmdir(alpha) == 0);
    tears_first_erase = TEARS_SIZE;
    app_code_adopt(root);
    tears_first_erase = TEARS_NOTHING;
    is_cut = false;
    CHECK(header_at(0)->magic == SLOT_MAGIC && header_at(0)->size > 3 * SECTOR_BYTES);
    app_code_start(); /* the next start */
    code = app_code_find(root, "beta", &size);
    CHECK(code != NULL && size == sizeof body && memcmp(code, body, sizeof body) == 0);
    app_code_adopt(root);
    code = app_code_find(root, "beta", &size);
    CHECK(code != NULL && size == sizeof body && memcmp(code, body, sizeof body) == 0);
    CHECK(rmdir(beta) == 0);
    app_code_adopt(root);
    CHECK(app_code_find(root, "beta", &size) == NULL && header_at(2)->magic != SLOT_MAGIC);

    /* Nor does it give the module of "a", gone, to "c", which is installed. */
    memset(flash, 0xff, sizeof flash);
    app_code_start();
    char a[128], a_module[128], c[128], c_module[128];
    snprintf(a, sizeof a, "%s/a", root);
    snprintf(a_module, sizeof a_module, "%s/a/app.wasm", root);
    snprintf(c, sizeof c, "%s/c", root);
    snprintf(c_module, sizeof c_module, "%s/c/app.wasm", root);
    CHECK(mkdir(a, 0700) == 0);
    write_bytes(a_module, "aaaa");
    app_code_adopt(root);
    CHECK(mkdir(c, 0700) == 0);
    write_bytes(c_module, "cccc");
    app_code_adopt(root);
    CHECK(strcmp(header_at(0)->name, "a") == 0 && strcmp(header_at(1)->name, "c") == 0);
    CHECK(rmdir(a) == 0);
    tears_first_erase = TEARS_NAME;
    app_code_adopt(root);
    tears_first_erase = TEARS_NOTHING;
    is_cut = false;
    CHECK(header_at(0)->magic == SLOT_MAGIC && strcmp(header_at(0)->name, "c") == 0);
    app_code_start(); /* the next start */
    code = app_code_find(root, "c", &size);
    CHECK(code != NULL && size == 4 && memcmp(code, "cccc", 4) == 0);
    app_code_adopt(root);
    code = app_code_find(root, "c", &size);
    CHECK(code != NULL && size == 4 && memcmp(code, "cccc", 4) == 0);
    CHECK(rmdir(c) == 0);
    CHECK(rmdir(root) == 0);
    REPORT();
}
