/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "web_server.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <time.h>

#include "devtools.h"
#include "display.h"
#include "log_capture.h"
#include "net_time.h"
#include "events.h"
#include "gfx_icons.h"
#include "host.h"
#include "m3_area.h"
#include "ota.h"
#include "poller.h"
#include "power.h"
#include "reset.h"
#include "settings.h"
#include "storage.h"
#include "web_apps.h"
#include "status.h"
#include "wifi.h"

static const char *TAG = "web";

#define SETTINGS_BODY_MAX     512
#define UPLOAD_CHUNK_BYTES    4096
#define VALIDATION_TEXT_MAX   128
/* How long a sender may say nothing in the middle of a request. */
#define RECEIVE_TIMEOUT_S     10
#define QUIET_PERIODS_MAX     3
#define HOST_HEADER_MAX       64
#define ORIGIN_HEADER_MAX     (HOST_HEADER_MAX + 8) /* with "https://" in front */
#define LOCAL_TIME_MAX        24
#define OWN_ROUTES            11
#define SERVER_STACK_BYTES    6144

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static bool       s_is_up;

/* A page of some other site, open in a browser on the same network, can
 * make that browser post to the device. The browser then says where the
 * request comes from; one that does not come from the device's own page
 * is refused. Tools that are not browsers send no Origin and pass. */
/* Whether the name the request was sent to is one of the device's own: its
 * address on the network or on its access point, or its host name. A page
 * of another site can make its own name resolve to the device's address
 * (DNS rebinding), and then its Origin and its Host agree with each other -
 * but not with any of these. */
static bool is_sent_to_the_device(const char *host)
{
    size_t name_length = strcspn(host, ":"); /* without the port */
    wifi_status_t wifi;
    wifi_get_status(&wifi);
    appconf_t settings;
    settings_read(&settings);
    char local_name[sizeof settings.hostname + sizeof ".local"];
    snprintf(local_name, sizeof local_name, "%s.local", settings.hostname);
    const char *const own[] = { wifi.address, WIFI_SETUP_ADDRESS, settings.hostname, local_name };
    for (size_t i = 0; i < sizeof own / sizeof own[0]; i++) {
        if (own[i][0] != '\0' && strlen(own[i]) == name_length &&
            strncasecmp(own[i], host, name_length) == 0) {
            return true;
        }
    }
    return false;
}

bool web_server_refuses_cross_site(httpd_req_t *request)
{
    /* A header too long for its buffer leaves the buffer as it was. */
    char origin[ORIGIN_HEADER_MAX] = "", host[HOST_HEADER_MAX] = "";
    bool has_host = httpd_req_get_hdr_value_str(request, "Host", host, sizeof host) == ESP_OK;
    bool has_origin = httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof origin) !=
                      ESP_ERR_NOT_FOUND;
    /* A request with no Origin is not a browser's cross-site one - a command
     * line, the device's own tools. An Origin too long to read is not ours. */
    const char *scheme_end = has_origin ? strstr(origin, "://") : NULL;
    bool is_own_page = !has_origin ||
                       (has_host && scheme_end != NULL && strcmp(scheme_end + 3, host) == 0);
    if (is_own_page && has_host && is_sent_to_the_device(host)) {
        return false;
    }
    ESP_LOGW(TAG, "refused a request from another site");
    httpd_resp_set_status(request, "403 Forbidden");
    httpd_resp_sendstr(request, "Forbidden.");
    return true;
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

/* The page reads the message; the status says whose fault it was: the
 * request's, the device's being busy, or its storage's. */
#define STATUS_BAD_REQUEST "400 Bad Request"
#define STATUS_BUSY        "503 Service Unavailable"
#define STATUS_STORAGE     "500 Internal Server Error"

static esp_err_t send_failure(httpd_req_t *request, const char *http_status, const char *message)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "error", message);
    return send_json(request, http_status, json);
}

static esp_err_t send_error(httpd_req_t *request, const char *message);

#define STATUS_FORBIDDEN "403 Forbidden"

/* What cannot be taken back is done only when somebody at the device says
 * so. False, with the answer sent, when nobody did. */
