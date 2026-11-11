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

#include "app_delivery.h"
#include "app_kv.h"
#include "app_manifest.h"
#include "app_store.h"
#include "host_api.h"
#include "launcher.h"
#include "poller.h"
#include "storage.h"
#include "web_server.h"

static const char *TAG = "web_apps";

#define QUERY_MAX       64
#define APP_NAME_BUFFER (APP_NAME_MAX + 1)
#define BODY_MAX        2048

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
static bool asked_app(httpd_req_t *request, char name[APP_NAME_BUFFER])
{
    char query[QUERY_MAX];
    return httpd_req_get_url_query_str(request, query, sizeof query) == ESP_OK &&
           httpd_query_key_value(query, "app", name, APP_NAME_BUFFER) == ESP_OK &&
           launcher_app_scene(name) != NULL;
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
    cJSON_Delete(manifest);
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
    int                   count;
    const app_manifest_t *installed = launcher_installed(&count);
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

/* All of them or none: a form is saved as a whole. `problem` names the
 * first setting that cannot be stored. */
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

static esp_err_t handle_settings(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    char app_name[APP_NAME_BUFFER];
    if (!asked_app(request, app_name)) {
        return send_message(request, "404 Not Found", "error", "no such application");
    }
    char  *body = read_body(request);
    cJSON *wanted = body != NULL ? cJSON_Parse(body) : NULL;
    free(body);
    char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
    const char *problem = NULL;
    esp_err_t   result;
    if (!cJSON_IsObject(wanted) || manifest == NULL) {
        result = send_message(request, "400 Bad Request", "error",
                              "send the settings as {\"key\": \"value\"}");
    } else if (!are_acceptable(manifest, wanted, &problem)) {
        char message[64];
        snprintf(message, sizeof message, "\"%s\" cannot be set to that", problem);
        result = send_message(request, "400 Bad Request", "error", message);
    } else {
        const cJSON *value;
        bool         stored = true;
        cJSON_ArrayForEach(value, wanted)
        {
            /* An empty value takes the setting back: kv_set removes the key. */
            stored = app_kv_set(app_name, value->string, value->valuestring,
                                (int32_t)strlen(value->valuestring)) == HOST_OK &&
                     stored;
        }
        if (stored) {
            ESP_LOGI(TAG, "settings of \"%s\" changed", app_name);
            app_delivery_t changed = { .kind = APP_DELIVERY_SETTINGS };
            poller_hand_over(app_name, &changed);
            poller_hurry(app_name);
            result = send_message(request, "200 OK", "ok", "saved");
        } else {
            result = send_message(request, "500 Internal Server Error", "error",
                                  "the settings could not be stored");
        }
    }
    cJSON_Delete(wanted);
    free(manifest);
    return result;
}

/* The list of applications is read once, at start, and a scene of the
 * removed one may be on the screen: the device starts over without it. */
static esp_err_t handle_remove(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    char app_name[APP_NAME_BUFFER];
    if (!asked_app(request, app_name)) {
        return send_message(request, "404 Not Found", "error", "no such application");
    }
    if (!app_store_remove(STORAGE_APPS_ROOT, app_name)) {
        return send_message(request, "500 Internal Server Error", "error",
                            "its files could not be removed");
    }
    app_kv_erase(app_name);
    ESP_LOGW(TAG, "\"%s\" removed, restarting", app_name);
    esp_err_t result = send_message(request, "200 OK", "ok", "removed, restarting");
    web_server_restart_soon();
    return result;
}

void web_apps_register(httpd_handle_t server)
{
    static const httpd_uri_t ROUTES[WEB_APPS_ROUTES] = {
        { .uri = "/api/apps", .method = HTTP_GET, .handler = handle_list },
        { .uri = "/api/apps/settings", .method = HTTP_POST, .handler = handle_settings },
        { .uri = "/api/apps/remove", .method = HTTP_POST, .handler = handle_remove },
    };
    for (int i = 0; i < WEB_APPS_ROUTES; i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ROUTES[i]));
    }
}
