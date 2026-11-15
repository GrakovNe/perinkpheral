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

#include "app_delivery.h"
#include "app_kv.h"
#include "app_store.h"
#include "display.h"
#include "events.h"
#include "host_api.h"
#include "host.h"
#include "launcher.h"
#include "poller.h"
#include "scene.h"
#include "settings.h"
#include "storage.h"
#include "units.h"
#include "web_server.h"

static const char *TAG = "devtools";

#define FRAME_BYTES       (DISPLAY_WIDTH / 8 * DISPLAY_HEIGHT)
#define HOST_ANSWER_MS    6000 /* the host may be in the middle of a full panel refresh */
#define QUERY_VALUE_MAX   24
#define UPLOAD_CHUNK      2048
#define NUMBER_TEXT_MAX   12

/* ---- asking the host loop: whatever touches the frame or the scenes ---- */

typedef struct {
    uint8_t  *frame; /* FRAME_BYTES */
    uint32_t  frames_shown;
    bool      frame_pending;
} screen_look_t;

static void look_at_screen(void *argument)
{
    screen_look_t *look = argument;
    memcpy(look->frame, display_shown_frame(), FRAME_BYTES);
    look->frames_shown = host_frames_shown();
    look->frame_pending = host_frame_is_pending();
}

typedef struct {
    const char *app_name;
    bool        is_opened;
} open_command_t;

static void open_app(void *argument)
{
    open_command_t *command = argument;
    scene_t        *scene = launcher_scene(command->app_name);
    command->is_opened = scene != NULL;
    if (scene != NULL) {
        host_open_from_menu(scene);
    }
}

/* ---- the routes ---- */

static esp_err_t refuse(httpd_req_t *http, const char *why)
{
    return web_send_text(http, "400 Bad Request", why);
}

/* A 1-bit BMP: two colours, rows from the bottom up, each padded to four
 * bytes. The frame's own bits fit as they are: 0 is ink, 1 is paper. */
#define BMP_HEADER_BYTES 62
#define BMP_ROW_BYTES    ((DISPLAY_WIDTH / 8 + 3) / 4 * 4)

/* Where the fields of the file's header are, in bytes from its start. */
enum {
    BMP_AT_FILE_BYTES = 2,
    BMP_AT_PIXELS_START = 10,
    BMP_AT_INFO_BYTES = 14,
    BMP_AT_WIDTH = 18,
    BMP_AT_HEIGHT = 22,
    BMP_AT_PLANES = 26,
    BMP_AT_BITS_PER_PIXEL = 28,
    BMP_AT_PIXELS_BYTES = 34,
    BMP_AT_COLOURS = 46,
    BMP_AT_PALETTE = 54,
};
#define BMP_INFO_BYTES 40
#define BMP_COLOURS    2
#define BMP_INK        0x00141413 /* the palette of the project: black #141413 */
#define BMP_PAPER      0x00E9E7E1 /* and paper #E9E7E1 */

static void put_u32(uint8_t *bytes, uint32_t value)
{
    for (size_t i = 0; i < sizeof value; i++) {
        bytes[i] = (uint8_t)(value >> (BITS_PER_BYTE * i));
    }
}

static void bmp_header(uint8_t header[BMP_HEADER_BYTES])
{
    memset(header, 0, BMP_HEADER_BYTES);
    header[0] = 'B';
    header[1] = 'M';
    put_u32(header + BMP_AT_FILE_BYTES, BMP_HEADER_BYTES + BMP_ROW_BYTES * DISPLAY_HEIGHT);
    put_u32(header + BMP_AT_PIXELS_START, BMP_HEADER_BYTES);
    put_u32(header + BMP_AT_INFO_BYTES, BMP_INFO_BYTES);
    put_u32(header + BMP_AT_WIDTH, DISPLAY_WIDTH);
    put_u32(header + BMP_AT_HEIGHT, DISPLAY_HEIGHT);
    header[BMP_AT_PLANES] = 1;
    header[BMP_AT_BITS_PER_PIXEL] = 1;
    put_u32(header + BMP_AT_PIXELS_BYTES, BMP_ROW_BYTES * DISPLAY_HEIGHT);
    put_u32(header + BMP_AT_COLOURS, BMP_COLOURS);
    put_u32(header + BMP_AT_PALETTE, BMP_INK);
    put_u32(header + BMP_AT_PALETTE + sizeof(uint32_t), BMP_PAPER);
}