static bool is_confirmed_at_the_device(httpd_req_t *request, const char *first,
                                       const char *second, int icon, const char *action)
{
    switch (host_ask_at_the_device(first, second, icon, action)) {
    case HOST_ANSWER_CONFIRMED:
        return true;
    case HOST_ANSWER_BUSY:
        send_failure(request, STATUS_BUSY, "Device busy.");
        return false;
    case HOST_ANSWER_NOT_CONFIRMED:
        break;
    }
    send_failure(request, STATUS_FORBIDDEN, "Not confirmed.");
    return false;
}

/* A change of settings that is asked about first: the question's first
 * line, its second with a %s for what the request names in `named_by`, the
 * icon and the word for doing it. */
typedef struct {
    const char *first;
    const char *second_format;
    int         icon;
    const char *action;
    const char *named_by;
} confirmation_t;

#define QUESTION_MAX 47

static esp_err_t send_error(httpd_req_t *request, const char *message)
{
    return send_failure(request, STATUS_BAD_REQUEST, message);
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
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    wifi_status_t wifi;
    wifi_get_status(&wifi);
    const esp_partition_t *running = esp_ota_get_running_partition();
    /* A copy of the settings as they are: the page may be a moment behind. */
    appconf_t settings;
    settings_read(&settings);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "name", settings.device_name);
    cJSON_AddStringToObject(json, "firmware", esp_app_get_description()->version);
    cJSON_AddStringToObject(json, "slot", running != NULL ? running->label : "?");
    cJSON_AddBoolToObject(json, "on_probation", ota_is_on_probation());
    cJSON_AddStringToObject(json, "wifi", phase_name(wifi.phase));
    cJSON_AddStringToObject(json, "ssid", wifi.ssid);
    cJSON_AddStringToObject(json, "address", wifi.address);
    cJSON_AddStringToObject(json, "tz", settings.tz);
    if (net_time_is_known()) {
        char   local_time[LOCAL_TIME_MAX];
        time_t now = (time_t)net_time_now();
        struct tm local;
        localtime_r(&now, &local);
        strftime(local_time, sizeof local_time, "%a %d %b %H:%M", &local);
        cJSON_AddStringToObject(json, "time", local_time);
    }
    cJSON_AddStringToObject(json, "hostname", settings.hostname);
    cJSON_AddStringToObject(json, "idf", esp_app_get_description()->idf_ver);
    cJSON_AddStringToObject(json, "built", esp_app_get_description()->date);
    cJSON_AddNumberToObject(json, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(json, "free_memory", esp_get_free_heap_size());
    cJSON_AddNumberToObject(json, "least_free_memory", esp_get_minimum_free_heap_size());
    /* Of the application area, the most the application loaded now has had. */
    cJSON_AddNumberToObject(json, "app_area_used", (double)m3_area_most_used());
    cJSON_AddNumberToObject(json, "app_area", M3_AREA_BYTES);
    cJSON_AddBoolToObject(json, "storage_ok", storage_is_mounted());
    cJSON_AddNumberToObject(json, "largest_free_block",
                            heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    cJSON_AddBoolToObject(json, "usb_host", usb_serial_jtag_is_connected());
    memory_counts_t memory;
    memory_read(&memory);
    cJSON_AddNumberToObject(json, "memory_refusals", memory.refusals);
    cJSON_AddNumberToObject(json, "failed_allocations", memory.failed_allocations);
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

static httpd_handle_t s_server;

/* The heap is critically short: whoever is talking to the page is cut
 * off, and what their requests held comes back. */
#define CONNECTIONS_MAX 8

static void close_connections(void)
{
    int    sockets[CONNECTIONS_MAX];
    size_t count = CONNECTIONS_MAX;
    if (s_server == NULL || httpd_get_client_list(s_server, &count, sockets) != ESP_OK) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        httpd_sess_trigger_close(s_server, sockets[i]);
    }
}

bool web_server_has_memory(httpd_req_t *request, memory_need_t need)
{
    if (memory_has(need)) {
        return true;
    }
    send_failure(request, STATUS_BUSY, "Not enough memory.");
    return false;
}

static esp_err_t handle_log(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    if (!web_server_has_memory(request, MEMORY_FOR_A_PAGE)) {
        return ESP_OK;
    }
    char *log = malloc(LOG_CAPTURE_BYTES + 1);
    if (log == NULL) {
        return httpd_resp_send_500(request);
    }
    uint32_t end = 0;
    size_t   length = log_capture_read(log, LOG_CAPTURE_BYTES + 1, &end);
    /* "?since=<where the last answer ended>" gives only what came after:
     * the log can be followed without reading it whole every time. What
     * was logged and already pushed out by newer lines is simply missing. */
    char   query[48], since_text[16];
    size_t skipped = 0;
    if (httpd_req_get_url_query_str(request, query, sizeof query) == ESP_OK &&
        httpd_query_key_value(query, "since", since_text, sizeof since_text) == ESP_OK) {
        uint32_t since = (uint32_t)strtoul(since_text, NULL, 10);
        uint32_t fresh = end - since; /* the counter wraps, and so does this */
        skipped = fresh < length ? length - fresh : 0;
    }
    char end_text[16];
    snprintf(end_text, sizeof end_text, "%" PRIu32, end);
    httpd_resp_set_hdr(request, "X-Log-End", end_text);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    esp_err_t err = httpd_resp_send(request, log + skipped, (ssize_t)(length - skipped));
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

/* ---- changing the host's settings ----
 *
 * The settings are the host loop's: a handler here reads what the page
 * sent and asks the host loop to make the change, which waits for the
 * panel if the panel is busy. */

/* Puts what the page sent into `wanted`; false when a field does not fit. */
typedef bool (*settings_edit_fn)(const cJSON *json, appconf_t *wanted);

typedef struct {
    const cJSON       *json;
    settings_edit_fn   edit;
    bool               fits;
    settings_outcome_t outcome;
    bool               needs_restart;
    char               problem[VALIDATION_TEXT_MAX];
} settings_command_t;

/* In the host loop: the change is made on the settings as they are now,
 * so that two of them never undo each other. */
static void change_settings(void *argument)
{
    settings_command_t *command = argument;
    appconf_t wanted;
    settings_read(&wanted);
    command->fits = command->edit(command->json, &wanted);
    if (command->fits) {
        command->outcome = settings_change(&wanted, command->problem, sizeof command->problem,
                                           &command->needs_restart);
    }
}

/* Reads the body, has the settings changed with `edit` and answers the
 * page. `restarts`: the change is one the device restarts for. */
static esp_err_t change_settings_as_asked(httpd_req_t *request, settings_edit_fn edit,
                                          bool restarts, const confirmation_t *confirmation)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    char body[SETTINGS_BODY_MAX];
    if (!read_body(request, body, sizeof body)) {
        return send_error(request, "Invalid request.");
    }
    cJSON *json = cJSON_Parse(body);
    if (json == NULL) {
        return send_error(request, "Invalid request.");
    }
    if (confirmation != NULL) {
        /* What is asked about is named by the request itself. */
        const cJSON *named = cJSON_GetObjectItemCaseSensitive(json, confirmation->named_by);
        char         second[QUESTION_MAX + 1];
        snprintf(second, sizeof second, confirmation->second_format,
                 cJSON_IsString(named) ? named->valuestring : "");
        if (!is_confirmed_at_the_device(request, confirmation->first, second, confirmation->icon,
                                        confirmation->action)) {
            cJSON_Delete(json);
            return ESP_OK;
        }
    }
    settings_command_t command = { .json = json, .edit = edit };
    bool is_asked = events_call(change_settings, &command);
    cJSON_Delete(json);
    if (!is_asked) {
        return send_failure(request, STATUS_BUSY, "Device busy.");
    }
    if (!command.fits) {
        return send_error(request, "Invalid value.");
    }
    if (command.outcome == SETTINGS_INVALID) {
        return send_error(request, command.problem);
    }
    if (command.outcome == SETTINGS_NOT_STORED) {
        return send_failure(request, STATUS_STORAGE, "Storage error.");
    }
    if (restarts) {
        ESP_LOGW(TAG, "settings changed, restarting");
        esp_err_t result = send_ok(request, "Saved. Restarting.");
        host_stop_soon(HOST_STOP_RESTART);
        return result;
    }
    return send_ok(request, command.needs_restart ? "Saved. Restart to apply." : "Saved.");
}

static bool edit_network(const cJSON *json, appconf_t *wanted)
{
    return take_string(json, "ssid", wanted->wifi_ssid, sizeof wanted->wifi_ssid) &&
           take_string(json, "password", wanted->wifi_password, sizeof wanted->wifi_password) &&
           take_string(json, "tz", wanted->tz, sizeof wanted->tz);
}

static esp_err_t handle_wifi(httpd_req_t *request)
{
    static const confirmation_t ASKED = { "Join", "%s?", GFX_ICON_SIGNAL, "Join", "ssid" };
    return change_settings_as_asked(request, edit_network, true, &ASKED);
}

/* The time zone takes effect at once: the header and the applications
 * read local time anew whenever they draw. */
static bool edit_time_zone(const cJSON *json, appconf_t *wanted)
{
    return cJSON_GetObjectItemCaseSensitive(json, "tz") != NULL &&
           take_string(json, "tz", wanted->tz, sizeof wanted->tz);
}

static esp_err_t handle_time(httpd_req_t *request)
{
    return change_settings_as_asked(request, edit_time_zone, false, NULL);
}

/* ---- the host's own settings ---- */

static esp_err_t handle_settings(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    appconf_t settings;
    settings_read(&settings);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "device_name", settings.device_name);
    cJSON_AddStringToObject(json, "hostname", settings.hostname);
    cJSON_AddStringToObject(json, "wifi_ssid", settings.wifi_ssid);
    cJSON_AddStringToObject(json, "ntp_server", settings.ntp_server);
    cJSON_AddStringToObject(json, "tz", settings.tz);
    cJSON_AddNumberToObject(json, "refresh_min_data_s", settings.refresh_min_data_s);
    cJSON_AddNumberToObject(json, "refresh_full_every", settings.refresh_full_every);
    cJSON_AddNumberToObject(json, "refresh_keepalive_h", settings.refresh_keepalive_h);
    cJSON_AddBoolToObject(json, "dither", settings.dither != 0);
    cJSON_AddNumberToObject(json, "font", settings.font);
    cJSON_AddBoolToObject(json, "refresh_fast", settings.refresh_fast != 0);
    cJSON_AddBoolToObject(json, "header_clock", settings.header_clock != 0);
    cJSON_AddBoolToObject(json, "time_auto", settings.time_auto != 0);
    cJSON_AddNumberToObject(json, "power_saving", settings.power_saving);
    cJSON_AddStringToObject(json, "default_app", settings.default_app);
    cJSON_AddNumberToObject(json, "menu_idle_min", settings.menu_idle_min);
    return send_json(request, "200 OK", json);
}


