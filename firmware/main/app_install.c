/* SPDX-License-Identifier: MIT */
#include "app_install.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"
#include "app_code.h"
#include "app_manifest.h"
#include "app_room.h"
#include "app_store.h"
#include "app_update_policy.h"
#include "catalog.h"
#include "events.h"
#include "host.h"
#include "host_api.h"
#include "launcher.h"
#include "power.h"
#include "poller.h"
#include "settings.h"
#include "storage.h"

#define SHA256_BYTES 32
static const char *TAG = "app_install";
static const char FETCH_NO_MEMORY[] = "Not enough memory.";

bool app_install_network_is_retryable(int status)
{
    return status == 0 || status == HOST_FAILED_NO_NETWORK || status == HOST_FAILED_NO_ANSWER ||
           status == HOST_FAILED_TIMEOUT || status == HOST_FAILED_NO_MEMORY ||
           status == 429 || (status >= 500 && status <= 599);
}

static app_manifest_t *read_installed(int *count)
{
    app_manifest_t *copy = malloc(LAUNCHER_INSTALLED_MAX * sizeof *copy);
    *count = copy != NULL ? launcher_read_installed(copy, LAUNCHER_INSTALLED_MAX) : 0;
    return copy;
}

/* ---- installing one ---- */

typedef struct {
    FILE                  *file;
    mbedtls_sha256_context hash;
    size_t                 received;
} download_t;

static bool store_piece(void *context, const uint8_t *piece, size_t length)
{
    download_t *download = context;
    download->received += length;
    return mbedtls_sha256_update(&download->hash, piece, length) == 0 &&
           fwrite(piece, 1, length, download->file) == length;
}

/* "0af3…" as the bytes it stands for; false when it is not SHA-256 in hex. */
static bool read_hash(const char *hex, uint8_t hash[SHA256_BYTES])
{
    if (strlen(hex) != 2 * SHA256_BYTES) {
        return false;
    }
    for (int i = 0; i < SHA256_BYTES; i++) {
        char byte[3] = { hex[2 * i], hex[2 * i + 1], '\0' }, *end;
        hash[i] = (uint8_t)strtoul(byte, &end, 16);
        if (*end != '\0') {
            return false;
        }
    }
    return true;
}

/* One file of the application from the source to its place on the
 * device, taken only if it is what the index said it would be. The
 * manifest is also read: the device installs nothing it would not list. */
