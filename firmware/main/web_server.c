/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "words.h"
#include "priorities.h"
#include "web_server.h"
#include "crash_note.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "json_tree.h"
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
#include "log_capture.h"
#include "net_time.h"
#include "app_system.h"
#include "host_api.h"
#include "events.h"
#include "gfx_icons.h"
#include "host.h"
#include "m3_area.h"
#include "ota.h"
#include "poller.h"
#include "power.h"
#include "reset.h"
#include "menu_order.h"
#include "settings.h"
#include "storage.h"
#include "web_apps.h"
#include "status.h"
#include "wifi.h"
#include "units.h"

static const char *TAG = "web";

#define SETTINGS_BODY_MAX     512
#define UPLOAD_CHUNK_BYTES    4096
#define VALIDATION_TEXT_MAX   128
/* How long a sender may say nothing in the middle of a request. */
#define RECEIVE_TIMEOUT_S     10
#define HOST_HEADER_MAX       64
#define ORIGIN_HEADER_MAX     (HOST_HEADER_MAX + 8) /* with "https://" in front */
#define LOCAL_TIME_MAX        24

/* The page, packed with gzip when the firmware was built. */
extern const char index_html_start[] asm("_binary_index_html_gz_start");
extern const char index_html_end[] asm("_binary_index_html_gz_end");

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
static bool is_addressed_to_device(const char *host)
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

static bool refuses_cross_site(httpd_req_t *request)
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
    if (is_own_page && has_host && is_addressed_to_device(host)) {
        return false;
    }
    ESP_LOGW(TAG, "refused a request from another site");
    httpd_resp_set_status(request, "403 Forbidden");
    httpd_resp_sendstr(request, "Forbidden.");
    return true;
}

/* The page reads the message; the status says whose fault it was: the
 * request's, the device's being busy, or its storage's. */
#define STATUS_BAD_REQUEST "400 Bad Request"
#define STATUS_BUSY        "503 Service Unavailable"
#define STATUS_STORAGE     "500 Internal Server Error"

static esp_err_t send_failure(httpd_req_t *request, const char *http_status, const char *message)
{
    return web_send_message(request, http_status, "error", message);
}

static esp_err_t send_error(httpd_req_t *request, const char *message);

#define STATUS_FORBIDDEN "403 Forbidden"

/* What cannot be taken back is done only when somebody at the device says
 * so. False, with the answer sent, when nobody did. */