/* A number that is there has to be a whole one from 0 to `most`; one that
 * is not leaves the value as it was. */
static bool take_number(const cJSON *json, const char *field, uint32_t most, uint32_t *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, field);
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > most ||
        item->valuedouble != (double)(uint32_t)item->valuedouble) {
        return false;
    }
    *value = (uint32_t)item->valuedouble;
    return true;
}

static bool take_flag(const cJSON *json, const char *field, uint8_t *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, field);
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsBool(item)) {
        return false;
    }
    *value = cJSON_IsTrue(item) ? 1 : 0;
    return true;
}

static bool edit_settings(const cJSON *json, appconf_t *wanted)
{
    uint32_t power_saving = wanted->power_saving, menu_idle_min = wanted->menu_idle_min;
    uint32_t min_data_s = wanted->refresh_min_data_s, full_every = wanted->refresh_full_every;
    uint32_t keepalive_h = wanted->refresh_keepalive_h;
    uint32_t font = wanted->font;
    bool fits =
        take_string(json, "device_name", wanted->device_name, sizeof wanted->device_name) &&
        take_string(json, "ntp_server", wanted->ntp_server, sizeof wanted->ntp_server) &&
        take_string(json, "tz", wanted->tz, sizeof wanted->tz) &&
        take_number(json, "refresh_min_data_s", UINT16_MAX, &min_data_s) &&
        take_number(json, "refresh_full_every", UINT8_MAX, &full_every) &&
        take_number(json, "refresh_keepalive_h", UINT8_MAX, &keepalive_h) &&
        take_number(json, "font", UINT8_MAX, &font) &&
        take_flag(json, "dither", &wanted->dither) &&
        take_flag(json, "refresh_fast", &wanted->refresh_fast) &&
        take_flag(json, "header_clock", &wanted->header_clock) &&
        take_flag(json, "time_auto", &wanted->time_auto) &&
        take_number(json, "power_saving", UINT8_MAX, &power_saving) &&
        take_string(json, "default_app", wanted->default_app, sizeof wanted->default_app) &&
        take_number(json, "menu_idle_min", UINT8_MAX, &menu_idle_min);
    wanted->power_saving = (uint8_t)power_saving;
    wanted->menu_idle_min = (uint8_t)menu_idle_min;
    wanted->refresh_min_data_s = (uint16_t)min_data_s;
    wanted->refresh_full_every = (uint8_t)full_every;
    wanted->refresh_keepalive_h = (uint8_t)keepalive_h;
    wanted->font = (uint8_t)font;
    return fits;
}

