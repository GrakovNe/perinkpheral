/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "web_apps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"

#include "app_delivery.h"
#include "app_kv.h"
#include "app_manifest.h"
#include "app_store.h"
#include "catalog.h"
#include "host_api.h"
#include "events.h"
#include "app_code.h"
#include "gfx_icons.h"
#include "host.h"
#include "launcher.h"
#include "power.h"
#include "poller.h"
#include "settings.h"
#include "storage.h"
#include "web_server.h"

static const char *TAG = "web_apps";

#define QUERY_MAX       64
#define APP_NAME_BUFFER (APP_NAME_MAX + 1)
#define BODY_MAX        2048
#define SHA256_BYTES    32
/* 500 applications of a few hundred bytes each. */
#define CATALOG_INDEX_MAX_BYTES (512 * 1024)

static esp_err_t send_json(httpd_req_t *request, const char *status, cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (text == NULL) {
        return httpd_resp_send_500(request);
    }
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

static esp_err_t send_message(httpd_req_t *request, const char *status, const char *field,
                              const char *message)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, field, message);
    return send_json(request, status, json);
}

/* The application the request names, if it is installed. */
/* The installed applications as this task last read them: the list is the
 * host loop's, and what is here may be a moment old. */
static app_manifest_t s_installed[LAUNCHER_INSTALLED_MAX];

static int read_installed(void)
{
    return launcher_read_installed(s_installed, LAUNCHER_INSTALLED_MAX);
}

static bool is_installed(const char *app_name)
{
    int count = read_installed();
    for (int i = 0; i < count; i++) {
        if (strcmp(s_installed[i].name, app_name) == 0) {
            return true;
        }
    }
    return false;
}

static bool asked_app(httpd_req_t *request, char name[APP_NAME_BUFFER])
{
    char query[QUERY_MAX];
    return httpd_req_get_url_query_str(request, query, sizeof query) == ESP_OK &&
           httpd_query_key_value(query, "app", name, APP_NAME_BUFFER) == ESP_OK &&
           is_installed(name);
}

/* A setting as text: what is in the store under its key, or empty. */
static void stored_value(const char *app_name, const char *key,
                         char value[APP_SETTING_VALUE_MAX + 1])
{
    int32_t length = app_kv_get(app_name, key, value, APP_SETTING_VALUE_MAX);
    value[length > 0 ? length : 0] = '\0';
}

/* The manifest's "settings" as they are, and beside them what is set. */
static void add_settings(cJSON *app, const char *app_name)
{
    char  *manifest_text = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
    cJSON *manifest = manifest_text != NULL ? cJSON_Parse(manifest_text) : NULL;
    free(manifest_text);
    cJSON *settings = cJSON_DetachItemFromObjectCaseSensitive(manifest, "settings");
    cJSON *icon = cJSON_DetachItemFromObjectCaseSensitive(manifest, "icon");
    cJSON_Delete(manifest);
    if (icon != NULL) {
        cJSON_AddItemToObject(app, "icon", icon);
    }
    if (settings == NULL) {
        settings = cJSON_CreateArray();
    }
    cJSON *values = cJSON_CreateObject();
    const cJSON *setting;
    cJSON_ArrayForEach(setting, settings)
    {
        const cJSON *key = cJSON_GetObjectItemCaseSensitive(setting, "key");
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(setting, "type");
        if (!cJSON_IsString(key) || !cJSON_IsString(type)) {
            continue;
        }
        char value[APP_SETTING_VALUE_MAX + 1];
        stored_value(app_name, key->valuestring, value);
        if (strcmp(type->valuestring, "secret") == 0) {
            /* A secret goes one way: the page learns only whether there is one. */
            cJSON_AddBoolToObject(values, key->valuestring, value[0] != '\0');
        } else {
            cJSON_AddStringToObject(values, key->valuestring, value);
        }
    }
    cJSON_AddItemToObject(app, "settings", settings);
    cJSON_AddItemToObject(app, "values", values);
}

