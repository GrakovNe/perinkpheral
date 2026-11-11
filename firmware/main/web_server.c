/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "web_server.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <time.h>

#include "devtools.h"
#include "log_capture.h"
#include "net_time.h"
#include "ota.h"
#include "reset.h"
#include "settings.h"
#include "status.h"
#include "wifi.h"

static const char *TAG = "web";

#define SETTINGS_BODY_MAX     512
#define UPLOAD_CHUNK_BYTES    4096
/* Long enough for the answer to reach the browser before the device goes. */
#define RESTART_DELAY_US      (1500 * 1000)
#define VALIDATION_TEXT_MAX   128
/* How long a sender may say nothing in the middle of a request. */
#define RECEIVE_TIMEOUT_S     10
#define QUIET_PERIODS_MAX     3
#define HOST_HEADER_MAX       64
#define LOCAL_TIME_MAX        24
#define OWN_ROUTES            7

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static appconf_t *s_config;
static char       s_device_id[5];
static bool       s_is_up;

static void restart_now(void *arg)
{
    (void)arg;
    esp_restart();
}

void web_server_restart_soon(void)
{
    static esp_timer_handle_t timer;
    esp_timer_create_args_t args = { .callback = restart_now, .name = "restart" };
    if (timer == NULL && esp_timer_create(&args, &timer) != ESP_OK) {
        esp_restart(); /* no way to wait: the answer may be lost, the restart not */
    }
    /* Already counting down is fine: the device restarts either way. */
    esp_timer_start_once(timer, RESTART_DELAY_US);
}

static esp_err_t send_json(httpd_req_t *request, const char *http_status, cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (text == NULL) {
        return httpd_resp_send_500(request);
    }
    httpd_resp_set_status(request, http_status);
    httpd_resp_set_type(request, "application/json");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

static esp_err_t send_error(httpd_req_t *request, const char *message)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "error", message);
    return send_json(request, "400 Bad Request", json);
}

static esp_err_t send_ok(httpd_req_t *request, const char *message)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "ok", message);
    return send_json(request, "200 OK", json);
}

static esp_err_t handle_page(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, index_html_start, index_html_end - index_html_start);
}

static const char *phase_name(wifi_phase_t phase)
{
    switch (phase) {
    case WIFI_PHASE_SETUP:
        return "setup";
    case WIFI_PHASE_CONNECTING:
        return "connecting";
    case WIFI_PHASE_ONLINE:
        return "online";
    case WIFI_PHASE_RESCUE:
        return "rescue";
    }
    return "?";
}

static esp_err_t handle_status(httpd_req_t *request)
{
    wifi_status_t wifi;
    wifi_get_status(&wifi);
    const esp_partition_t *running = esp_ota_get_running_partition();

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "name", s_config->device_name);
    cJSON_AddStringToObject(json, "firmware", esp_app_get_description()->version);
    cJSON_AddStringToObject(json, "slot", running != NULL ? running->label : "?");
    cJSON_AddBoolToObject(json, "on_probation", ota_is_on_probation());
    cJSON_AddStringToObject(json, "wifi", phase_name(wifi.phase));
    cJSON_AddStringToObject(json, "ssid", wifi.ssid);
    cJSON_AddStringToObject(json, "address", wifi.address);
    cJSON_AddStringToObject(json, "tz", s_config->tz);
    if (net_time_is_known()) {
        char   local_time[LOCAL_TIME_MAX];
        time_t now = (time_t)net_time_now();
        struct tm local;
        localtime_r(&now, &local);
        strftime(local_time, sizeof local_time, "%a %d %b %H:%M", &local);
        cJSON_AddStringToObject(json, "time", local_time);
    }
    cJSON_AddNumberToObject(json, "free_memory", esp_get_free_heap_size());
    cJSON_AddNumberToObject(json, "least_free_memory", esp_get_minimum_free_heap_size());
    return send_json(request, "200 OK", json);
}

/* The next bytes of the request body; 0 or less when the client is gone
 * or has stopped sending. One handler runs at a time, so a client that
 * goes quiet must not be waited for without end: it would hold the server
 * and, in an upload, the firmware slot. */
static int receive(httpd_req_t *request, char *buffer, size_t length)
{
    for (int quiet_periods = 0; quiet_periods < QUIET_PERIODS_MAX; quiet_periods++) {
        int count = httpd_req_recv(request, buffer, length);
        if (count != HTTPD_SOCK_ERR_TIMEOUT) {
            return count;
        }
    }
    return 0;
}

