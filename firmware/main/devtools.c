/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "devtools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_kv.h"
#include "app_store.h"
#include "display.h"
#include "events.h"
#include "host_api.h"
#include "host.h"
#include "launcher.h"
#include "settings.h"
#include "storage.h"
#include "web_server.h"

static const char *TAG = "devtools";

#define FRAME_BYTES       (DISPLAY_WIDTH / 8 * DISPLAY_HEIGHT)
#define HOST_ANSWER_MS    6000 /* the host may be in the middle of a full panel refresh */
#define QUERY_MAX         96
#define QUERY_VALUE_MAX   24
#define UPLOAD_CHUNK      2048
#define NUMBER_TEXT_MAX   12

/* ---- asking the host loop ---- */

typedef enum {
    REQUEST_SCREEN,
    REQUEST_OPEN,
} request_kind_t;

/* Made by the web server's task, filled in by the host's. Whoever is
 * later frees it: the asker may have given up waiting. */
typedef struct {
    request_kind_t    kind;
    SemaphoreHandle_t answered;
    SemaphoreHandle_t lock;
    bool              abandoned; /* the asker gave up: the host frees it */
    bool              served;    /* the host has filled it in: the asker frees it */
    char              app_name[APP_NAME_MAX + 1]; /* REQUEST_OPEN */
    bool              done;                       /* REQUEST_OPEN: there is such an application */
    uint32_t          frames_shown;               /* REQUEST_SCREEN */
    bool              frame_pending;
    uint8_t           frame[];                    /* REQUEST_SCREEN: FRAME_BYTES */
} request_t;

static void free_request(request_t *request)
{
    vSemaphoreDelete(request->answered);
    vSemaphoreDelete(request->lock);
    free(request);
}

static request_t *new_request(request_kind_t kind)
{
    size_t frame_bytes = kind == REQUEST_SCREEN ? FRAME_BYTES : 0;
    request_t *request = calloc(1, sizeof *request + frame_bytes);
    if (request == NULL) {
        return NULL;
    }
    request->kind = kind;
    request->answered = xSemaphoreCreateBinary();
    request->lock = xSemaphoreCreateMutex();
    if (request->answered == NULL || request->lock == NULL) {
        if (request->answered != NULL) {
            vSemaphoreDelete(request->answered);
        }
        if (request->lock != NULL) {
            vSemaphoreDelete(request->lock);
        }
        free(request);
        return NULL;
    }
    return request;
}

/* True when the host answered in time; the request is then the caller's
 * to read and to free. Otherwise it is gone or will be freed by the host. */
static bool ask_host(request_t *request)
{
    event_t event = { .kind = EVENT_DEV_REQUEST, .dev_request = request };
    if (!events_post(&event)) {
        free_request(request);
        return false;
    }
    if (xSemaphoreTake(request->answered, pdMS_TO_TICKS(HOST_ANSWER_MS)) == pdTRUE) {
        return true;
    }
    /* Too late, or just in time: the lock says which of the two it was. */
    xSemaphoreTake(request->lock, portMAX_DELAY);
    bool served = request->served;
    request->abandoned = !served;
    xSemaphoreGive(request->lock);
    if (served) {
        xSemaphoreTake(request->answered, portMAX_DELAY);
    }
    return served;
}

void devtools_serve(void *opaque, wm_t *wm, uint32_t frames_shown, bool frame_pending)
{
    request_t *request = opaque;
    xSemaphoreTake(request->lock, portMAX_DELAY);
    if (request->abandoned) {
        xSemaphoreGive(request->lock);
        free_request(request);
        return;
    }
    if (request->kind == REQUEST_SCREEN) {
        memcpy(request->frame, display_shown_frame(), FRAME_BYTES);
        request->frames_shown = frames_shown;
        request->frame_pending = frame_pending;
    } else {
        scene_t *scene = launcher_scene(request->app_name);
        request->done = scene != NULL;
        if (scene != NULL) {
            wm_open_anew(wm, scene); /* as if chosen in the menu */
        }
    }
    request->served = true;
    xSemaphoreGive(request->lock);
    /* The last touch: the asker frees the request as soon as it wakes. */
    xSemaphoreGive(request->answered);
}

