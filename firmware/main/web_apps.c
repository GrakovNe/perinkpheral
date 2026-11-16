/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "web_apps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_tree.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"

#include "app_delivery.h"
#include "app_kv.h"
#include "app_code.h"
#include "app_manifest.h"
#include "app_room.h"
#include "app_store.h"
#include "catalog.h"
#include "host_api.h"
#include "events.h"
#include "gfx_icons.h"
#include "glyphs.h"
#include "words.h"
#include "host.h"
#include "launcher.h"
#include "power.h"
#include "poller.h"
#include "settings.h"
#include "storage.h"
#include "web_server.h"
#include "units.h"

static const char *TAG = "web_apps";

#define APP_NAME_BUFFER (APP_NAME_MAX + 1)
#define BODY_MAX        2048
/* Eight settings of 255 bytes each, as JSON writes them: quotes and
 * backslashes in them take twice their room. */
#define SETTINGS_BODY_MAX 4608
#define SHA256_BYTES    32
/* 500 applications of a few hundred bytes each. */
#define CATALOG_INDEX_MAX_BYTES (512 * 1024)

/* The application the request names, if it is installed. */
/* The installed applications as they are now, copied for this request:
 * the list is the host loop's. NULL when there is no memory for the copy;
 * the caller frees it. */
static app_manifest_t *read_installed(int *count)
{
    app_manifest_t *copy = malloc(LAUNCHER_INSTALLED_MAX * sizeof *copy);
    *count = copy != NULL ? launcher_read_installed(copy, LAUNCHER_INSTALLED_MAX) : 0;
    return copy;
}

static bool is_installed(const char *app_name)
{
    int count;
    app_manifest_t *installed = read_installed(&count);
    bool found = false;
    for (int i = 0; i < count && !found; i++) {
        found = strcmp(installed[i].name, app_name) == 0;
    }
    free(installed);
    return found;
}

static bool asked_app(httpd_req_t *request, char name[APP_NAME_BUFFER])
{
    return web_query_value(request, "app", name, APP_NAME_BUFFER) && is_installed(name);
}

/* A setting as text: what is in the store under its key, or empty. */
static void stored_value(const char *app_name, const char *key,
                         char value[APP_SETTING_VALUE_MAX + 1])
{
    int32_t length = app_kv_get(app_name, key, value, APP_SETTING_VALUE_MAX);
    value[length > 0 ? length : 0] = '\0';
}

/* What the application says of itself in the device's language, or NULL. */
static json_t *translation_of(const char *app_name)
{
    appconf_t settings;
    settings_read(&settings);
    if (strcmp(settings.language, "en") == 0) {
        return NULL;
    }
    char  *text = app_store_read_translation(STORAGE_APPS_ROOT, app_name, settings.language);
    json_t *translation = text != NULL ? json_parse(text) : NULL;
    free(text);
    return translation;
}

/* A setting as the page shows it: its title and help in the device's
 * language, and for a choice a label beside every option - the options
 * themselves stay what is stored and what the application compares. */
static void translate_setting(json_t *setting, const json_t *translated)
{
    static const char *const WORDS[] = { "title", "help" };
    for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++) {
        const json_t *text = json_get(translated, WORDS[i]);
        if (json_is_string(text) && text->text[0] != '\0') {
            json_remove(setting, WORDS[i]);
            json_add_string(setting, WORDS[i], text->text);
        }
    }
    const json_t *names = json_get(translated, "options");
    const json_t *options = json_get(setting, "options");
    if (!json_is_object(names) || !json_is_array(options)) {
        return;
    }
    json_t       *labels = json_new_array();
    const json_t *option;
    json_for_each(option, options)
    {
        const json_t *label = json_is_string(option)
                                 ? json_get(names, option->text)
                                 : NULL;
        json_array_add(labels, json_new_string(json_is_string(label) ? label->text
                                                        : json_is_string(option) ? option->text
                                                                                 : ""));
    }
    json_add_item(setting, "labels", labels);
}