static const char *install_file(const char *source, int source_index, const char *app_name, const json_t *entry,
                                bool *asks_for_system, bool *writes_files, char **manifest_text, bool *retryable)
{
    const json_t *name = json_get(entry, "name");
    const json_t *size = json_get(entry, "size");
    const json_t *sha256 = json_get(entry, "sha256");
    uint8_t      wanted_hash[SHA256_BYTES], hash[SHA256_BYTES];
    char         url[CATALOG_URL_MAX + 1], path[APP_STORE_PATH_MAX];
    if (!json_is_string(name) || !json_is_number(size) || !json_is_string(sha256) ||
        !app_store_is_file_name(name->text) || !read_hash(sha256->text, wanted_hash) ||
        size->number < 1 || size->number > (int64_t)app_store_file_max_bytes(name->text) ||
        !catalog_url(source, app_name, name->text, url, sizeof url)) {
        return "Invalid catalog entry.";
    }
    if (!app_store_begin_file(STORAGE_APPS_ROOT, app_name, name->text, path)) {
        return "Storage error.";
    }
    download_t download = { .file = fopen(path, "wb") };
    if (download.file == NULL) {
        return "Storage error.";
    }
    mbedtls_sha256_init(&download.hash);
    mbedtls_sha256_starts(&download.hash, 0);
    int  http_status;
    bool fetched = poller_fetch(url, (size_t)size->number, store_piece, &download,
                                &http_status);
    mbedtls_sha256_finish(&download.hash, hash);
    mbedtls_sha256_free(&download.hash);
    bool stored = fclose(download.file) == 0;
    const char *problem = NULL;
    if (http_status == HOST_FAILED_NO_MEMORY) {
        if (retryable != NULL) *retryable = true;
        problem = FETCH_NO_MEMORY;
    } else if (!stored) {
        problem = "Storage error.";
    } else if (!fetched) {
        if (retryable != NULL) *retryable = app_install_network_is_retryable(http_status);
        problem = "Download failed.";
    } else if (download.received != (size_t)size->number ||
               memcmp(hash, wanted_hash, SHA256_BYTES) != 0) {
        problem = "Checksum mismatch.";
    } else if (strcmp(name->text, APP_FILE_MANIFEST) == 0) {
        /* Still under its temporary name: read it there. */
        char          *text = malloc(download.received + 1);
        FILE          *incoming = text != NULL ? fopen(path, "rb") : NULL;
        app_manifest_t manifest;
        bool is_read = incoming != NULL &&
                       fread(text, 1, download.received, incoming) == download.received;
        if (incoming != NULL) {
            fclose(incoming);
        }
        if (is_read) {
            text[download.received] = '\0';
        }
        app_manifest_result_t result = is_read ? app_manifest_parse(text, &manifest)
                                               : APP_MANIFEST_NOT_JSON;
        if (result == APP_MANIFEST_OTHER_ABI) {
            problem = "Update the app.";
        } else if (result != APP_MANIFEST_OK || strcmp(manifest.name, app_name) != 0) {
            problem = "Invalid manifest.";
        } else if (!catalog_author_is_allowed(manifest.author, source_index)) {
            problem = "Invalid app author.";
        } else if (!app_manifest_runs_on(&manifest, esp_app_get_description()->version)) {
            problem = "Update the firmware.";
        } else {
            *asks_for_system = manifest.system;
            *writes_files = manifest.writes_files;
            *manifest_text = text;
            text = NULL;
        }
        free(text);
    }
    /* It stays under its temporary name: the host loop puts it in place. */
    if (problem != NULL) {
        remove(path);
    }
    return problem;
}

/* The files as the page read them from the index: the module first, the
 * manifest last, both there, and none twice. */
bool app_install_files_are_valid(const json_t *files)
{
    int count = json_is_array(files) ? json_count(files) : 0;
    if (count < 2 || count > CATALOG_APP_FILES_MAX) {
        return false;
    }
    const json_t *first = json_get(json_at(files, 0), "name");
    const json_t *last =
        json_get(json_at(files, count - 1), "name");
    if (!json_is_string(first) || strcmp(first->text, APP_FILE_MODULE) != 0 ||
        !json_is_string(last) || strcmp(last->text, APP_FILE_MANIFEST) != 0) {
        return false;
    }
    /* Each file once: a second download of a name would replace the first
     * unseen - the manifest's kept text among it - and be counted twice. */
    for (int i = 0; i < count; i++) {
        const json_t *name = json_get(json_at(files, i), "name");
        if (!json_is_string(name)) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (strcmp(name->text, json_get(json_at(files, j), "name")->text) == 0) {
                return false;
            }
        }
    }
    /* The two have a size, as the room for them is reckoned from it. */
    const json_t *first_size = json_get(json_at(files, 0), "size");
    const json_t *last_size = json_get(json_at(files, count - 1), "size");
    if (!json_is_number(first_size) || first_size->number < 0 ||
        first_size->number > APP_MODULE_MAX_BYTES || !json_is_number(last_size) ||
        last_size->number < 0 || last_size->number > (int64_t)app_store_file_max_bytes(APP_FILE_MANIFEST)) {
        return false;
    }
    /* What lies between them is the application's files, and all of them
     * together have a share, each counted as the whole blocks it takes: a
     * catalog cannot bring more than an application may write. */
    long taken = 0;
    for (int i = 1; i < count - 1; i++) {
        const json_t *size = json_get(json_at(files, i), "size");
        /* Bounded before it is made a whole number: a size beyond any
         * number cannot be one. */
        if (!json_is_number(size) || size->number < 0 ||
            size->number > APP_FILES_MAX_BYTES) {
            return false;
        }
        long blocks = ((long)size->number + MANIFEST_FILE_BLOCK_BYTES - 1) /
                      MANIFEST_FILE_BLOCK_BYTES;
        taken += blocks * MANIFEST_FILE_BLOCK_BYTES;
    }
    return taken <= APP_FILES_MAX_BYTES;
}