static esp_err_t handle_list(httpd_req_t *request)
{
    int                   count = read_installed();
    const app_manifest_t *installed = s_installed;
    cJSON                *apps = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *app = cJSON_CreateObject();
        cJSON_AddStringToObject(app, "name", installed[i].name);
        cJSON_AddStringToObject(app, "title", installed[i].title);
        cJSON_AddStringToObject(app, "version", installed[i].version);
        add_settings(app, installed[i].name);
        cJSON_AddItemToArray(apps, app);
    }
    return send_json(request, "200 OK", apps);
}

static char *read_body(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len > BODY_MAX) {
        return NULL;
    }
    char *body = malloc(request->content_len + 1);
    size_t received = 0;
    while (body != NULL && received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) {
            free(body);
            return NULL;
        }
        received += (size_t)count;
    }
    if (body != NULL) {
        body[received] = '\0';
    }
    return body;
}

/* A form is checked as a whole before any of it is stored: one value
 * that does not fit refuses all of them. `problem` names it. */
static bool are_acceptable(const char *manifest, const cJSON *wanted, const char **problem)
{
    const cJSON *value;
    cJSON_ArrayForEach(value, wanted)
    {
        if (!cJSON_IsString(value) ||
            !app_manifest_accepts_setting(manifest, value->string, value->valuestring, NULL)) {
            *problem = value->string;
            return false;
        }
    }
    return true;
}

/* Asked of the host loop: the store is the application's, and it may be
 * writing there itself. */
typedef struct {
    const char  *app_name;
    const cJSON *wanted;
    const char  *status;      /* of the answer to the page */
    char         message[64];
} app_settings_command_t;

static void change_app_settings(void *argument)
{
    app_settings_command_t *command = argument;
    char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, command->app_name);
    const char *problem = NULL;
    if (!cJSON_IsObject(command->wanted) || manifest == NULL) {
        command->status = "400 Bad Request";
        snprintf(command->message, sizeof command->message, "Invalid request.");
    } else if (!are_acceptable(manifest, command->wanted, &problem)) {
        command->status = "400 Bad Request";
        snprintf(command->message, sizeof command->message, "Invalid value: %s.", problem);
    } else {
        const cJSON *value;
        bool         stored = true;
        cJSON_ArrayForEach(value, command->wanted)
        {
            /* An empty value takes the setting back: kv_set removes the key. */
            stored = app_kv_set(command->app_name, value->string, value->valuestring,
                                (int32_t)strlen(value->valuestring)) == HOST_OK &&
                     stored;
        }
        if (stored) {
            ESP_LOGI(TAG, "settings of \"%s\" changed", command->app_name);
            app_delivery_t changed = { .kind = APP_DELIVERY_SETTINGS };
            host_deliver(command->app_name, &changed);
            poller_hurry(command->app_name);
            command->status = "200 OK";
            snprintf(command->message, sizeof command->message, "Saved.");
        } else {
            command->status = "500 Internal Server Error";
            snprintf(command->message, sizeof command->message, "Storage error.");
        }
    }
    free(manifest);
}

static esp_err_t handle_settings(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    char app_name[APP_NAME_BUFFER];
    if (!asked_app(request, app_name)) {
        return send_message(request, "404 Not Found", "error", "App not found.");
    }
    char  *body = read_body(request);
    cJSON *wanted = body != NULL ? cJSON_Parse(body) : NULL;
    free(body);
    app_settings_command_t command = { .app_name = app_name, .wanted = wanted };
    bool is_asked = events_call(change_app_settings, &command);
    cJSON_Delete(wanted);
    if (!is_asked) {
        return send_message(request, "503 Service Unavailable", "error", "Device busy.");
    }
    return send_message(request, command.status,
                        strcmp(command.status, "200 OK") == 0 ? "ok" : "error", command.message);
}

/* ---- where applications come from ---- */