static bool is_confirmed_on_device(httpd_req_t *request, const char *first,
                                       const char *second, int icon, const char *action)
{
    switch (host_confirm_on_device(first, second, icon, action)) {
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
    return web_send_message(request, "200 OK", "ok", message);
}

static esp_err_t handle_page(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
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
    /* A copy of the settings as they are: the page may be a moment behind. */
    appconf_t settings;
    settings_read(&settings);

    json_t *json = json_new_object();
    json_add_string(json, "name", settings.device_name);
    json_add_string(json, "firmware", esp_app_get_description()->version);
    json_add_string(json, "slot", running != NULL ? running->label : "?");
    json_add_bool(json, "on_probation", ota_is_on_probation());
#ifdef CONFIG_APP_DEV_CONSOLE
    /* The page offers Control by this, and does not ask for a screen a release has not got. */
    json_add_bool(json, "dev_tools", true);
#endif
    json_add_string(json, "wifi", phase_name(wifi.phase));
    json_add_string(json, "ssid", wifi.ssid);
    json_add_string(json, "address", wifi.address);
    json_add_string(json, "tz", settings.tz);
    if (net_time_is_known()) {
        char   local_time[LOCAL_TIME_MAX];
        time_t now = (time_t)net_time_now();
        struct tm local;
        localtime_r(&now, &local);
        /* In the device's language, as its header says it: "Tue 17 Nov", "Вт 17 ноя". */
        snprintf(local_time, sizeof local_time, "%s %d %s %02d:%02d", word(WORD_SUN + local.tm_wday),
                 local.tm_mday, word(WORD_JAN + local.tm_mon), local.tm_hour, local.tm_min);
        json_add_string(json, "time", local_time);
    }
    json_add_string(json, "hostname", settings.hostname);
    json_add_string(json, "idf", esp_app_get_description()->idf_ver);
    json_add_string(json, "built", esp_app_get_description()->date);
    json_add_number(json, "uptime_s", (int64_t)(esp_timer_get_time() / US_PER_SECOND));
    json_add_number(json, "free_memory", esp_get_free_heap_size());
    json_add_number(json, "least_free_memory", esp_get_minimum_free_heap_size());
    /* Why the device started this time: a number of esp_reset_reason_t - 1 power, 3 a restart
     * asked for, 4 a fault, 5-7 a watchdog, 9 the supply sagged. */
    json_add_number(json, "reset_reason", esp_reset_reason());
    static char crash[CRASH_NOTE_TEXT_BYTES];
    if (crash_note_text(crash, sizeof crash)) {
        json_add_string(json, "last_panic", crash);
    }
    /* Of the application area, the most the application loaded now has had. */
    json_add_number(json, "app_area_used", (int64_t)m3_area_most_used());
    json_add_number(json, "app_area", M3_AREA_BYTES);
    json_add_bool(json, "storage_ok", storage_is_mounted());
    size_t storage_used = 0, storage_total = 0;
    storage_usage(&storage_used, &storage_total);
    json_add_number(json, "storage_used", (int64_t)storage_used);
    json_add_number(json, "storage_total", (int64_t)storage_total);
    json_add_number(json, "largest_free_block",
                            heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    json_add_bool(json, "usb_host", usb_serial_jtag_is_connected());
    memory_counts_t memory;
    memory_read(&memory);
    json_add_number(json, "memory_refusals", memory.refusals);
    json_add_number(json, "failed_allocations", memory.failed_allocations);
    return web_send_json(request, "200 OK", json);
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

/* What every request passes before its handler: the checks of its route. */
/* A request that waits, on its own task: the server's goes on answering. */
typedef struct {
    esp_err_t (*handler)(httpd_req_t *request);
    httpd_req_t *request; /* the server's copy, ours until it is completed */
} waiting_t;

static atomic_bool s_is_waiting; /* one such request at a time */

static void answer_waiting(void *argument)
{
    waiting_t *waiting = argument;
    waiting->handler(waiting->request);
    httpd_req_async_handler_complete(waiting->request);
    free(waiting);
    atomic_store(&s_is_waiting, false);
    vTaskDelete(NULL);
}

static esp_err_t answer_off_the_server(httpd_req_t *request, const web_route_t *route)
{
    if (atomic_exchange(&s_is_waiting, true)) {
        return send_failure(request, STATUS_BUSY, "Device busy.");
    }
    waiting_t *waiting = malloc(sizeof *waiting);
    if (waiting == NULL || httpd_req_async_handler_begin(request, &waiting->request) != ESP_OK) {
        free(waiting);
        atomic_store(&s_is_waiting, false);
        return send_failure(request, STATUS_BUSY, "Not enough memory.");
    }
    waiting->handler = route->handler;
    if (xTaskCreate(answer_waiting, "web_waits", STACK_WEB_WAITS, waiting, PRIORITY_SERVER, NULL) !=
        pdPASS) {
        /* The copy is the request now: answered through it, and let go. */
        send_failure(waiting->request, STATUS_BUSY, "Not enough memory.");
        httpd_req_async_handler_complete(waiting->request);
        free(waiting);
        atomic_store(&s_is_waiting, false);
    }
    return ESP_OK;
}

static esp_err_t answer_route(httpd_req_t *request)
{
    const web_route_t *route = request->user_ctx;
    if (route->scope != WEB_OPEN && refuses_cross_site(request)) {
        return ESP_OK;
    }
    if (route->memory != WEB_NO_MEMORY_NEED && !web_server_has_memory(request, route->memory)) {
        return ESP_OK;
    }
    if (route->pace == WEB_WAITS) {
        return answer_off_the_server(request, route);
    }
    return route->handler(request);
}

static void register_routes(httpd_handle_t server, const web_route_t *routes, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        const httpd_uri_t uri = { .uri = routes[i].uri, .method = routes[i].method,
                                  .handler = answer_route, .user_ctx = (void *)&routes[i] };
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri));
    }
}