static esp_err_t handle_screen(httpd_req_t *http)
{
    screen_look_t look = { .frame = malloc(FRAME_BYTES) };
    if (look.frame == NULL) {
        return web_send_text(http, "503 Service Unavailable", "no memory");
    }
    if (!events_call(look_at_screen, &look)) {
        free(look.frame);
        return web_send_text(http, "503 Service Unavailable", "the host did not answer");
    }
    char frames_shown[NUMBER_TEXT_MAX];
    snprintf(frames_shown, sizeof frames_shown, "%u", (unsigned)look.frames_shown);
    httpd_resp_set_type(http, "image/bmp");
    httpd_resp_set_hdr(http, "X-Frame", frames_shown);
    httpd_resp_set_hdr(http, "X-Pending", look.frame_pending ? "1" : "0");
    httpd_resp_set_hdr(http, "Cache-Control", "no-store");

    uint8_t header[BMP_HEADER_BYTES];
    bmp_header(header);
    esp_err_t err = httpd_resp_send_chunk(http, (const char *)header, sizeof header);
    const int frame_row_bytes = DISPLAY_WIDTH / 8;
    for (int row = DISPLAY_HEIGHT - 1; row >= 0 && err == ESP_OK; row--) {
        uint8_t padded[BMP_ROW_BYTES] = { 0 };
        memcpy(padded, look.frame + row * frame_row_bytes, (size_t)frame_row_bytes);
        err = httpd_resp_send_chunk(http, (const char *)padded, sizeof padded);
    }
    free(look.frame);
    return err == ESP_OK ? httpd_resp_send_chunk(http, NULL, 0) : err;
}

static esp_err_t handle_button(httpd_req_t *http)
{
    static const char *const PRESSES[] = {
        [EVENT_PRESS_DOWN] = "down",
        [EVENT_PRESS_UP] = "up",
        [EVENT_PRESS_SHORT] = "short",
        [EVENT_PRESS_LONG] = "long",
    };
    char button_text[QUERY_VALUE_MAX], press_text[QUERY_VALUE_MAX] = "short";
    if (!web_query_value(http, "button", button_text, sizeof button_text)) {
        return refuse(http, "button=1..4 is missing");
    }
    web_query_value(http, "press", press_text, sizeof press_text);
    int button = atoi(button_text);
    int press = -1;
    for (int i = 0; i < (int)(sizeof PRESSES / sizeof PRESSES[0]); i++) {
        if (strcmp(press_text, PRESSES[i]) == 0) {
            press = i;
        }
    }
    if (button < 1 || button > SCENE_BUTTON_COUNT || press < 0) {
        return refuse(http, "button is 1..4, press is short, long, down or up");
    }
    events_post_button((uint8_t)(button - 1), (event_press_t)press);
    return web_send_text(http, "200 OK", "pressed");
}

static esp_err_t handle_open(httpd_req_t *http)
{
    char app_name[APP_NAME_MAX + 1];
    if (!web_query_value(http, "app", app_name, sizeof app_name)) {
        return refuse(http, "app=<name> is missing");
    }
    open_command_t command = { .app_name = app_name };
    if (!events_call(open_app, &command)) {
        return web_send_text(http, "503 Service Unavailable", "Device busy.");
    }
    return command.is_opened ? web_send_text(http, "200 OK", "opened")
                : web_send_text(http, "404 Not Found", "no such application is installed");
}

/* The body goes to the file a chunk at a time; false when the client went
 * away or the file system is full. */
static bool receive_into(httpd_req_t *http, FILE *file)
{
    static char chunk[UPLOAD_CHUNK]; /* one handler runs at a time */
    size_t left = http->content_len;
    while (left > 0) {
        int count = web_receive(http, chunk, left < sizeof chunk ? left : sizeof chunk);
        if (count <= 0 || fwrite(chunk, 1, (size_t)count, file) != (size_t)count) {
            return false;
        }
        left -= (size_t)count;
    }
    return true;
}

static esp_err_t handle_app_file(httpd_req_t *http)
{
    char name[QUERY_VALUE_MAX], file_name[QUERY_VALUE_MAX];
    if (!web_query_value(http, "name", name, sizeof name) ||
        !web_query_value(http, "file", file_name, sizeof file_name)) {
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
        return web_send_text(http, "500 Internal Server Error", "the file could not be stored");
    }
    ESP_LOGI(TAG, "%s of \"%s\" replaced, %u bytes", file_name, name, (unsigned)http->content_len);
    return web_send_text(http, "200 OK", "stored");
}

/* A firmware image, as /api/ota takes it, with nobody asked at the device:
 * a device on the bench is flashed many times an hour by whoever works on
 * it. Not in a build for release, as nothing here is. */