static esp_err_t handle_set_settings(httpd_req_t *request)
{
    return change_settings_as_asked(request, edit_settings, false, NULL);
}

static esp_err_t handle_restart(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "restart asked for");
    esp_err_t result = send_ok(request, "Restarting.");
    host_stop_soon(HOST_STOP_RESTART);
    return result;
}

/* Back to how the device left the bench: the settings and everything the
 * applications stored are erased on the way up, and with no network the
 * device comes back with its setup access point. */
static esp_err_t handle_reset(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    if (!is_confirmed_at_the_device(request, "Factory", "reset?", GFX_ICON_WARNING, "Reset")) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "factory reset asked for, restarting");
    esp_err_t result = send_ok(request, "Reset. Restarting.");
    host_stop_soon(HOST_STOP_FACTORY_RESET);
    return result;
}

/* A release of the project: the device fetches the image itself - a page
 * in a browser cannot, the file being on another site - from where the
 * project's releases are and nowhere else. */
#define RELEASE_URL_PREFIX "https://github.com/GrakovNe/perinkpheral/releases/download/"
#define RELEASE_URL_MAX    255

static bool take_release_piece(void *context, const uint8_t *piece, size_t length)
{
    const char **error = context;
    *error = ota_upload_data(piece, length);
    return *error == NULL;
}