/* The manifest's "settings" as they are, and beside them what is set. */
/* The name of a certificate authority, as its subject's CN says it; empty
 * when the file is no certificate. */
static void authority_of(const char *app_name, const char *file, char *name, size_t size)
{
    name[0] = '\0';
    long length = app_store_file_size(STORAGE_APPS_ROOT, app_name, file);
    uint8_t *bytes = length > 0 && length <= MANIFEST_CERTIFICATE_MAX_BYTES ? malloc((size_t)length + 1) : NULL;
    if (bytes == NULL || app_store_read_file(STORAGE_APPS_ROOT, app_name, file, 0, bytes, (size_t)length) != length) {
        free(bytes);
        return;
    }
    bytes[length] = '\0';
    bool is_pem = length > 10 && memcmp(bytes, "-----BEGIN", 10) == 0;
    mbedtls_x509_crt certificate;
    mbedtls_x509_crt_init(&certificate);
    char subject[256];
    if (mbedtls_x509_crt_parse(&certificate, bytes, (size_t)length + (is_pem ? 1 : 0)) == 0 &&
        mbedtls_x509_dn_gets(subject, sizeof subject, &certificate.subject) > 0) {
        const char *common = strstr(subject, "CN=");
        if (common != NULL) {
            common += 3;
            snprintf(name, size, "%.*s", (int)strcspn(common, ","), common);
        }
    }
    mbedtls_x509_crt_free(&certificate);
    free(bytes);
}

/* "trusts": the certificate authorities it trusts beside the browsers'
 * set, by their names, for the page to say before anything else. */
static void add_trusts(json_t *app, const char *app_name, const char *manifest_text)
{
    char files[MANIFEST_CERTIFICATES_MAX][MANIFEST_FILE_NAME_MAX + 1];
    int  count = app_manifest_certificates(manifest_text, files, MANIFEST_CERTIFICATES_MAX);
    if (count == 0) {
        return;
    }
    json_t *trusts = json_new_array();
    for (int i = 0; i < count; i++) {
        char name[96];
        authority_of(app_name, files[i], name, sizeof name);
        json_array_add(trusts, json_new_string(name[0] != '\0' ? name : files[i]));
    }
    json_add_item(app, "trusts", trusts);
}

static void add_settings(json_t *app, const char *app_name)
{
    char  *manifest_text = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
    json_t *manifest = manifest_text != NULL ? json_parse(manifest_text) : NULL;
    if (manifest_text != NULL) {
        add_trusts(app, app_name, manifest_text);
    }
    free(manifest_text);
    json_t *settings = json_detach(manifest, "settings");
    json_t *icon = json_detach(manifest, "icon");
    json_t *companion = json_detach(manifest, "companion");
    /* What the application is and reaches, for the page to say in words. */
    static const char *const FACTS[] = { "hosts", "polls", "intents", "system", "writes_files",
                                         "fullscreen" };
    for (size_t i = 0; i < sizeof FACTS / sizeof FACTS[0]; i++) {
        json_t *fact = json_detach(manifest, FACTS[i]);
        if (fact != NULL) {
            json_add_item(app, FACTS[i], fact);
        }
    }
    json_free(manifest);
    if (companion != NULL) {
        json_add_item(app, "companion", companion);
    }
    if (icon != NULL) {
        json_add_item(app, "icon", icon);
    }
    if (settings == NULL) {
        settings = json_new_array();
    }
    json_t *values = json_new_object();
    json_t *translation = translation_of(app_name);
    json_t *setting;
    json_for_each(setting, settings)
    {
        const json_t *key = json_get(setting, "key");
        const json_t *type = json_get(setting, "type");
        if (!json_is_string(key) || !json_is_string(type)) {
            continue;
        }
        translate_setting(setting, json_get(
                                       json_get(translation, "settings"),
                                       key->text));
        char value[APP_SETTING_VALUE_MAX + 1];
        stored_value(app_name, key->text, value);
        if (strcmp(type->text, "secret") == 0 || strcmp(type->text, "link") == 0) {
            /* A secret goes one way: the page learns only whether there is one.
             * A link is one too - whoever has a calendar's address reads the
             * calendar. */
            json_add_bool(values, key->text, value[0] != '\0');
        } else {
            json_add_string(values, key->text, value);
        }
    }
    json_free(translation);
    json_add_item(app, "settings", settings);
    json_add_item(app, "values", values);
}