static esp_err_t handle_sources(httpd_req_t *request)
{
    char *sources = malloc(CATALOG_SOURCES_TEXT_MAX);
    if (sources == NULL) {
        return httpd_resp_send_500(request);
    }
    settings_load_sources(sources, CATALOG_SOURCES_TEXT_MAX);
    cJSON *list = cJSON_CreateArray();
    char   source[CATALOG_SOURCE_URL_MAX + 1];
    for (int i = 0; catalog_source_at(sources, i, source, sizeof source); i++) {
        cJSON_AddItemToArray(list, cJSON_CreateString(source));
    }
    free(sources);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddItemToObject(json, "sources", list);
    return send_json(request, "200 OK", json);
}

static esp_err_t handle_set_sources(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    char  *body = read_body(request);
    cJSON *json = body != NULL ? cJSON_Parse(body) : NULL;
    free(body);
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(json, "sources");
    char        *sources = calloc(1, CATALOG_SOURCES_TEXT_MAX + 1);
    bool         fits = cJSON_IsArray(list) && sources != NULL;
    size_t       used = 0;
    const cJSON *source;
    cJSON_ArrayForEach(source, list)
    {
        size_t length = cJSON_IsString(source) ? strlen(source->valuestring) : 0;
        fits = fits && length > 0 && used + length + 1 <= CATALOG_SOURCES_TEXT_MAX;
        /* The official source is there anyway: it is not stored with the rest. */
        bool is_official = fits && strcmp(source->valuestring, CATALOG_OFFICIAL_SOURCE) == 0;
        if (fits && !is_official) {
            memcpy(sources + used, source->valuestring, length);
            sources[used + length] = '\n';
            used += length + 1;
        }
    }
    esp_err_t result;
    if (fits && catalog_source_count(sources) > CATALOG_ADDED_SOURCES_MAX) {
        result = send_message(request, "400 Bad Request", "error", "Too many sources.");
    } else if (!fits || !catalog_sources_are_valid(sources)) {
        result = send_message(request, "400 Bad Request", "error",
                              "Invalid source URL.");
    } else if (settings_save_sources(sources) != ESP_OK) {
        result = send_message(request, "500 Internal Server Error", "error",
                              "Storage error.");
    } else {
        result = send_message(request, "200 OK", "ok", "Saved.");
    }
    cJSON_Delete(json);
    free(sources);
    return result;
}