#define LOG_QUERY_MAX 48 /* "since=" and a number */

static esp_err_t handle_log(httpd_req_t *request)
{
    char *log = malloc(LOG_CAPTURE_BYTES + 1);
    if (log == NULL) {
        return httpd_resp_send_500(request);
    }
    uint32_t end = 0;
    size_t   length = log_capture_read(log, LOG_CAPTURE_BYTES + 1, &end);
    /* "?since=<where the last answer ended>" gives only what came after:
     * the log can be followed without reading it whole every time. What
     * was logged and already pushed out by newer lines is simply missing. */
    char   query[LOG_QUERY_MAX], since_text[INT32_TEXT_BYTES];
    size_t skipped = 0;
    if (httpd_req_get_url_query_str(request, query, sizeof query) == ESP_OK &&
        httpd_query_key_value(query, "since", since_text, sizeof since_text) == ESP_OK) {
        uint32_t since = (uint32_t)strtoul(since_text, NULL, 10);
        uint32_t fresh = end - since; /* the counter wraps, and so does this */
        skipped = fresh < length ? length - fresh : 0;
    }
    char end_text[INT32_TEXT_BYTES];
    snprintf(end_text, sizeof end_text, "%" PRIu32, end);
    httpd_resp_set_hdr(request, "X-Log-End", end_text);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    esp_err_t err = httpd_resp_send(request, log + skipped, (ssize_t)(length - skipped));
    free(log);
    return err;
}

/* False when the field is there and is not a string that fits. A missing
 * field leaves the value as it is. */
static bool take_string(const json_t *json, const char *field, char *value, size_t value_size)
{
    const json_t *item = json_get(json, field);
    if (item == NULL) {
        return true;
    }
    if (!json_is_string(item) || strlen(item->text) >= value_size) {
        return false;
    }
    strcpy(value, item->text);
    return true;
}

/* ---- changing the host's settings ----
 *
 * The settings are the host loop's: a handler here reads what the page
 * sent and asks the host loop to make the change, which waits for the
 * panel if the panel is busy. */

/* Puts what the page sent into `wanted`; false when a field does not fit. */
typedef bool (*settings_edit_fn)(const json_t *json, appconf_t *wanted);