static esp_err_t handle_list(httpd_req_t *request)
{
    int             count;
    app_manifest_t *installed = read_installed(&count);
    if (installed == NULL) {
        return web_send_message(request, "503 Service Unavailable", "error", "Not enough memory.");
    }
    json_t *apps = json_new_array();
    for (int i = 0; i < count; i++) {
        json_t *app = json_new_object();
        json_add_string(app, "name", installed[i].name);
        json_add_string(app, "title", installed[i].title);
        json_add_string(app, "version", installed[i].version);
        add_settings(app, installed[i].name);
        json_array_add(apps, app);
    }
    free(installed);
    return web_send_json(request, "200 OK", apps);
}

/* A form is checked as a whole before any of it is stored: one value
 * that does not fit refuses all of them. `problem` names it. */
static bool are_acceptable(const char *manifest, const json_t *wanted, const char **problem)
{
    const json_t *value;
    json_for_each(value, wanted)
    {
        if (!json_is_string(value) ||
            !app_manifest_accepts_setting(manifest, value->name, value->text, NULL)) {
            *problem = value->name;
            return false;
        }
    }
    return true;
}

/* Asked of the host loop: the store is the application's, and it may be
 * writing there itself. */
typedef struct {
    const char  *app_name;
    const json_t *wanted;
    const char  *status;      /* of the answer to the page */
    char         message[WEB_MESSAGE_MAX];
} app_settings_command_t;

/* Whether the request puts another server where one was set. */
static bool references_other_server(const char *manifest, const char *app_name, const json_t *wanted)
{
    const json_t *value;
    json_for_each(value, wanted)
    {
        int type = app_manifest_setting_type(manifest, value->name);
        if (type != APP_SETTING_URL && type != APP_SETTING_LINK) {
            continue;
        }
        char stored[APP_SETTING_VALUE_MAX + 1];
        stored_value(app_name, value->name, stored);
        if (stored[0] != '\0' && strcmp(stored, value->text) != 0) {
            return true;
        }
    }
    return false;
}

/* The token was given for the server that was there. Whoever points the
 * application at another one - anybody on the network can - does not get
 * the token sent to it: what is not given anew goes. */
static void clear_stale_secrets(const char *manifest, const char *app_name,
                                     const json_t *wanted)
{
    char keys[APP_SETTINGS_MAX][APP_SETTING_KEY_MAX + 1];
    int  count = app_manifest_secret_keys(manifest, keys, APP_SETTINGS_MAX);
    for (int i = 0; i < count; i++) {
        const json_t *given = json_get(wanted, keys[i]);
        if (!json_is_string(given) || given->text[0] == '\0') {
            app_kv_set(app_name, keys[i], "", 0);
        }
    }
}