/* The source the request names by its number, as an address. */
static bool asked_source(httpd_req_t *request, char source[CATALOG_SOURCE_URL_MAX + 1])
{
    char query[QUERY_MAX], number[4];
    if (httpd_req_get_url_query_str(request, query, sizeof query) != ESP_OK ||
        httpd_query_key_value(query, "source", number, sizeof number) != ESP_OK) {
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
        return send_message(request, "404 Not Found", "error", "Source not found.");
    }
    passing_t passing = { request, false };
    int       http_status;
    bool      is_whole = poller_fetch(url, CATALOG_INDEX_MAX_BYTES, pass_on, &passing,
                                      &http_status);
    if (!passing.has_started) {
        char message[64];
        snprintf(message, sizeof message, "Source unavailable (HTTP %d).",
                 http_status);
        return send_message(request, "502 Bad Gateway", "error", message);
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
static const char *install_file(const char *source, const char *app_name, const cJSON *entry,
                                bool *asks_for_system)
{
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(entry, "size");
    const cJSON *sha256 = cJSON_GetObjectItemCaseSensitive(entry, "sha256");
    uint8_t      wanted_hash[SHA256_BYTES], hash[SHA256_BYTES];
    char         url[CATALOG_URL_MAX + 1], path[APP_STORE_PATH_MAX];
    if (!cJSON_IsString(name) || !cJSON_IsNumber(size) || !cJSON_IsString(sha256) ||
        !app_store_is_file_name(name->valuestring) || !read_hash(sha256->valuestring, wanted_hash) ||
        size->valuedouble < 1 || size->valuedouble > app_store_file_max_bytes(name->valuestring) ||
        !catalog_url(source, app_name, name->valuestring, url, sizeof url)) {
        return "Invalid catalog entry.";
    }
    if (!app_store_begin_file(STORAGE_APPS_ROOT, app_name, name->valuestring, path)) {
        return "Storage error.";
    }
    download_t download = { .file = fopen(path, "wb") };
    if (download.file == NULL) {
        return "Storage error.";
    }
    mbedtls_sha256_init(&download.hash);
    mbedtls_sha256_starts(&download.hash, 0);
    int  http_status;
    bool fetched = poller_fetch(url, (size_t)size->valuedouble, store_piece, &download,
                                &http_status);
    mbedtls_sha256_finish(&download.hash, hash);
    mbedtls_sha256_free(&download.hash);
    bool stored = fclose(download.file) == 0;
    const char *problem = NULL;
    if (!fetched || !stored) {
        problem = "Download failed.";
    } else if (download.received != (size_t)size->valuedouble ||
               memcmp(hash, wanted_hash, SHA256_BYTES) != 0) {
        problem = "Checksum mismatch.";
    } else if (strcmp(name->valuestring, APP_FILE_MANIFEST) == 0) {
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
        } else {
            *asks_for_system = manifest.system;
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
    const cJSON *files;
    bool         is_done;
} install_command_t;

static void put_in_place(void *argument)
{
    install_command_t *command = argument;
    const cJSON *entry;
    command->is_done = true;
    cJSON_ArrayForEach(entry, command->files)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        command->is_done = app_store_commit_file(STORAGE_APPS_ROOT, command->app_name,
                                                 name->valuestring) &&
                           command->is_done;
    }
    host_reload_apps();
}

typedef struct {
    const char *app_name;
    bool        forgets; /* what it stored goes too */
    bool        is_done;
} remove_command_t;

static void take_away(void *argument)
{
    remove_command_t *command = argument;
    command->is_done = launcher_remove(command->app_name, command->forgets);
    host_reload_apps();
}


/* The files as the page read them from the index: the module first, the
 * manifest last, and both there. */
static bool is_sound_file_list(const cJSON *files)
{
    int count = cJSON_IsArray(files) ? cJSON_GetArraySize(files) : 0;
    if (count < 2 || count > CATALOG_APP_FILES_MAX) {
        return false;
    }
    const cJSON *first = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(files, 0), "name");
    const cJSON *last =
        cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(files, count - 1), "name");
    return cJSON_IsString(first) && strcmp(first->valuestring, APP_FILE_MODULE) == 0 &&
           cJSON_IsString(last) && strcmp(last->valuestring, APP_FILE_MANIFEST) == 0;
}