/* What the files of the list take on the device, each in whole blocks:
 * the module's, and the data files' between it and the manifest. */
static long bytes_in_blocks(double bytes)
{
    return ((long)bytes + MANIFEST_FILE_BLOCK_BYTES - 1) / MANIFEST_FILE_BLOCK_BYTES *
           MANIFEST_FILE_BLOCK_BYTES;
}

static long data_files_bytes(const json_t *files)
{
    long taken = 0;
    for (int i = 1; i < json_count(files) - 1; i++) {
        taken += bytes_in_blocks(json_get(json_at(files, i), "size")->number);
    }
    return taken;
}

/* What the data files of the folder take that the package does not
 * replace: an update keeps them, those the application wrote among them,
 * so they count with the package's. */
static long kept_files_bytes(const char *app_name, const json_t *files)
{
    long kept = app_store_files_bytes(STORAGE_APPS_ROOT, app_name);
    for (int i = 1; i < json_count(files) - 1; i++) {
        long replaced = app_store_file_size(STORAGE_APPS_ROOT, app_name,
                                            json_get(json_at(files, i), "name")->text);
        kept -= replaced > 0 ? bytes_in_blocks((double)replaced) : 0;
    }
    return kept;
}

/* Whether the files promised to the applications that write, the one being
 * installed among them when `writes_files`, and the file system's own
 * spare, are still free beside what is on the device now. An application
 * that is replaced is counted as it will be. */
static bool files_room_holds(const char *app_name, bool writes_files, long files_bytes,
                             long more_bytes)
{
    int             count;
    app_manifest_t *installed = read_installed(&count);
    if (installed == NULL) {
        return false;
    }
    app_room_files_t apps[LAUNCHER_INSTALLED_MAX + 1];
    int listed = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(installed[i].name, app_name) != 0) {
            apps[listed++] = (app_room_files_t){
                installed[i].writes_files,
                app_store_files_bytes(STORAGE_APPS_ROOT, installed[i].name) };
        }
    }
    free(installed);
    apps[listed++] = (app_room_files_t){ writes_files, files_bytes };
    size_t used = 0, total = 0;
    storage_usage(&used, &total);
    long free_bytes = (long)total - (long)used;
    return free_bytes >= more_bytes + app_room_promised(apps, listed) + APP_ROOM_FILES_SPARE_BYTES;
}


static SemaphoreHandle_t s_operation;
static portMUX_TYPE s_busy_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_busy_name[APP_NAME_MAX + 1];

void app_install_start(void)
{
    s_operation = xSemaphoreCreateMutex();
    configASSERT(s_operation != NULL);
}

static void mark_busy(const char *app_name)
{
    portENTER_CRITICAL(&s_busy_lock);
    snprintf(s_busy_name, sizeof s_busy_name, "%s", app_name != NULL ? app_name : "");
    portEXIT_CRITICAL(&s_busy_lock);
}

bool app_install_begin(const char *app_name)
{
    if (s_operation == NULL || xSemaphoreTake(s_operation, 0) != pdTRUE) {
        return false;
    }
    mark_busy(app_name);
    return true;
}

void app_install_end(void)
{
    mark_busy(NULL);
    xSemaphoreGive(s_operation);
}

bool app_install_app_is_busy(const char *app_name)
{
    portENTER_CRITICAL(&s_busy_lock);
    bool busy = s_busy_name[0] != '\0' && strcmp(s_busy_name, app_name) == 0;
    portEXIT_CRITICAL(&s_busy_lock);
    return busy;
}