static void change_app_settings(void *argument)
{
    app_settings_command_t *command = argument;
    char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, command->app_name);
    const char *problem = NULL;
    if (!json_is_object(command->wanted) || manifest == NULL) {
        command->status = "400 Bad Request";
        snprintf(command->message, sizeof command->message, "Invalid request.");
    } else if (!are_acceptable(manifest, command->wanted, &problem)) {
        command->status = "400 Bad Request";
        snprintf(command->message, sizeof command->message, "Invalid value: %s.", problem);
    } else {
        const json_t *value;
        bool         stored = true;
        if (references_other_server(manifest, command->app_name, command->wanted)) {
            clear_stale_secrets(manifest, command->app_name, command->wanted);
        }
        bool    is_full = false;
        int32_t written = 0;
        json_for_each(value, command->wanted)
        {
            /* An empty value takes the setting back: kv_set removes the key. */
            int32_t result = app_kv_set(command->app_name, value->name, value->text,
                                        (int32_t)strlen(value->text));
            is_full = is_full || result == HOST_ERR_NO_SPACE;
            stored = result == HOST_OK && stored;
            written += result == HOST_OK;
        }
        /* What was written is the application's to know of, all of it or
         * not: the store keeps what fitted, and the user is told the rest. */
        if (written > 0) {
            ESP_LOGI(TAG, "settings of \"%s\" changed", command->app_name);
            app_delivery_t changed = { .kind = APP_DELIVERY_SETTINGS };
            host_deliver(command->app_name, &changed);
            poller_hurry(command->app_name);
        }
        if (stored) {
            command->status = "200 OK";
            snprintf(command->message, sizeof command->message, "Saved.");
        } else if (is_full) {
            command->status = "507 Insufficient Storage";
            snprintf(command->message, sizeof command->message, "Not enough storage.");
        } else {
            command->status = "500 Internal Server Error";
            snprintf(command->message, sizeof command->message, "Storage error.");
        }
    }
    free(manifest);
}

static esp_err_t handle_settings(httpd_req_t *request)
{
    char app_name[APP_NAME_BUFFER];
    if (!asked_app(request, app_name)) {
        return web_send_message(request, "404 Not Found", "error", "App not found.");
    }
    char  *body = web_read_body_alloc(request, SETTINGS_BODY_MAX);
    json_t *wanted = body != NULL ? json_parse(body) : NULL;
    free(body);
    app_settings_command_t command = { .app_name = app_name, .wanted = wanted };
    bool is_asked = events_call(change_app_settings, &command);
    json_free(wanted);
    if (!is_asked) {
        return web_send_message(request, "503 Service Unavailable", "error", "Device busy.");
    }
    return web_send_message(request, command.status,
                        strcmp(command.status, "200 OK") == 0 ? "ok" : "error", command.message);
}

/* ---- what was here before ---- */

#define REMOVED_LISTED_MAX 16

static esp_err_t handle_removed(httpd_req_t *request)
{
    char   names[REMOVED_LISTED_MAX][APP_NAME_MAX + 1];
    int    count = app_kv_orphaned(names, REMOVED_LISTED_MAX);
    json_t *list = json_new_array();
    for (int i = 0; i < count; i++) {
        json_array_add(list, json_new_string(names[i]));
    }
    return web_send_json(request, "200 OK", list);
}

/* ---- glyph packs: letters beyond ASCII ---- */

static esp_err_t handle_glyphs(httpd_req_t *request)
{
    glyphs_pack_t packs[GLYPHS_PACKS_MAX];
    int           count = glyphs_list(packs);
    json_t        *list = json_new_array();
    for (int i = 0; i < count; i++) {
        json_t *pack = json_new_object();
        json_add_string(pack, "name", packs[i].name);
        json_add_number(pack, "bytes", (int64_t)packs[i].bytes);
        json_array_add(list, pack);
    }
    json_t *json = json_new_object();
    json_add_item(json, "packs", list);
    json_add_number(json, "free", (int64_t)glyphs_free_bytes());
    return web_send_json(request, "200 OK", json);
}

typedef struct {
    const uint8_t *pack; /* to install; NULL: `name` is to be removed */
    size_t         size;
    const char    *name;
    bool           is_done;
} glyphs_command_t;

/* In the host loop: the fonts are its, and the picture is another. */
static void change_glyphs(void *argument)
{
    glyphs_command_t *command = argument;
    command->is_done = command->pack != NULL ? glyphs_install(command->pack, command->size)
                                             : glyphs_remove(command->name);
    host_redraw(true);
}