static esp_err_t handle_ota_release(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    char body[SETTINGS_BODY_MAX];
    if (!read_body(request, body, sizeof body)) {
        return send_error(request, "Invalid request.");
    }
    cJSON *json = cJSON_Parse(body);
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(json, "url");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(json, "size");
    char   address[RELEASE_URL_MAX + 1] = "";
    size_t image_size = 0;
    if (cJSON_IsString(url) && strlen(url->valuestring) <= RELEASE_URL_MAX &&
        strncmp(url->valuestring, RELEASE_URL_PREFIX, strlen(RELEASE_URL_PREFIX)) == 0 &&
        cJSON_IsNumber(size) && size->valuedouble > 0) {
        strcpy(address, url->valuestring);
        image_size = (size_t)size->valuedouble;
    }
    cJSON_Delete(json);
    if (address[0] == '\0') {
        return send_error(request, "Invalid request.");
    }

    if (!is_confirmed_at_the_device(request, "Update", "firmware?", GFX_ICON_UPDATE, "Update")) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "installing the release at %s", address);
    const char *error = ota_upload_begin(image_size);
    if (error == NULL) {
        int  http_status = 0;
        bool is_whole = poller_fetch(address, image_size, take_release_piece, &error, &http_status);
        if (error == NULL && !is_whole) {
            error = http_status == 0 ? "GitHub unreachable." : "Download failed.";
        }
        if (error == NULL) {
            error = ota_upload_end();
        } else {
            ota_upload_abort();
        }
    }
    if (error != NULL) {
        return send_error(request, error);
    }
    esp_err_t result = send_ok(request, "Installed. Restarting.");
    host_stop_soon(HOST_STOP_RESTART);
    return result;
}

/* The image is the request body:
 *   curl --data-binary @build/perinkpheral.bin http://<address>/api/ota */