typedef struct {
    const char *name;
    const char *source;
    int source_index;
    const app_install_options_t *options;
    const json_t *files;
    long files_bytes, manifest_bytes; /* the package's data files and manifest, in blocks */
    bool writes_files;
    char *previous_manifest;
    char *incoming_manifest;
    uint32_t previous_id;
    bool was_installed;
    bool committed;
    const char *problem;
} install_job_t;

static void settle_install(void *argument)
{
    install_job_t *job = argument;
    bool was_ready = app_store_is_ready(STORAGE_APPS_ROOT, job->name);
    if (!app_store_drop_incoming(STORAGE_APPS_ROOT, job->name)) {
        job->problem = "Storage error.";
    }
    if (!was_ready) {
        host_reload_apps();
    }
}

static bool source_is_current(const install_job_t *job)
{
    char sources[CATALOG_SOURCES_TEXT_MAX], source[CATALOG_SOURCE_URL_MAX + 1];
    settings_load_sources(sources, sizeof sources);
    return catalog_source_at(sources, job->source_index, source, sizeof source) &&
           strcmp(source, job->source) == 0;
}

static void prepare_install(void *argument)
{
    install_job_t *job = argument;
    settle_install(job);
    if (job->problem != NULL) {
        return;
    }
    if (!source_is_current(job)) {
        job->problem = "Source changed.";
        return;
    }
    const app_manifest_t *previous = launcher_find(job->name);
    job->was_installed = previous != NULL;
    job->previous_id = previous != NULL ? previous->id : 0;
    if (job->options->installed_id != 0) {
        if (previous == NULL || previous->id != job->options->installed_id) {
            job->problem = "App changed.";
            return;
        }
        job->previous_manifest = app_store_read_manifest(STORAGE_APPS_ROOT, job->name);
        if (job->previous_manifest == NULL) {
            job->problem = "Storage error.";
        }
    }
}

static bool same_certificate(const char *app_name, const char *file)
{
    char old_path[APP_STORE_PATH_MAX], new_path[APP_STORE_PATH_MAX];
    snprintf(old_path, sizeof old_path, "%s/%s/%s", STORAGE_APPS_ROOT, app_name, file);
    snprintf(new_path, sizeof new_path, "%s/%s/%s.new", STORAGE_APPS_ROOT, app_name, file);
    FILE *old = fopen(old_path, "rb"), *incoming = fopen(new_path, "rb");
    bool same = old != NULL && incoming != NULL;
    unsigned char before[128], after[128];
    while (same) {
        size_t old_count = fread(before, 1, sizeof before, old);
        size_t new_count = fread(after, 1, sizeof after, incoming);
        same = old_count == new_count && memcmp(before, after, old_count) == 0;
        if (!same || old_count < sizeof before) {
            same = same && !ferror(old) && !ferror(incoming);
            break;
        }
    }
    if (old != NULL) fclose(old);
    if (incoming != NULL) fclose(incoming);
    return same;
}

static void commit_install(void *argument)
{
    install_job_t *job = argument;
    const app_manifest_t *current = launcher_find(job->name);
    if ((current != NULL ? current->id : 0) != job->previous_id || !source_is_current(job)) {
        job->problem = "App or source changed.";
        return;
    }
    if (job->options->installed_id != 0) {
        appconf_t settings;
        settings_read(&settings);
        if (!settings.app_auto_update) {
            job->problem = "Automatic updates disabled.";
            return;
        }
        if (!app_update_policy_allows(job->previous_manifest, job->incoming_manifest,
                                       job->options->version, esp_app_get_description()->version)) {
            job->problem = "Update requires manual installation.";
            return;
        }
        char certificates[MANIFEST_CERTIFICATES_MAX][MANIFEST_FILE_NAME_MAX + 1];
        int count = app_manifest_certificates(job->incoming_manifest, certificates, MANIFEST_CERTIFICATES_MAX);
        for (int i = 0; i < count; i++) {
            if (!same_certificate(job->name, certificates[i])) {
                job->problem = "Update requires manual installation.";
                return;
            }
        }
    }
    /* Counted again here, where the application cannot write meanwhile: it
     * may have while the package was downloaded. */
    if (job->writes_files && job->files_bytes + kept_files_bytes(job->name, job->files) +
                                     job->manifest_bytes > HOST_FILES_WRITER_MAX) {
        job->problem = "Not enough storage.";
        return;
    }
    job->committed = app_store_commit_all(STORAGE_APPS_ROOT, job->name);
    /* Installation callbacks may remove the application: the files no
     * longer have an open download, and the writer lock still excludes uploads. */
    mark_busy(NULL);
    host_reload_apps();
    if (!job->committed) {
        job->problem = "Storage error.";
    } else if (!job->was_installed) {
        host_open_installed_app(job->name);
    }
}