/* ---- the routes ---- */

static esp_err_t send_text(httpd_req_t *http, const char *status, const char *text)
{
    httpd_resp_set_status(http, status);
    httpd_resp_set_type(http, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(http, text);
}

static esp_err_t refuse(httpd_req_t *http, const char *why)
{
    return send_text(http, "400 Bad Request", why);
}

static bool query_value(httpd_req_t *http, const char *key, char *value, size_t value_size)
{
    char query[QUERY_MAX];
    return httpd_req_get_url_query_str(http, query, sizeof query) == ESP_OK &&
           httpd_query_key_value(query, key, value, value_size) == ESP_OK;
}

/* A 1-bit BMP: two colours, rows from the bottom up, each padded to four
 * bytes. The frame's own bits fit as they are: 0 is ink, 1 is paper. */
#define BMP_HEADER_BYTES 62
#define BMP_ROW_BYTES    ((DISPLAY_WIDTH / 8 + 3) / 4 * 4)

static void put_u32(uint8_t *bytes, uint32_t value)
{
    for (int i = 0; i < 4; i++) {
        bytes[i] = (uint8_t)(value >> (8 * i));
    }
}

static void bmp_header(uint8_t header[BMP_HEADER_BYTES])
{
    memset(header, 0, BMP_HEADER_BYTES);
    header[0] = 'B';
    header[1] = 'M';
    put_u32(header + 2, BMP_HEADER_BYTES + BMP_ROW_BYTES * DISPLAY_HEIGHT);
    put_u32(header + 10, BMP_HEADER_BYTES); /* where the pixels start */
    put_u32(header + 14, 40);               /* the size of the info header */
    put_u32(header + 18, DISPLAY_WIDTH);
    put_u32(header + 22, DISPLAY_HEIGHT);
    header[26] = 1;                         /* one plane */
    header[28] = 1;                         /* one bit per pixel */
    put_u32(header + 34, BMP_ROW_BYTES * DISPLAY_HEIGHT);
    put_u32(header + 46, 2);                /* two colours: ink, then paper */
    put_u32(header + 54, 0x00141413);
    put_u32(header + 58, 0x00E9E7E1);
}

static esp_err_t handle_screen(httpd_req_t *http)
{
    if (web_server_refuses_cross_site(http)) {
        return ESP_OK;
    }
    request_t *request = new_request(REQUEST_SCREEN);
    if (request == NULL || !ask_host(request)) {
        return send_text(http, "503 Service Unavailable", "the host did not answer");
    }
    char frames_shown[NUMBER_TEXT_MAX];
    snprintf(frames_shown, sizeof frames_shown, "%u", (unsigned)request->frames_shown);
    httpd_resp_set_type(http, "image/bmp");
    httpd_resp_set_hdr(http, "X-Frame", frames_shown);
    httpd_resp_set_hdr(http, "X-Pending", request->frame_pending ? "1" : "0");
    httpd_resp_set_hdr(http, "Cache-Control", "no-store");

    uint8_t header[BMP_HEADER_BYTES];
    bmp_header(header);
    esp_err_t err = httpd_resp_send_chunk(http, (const char *)header, sizeof header);
    const int frame_row_bytes = DISPLAY_WIDTH / 8;
    for (int row = DISPLAY_HEIGHT - 1; row >= 0 && err == ESP_OK; row--) {
        uint8_t padded[BMP_ROW_BYTES] = { 0 };
        memcpy(padded, request->frame + row * frame_row_bytes, (size_t)frame_row_bytes);
        err = httpd_resp_send_chunk(http, (const char *)padded, sizeof padded);
    }
    free_request(request);
    return err == ESP_OK ? httpd_resp_send_chunk(http, NULL, 0) : err;
}

static esp_err_t handle_button(httpd_req_t *http)
{
    if (web_server_refuses_cross_site(http)) {
        return ESP_OK;
    }
    static const char *const PRESSES[] = {
        [EVENT_PRESS_DOWN] = "down",
        [EVENT_PRESS_UP] = "up",
        [EVENT_PRESS_SHORT] = "short",
        [EVENT_PRESS_LONG] = "long",
    };
    char button_text[QUERY_VALUE_MAX], press_text[QUERY_VALUE_MAX] = "short";
    if (!query_value(http, "button", button_text, sizeof button_text)) {
        return refuse(http, "button=1..4 is missing");
    }
    query_value(http, "press", press_text, sizeof press_text);
    int button = atoi(button_text);
    int press = -1;
    for (int i = 0; i < (int)(sizeof PRESSES / sizeof PRESSES[0]); i++) {
        if (strcmp(press_text, PRESSES[i]) == 0) {
            press = i;
        }
    }
    if (button < 1 || button > 4 || press < 0) {
        return refuse(http, "button is 1..4, press is short, long, down or up");
    }
    events_post_button((uint8_t)(button - 1), (event_press_t)press);
    return send_text(http, "200 OK", "pressed");
}

static esp_err_t handle_open(httpd_req_t *http)
{
    if (web_server_refuses_cross_site(http)) {
        return ESP_OK;
    }
    request_t *request = new_request(REQUEST_OPEN);
    if (request == NULL) {
        return send_text(http, "503 Service Unavailable", "no memory");
    }
    if (!query_value(http, "app", request->app_name, sizeof request->app_name)) {
        free_request(request);
        return refuse(http, "app=<name> is missing");
    }
    if (!ask_host(request)) {
        return send_text(http, "503 Service Unavailable", "the host did not answer");
    }
    bool done = request->done;
    free_request(request);
    return done ? send_text(http, "200 OK", "opened")
                : send_text(http, "404 Not Found", "no such application is installed");
}

/* The body goes to the file a chunk at a time; false when the client went
 * away or the file system is full. */
static bool receive_into(httpd_req_t *http, FILE *file)
{
    static char chunk[UPLOAD_CHUNK]; /* one handler runs at a time */
    size_t left = http->content_len;
    while (left > 0) {
        int count = httpd_req_recv(http, chunk, left < sizeof chunk ? left : sizeof chunk);
        if (count <= 0 || fwrite(chunk, 1, (size_t)count, file) != (size_t)count) {
            return false;
        }
        left -= (size_t)count;
    }
    return true;
}

static esp_err_t handle_app_file(httpd_req_t *http)
{
    if (web_server_refuses_cross_site(http)) {
        return ESP_OK;
    }
    char name[QUERY_VALUE_MAX], file_name[QUERY_VALUE_MAX];
    if (!query_value(http, "name", name, sizeof name) ||
        !query_value(http, "file", file_name, sizeof file_name)) {
        return refuse(http, "name=<application>&file=<its file>");
    }
    const char *file = file_name;
    if (!app_store_is_file_name(file)) {
        return refuse(http, "not a file's name");
    }
    if (http->content_len == 0 || http->content_len > app_store_file_max_bytes(file)) {
        return refuse(http, "the file is empty or too large for an application");
    }
    char path[APP_STORE_PATH_MAX];
    if (!app_store_begin_file(STORAGE_APPS_ROOT, name, file, path)) {
        return refuse(http, "not an application's name, or the storage refused");
    }
    FILE *incoming = fopen(path, "wb");
    bool received = incoming != NULL && receive_into(http, incoming);
    bool stored = incoming != NULL && fclose(incoming) == 0 && received;
    if (!stored || !app_store_commit_file(STORAGE_APPS_ROOT, name, file)) {
        remove(path);
        return send_text(http, "500 Internal Server Error", "the file could not be stored");
    }
    ESP_LOGI(TAG, "%s of \"%s\" replaced, %u bytes", file_name, name, (unsigned)http->content_len);
    return send_text(http, "200 OK", "stored");
}

/* A firmware image, as /api/ota takes it, with nobody asked at the device:
 * a device on the bench is flashed many times an hour by whoever works on
 * it. Not in a build for release, as nothing here is. */
static esp_err_t handle_flash(httpd_req_t *http)
{
    if (web_server_refuses_cross_site(http)) {
        return ESP_OK;
    }
    return web_server_take_firmware(http);
}

/* The list of applications is read once, at start, so a new one is
 * started by starting over: the device comes back showing what it showed
 * last, and that is made to be this application. */
static esp_err_t handle_run(httpd_req_t *http)
{
    if (web_server_refuses_cross_site(http)) {
        return ESP_OK;
    }
    char name[QUERY_VALUE_MAX];
    if (!query_value(http, "app", name, sizeof name) || !app_manifest_is_valid_name(name)) {
        return refuse(http, "app=<name> is missing or is not an application's name");
    }
    settings_save_last_app(name);
    ESP_LOGW(TAG, "restarting into \"%s\"", name);
    esp_err_t err = send_text(http, "200 OK", "restarting");
    host_stop_soon(HOST_STOP_RESTART);
    return err;
}

/* A value into an application's store, as its own kv_set would put it:
 * how a setting is given to an application before there is a form for it. */
static esp_err_t handle_kv(httpd_req_t *http)
{
    if (web_server_refuses_cross_site(http)) {
        return ESP_OK;
    }
    char app[QUERY_VALUE_MAX], key[QUERY_VALUE_MAX];
    static char value[HOST_KV_VALUE_MAX]; /* one handler runs at a time */
    if (!query_value(http, "app", app, sizeof app) || !query_value(http, "key", key, sizeof key) ||
        !app_manifest_is_valid_name(app) || key[0] == '\0' || strlen(key) > HOST_KV_KEY_MAX) {
        return refuse(http, "app=<name>&key=<key>, the value as the body");
    }
    if (http->content_len > sizeof value) {
        return refuse(http, "the value is too long");
    }
    size_t received = 0;
    while (received < http->content_len) {
        int count = httpd_req_recv(http, value + received, http->content_len - received);
        if (count <= 0) {
            return refuse(http, "the value did not arrive");
        }
        received += (size_t)count;
    }
    int32_t result = app_kv_set(app, key, value, (int32_t)received);
    return result == HOST_OK ? send_text(http, "200 OK", "stored")
                             : send_text(http, "500 Internal Server Error", "not stored");
}

static esp_err_t handle_restart(httpd_req_t *http)
{
    if (web_server_refuses_cross_site(http)) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "restart asked for");
    esp_err_t err = send_text(http, "200 OK", "restarting");
    host_stop_soon(HOST_STOP_RESTART);
    return err;
}

void devtools_register(httpd_handle_t server)
{
    static const httpd_uri_t ROUTES[DEVTOOLS_ROUTES] = {
        { .uri = "/api/dev/screen", .method = HTTP_GET, .handler = handle_screen },
        { .uri = "/api/dev/button", .method = HTTP_POST, .handler = handle_button },
        { .uri = "/api/dev/open", .method = HTTP_POST, .handler = handle_open },
        { .uri = "/api/dev/app", .method = HTTP_POST, .handler = handle_app_file },
        { .uri = "/api/dev/run", .method = HTTP_POST, .handler = handle_run },
        { .uri = "/api/dev/flash", .method = HTTP_POST, .handler = handle_flash },
        { .uri = "/api/dev/restart", .method = HTTP_POST, .handler = handle_restart },
        { .uri = "/api/dev/kv", .method = HTTP_POST, .handler = handle_kv },
    };
    for (int i = 0; i < DEVTOOLS_ROUTES; i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ROUTES[i]));
    }
    ESP_LOGW(TAG, "development tools are on at /api/dev");
}