static esp_err_t handle_flash(httpd_req_t *http)
{
    return web_server_take_firmware(http);
}

/* The list of applications is read once, at start, so a new one is
 * started by starting over: the device comes back showing what it showed
 * last, and that is made to be this application. */
static esp_err_t handle_run(httpd_req_t *http)
{
    char name[QUERY_VALUE_MAX];
    if (!web_query_value(http, "app", name, sizeof name) || !app_manifest_is_valid_name(name)) {
        return refuse(http, "app=<name> is missing or is not an application's name");
    }
    settings_save_last_app(name);
    ESP_LOGW(TAG, "restarting into \"%s\"", name);
    esp_err_t err = web_send_text(http, "200 OK", "restarting");
    host_stop_soon(HOST_STOP_RESTART);
    return err;
}

/* In the host loop: the application is told its settings changed and its
 * polls are asked again, as when the page saves them. */
typedef struct {
    const char *app_name, *key, *value;
    int32_t     length;
    int32_t     result;
} kv_command_t;

/* In the host loop, as the page's own settings are: the store is the host
 * loop's to write, and the application is told at once. */
static void store_value(void *argument)
{
    kv_command_t *command = argument;
    command->result = app_kv_set(command->app_name, command->key, command->value, command->length);
    if (command->result == HOST_OK) {
        app_delivery_t changed = { .kind = APP_DELIVERY_SETTINGS };
        host_deliver(command->app_name, &changed);
        poller_hurry(command->app_name);
    }
}

/* A value into an application's store, as its own kv_set would put it, and
 * the application told as when the page saves its settings: how a setting
 * is given to an application from the command line. */
static esp_err_t handle_kv(httpd_req_t *http)
{
    char app[QUERY_VALUE_MAX], key[QUERY_VALUE_MAX];
    static char value[HOST_KV_VALUE_MAX]; /* one handler runs at a time */
    if (!web_query_value(http, "app", app, sizeof app) || !web_query_value(http, "key", key, sizeof key) ||
        !app_manifest_is_valid_name(app) || key[0] == '\0' || strlen(key) > HOST_KV_KEY_MAX) {
        return refuse(http, "app=<name>&key=<key>, the value as the body");
    }
    if (http->content_len > sizeof value) {
        return refuse(http, "the value is too long");
    }
    size_t received = 0;
    while (received < http->content_len) {
        int count = web_receive(http, value + received, http->content_len - received);
        if (count <= 0) {
            return refuse(http, "the value did not arrive");
        }
        received += (size_t)count;
    }
    kv_command_t command = { app, key, value, (int32_t)received, HOST_ERR_ARGUMENT };
    if (!events_call(store_value, &command)) {
        return web_send_text(http, "503 Service Unavailable", "Device busy.");
    }
    return command.result == HOST_OK ? web_send_text(http, "200 OK", "stored")
                             : web_send_text(http, "500 Internal Server Error", "not stored");
}

/* POST /api/dev/crash?kind=wdt|abort: a deliberate fault, for testing what
 * the device records and how it comes back. wdt: interrupts off until the
 * interrupt watchdog fires. */
static esp_err_t handle_crash(httpd_req_t *http)
{
    char kind[QUERY_VALUE_MAX] = "";
    web_query_value(http, "kind", kind, sizeof kind);
    if (strcmp(kind, "wdt") == 0) {
        static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&lock);
        for (;;) {
        }
    }
    if (strcmp(kind, "abort") == 0) {
        abort();
    }
    return httpd_resp_send_err(http, HTTPD_400_BAD_REQUEST, "kind=wdt|abort");
}

static esp_err_t handle_restart(httpd_req_t *http)
{
    ESP_LOGW(TAG, "restart asked for");
    esp_err_t err = web_send_text(http, "200 OK", "restarting");
    host_stop_soon(HOST_STOP_RESTART);
    return err;
}

const web_route_t DEVTOOLS_ROUTES[] = {
    { "/api/dev/screen", HTTP_GET, handle_screen, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/dev/button", HTTP_POST, handle_button, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/dev/open", HTTP_POST, handle_open, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/dev/app", HTTP_POST, handle_app_file, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/dev/run", HTTP_POST, handle_run, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/dev/flash", HTTP_POST, handle_flash, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/dev/restart", HTTP_POST, handle_restart, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/dev/kv", HTTP_POST, handle_kv, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
    { "/api/dev/crash", HTTP_POST, handle_crash, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE },
};
const size_t DEVTOOLS_ROUTE_COUNT = WEB_ROUTE_COUNT(DEVTOOLS_ROUTES);