/* POST /api/glyphs, the body a pack as tools/fontgen/mkpack.py makes it. */
static esp_err_t handle_glyphs_install(httpd_req_t *request)
{
    if (request->content_len < 1 || request->content_len > GLYPHS_PACK_MAX_BYTES) {
        return web_send_message(request, "400 Bad Request", "error", "Invalid pack.");
    }
    uint8_t *pack = malloc(request->content_len);
    if (pack == NULL) {
        return httpd_resp_send_500(request);
    }
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, (char *)pack + received,
                                   request->content_len - received);
        if (count <= 0) {
            free(pack);
            return ESP_FAIL;
        }
        received += (size_t)count;
    }
    glyphs_command_t command = { .pack = pack, .size = received };
    bool             is_asked = events_call(change_glyphs, &command);
    free(pack);
    if (!is_asked) {
        return web_send_message(request, "503 Service Unavailable", "error", "Device busy.");
    }
    return command.is_done ? web_send_message(request, "200 OK", "ok", "Installed.")
                           : web_send_message(request, "400 Bad Request", "error", "Invalid pack.");
}

static esp_err_t handle_glyphs_remove(httpd_req_t *request)
{
    char name[GFX_PACK_NAME_MAX + 1] = "";
    web_query_value(request, "name", name, sizeof name);
    glyphs_command_t command = { .name = name };
    if (!events_call(change_glyphs, &command)) {
        return web_send_message(request, "503 Service Unavailable", "error", "Device busy.");
    }
    return command.is_done ? web_send_message(request, "200 OK", "ok", "Removed.")
                           : web_send_message(request, "404 Not Found", "error", "Not installed.");
}

/* ---- where applications come from ---- */

static esp_err_t handle_sources(httpd_req_t *request)
{
    char *sources = malloc(CATALOG_SOURCES_TEXT_MAX);
    if (sources == NULL) {
        return httpd_resp_send_500(request);
    }
    settings_load_sources(sources, CATALOG_SOURCES_TEXT_MAX);
    json_t *list = json_new_array();
    char   source[CATALOG_SOURCE_URL_MAX + 1];
    for (int i = 0; catalog_source_at(sources, i, source, sizeof source); i++) {
        json_array_add(list, json_new_string(source));
    }
    free(sources);
    json_t *json = json_new_object();
    json_add_item(json, "sources", list);
    return web_send_json(request, "200 OK", json);
}

static esp_err_t handle_set_sources(httpd_req_t *request)
{
    char  *body = web_read_body_alloc(request, BODY_MAX);
    json_t *json = body != NULL ? json_parse(body) : NULL;
    free(body);
    const json_t *list = json_get(json, "sources");
    char        *sources = calloc(1, CATALOG_SOURCES_TEXT_MAX + 1);
    bool         fits = json_is_array(list) && sources != NULL;
    size_t       used = 0;
    const json_t *source;
    json_for_each(source, list)
    {
        size_t length = json_is_string(source) ? strlen(source->text) : 0;
        fits = fits && length > 0 && used + length + 1 <= CATALOG_SOURCES_TEXT_MAX;
        /* The official source is there anyway: it is not stored with the rest. */
        bool is_official = fits && strcmp(source->text, CATALOG_OFFICIAL_SOURCE) == 0;
        if (fits && !is_official) {
            memcpy(sources + used, source->text, length);
            sources[used + length] = '\n';
            used += length + 1;
        }
    }
    esp_err_t result;
    if (fits && catalog_source_count(sources) > CATALOG_ADDED_SOURCES_MAX) {
        result = web_send_message(request, "400 Bad Request", "error", "Too many sources.");
    } else if (!fits || !catalog_sources_are_valid(sources)) {
        result = web_send_message(request, "400 Bad Request", "error",
                              "Invalid source URL.");
    } else if (settings_save_sources(sources) != ESP_OK) {
        result = web_send_message(request, "500 Internal Server Error", "error",
                              "Storage error.");
    } else {
        result = web_send_message(request, "200 OK", "ok", "Saved.");
    }
    json_free(json);
    free(sources);
    return result;
}