static esp_err_t handle_install(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    char query[QUERY_MAX], app_name[APP_NAME_BUFFER], source[CATALOG_SOURCE_URL_MAX + 1];
    if (!asked_source(request, source) ||
        httpd_req_get_url_query_str(request, query, sizeof query) != ESP_OK ||
        httpd_query_key_value(query, "app", app_name, sizeof app_name) != ESP_OK ||
        !app_manifest_is_valid_name(app_name)) {
        return send_message(request, "400 Bad Request", "error", "Invalid request.");
    }
    bool was_installed = is_installed(app_name);
    int  count = read_installed();
    if (!was_installed && count >= CATALOG_INSTALLED_MAX) {
        return send_message(request, "409 Conflict", "error",
                            "No free slots.");
    }
    char  *body = read_body(request);
    cJSON *json = body != NULL ? cJSON_Parse(body) : NULL;
    free(body);
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(json, "files");
    if (!is_sound_file_list(files)) {
        cJSON_Delete(json);
        return send_message(request, "400 Bad Request", "error",
                            "Invalid request.");
    }
    const char  *problem = NULL;
    const cJSON *entry;
    bool         asks_for_system = false;
    /* The device stays on the network until the last file is here. */
    power_hold_radio();
    cJSON_ArrayForEach(entry, files)
    {
        problem = install_file(source, app_name, entry, &asks_for_system);
        if (problem != NULL) {
            break;
        }
    }
    power_release_radio();
    /* An application with the system API can do what the network may not
     * do unasked - reset the device, forget its network: installing one is
     * asked about like those. */
    if (problem == NULL && asks_for_system) {
        host_answer_t answer = host_ask_at_the_device("Install", "system app?", GFX_ICON_GEAR, "Install");
        problem = answer == HOST_ANSWER_CONFIRMED ? NULL
                  : answer == HOST_ANSWER_BUSY    ? "Device busy."
                                                  : "Not confirmed.";
    }
    if (problem == NULL) {
        install_command_t command = { .app_name = app_name, .files = files };
        if (!events_call(put_in_place, &command)) {
            problem = "Device busy.";
        } else if (!command.is_done) {
            problem = "Storage error.";
        }
    }
    cJSON_Delete(json);
    if (problem != NULL) {
        /* What was fetched and not put in place is nothing: a new
         * application's folder goes, an installed one stays as it was. */
        if (!was_installed) {
            remove_command_t undo = { .app_name = app_name };
            events_call(take_away, &undo);
        }
        ESP_LOGW(TAG, "\"%s\" not installed: %s", app_name, problem);
        /* Whose fault: the device's being busy, its storage's, or the
         * source's, which sent something else than its index promised. */
        const char *status = strcmp(problem, "Device busy.") == 0     ? "503 Service Unavailable"
                             : strcmp(problem, "Storage error.") == 0 ? "500 Internal Server Error"
                             : strcmp(problem, "Not confirmed.") == 0 ? "403 Forbidden"
                                                                      : "502 Bad Gateway";
        return send_message(request, status, "error", problem);
    }
    ESP_LOGI(TAG, "\"%s\" installed", app_name);
    return send_message(request, "200 OK", "ok", "Installed.");
}

/* What the application stored - its settings among it - stays unless the
 * user asks otherwise: with five places for applications they come and
 * go, and one that comes back should not have to be set up again. */
static esp_err_t handle_remove(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    char app_name[APP_NAME_BUFFER], query[QUERY_MAX], forget[4] = "";
    if (!asked_app(request, app_name)) {
        return send_message(request, "404 Not Found", "error", "App not found.");
    }
    if (httpd_req_get_url_query_str(request, query, sizeof query) == ESP_OK) {
        httpd_query_key_value(query, "forget", forget, sizeof forget);
    }
    remove_command_t command = { .app_name = app_name, .forgets = strcmp(forget, "1") == 0 };
    if (!events_call(take_away, &command)) {
        return send_message(request, "503 Service Unavailable", "error", "Device busy.");
    }
    if (!command.is_done) {
        return send_message(request, "500 Internal Server Error", "error", "Storage error.");
    }
    ESP_LOGI(TAG, "\"%s\" removed", app_name);
    return send_message(request, "200 OK", "ok", "Removed.");
}

void web_apps_register(httpd_handle_t server)
{
    static const httpd_uri_t ROUTES[WEB_APPS_ROUTES] = {
        { .uri = "/api/apps", .method = HTTP_GET, .handler = handle_list },
        { .uri = "/api/apps/settings", .method = HTTP_POST, .handler = handle_settings },
        { .uri = "/api/apps/remove", .method = HTTP_POST, .handler = handle_remove },
        { .uri = "/api/apps/install", .method = HTTP_POST, .handler = handle_install },
        { .uri = "/api/catalog", .method = HTTP_GET, .handler = handle_catalog },
        { .uri = "/api/sources", .method = HTTP_GET, .handler = handle_sources },
        { .uri = "/api/sources", .method = HTTP_POST, .handler = handle_set_sources },
    };
    for (int i = 0; i < WEB_APPS_ROUTES; i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ROUTES[i]));
    }
}