static esp_err_t handle_ota(httpd_req_t *request)
{
    if (web_server_refuses_cross_site(request)) {
        return ESP_OK;
    }
    if (request->content_len == 0) {
        return send_error(request, "No image.");
    }
    if (!is_confirmed_at_the_device(request, "Update", "firmware?", GFX_ICON_UPDATE, "Update")) {
        return ESP_OK;
    }
    return web_server_take_firmware(request);
}

esp_err_t web_server_take_firmware(httpd_req_t *request)
{
    if (!web_server_has_memory(request, MEMORY_FOR_AN_UPDATE)) {
        return ESP_OK;
    }
    const char *error = ota_upload_begin(request->content_len);
    uint8_t *chunk = error == NULL ? malloc(UPLOAD_CHUNK_BYTES) : NULL;
    if (error == NULL && chunk == NULL) {
        ota_upload_abort();
        error = "Out of memory.";
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
    esp_err_t result = send_ok(request, "Installed. Restarting.");
    host_stop_soon(HOST_STOP_RESTART);
    return result;
}

/* True when the Host header names this address, with a port or without. */
static bool is_address(const char *host, const char *address)
{
    size_t length = strlen(address);
    return length > 0 && strncmp(host, address, length) == 0 &&
           (host[length] == '\0' || host[length] == ':');
}

/* A phone on the setup access point asks for pages of its own to find out
 * whether it is online; every name leads here (captive_dns), and being
 * sent to the device's page is what makes it show that page. Anybody who
 * asked the device by its address, and everybody while the access point
 * is down, just gets "not found". */
static esp_err_t handle_not_found(httpd_req_t *request, httpd_err_code_t error)
{
    (void)error;
    char host[HOST_HEADER_MAX];
    bool has_host = httpd_req_get_hdr_value_str(request, "Host", host, sizeof host) == ESP_OK;
    wifi_status_t wifi;
    wifi_get_status(&wifi);
    bool access_point_is_up = wifi.phase == WIFI_PHASE_SETUP || wifi.phase == WIFI_PHASE_RESCUE;
    bool asked_us_by_address = has_host && (is_address(host, WIFI_SETUP_ADDRESS) ||
                                            is_address(host, wifi.address));
    if (!access_point_is_up || asked_us_by_address) {
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "not found");
    }
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "http://" WIFI_SETUP_ADDRESS "/");
    return httpd_resp_send(request, NULL, 0);
}

void web_server_start(void)
{
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.recv_wait_timeout = RECEIVE_TIMEOUT_S;
    /* Installing an application hashes files and reads a manifest on this task. */
    server_config.stack_size = SERVER_STACK_BYTES;
    server_config.max_uri_handlers = OWN_ROUTES + WEB_APPS_ROUTES + DEVTOOLS_ROUTES;
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &server_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot start: %s", esp_err_to_name(err));
        return;
    }
    s_server = server;
    memory_on_pressure(close_connections);
    static const httpd_uri_t ROUTES[OWN_ROUTES] = {
        { .uri = "/", .method = HTTP_GET, .handler = handle_page },
        { .uri = "/api/status", .method = HTTP_GET, .handler = handle_status },
        { .uri = "/api/log", .method = HTTP_GET, .handler = handle_log },
        { .uri = "/api/wifi", .method = HTTP_POST, .handler = handle_wifi },
        { .uri = "/api/time", .method = HTTP_POST, .handler = handle_time },
        { .uri = "/api/ota", .method = HTTP_POST, .handler = handle_ota },
        { .uri = "/api/ota/release", .method = HTTP_POST, .handler = handle_ota_release },
        { .uri = "/api/reset", .method = HTTP_POST, .handler = handle_reset },
        { .uri = "/api/restart", .method = HTTP_POST, .handler = handle_restart },
        { .uri = "/api/settings", .method = HTTP_GET, .handler = handle_settings },
        { .uri = "/api/settings", .method = HTTP_POST, .handler = handle_set_settings },
    };
    for (size_t i = 0; i < sizeof ROUTES / sizeof ROUTES[0]; i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ROUTES[i]));
    }
    web_apps_register(server);
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