/* The source the request names by its number, as an address. */
static bool asked_source(httpd_req_t *request, char source[CATALOG_SOURCE_URL_MAX + 1])
{
    char number[INT32_TEXT_BYTES];
    if (!web_query_value(request, "source", number, sizeof number)) {
        return false;
    }
    char *sources = malloc(CATALOG_SOURCES_TEXT_MAX);
    if (sources == NULL) {
        return false;
    }
    settings_load_sources(sources, CATALOG_SOURCES_TEXT_MAX);
    bool found = catalog_source_at(sources, atoi(number), source, CATALOG_SOURCE_URL_MAX + 1);
    free(sources);
    return found;
}

/* The index of a source goes to the page as it comes: the device does
 * not read it, and it may be longer than the device has memory for. */
typedef struct {
    httpd_req_t *request;
    bool         has_started;
} passing_t;

static bool pass_on(void *context, const uint8_t *piece, size_t length)
{
    passing_t *passing = context;
    if (!passing->has_started) {
        httpd_resp_set_type(passing->request, "application/json");
        passing->has_started = true;
    }
    return httpd_resp_send_chunk(passing->request, (const char *)piece, (ssize_t)length) == ESP_OK;
}

static esp_err_t handle_catalog(httpd_req_t *request)
{
    char source[CATALOG_SOURCE_URL_MAX + 1], url[CATALOG_URL_MAX + 1];
    if (!asked_source(request, source) ||
        !catalog_url(source, NULL, CATALOG_INDEX_FILE, url, sizeof url)) {
        return web_send_message(request, "404 Not Found", "error", "Source not found.");
    }
    passing_t passing = { request, false };
    int       http_status;
    bool      is_whole = poller_fetch(url, CATALOG_INDEX_MAX_BYTES, pass_on, &passing,
                                      &http_status);
    if (!passing.has_started) {
        char message[WEB_MESSAGE_MAX];
        snprintf(message, sizeof message, "Source unavailable (HTTP %d).",
                 http_status);
        return web_send_message(request, "502 Bad Gateway", "error", message);
    }
    /* Cut short after the beginning went out: closing the connection
     * without the last chunk is how the page learns of it. */
    return is_whole ? httpd_resp_send_chunk(request, NULL, 0) : ESP_FAIL;
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
static const char *install_file(const char *source, const char *app_name, const json_t *entry,
                                bool *asks_for_system, bool *writes_files)
{
    const json_t *name = json_get(entry, "name");
    const json_t *size = json_get(entry, "size");
    const json_t *sha256 = json_get(entry, "sha256");
    uint8_t      wanted_hash[SHA256_BYTES], hash[SHA256_BYTES];
    char         url[CATALOG_URL_MAX + 1], path[APP_STORE_PATH_MAX];
    if (!json_is_string(name) || !json_is_number(size) || !json_is_string(sha256) ||
        !app_store_is_file_name(name->text) || !read_hash(sha256->text, wanted_hash) ||
        size->number < 1 || size->number > app_store_file_max_bytes(name->text) ||
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
    if (!fetched || !stored) {
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
            problem = "Incompatible firmware version.";
        } else if (result != APP_MANIFEST_OK || strcmp(manifest.name, app_name) != 0) {
            problem = "Invalid manifest.";
        } else if (!app_manifest_runs_on(&manifest, esp_app_get_description()->version)) {
            problem = "Update the firmware.";
        } else {
            *asks_for_system = manifest.system;
            *writes_files = manifest.writes_files;
        }
        free(text);
    }
    /* It stays under its temporary name: the host loop puts it in place. */
    if (problem != NULL) {
        remove(path);
    }
    return problem;
}

/* Asked of the host loop: what was fetched takes the place of what was
 * there, and the list of applications is read anew - in one step, with
 * nothing of the host looking at the folder meanwhile. */
typedef struct {
    const char  *app_name;
    const json_t *files;
    bool         is_done;
} install_command_t;

static void commit_install(void *argument)
{
    install_command_t *command = argument;
    command->is_done = app_store_commit_all(STORAGE_APPS_ROOT, command->app_name);
    host_reload_apps();
}

typedef struct {
    const char *app_name;
    bool        forgets; /* what it stored goes too */
    bool        is_done;
} remove_command_t;

static void remove_command(void *argument)
{
    remove_command_t *command = argument;
    host_close_app(command->app_name);
    command->is_done = launcher_remove(command->app_name, command->forgets);
    host_reload_apps();
}


/* The files as the page read them from the index: the module first, the
 * manifest last, and both there. */
static bool is_sound_file_list(const json_t *files)
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
    /* The two have a size, as the room for them is reckoned from it. */
    const json_t *first_size = json_get(json_at(files, 0), "size");
    const json_t *last_size = json_get(json_at(files, count - 1), "size");
    if (!json_is_number(first_size) || first_size->number < 0 ||
        first_size->number > APP_MODULE_MAX_BYTES || !json_is_number(last_size) ||
        last_size->number < 0 || last_size->number > app_store_file_max_bytes(APP_FILE_MANIFEST)) {
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
static long blocks_of(double bytes)
{
    return ((long)bytes + MANIFEST_FILE_BLOCK_BYTES - 1) / MANIFEST_FILE_BLOCK_BYTES *
           MANIFEST_FILE_BLOCK_BYTES;
}

static long data_files_bytes(const json_t *files)
{
    long taken = 0;
    for (int i = 1; i < json_count(files) - 1; i++) {
        taken += blocks_of(json_get(json_at(files, i), "size")->number);
    }
    return taken;
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

static esp_err_t handle_install(httpd_req_t *request)
{
    char app_name[APP_NAME_BUFFER], source[CATALOG_SOURCE_URL_MAX + 1];
    if (!asked_source(request, source) || !web_query_value(request, "app", app_name, sizeof app_name) ||
        !app_manifest_is_valid_name(app_name)) {
        return web_send_message(request, "400 Bad Request", "error", "Invalid request.");
    }
    int             count;
    app_manifest_t *installed = read_installed(&count);
    if (installed == NULL) {
        return web_send_message(request, "503 Service Unavailable", "error", "Not enough memory.");
    }
    free(installed);
    bool was_installed = is_installed(app_name);
    char  *body = web_read_body_alloc(request, BODY_MAX);
    json_t *json = body != NULL ? json_parse(body) : NULL;
    free(body);
    const json_t *files = json_get(json, "files");
    if (!is_sound_file_list(files)) {
        json_free(json);
        return web_send_message(request, "400 Bad Request", "error",
                            "Invalid request.");
    }
    /* Room, before a byte is fetched: a store for one more (every one has
     * its store's share kept), its code, and its files beside what is
     * promised to those that write. Whether it writes itself is known
     * when its manifest is here. */
    long module_bytes = (long)json_get(json_at(files, 0), "size")->number;
    long files_bytes = data_files_bytes(files);
    long manifest_bytes = blocks_of(json_get(json_at(files, json_count(files) - 1), "size")->number);
    if ((!was_installed && count >= CATALOG_INSTALLED_MAX) ||
        !app_code_has_room(app_name, (size_t)module_bytes) ||
        !files_room_holds(app_name, false, files_bytes,
                          blocks_of((double)module_bytes) + files_bytes + manifest_bytes)) {
        json_free(json);
        return web_send_message(request, "409 Conflict", "error", "Not enough storage.");
    }
    const char  *problem = NULL;
    const json_t *entry;
    bool         asks_for_system = false, writes_files = false;
    /* What an install that failed left behind is not this one's. */
    app_store_drop_incoming(STORAGE_APPS_ROOT, app_name);
    /* The device stays on the network until the last file is here. */
    power_hold_radio();
    json_for_each(entry, files)
    {
        problem = install_file(source, app_name, entry, &asks_for_system, &writes_files);
        if (problem != NULL) {
            break;
        }
    }
    power_release_radio();
    /* Here, it takes what it fetched; one that writes has its share kept
     * from now on. */
    if (problem == NULL && writes_files && !files_room_holds(app_name, true, files_bytes, 0)) {
        problem = "Not enough storage.";
    }
    /* An application with the system API can do what the network may not
     * do unasked - reset the device, forget its network: installing one is
     * asked about like those. */
    if (problem == NULL && asks_for_system) {
        host_answer_t answer = host_confirm_on_device(word(WORD_INSTALL), word(WORD_INSTALL_SYSTEM_APP),
                                                      GFX_ICON_GEAR, word(WORD_INSTALL));
        problem = answer == HOST_ANSWER_CONFIRMED ? NULL
                  : answer == HOST_ANSWER_BUSY    ? "Device busy."
                                                  : "Not confirmed.";
    }
    if (problem == NULL) {
        install_command_t command = { .app_name = app_name, .files = files };
        if (!events_call(commit_install, &command)) {
            problem = "Device busy.";
        } else if (!command.is_done) {
            problem = "Storage error.";
        }
    }
    json_free(json);
    if (problem != NULL) {
        /* What was fetched and not put in place is nothing: a new
         * application's folder goes, an installed one stays as it was. */
        if (!was_installed) {
            remove_command_t undo = { .app_name = app_name };
            events_call(remove_command, &undo);
        }
        ESP_LOGW(TAG, "\"%s\" not installed: %s", app_name, problem);
        /* Whose fault: the device's being busy, its storage's, or the
         * source's, which sent something else than its index promised. */
        const char *status = strcmp(problem, "Device busy.") == 0     ? "503 Service Unavailable"
                             : strcmp(problem, "Storage error.") == 0 ? "500 Internal Server Error"
                             : strcmp(problem, "Not enough storage.") == 0 ? "409 Conflict"
                             : strcmp(problem, "Not confirmed.") == 0 ? "403 Forbidden"
                                                                      : "502 Bad Gateway";
        return web_send_message(request, status, "error", problem);
    }
    ESP_LOGI(TAG, "\"%s\" installed", app_name);
    return web_send_message(request, "200 OK", "ok", "Installed.");
}

/* What the application stored - its settings among it - stays unless the
 * user asks otherwise: applications come and go, and one that comes back
 * should not have to be set up again. */
static esp_err_t handle_remove(httpd_req_t *request)
{
    char app_name[APP_NAME_BUFFER], forget[INT32_TEXT_BYTES] = "";
    if (!asked_app(request, app_name)) {
        return web_send_message(request, "404 Not Found", "error", "App not found.");
    }
    web_query_value(request, "forget", forget, sizeof forget);
    remove_command_t command = { .app_name = app_name, .forgets = strcmp(forget, "1") == 0 };
    if (!events_call(remove_command, &command)) {
        return web_send_message(request, "503 Service Unavailable", "error", "Device busy.");
    }
    if (!command.is_done) {
        return web_send_message(request, "500 Internal Server Error", "error", "Storage error.");
    }
    ESP_LOGI(TAG, "\"%s\" removed", app_name);
    return web_send_message(request, "200 OK", "ok", "Removed.");
}

const web_route_t WEB_APPS_ROUTES[] = {
    { "/api/apps", HTTP_GET, handle_list, MEMORY_FOR_A_PAGE, WEB_OWN_PAGE },
    { "/api/apps/settings", HTTP_POST, handle_settings, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/apps/removed", HTTP_GET, handle_removed, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/glyphs", HTTP_GET, handle_glyphs, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/glyphs", HTTP_POST, handle_glyphs_install, MEMORY_FOR_A_PAGE, WEB_OWN_PAGE },
    { "/api/glyphs/remove", HTTP_POST, handle_glyphs_remove, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/apps/remove", HTTP_POST, handle_remove, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/apps/install", HTTP_POST, handle_install, MEMORY_FOR_A_SESSION, WEB_OWN_PAGE },
    { "/api/catalog", HTTP_GET, handle_catalog, MEMORY_FOR_A_SESSION, WEB_OWN_PAGE },
    { "/api/sources", HTTP_GET, handle_sources, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/sources", HTTP_POST, handle_set_sources, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
};
const size_t WEB_APPS_ROUTE_COUNT = WEB_ROUTE_COUNT(WEB_APPS_ROUTES);
