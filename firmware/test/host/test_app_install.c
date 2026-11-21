/* SPDX-License-Identifier: MIT */
/* The actual installer, with network, host-loop and storage fault doubles. */
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tinytest.h"
#include "../../main/storage.h"
static char test_root[sizeof "/tmp/perink-install-XXXXXX"];
#undef STORAGE_APPS_ROOT
#define STORAGE_APPS_ROOT test_root
#include "../../main/app_install.c"

static app_manifest_t present;
static bool has_present, mutex_taken, marker, fail_commit, is_host;
static bool change_id, change_source, turn_off, mismatch;
static bool last_retryable;
static int network_error, downloads, opens, commits, radio_holds;
static appconf_t config;
static char source_text[CATALOG_SOURCES_TEXT_MAX];
static char sequence[64];
static const char *new_manifest;
static const char *new_certificate;
static const char *module = "test module";

static void note(char step) { size_t used = strlen(sequence); sequence[used] = step; sequence[used + 1] = '\0'; }
static void file_path(char *path, const char *file) { snprintf(path, APP_STORE_PATH_MAX, "%s/test/%s", test_root, file); }
static void put_file(const char *file, const char *contents) {
    char path[APP_STORE_PATH_MAX]; file_path(path, file);
    FILE *output = fopen(path, "wb"); CHECK(output != NULL);
    if (output != NULL) { CHECK(fwrite(contents, 1, strlen(contents), output) == strlen(contents)); fclose(output); }
}
static char *get_file(const char *file) {
    char path[APP_STORE_PATH_MAX]; file_path(path, file);
    FILE *input = fopen(path, "rb"); if (input == NULL) return NULL;
    char *text = calloc(1, 8193); size_t count = fread(text, 1, 8192, input); text[count] = '\0'; fclose(input); return text;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &mutex_taken; }
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned wait) { (void)wait; if (*semaphore) return pdFALSE; *semaphore = true; return pdTRUE; }
void xSemaphoreGive(SemaphoreHandle_t semaphore) { *semaphore = false; }
const esp_app_desc_t *esp_app_get_description(void) { static const esp_app_desc_t description = { "0.1.45" }; return &description; }
bool events_call(events_call_fn run, void *argument) { CHECK(!is_host); is_host = true; run(argument); is_host = false; return true; }
int launcher_read_installed(app_manifest_t *copy, int capacity) { (void)capacity; if (has_present) copy[0] = present; return has_present ? 1 : 0; }
const app_manifest_t *launcher_find(const char *name) { CHECK(is_host); return has_present && strcmp(name, "test") == 0 ? &present : NULL; }
void settings_load_sources(char *text, size_t capacity) { snprintf(text, capacity, "%s", source_text); }
void settings_read(appconf_t *copy) { *copy = config; }
bool app_code_has_room(const char *name, size_t size) { (void)name; (void)size; return true; }
void storage_usage(size_t *used, size_t *total) { *used = 0; *total = 640 * 1024; }
static bool is_data_file(const char *file) {
    size_t length = strlen(file);
    return file[0] != '.' && strcmp(file, "manifest.json") != 0 && strcmp(file, "app.wasm") != 0 &&
           (length < 4 || strcmp(file + length - 4, ".new") != 0);
}
long app_store_file_size(const char *root, const char *name, const char *file) {
    (void)root; (void)name; char path[APP_STORE_PATH_MAX]; file_path(path, file); struct stat found;
    return stat(path, &found) == 0 ? (long)found.st_size : -1;
}
long app_store_files_bytes(const char *root, const char *name) {
    char folder[APP_STORE_PATH_MAX]; snprintf(folder, sizeof folder, "%s/test", test_root);
    DIR *directory = opendir(folder); struct dirent *entry; long taken = 0;
    while (directory != NULL && (entry = readdir(directory)) != NULL)
        if (is_data_file(entry->d_name)) taken += bytes_in_blocks((double)app_store_file_size(root, name, entry->d_name));
    if (directory != NULL) closedir(directory);
    return taken;
}
void power_hold_radio(void) { radio_holds++; }
void power_release_radio(void) { radio_holds--; }
bool app_store_is_ready(const char *root, const char *name) { (void)root; (void)name; return !marker; }
char *app_store_read_manifest(const char *root, const char *name) { (void)root; (void)name; CHECK(is_host); return get_file("manifest.json"); }
bool app_store_is_file_name(const char *file) { return file != NULL && strchr(file, '/') == NULL; }
size_t app_store_file_max_bytes(const char *file) { return strcmp(file, APP_FILE_MODULE) == 0 ? APP_MODULE_MAX_BYTES : 8192; }
bool app_store_begin_file(const char *root, const char *name, const char *file, char path[APP_STORE_PATH_MAX]) {
    (void)root; (void)name; snprintf(path, APP_STORE_PATH_MAX, "%s/test/%s.new", test_root, file); return true;
}
static void commit_files(void) {
    char folder[APP_STORE_PATH_MAX]; snprintf(folder, sizeof folder, "%s/test", test_root);
    DIR *directory = opendir(folder); struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        size_t length = strlen(entry->d_name);
        if (length < 4 || strcmp(entry->d_name + length - 4, ".new") != 0) continue;
        char from[APP_STORE_PATH_MAX], to[APP_STORE_PATH_MAX], file[APP_FILE_NAME_MAX + 1];
        CHECK(length - 4 <= APP_FILE_NAME_MAX);
        memcpy(file, entry->d_name, length - 4); file[length - 4] = '\0';
        file_path(from, entry->d_name); file_path(to, file); CHECK(rename(from, to) == 0);
    }
    closedir(directory); marker = false;
}
bool app_store_commit_all(const char *root, const char *name) {
    (void)root; (void)name; CHECK(is_host); commits++; note('C'); marker = true;
    if (fail_commit) return false;
    commit_files(); return true;
}
bool app_store_drop_incoming(const char *root, const char *name) {
    (void)root; (void)name; CHECK(is_host);
    if (marker) { commit_files(); return true; }
    const char *files[] = { "app.wasm.new", "root.pem.new", "manifest.json.new" };
    for (size_t i = 0; i < 3; i++) { char path[APP_STORE_PATH_MAX]; file_path(path, files[i]); unlink(path); }
    return true;
}
void host_reload_apps(void) {
    CHECK(is_host); note('R'); has_present = false;
    char *text = get_file("manifest.json");
    if (!marker && text != NULL && app_manifest_parse(text, &present) == APP_MANIFEST_OK) { has_present = true; present.id = 2; }
    free(text);
}
void host_open_installed_app(const char *name) { CHECK(is_host); CHECK(has_present && !marker); CHECK(strcmp(name, "test") == 0); opens++; note('O'); }
bool poller_fetch(const char *url, size_t maximum, poller_take_fn take, void *context, int *status) {
    CHECK(!is_host); CHECK(app_install_app_is_busy("test")); CHECK(!app_install_begin("other")); downloads++;
    if (network_error) { *status = network_error; return false; }
    const char *file = strrchr(url, '/') + 1;
    const char *bytes = strcmp(file, "app.wasm") == 0 ? module : strcmp(file, "manifest.json") != 0 ? new_certificate : new_manifest;
    if (change_id) present.id = 99;
    if (change_source) strcpy(source_text, "https://changed.example/catalog");
    if (turn_off) config.app_auto_update = 0;
    *status = 200;
    if (mismatch) bytes = "wrong";
    CHECK(strlen(bytes) <= maximum || mismatch);
    return take(context, (const uint8_t *)bytes, strlen(bytes));
}