static const char *install_package(install_job_t *job, const json_t *files)
{
    if (!events_call(prepare_install, job)) return "Device busy.";
    if (job->problem != NULL) return job->problem;
    int count;
    app_manifest_t *installed = read_installed(&count);
    if (installed == NULL) return FETCH_NO_MEMORY;
    free(installed);
    long module_bytes = (long)json_get(json_at(files, 0), "size")->number;
    long files_bytes = data_files_bytes(files);
    long files_after = files_bytes + kept_files_bytes(job->name, files);
    long manifest_bytes = bytes_in_blocks(json_get(json_at(files, json_count(files) - 1), "size")->number);
    if ((!job->was_installed && count >= CATALOG_INSTALLED_MAX) ||
        !app_code_has_room(job->name, (size_t)module_bytes) ||
        !files_room_holds(job->name, false, files_after,
                          bytes_in_blocks((double)module_bytes) + files_bytes + manifest_bytes)) {
        return "Not enough storage.";
    }
    bool asks_for_system = false, writes_files = false;
    const char *problem = NULL;
    const json_t *entry;
    power_hold_radio();
    json_for_each(entry, files) {
        problem = install_file(job->source, job->source_index, job->name, entry,
                               &asks_for_system, &writes_files, &job->incoming_manifest, job->options->retryable);
        if (problem != NULL) break;
    }
    power_release_radio();
    if (problem != NULL) return problem;
    app_manifest_t incoming;
    if (job->incoming_manifest == NULL || app_manifest_parse(job->incoming_manifest, &incoming) != APP_MANIFEST_OK ||
        (job->options->version != NULL && strcmp(job->options->version, incoming.version) != 0)) {
        return "Invalid manifest.";
    }
    if (writes_files && (files_after + manifest_bytes > HOST_FILES_WRITER_MAX ||
                        !files_room_holds(job->name, true, files_after, 0))) {
        return "Not enough storage.";
    }
    job->files = files;
    job->files_bytes = files_bytes;
    job->manifest_bytes = manifest_bytes;
    job->writes_files = writes_files;
    if (asks_for_system && job->options->installed_id == 0) {
        if (job->options->confirm_system == NULL) return "Not confirmed.";
        problem = job->options->confirm_system();
        if (problem != NULL) return problem;
    }
    if (!events_call(commit_install, job)) return "Device busy.";
    return job->problem;
}

const char *app_install_run(const char *source, int source_index, const char *app_name,
                            const json_t *files, const app_install_options_t *options)
{
    if (options->retryable != NULL) *options->retryable = false;
    if (!app_install_files_are_valid(files)) return "Invalid catalog entry.";
    if (!app_install_begin(app_name)) return "Device busy.";
    install_job_t job = { .name = app_name, .source = source, .source_index = source_index, .options = options };
    const char *problem = install_package(&job, files);
    if (problem != NULL) {
        events_call(settle_install, &job);
        ESP_LOGW(TAG, "%s: %s", app_name, problem);
    } else {
        ESP_LOGI(TAG, "%s installed", app_name);
    }
    free(job.previous_manifest);
    free(job.incoming_manifest);
    app_install_end();
    return problem;
}