static esp_err_t handle_log(httpd_req_t *request)
{
    char *log = malloc(LOG_CAPTURE_BYTES + 1);
    if (log == NULL) {
        return httpd_resp_send_500(request);
    }
    size_t length = log_capture_read(log, LOG_CAPTURE_BYTES + 1);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    esp_err_t err = httpd_resp_send(request, log, (ssize_t)length);
    free(log);
    return err;
}

/* The whole body as a terminated string; false when it is missing, too
 * long or the client went away. */
static bool read_body(httpd_req_t *request, char *body, size_t body_size)
{
    if (request->content_len == 0 || request->content_len >= body_size) {
        return false;
    }
    size_t received = 0;
    while (received < request->content_len) {
        int count = receive(request, body + received, request->content_len - received);
        if (count <= 0) {
            return false;
        }
        received += (size_t)count;
    }
    body[received] = '\0';
    return true;
}

/* False when the field is there and is not a string that fits. A missing
 * field leaves the value as it is. */
static bool take_string(const cJSON *json, const char *field, char *value, size_t value_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, field);
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= value_size) {
        return false;
    }
    strcpy(value, item->valuestring);
    return true;
}

static esp_err_t handle_wifi(httpd_req_t *request)
{
    char body[SETTINGS_BODY_MAX];
    if (!read_body(request, body, sizeof body)) {
        return send_error(request, "send {\"ssid\": \"...\", \"password\": \"...\"}");
    }
    cJSON *json = cJSON_Parse(body);
    /* Worked on in a copy: the live settings change only if all of it is good. */
    appconf_t *wanted = malloc(sizeof *wanted);
    if (json == NULL || wanted == NULL) {
        cJSON_Delete(json);
        free(wanted);
        return send_error(request, "the request is not JSON");
    }
    *wanted = *s_config;
    char problem[VALIDATION_TEXT_MAX] = "";
    bool fields_fit =
        take_string(json, "ssid", wanted->wifi_ssid, sizeof wanted->wifi_ssid) &&
        take_string(json, "password", wanted->wifi_password, sizeof wanted->wifi_password) &&
        take_string(json, "tz", wanted->tz, sizeof wanted->tz);
    cJSON_Delete(json);

    esp_err_t result;
    if (!fields_fit) {
        result = send_error(request, "the network name, the password or the time zone is too long");
    } else if (!appconf_validate(wanted, problem, sizeof problem)) {
        result = send_error(request, problem);
    } else if (settings_save(wanted) != ESP_OK) {
        result = send_error(request, "the settings could not be stored");
    } else {
        *s_config = *wanted;
        appconf_derive(s_config, s_device_id);
        ESP_LOGW(TAG, "Wi-Fi settings changed, restarting");
        result = send_ok(request, "stored, restarting");
        web_server_restart_soon();
    }
    free(wanted);
    return result;
}

/* The time zone takes effect at once: the header and the applications
 * read local time anew whenever they draw. */
static esp_err_t handle_time(httpd_req_t *request)
{
    char body[SETTINGS_BODY_MAX];
    if (!read_body(request, body, sizeof body)) {
        return send_error(request, "send {\"tz\": \"...\"}");
    }
    cJSON *json = cJSON_Parse(body);
    appconf_t *wanted = malloc(sizeof *wanted);
    if (json == NULL || wanted == NULL) {
        cJSON_Delete(json);
        free(wanted);
        return send_error(request, "the request is not JSON");
    }
    *wanted = *s_config;
    char problem[VALIDATION_TEXT_MAX] = "";
    bool has_zone = cJSON_GetObjectItemCaseSensitive(json, "tz") != NULL;
    bool zone_fits = take_string(json, "tz", wanted->tz, sizeof wanted->tz);
    cJSON_Delete(json);

    esp_err_t result;
    if (!has_zone || !zone_fits) {
        result = send_error(request, "send a time zone that fits");
    } else if (!appconf_validate(wanted, problem, sizeof problem)) {
        result = send_error(request, problem);
    } else if (settings_save(wanted) != ESP_OK) {
        result = send_error(request, "the settings could not be stored");
    } else {
        *s_config = *wanted;
        net_time_set_zone(s_config->tz);
        status_check(); /* the date in the header may have just changed */
        result = send_ok(request, "stored");
    }
    free(wanted);
    return result;
}