static const char *data_name = "root.pem";
static json_t *files_for(void) {
    json_t *files = json_new_array();
    const char *names[] = { "app.wasm", data_name, "manifest.json" };
    const char *contents[] = { module, new_certificate, new_manifest };
    for (int i = 0; i < 3; i++) {
        if (contents[i] == NULL) continue;
        mbedtls_sha256_context hash; uint8_t digest[32]; char hex[65];
        mbedtls_sha256_init(&hash); mbedtls_sha256_starts(&hash, 0);
        mbedtls_sha256_update(&hash, (const unsigned char *)contents[i], strlen(contents[i])); mbedtls_sha256_finish(&hash, digest);
        for (int byte = 0; byte < 32; byte++) snprintf(hex + byte * 2, 3, "%02x", digest[byte]);
        json_t *file = json_new_object(); json_add_string(file, "name", names[i]); json_add_number(file, "size", strlen(contents[i])); json_add_string(file, "sha256", hex); json_array_add(files, file);
    }
    return files;
}
static const char OLD[] = "{\"name\":\"test\",\"title\":\"Test\",\"version\":\"1.0.0\",\"abi\":3}";
static const char NEW[] = "{\"name\":\"test\",\"title\":\"Test\",\"version\":\"1.1.0\",\"abi\":3}";
static void reset(const char *previous) {
    char template[] = "/tmp/perink-install-XXXXXX"; char *made = mkdtemp(template); CHECK(made != NULL); strcpy(test_root, made);
    char directory[APP_STORE_PATH_MAX]; snprintf(directory, sizeof directory, "%s/test", test_root); CHECK(mkdir(directory, 0700) == 0);
    has_present = previous != NULL; marker = fail_commit = change_id = change_source = turn_off = mismatch = false;
    downloads = opens = commits = radio_holds = network_error = 0; sequence[0] = '\0';
    config.app_auto_update = 1; strcpy(source_text, CATALOG_OFFICIAL_SOURCE);
    new_manifest = NEW; new_certificate = NULL;
    if (previous != NULL) { CHECK(app_manifest_parse(previous, &present) == APP_MANIFEST_OK); present.id = 1; put_file("manifest.json", previous); put_file("app.wasm", module); }
}
static void cleanup(void) {
    char folder[APP_STORE_PATH_MAX]; snprintf(folder, sizeof folder, "%s/test", test_root); DIR *directory = opendir(folder); struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) { if (entry->d_name[0] == '.') continue; char file[APP_STORE_PATH_MAX]; file_path(file, entry->d_name); unlink(file); }
    closedir(directory); rmdir(folder); rmdir(test_root);
    CHECK(!mutex_taken && !app_install_app_is_busy("test") && radio_holds == 0);
}
static const char *run(bool automatic) {
    json_t *files = files_for(); app_install_options_t options = { .installed_id = automatic ? 1 : 0, .version = "1.1.0", .retryable = &last_retryable };
    const char *result = app_install_run(CATALOG_OFFICIAL_SOURCE, 0, "test", files, &options); json_free(files); return result;
}
int main(void) {
    app_install_start();
    reset(NULL); CHECK(run(false) == NULL); CHECK_STR(sequence, "CRO"); CHECK(opens == 1); cleanup();
    reset(OLD); CHECK(run(false) == NULL); CHECK_STR(sequence, "CR"); CHECK(opens == 0); cleanup();
    reset(OLD); CHECK(run(true) == NULL); CHECK_STR(sequence, "CR"); CHECK(opens == 0); cleanup();
    reset(NULL); network_error = HOST_FAILED_NO_ANSWER; CHECK_STR(run(false), "Download failed."); CHECK(commits == 0 && opens == 0 && last_retryable); cleanup();
    reset(NULL); network_error = HOST_FAILED_NOT_TRUSTED; CHECK_STR(run(false), "Download failed."); CHECK(commits == 0 && opens == 0 && !last_retryable); cleanup();
    reset(NULL); mismatch = true; CHECK_STR(run(false), "Checksum mismatch."); CHECK(commits == 0 && opens == 0 && !last_retryable); cleanup();
    reset(NULL); new_manifest = OLD; CHECK_STR(run(false), "Invalid manifest."); CHECK(commits == 0 && opens == 0); cleanup();
    reset(NULL); fail_commit = true; CHECK_STR(run(false), "Storage error."); CHECK_STR(sequence, "CRR"); CHECK(has_present && opens == 0 && !marker); fail_commit = false; CHECK(run(false) == NULL); CHECK(opens == 0); cleanup();
    reset(OLD); change_id = true; CHECK_STR(run(true), "App or source changed."); CHECK(commits == 0 && opens == 0); cleanup();
    reset(OLD); change_source = true; CHECK_STR(run(true), "App or source changed."); CHECK(commits == 0 && opens == 0); cleanup();
    reset(OLD); turn_off = true; CHECK_STR(run(true), "Automatic updates disabled."); CHECK(commits == 0); cleanup();
    reset(OLD); new_manifest = "{\"name\":\"test\",\"title\":\"Test\",\"version\":\"1.1.0\",\"abi\":3,\"system\":true}"; CHECK_STR(run(true), "Update requires manual installation."); CHECK(commits == 0); cleanup();
    reset("{\"name\":\"test\",\"title\":\"Test\",\"version\":\"1.0.0\",\"abi\":3,\"certificates\":[\"root.pem\"]}");
    put_file("root.pem", "old authority"); new_certificate = "new authority";
    new_manifest = "{\"name\":\"test\",\"title\":\"Test\",\"version\":\"1.1.0\",\"abi\":3,\"certificates\":[\"root.pem\"]}";
    CHECK_STR(run(true), "Update requires manual installation."); CHECK(commits == 0); new_certificate = "old authority"; CHECK(run(true) == NULL); CHECK(opens == 0); cleanup();
    /* A name twice in an entry is refused before anything is downloaded. */
    reset(NULL); data_name = "manifest.json"; new_certificate = NEW;
    CHECK_STR(run(false), "Invalid catalog entry."); CHECK(downloads == 0 && commits == 0); data_name = "root.pem"; cleanup();
    /* An update keeps the files it does not replace, and a writer's quota counts them. */
    static const char WRITER[] = "{\"name\":\"test\",\"title\":\"Test\",\"version\":\"1.1.0\",\"abi\":3,\"writes_files\":true}";
    static char kept[HOST_FILES_WRITER_MAX - 2 * MANIFEST_FILE_BLOCK_BYTES + 2]; /* a byte past six blocks */
    memset(kept, 'k', sizeof kept - 1);
    reset(OLD); new_manifest = WRITER; new_certificate = "data"; data_name = "new.dat"; put_file("old.dat", kept);
    CHECK_STR(run(false), "Not enough storage."); CHECK(commits == 0); cleanup();
    reset(OLD); new_manifest = WRITER; new_certificate = "data"; data_name = "old.dat"; put_file("old.dat", kept);
    CHECK(run(false) == NULL); CHECK(commits == 1); cleanup();
    kept[sizeof kept - 2] = '\0'; /* a block less beside the package's: it fits */
    reset(OLD); new_manifest = WRITER; new_certificate = "data"; data_name = "new.dat"; put_file("old.dat", kept);
    CHECK(run(false) == NULL); CHECK(commits == 1); data_name = "root.pem"; cleanup();
    CHECK(app_install_network_is_retryable(503)); CHECK(app_install_network_is_retryable(HOST_FAILED_TIMEOUT)); CHECK(!app_install_network_is_retryable(404)); CHECK(!app_install_network_is_retryable(HOST_FAILED_NOT_TRUSTED));
    REPORT();
}