typedef struct {
    const json_t       *json;
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
    char body[SETTINGS_BODY_MAX];
    if (!web_read_body(request, body, sizeof body)) {
        return send_error(request, "Invalid request.");
    }
    json_t *json = json_parse(body);
    if (json == NULL) {
        return send_error(request, "Invalid request.");
    }
    if (confirmation != NULL) {
        /* What is asked about is named by the request itself. */
        const json_t *named = json_get(json, confirmation->named_by);
        char         second[QUESTION_MAX + 1];
        snprintf(second, sizeof second, confirmation->second_format,
                 json_is_string(named) ? named->text : "");
        if (!is_confirmed_on_device(request, confirmation->first, second, confirmation->icon,
                                        confirmation->action)) {
            json_free(json);
            return ESP_OK;
        }
    }
    settings_command_t command = { .json = json, .edit = edit };
    bool is_asked = events_call(change_settings, &command);
    json_free(json);
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

static bool edit_network(const json_t *json, appconf_t *wanted)
{
    return take_string(json, "ssid", wanted->wifi_ssid, sizeof wanted->wifi_ssid) &&
           take_string(json, "password", wanted->wifi_password, sizeof wanted->wifi_password) &&
           take_string(json, "tz", wanted->tz, sizeof wanted->tz) &&
           (json_get(json, "tz_city") == NULL ||
            take_string(json, "tz_city", wanted->tz_city, sizeof wanted->tz_city));
}

static esp_err_t handle_wifi(httpd_req_t *request)
{
    const confirmation_t ASKED = { word(WORD_JOIN), "%s?", GFX_ICON_SIGNAL, word(WORD_JOIN), "ssid" };
    return change_settings_as_asked(request, edit_network, true, &ASKED);
}

/* The time zone takes effect at once: the header and the applications
 * read local time anew whenever they draw. */
static bool edit_time_zone(const json_t *json, appconf_t *wanted)
{
    /* The city it was chosen by goes with it; none, for a zone of one's own. */
    wanted->tz_city[0] = '\0';
    return json_get(json, "tz") != NULL &&
           take_string(json, "tz", wanted->tz, sizeof wanted->tz) &&
           (json_get(json, "tz_city") == NULL ||
            take_string(json, "tz_city", wanted->tz_city, sizeof wanted->tz_city));
}

static esp_err_t handle_time(httpd_req_t *request)
{
    return change_settings_as_asked(request, edit_time_zone, false, NULL);
}

/* The clock set by hand, as Settings on the device sets it: in the host loop. The
 * network's time sets it again while "time_auto" is on. */
typedef struct {
    int64_t unix_seconds;
    bool    is_set;
} clock_command_t;

static void set_clock(void *argument)
{
    clock_command_t *command = argument;
    command->is_set = app_system_time_set(command->unix_seconds) == HOST_OK;
}

/* After 2020 and before 2100: a clock set by hand is in the years a person means. */
#define CLOCK_LEAST_S 1577836800LL
#define CLOCK_MOST_S  4102444800LL

static esp_err_t handle_clock(httpd_req_t *request)
{
    char body[SETTINGS_BODY_MAX];
    if (!web_read_body(request, body, sizeof body)) {
        return send_error(request, "Invalid request.");
    }
    json_t *json = json_parse(body);
    const json_t *unix_seconds = json_get(json, "unix");
    clock_command_t command = { 0 };
    bool is_valid = json_is_number(unix_seconds) && unix_seconds->is_whole &&
                    unix_seconds->number >= CLOCK_LEAST_S && unix_seconds->number < CLOCK_MOST_S;
    if (is_valid) {
        command.unix_seconds = unix_seconds->number;
    }
    json_free(json);
    if (!is_valid) {
        return send_error(request, "Invalid value.");
    }
    /* While the network's time is taken, it is the network's to set. */
    appconf_t settings;
    settings_read(&settings);
    if (settings.time_auto) {
        return send_error(request, "Time is set automatically.");
    }
    if (!events_call(set_clock, &command)) {
        return send_failure(request, STATUS_BUSY, "Device busy.");
    }
    return command.is_set ? send_ok(request, "Saved.") : send_error(request, "Invalid value.");
}

/* ---- the host's own settings ---- */

static esp_err_t handle_settings(httpd_req_t *request)
{
    appconf_t settings;
    settings_read(&settings);
    json_t *json = json_new_object();
    /* Every setting there is, by its key, as the store has it; a secret is
     * never read back. */
    int count;
    const appconf_field_t *fields = appconf_fields(&count);
    for (int i = 0; i < count; i++) {
        const appconf_field_t *field = &fields[i];
        if ((field->flags & APPCONF_SECRET) != 0) {
            continue;
        }
        if (field->kind == APPCONF_TEXT) {
            json_add_string(json, field->key, appconf_text(&settings, field));
        } else if (field->kind == APPCONF_FLAG) {
            json_add_bool(json, field->key, appconf_number(&settings, field) != 0);
        } else {
            json_add_number(json, field->key, appconf_number(&settings, field));
        }
    }
    json_add_string(json, "hostname", settings.hostname);
    {
        json_t *languages = json_new_array();
        const appconf_language_t *known = appconf_languages(&count);
        for (int i = 0; i < count; i++) {
            json_t *language = json_new_object();
            json_add_string(language, "code", known[i].code);
            json_add_string(language, "name", known[i].name);
            json_array_add(languages, language);
        }
        json_add_item(json, "languages", languages);
    }
    {
        /* The order of the device's menu, which the page shows its tiles by. */
        char *order = malloc(MENU_ORDER_MAX);
        json_t *names = json_new_array();
        if (order != NULL) {
            settings_load_menu_order(order, MENU_ORDER_MAX);
            char *rest = NULL;
            for (char *name = strtok_r(order, "\n", &rest); name != NULL;
                 name = strtok_r(NULL, "\n", &rest)) {
                json_array_add(names, json_new_string(name));
            }
            free(order);
        }
        json_add_item(json, "menu_order", names);
    }
    return web_send_json(request, "200 OK", json);
}

/* A setting the page sent, as text for the store: a flag is true or false,
 * a number a whole one, a text a string. False when it is none of these. */
static bool value_of(const appconf_field_t *field, const json_t *item, char *text, size_t size)
{
    switch (field->kind) {
    case APPCONF_FLAG:
        if (!json_is_bool(item)) {
            return false;
        }
        snprintf(text, size, "%d", json_is_true(item) ? 1 : 0);
        return true;
    case APPCONF_NUMBER:
        if (!json_is_number(item) || !item->is_whole) {
            return false;
        }
        snprintf(text, size, "%lld", (long long)item->number);
        return true;
    case APPCONF_TEXT:
        if (!json_is_string(item) || strlen(item->text) >= size) {
            return false;
        }
        strcpy(text, item->text);
        return true;
    }
    return false;
}

/* Every setting the page sent, by the same keys it reads them by; the
 * network is set its own way, and what is not a setting is not read. */
static bool edit_settings(const json_t *json, appconf_t *wanted)
{
    int count;
    const appconf_field_t *fields = appconf_fields(&count);
    for (int i = 0; i < count; i++) {
        const json_t *item = json_get(json, fields[i].key);
        if (item == NULL || (fields[i].flags & APPCONF_OWN_WAY) != 0) {
            continue;
        }
        char text[APPCONF_VALUE_MAX];
        if (!value_of(&fields[i], item, text, sizeof text) || !appconf_put(wanted, &fields[i], text)) {
            return false;
        }
    }
    return true;
}

static esp_err_t handle_set_settings(httpd_req_t *request)
{
    return change_settings_as_asked(request, edit_settings, false, NULL);
}

static esp_err_t handle_restart(httpd_req_t *request)
{
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
    if (!is_confirmed_on_device(request, word(WORD_FACTORY), word(WORD_FACTORY_RESET), GFX_ICON_WARNING,
                                    word(WORD_RESET))) {
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
    char body[SETTINGS_BODY_MAX];
    if (!web_read_body(request, body, sizeof body)) {
        return send_error(request, "Invalid request.");
    }
    json_t *json = json_parse(body);
    const json_t *url = json_get(json, "url");
    const json_t *size = json_get(json, "size");
    char   address[RELEASE_URL_MAX + 1] = "";
    size_t image_size = 0;
    if (json_is_string(url) && strlen(url->text) <= RELEASE_URL_MAX &&
        strncmp(url->text, RELEASE_URL_PREFIX, strlen(RELEASE_URL_PREFIX)) == 0 &&
        json_is_number(size) && size->number > 0) {
        strcpy(address, url->text);
        image_size = (size_t)size->number;
    }
    json_free(json);
    if (address[0] == '\0') {
        return send_error(request, "Invalid request.");
    }

    if (!is_confirmed_on_device(request, word(WORD_UPDATE), word(WORD_UPDATE_FIRMWARE),
                                    GFX_ICON_UPDATE, word(WORD_UPDATE))) {
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
    if (request->content_len == 0) {
        return send_error(request, "No image.");
    }
    if (!is_confirmed_on_device(request, word(WORD_UPDATE), word(WORD_UPDATE_FIRMWARE),
                                    GFX_ICON_UPDATE, word(WORD_UPDATE))) {
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
        int count = web_receive(request, (char *)chunk,
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

/* The page, the device and its settings. */
static const web_route_t ROUTES[] = {
    { "/", HTTP_GET, handle_page, WEB_NO_MEMORY_NEED, WEB_OPEN, WEB_AT_ONCE },
    { "/api/status", HTTP_GET, handle_status, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE, WEB_AT_ONCE },
    { "/api/log", HTTP_GET, handle_log, MEMORY_FOR_A_PAGE, WEB_OWN_PAGE, WEB_AT_ONCE },
    { "/api/wifi", HTTP_POST, handle_wifi, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE, WEB_WAITS },
    { "/api/time", HTTP_POST, handle_time, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE, WEB_AT_ONCE },
    { "/api/clock", HTTP_POST, handle_clock, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE, WEB_AT_ONCE },
    { "/api/ota", HTTP_POST, handle_ota, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE, WEB_WAITS },
    { "/api/ota/release", HTTP_POST, handle_ota_release, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE, WEB_WAITS },
    { "/api/reset", HTTP_POST, handle_reset, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE, WEB_WAITS },
    { "/api/restart", HTTP_POST, handle_restart, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE, WEB_AT_ONCE },
    { "/api/settings", HTTP_GET, handle_settings, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE, WEB_AT_ONCE },
    { "/api/settings", HTTP_POST, handle_set_settings, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE, WEB_AT_ONCE },
};

/* The device's sockets (CONFIG_LWIP_MAX_SOCKETS) and who holds them: the
 * server three - the one it listens on, the one it is told through and the
 * one it tells itself with (seen in /api/dev/sockets) -, the setup access
 * point's name server one for as long as the device runs, and the poller
 * the one request it makes at a time. The page's connections are what is
 * left. */
#define WEB_SERVER_OWN_SOCKETS 3
#define NAME_SERVER_SOCKETS    1
#define POLLER_SOCKETS         1
#define WEB_PAGE_SOCKETS                                                                           \
    (CONFIG_LWIP_MAX_SOCKETS - WEB_SERVER_OWN_SOCKETS - NAME_SERVER_SOCKETS - POLLER_SOCKETS)
_Static_assert(WEB_PAGE_SOCKETS >= 6, "a browser opens six to a host at once: give lwIP more sockets");

void web_server_start(void)
{
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.recv_wait_timeout = RECEIVE_TIMEOUT_S;
    /* Installing an application hashes files and reads a manifest on this task. */
    server_config.stack_size = STACK_WEB_SERVER;
    server_config.task_priority = PRIORITY_SERVER;
    /* A phone that joins the setup access point opens several connections at
     * once - its own checks, the page, what the page asks - and keeps them;
     * with all the server's sockets taken, the next one waits for nothing.
     * The oldest one is closed instead. */
    server_config.lru_purge_enable = true;
    /* But the oldest is closed only when the server holds as many as it may,
     * and the device's sockets are shared: if it may hold more than are left
     * for it, the next connection finds none - "error in accept (23)" - while
     * the server still has room, nothing is closed, and the page is dead until
     * a browser lets one go. Seen on 2026-11-17: seven promised where five were
     * left. So it may hold what the others leave. */
    server_config.max_open_sockets = WEB_PAGE_SOCKETS;
    server_config.max_uri_handlers = WEB_ROUTE_COUNT(ROUTES) + WEB_APPS_ROUTE_COUNT;
#ifdef CONFIG_APP_DEV_CONSOLE
    server_config.max_uri_handlers += DEVTOOLS_ROUTE_COUNT;
#endif
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &server_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot start: %s", esp_err_to_name(err));
        return;
    }
    s_server = server;
    memory_on_pressure(close_connections);
    register_routes(server, ROUTES, WEB_ROUTE_COUNT(ROUTES));
    register_routes(server, WEB_APPS_ROUTES, WEB_APPS_ROUTE_COUNT);
#ifdef CONFIG_APP_DEV_CONSOLE
    register_routes(server, DEVTOOLS_ROUTES, DEVTOOLS_ROUTE_COUNT);
    ESP_LOGW(TAG, "development tools are on at /api/dev");
#endif
    ESP_ERROR_CHECK(httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, handle_not_found));
    s_is_up = true;
    ESP_LOGI(TAG, "serving on port %d", server_config.server_port);
}

bool web_server_is_up(void)
{
    return s_is_up;
}