/* Back to how the device left the bench: the settings and everything the
 * applications stored are erased on the way up, and with no network the
 * device comes back with its setup access point. */
static esp_err_t handle_reset(httpd_req_t *request)
{
    ESP_LOGW(TAG, "factory reset asked for, restarting");
    reset_mark_factory();
    esp_err_t result = send_ok(request, "erased, restarting");
    web_server_restart_soon();
    return result;
}

/* The image is the request body:
 *   curl --data-binary @build/perinkpheral.bin http://<address>/api/ota */
static esp_err_t handle_ota(httpd_req_t *request)
{
    if (request->content_len == 0) {
        return send_error(request, "send the firmware image as the request body");
    }
    const char *error = ota_upload_begin(request->content_len);
    uint8_t *chunk = error == NULL ? malloc(UPLOAD_CHUNK_BYTES) : NULL;
    if (error == NULL && chunk == NULL) {
        ota_upload_abort();
        error = "not enough memory for the upload";
    }
    size_t remaining = request->content_len;
    while (error == NULL && remaining > 0) {
        int count = receive(request, (char *)chunk,
                            remaining < UPLOAD_CHUNK_BYTES ? remaining : UPLOAD_CHUNK_BYTES);
        if (count <= 0) {
            /* The sender is gone: there is nobody to answer. */
            ota_upload_abort();
            free(chunk);
            return ESP_FAIL;
        }
        error = ota_upload_data(chunk, (size_t)count);
        remaining -= (size_t)count;
    }
    free(chunk);
    if (error == NULL) {
        error = ota_upload_end();
    }
    if (error != NULL) {
        return send_error(request, error);
    }
    esp_err_t result = send_ok(request, "installed, restarting");
    web_server_restart_soon();
    return result;
}

/* A phone on the setup access point asks for pages of its own to find out
 * whether it is online; every name leads here (captive_dns), and being
 * sent to the device's page is what makes it show that page. Anybody who
 * asked the device by its address just gets "not found". */
static esp_err_t handle_not_found(httpd_req_t *request, httpd_err_code_t error)
{
    (void)error;
    char host[HOST_HEADER_MAX];
    bool has_host = httpd_req_get_hdr_value_str(request, "Host", host, sizeof host) == ESP_OK;
    wifi_status_t wifi;
    wifi_get_status(&wifi);
    bool asked_us_by_address = has_host && (strcmp(host, WIFI_SETUP_ADDRESS) == 0 ||
                                            strcmp(host, wifi.address) == 0);
    if (asked_us_by_address) {
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "not found");
    }
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "http://" WIFI_SETUP_ADDRESS "/");
    return httpd_resp_send(request, NULL, 0);
}

void web_server_start(appconf_t *config, const char device_id[5])
{
    s_config = config;
    memcpy(s_device_id, device_id, sizeof s_device_id);

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.recv_wait_timeout = RECEIVE_TIMEOUT_S;
    server_config.max_uri_handlers = OWN_ROUTES + DEVTOOLS_ROUTES;
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &server_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot start: %s", esp_err_to_name(err));
        return;
    }
    static const httpd_uri_t ROUTES[OWN_ROUTES] = {
        { .uri = "/", .method = HTTP_GET, .handler = handle_page },
        { .uri = "/api/status", .method = HTTP_GET, .handler = handle_status },
        { .uri = "/api/log", .method = HTTP_GET, .handler = handle_log },
        { .uri = "/api/wifi", .method = HTTP_POST, .handler = handle_wifi },
        { .uri = "/api/time", .method = HTTP_POST, .handler = handle_time },
        { .uri = "/api/ota", .method = HTTP_POST, .handler = handle_ota },
        { .uri = "/api/reset", .method = HTTP_POST, .handler = handle_reset },
    };
    for (size_t i = 0; i < sizeof ROUTES / sizeof ROUTES[0]; i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ROUTES[i]));
    }
#ifdef CONFIG_APP_DEV_CONSOLE
    devtools_register(server);
#endif
    ESP_ERROR_CHECK(httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, handle_not_found));
    s_is_up = true;
    ESP_LOGI(TAG, "serving on port %d", server_config.server_port);
}

bool web_server_is_up(void)
{
    return s_is_up;
}
