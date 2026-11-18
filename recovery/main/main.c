// SPDX-License-Identifier: MIT
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "board.h"
#include "deadlines.h"
#include "display.h"
#include "gfx.h"
#include "fonts.h"
#include "words.h"
#include "ota.h"
#include "settings.h"
#include "screen.h"
#include "page.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"

enum { OK_GPIO = 1, BACK_GPIO = 21, CONFIRM_SECONDS = 60, UPLOAD_SECONDS = 180,
       CHUNK_BYTES = 2048, NETWORK_RETRY_SECONDS = 10 };
static recovery_control_t control;
static uint32_t boot_id;
static portMUX_TYPE version_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool online;
static uint8_t frame[DISPLAY_FRAME_SIZE];
static appconf_t settings;
static esp_netif_t *station;
static char access_point[33];
static char image_version[33];

static uint32_t now_seconds(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static void station_address(char *out, size_t size)
{
    esp_netif_ip_info_t info;
    out[0] = '\0';
    if (station && atomic_load(&online) && esp_netif_get_ip_info(station, &info) == ESP_OK) {
        snprintf(out, size, IPSTR, IP2STR(&info.ip));
    }
}

static bool own_request(httpd_req_t *request)
{
    char host[128], origin[144], expected[144], address[16], local_name[80];
    if (httpd_req_get_hdr_value_str(request, "Host", host, sizeof host) != ESP_OK) {
        return false;
    }
    if (httpd_req_get_hdr_value_len(request, "Origin")) {
        snprintf(expected, sizeof expected, "http://%s", host);
        if (httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof origin) != ESP_OK ||
            strcmp(origin, expected) != 0) {
            return false;
        }
    }
    station_address(address, sizeof address);
    snprintf(local_name, sizeof local_name, "%s.local", settings.hostname);
    const char *names[] = { "192.168.4.1", address, settings.hostname, local_name };
    size_t host_length = strcspn(host, ":");
    if (host[host_length] && strcmp(host + host_length, ":80") != 0) { return false; }
    for (size_t index = 0; index < sizeof names / sizeof names[0]; index++) {
        if (names[index][0] && strlen(names[index]) == host_length &&
            strncasecmp(host, names[index], host_length) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t failure(httpd_req_t *request, const char *message)
{
    /* An unread binary body must never become a second request on this connection. */
    httpd_resp_set_hdr(request, "Connection", "close");
    httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, message);
    return ESP_FAIL;
}

static esp_err_t get_page(httpd_req_t *request)
{
    if (!own_request(request)) { return failure(request, "Forbidden."); }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, recovery_page, sizeof recovery_page - 1);
}

static const char *upload_state(void)
{
    switch (recovery_control_state(&control)) {
        case WAITING: return "idle";
        case RECEIVING: case CANCELLED: return "receiving";
        case CONFIRM: return "confirming";
        case ACCEPTED: case WRITING: return "writing";
        case FAILED: return "failed";
        case RETRYING: case RESTART: return "restarting";
        default: return "failed";
    }
}

static esp_err_t get_status(httpd_req_t *request)
{
    if (!own_request(request)) { return failure(request, "Forbidden."); }
    char body[320];
    snprintf(body, sizeof body,
             "{\"mode\":\"recovery\",\"ota_protocol\":1,\"firmware\":\"recovery-1\","
             "\"on_probation\":false,\"uptime_s\":%lu,\"boot_id\":%lu,"
             "\"partition_table\":%lu,\"upload_state\":\"%s\"}",
             (unsigned long)now_seconds(), (unsigned long)boot_id,
             (unsigned long)RECOVERY_LAYOUT, upload_state());
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, body);
}

static bool receive_bytes(httpd_req_t *request, uint8_t *buffer, size_t length, uint32_t deadline)
{
    size_t received = 0;
    while (received < length && now_seconds() < deadline) {
        int count = httpd_req_recv(request, (char *)buffer + received, length - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) { continue; }
        if (count <= 0) { return false; }
        received += (size_t)count;
    }
    return received == length;
}

static esp_err_t post_image(httpd_req_t *request)
{
    if (!own_request(request)) { return failure(request, "Forbidden."); }
    if (!recovery_control_claim(&control, RECEIVING)) {
        return failure(request, "Device busy.");
    }
    ota_stream_t stream;
    if (!recovery_ota_start(&stream, request->content_len)) {
        recovery_control_set(&control, FAILED);
        return failure(request, "Invalid image size.");
    }
    uint8_t header[OTA_HEAD_BYTES];
    if (!receive_bytes(request, header, sizeof header, now_seconds() + 15)) {
        recovery_control_set(&control, FAILED);
        return failure(request, "Upload interrupted.");
    }
    const char *error = recovery_ota_check_header(header);
    if (error) {
        recovery_control_set(&control, FAILED);
        return failure(request, error);
    }
    /* The version is display text from an untrusted header, never markup or a format string. */
    esp_app_desc_t description;
    memcpy(&description, header + 32, sizeof description);
    portENTER_CRITICAL(&version_lock);
    for (size_t index = 0; index < sizeof image_version - 1; index++) {
        unsigned char letter = description.version[index];
        image_version[index] = letter >= 32 && letter <= 126 ? (char)letter : ' ';
    }
    image_version[sizeof image_version - 1] = '\0';
    portEXIT_CRITICAL(&version_lock);
    recovery_control_set(&control, CONFIRM);
    uint32_t deadline = now_seconds() + CONFIRM_SECONDS;
    while (recovery_control_state(&control) == CONFIRM && now_seconds() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    /* Seal an expired prompt before testing acceptance; a late key cannot
     * revive it while the handler returns an error. */
    if (now_seconds() >= deadline) {
        recovery_control_change(&control, CONFIRM, CANCELLED);
        recovery_control_change(&control, ACCEPTED, CANCELLED);
    }
    if (!recovery_control_change(&control, ACCEPTED, WRITING)) {
        recovery_control_set(&control, WAITING);
        return failure(request, "Not confirmed.");
    }
    if (!ota_stream_feed(&stream, header, sizeof header)) { goto failed; }
    uint8_t chunk[CHUNK_BYTES];
    size_t received = sizeof header;
    deadline = now_seconds() + UPLOAD_SECONDS;
    while (received < request->content_len) {
        size_t count = request->content_len - received;
        if (count > sizeof chunk) { count = sizeof chunk; }
        if (!receive_bytes(request, chunk, count, deadline)) {
            ota_stream_abort(&stream);
            goto failed;
        }
        if (!ota_stream_feed(&stream, chunk, count)) { goto failed; }
        received += count;
    }
    if (!ota_stream_end(&stream)) { goto failed; }
    httpd_resp_set_type(request, "application/json");
    esp_err_t result = httpd_resp_sendstr(request, "{\"message\":\"Installed. Restarting.\"}");
    recovery_control_set(&control, RESTART);
    return result;
failed:
    recovery_control_set(&control, FAILED);
    return failure(request, ota_stream_error(&stream));
}

static void network_event(void *argument, esp_event_base_t base, int32_t event, void *data)
{
    (void)argument; (void)data;
    if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) { atomic_store(&online, true); }
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) { atomic_store(&online, false); }
}

static void start_network(void)
{
    recovery_settings_read(&settings);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    if (settings.wifi_ssid[0]) {
        station = esp_netif_create_default_wifi_sta();
        ESP_ERROR_CHECK(esp_netif_set_hostname(station, settings.hostname));
    }
    wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
    initialization.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&initialization));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL));
    snprintf(access_point, sizeof access_point, "perink-recovery");
    wifi_config_t ap = { .ap = { .channel = 1, .max_connection = 2, .authmode = WIFI_AUTH_OPEN } };
    memcpy(ap.ap.ssid, access_point, strlen(access_point));
    ap.ap.ssid_len = strlen(access_point);
    ESP_ERROR_CHECK(esp_wifi_set_mode(station ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    if (station) {
        wifi_config_t sta = {0};
        memcpy(sta.sta.ssid, settings.wifi_ssid, strlen(settings.wifi_ssid));
        memcpy(sta.sta.password, settings.wifi_password, strlen(settings.wifi_password));
        sta.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
        if (esp_wifi_set_config(WIFI_IF_STA, &sta) != ESP_OK) {
            /* Unusable stored credentials must not take down the rescue AP. */
            ESP_LOGW("recovery", "stored Wi-Fi credentials rejected; keeping recovery AP");
            station = NULL;
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        }
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(BOARD_WIFI_TX_POWER_DBM * 4));
    int8_t tx_power;
    ESP_ERROR_CHECK(esp_wifi_get_max_tx_power(&tx_power));
    ESP_LOGI("recovery", "TX power limit: %d quarter-dBm", tx_power);
    boot_id = esp_random();
    if (mdns_init() == ESP_OK) {
        if (mdns_hostname_set(settings.hostname) != ESP_OK ||
            mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0) != ESP_OK) {
            ESP_LOGW("recovery", "discovery unavailable; use the address on the screen");
        }
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_open_sockets = 3;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.lru_purge_enable = true;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = get_page },
        { .uri = "/api/status", .method = HTTP_GET, .handler = get_status },
        { .uri = "/api/ota", .method = HTTP_POST, .handler = post_image },
    };
    for (size_t index = 0; index < sizeof routes / sizeof routes[0]; index++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[index]));
    }
}

static bool due(void) { return true; }

static display_result_t draw(int current_state, recovery_wait_t waiting)
{
    gfx_t graphics;
    gfx_init(&graphics, frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    char address[16], version[sizeof image_version];
    station_address(address, sizeof address);
    portENTER_CRITICAL(&version_lock);
    memcpy(version, image_version, sizeof version);
    portEXIT_CRITICAL(&version_lock);
    recovery_screen_draw(&graphics, current_state, version, address, access_point, waiting);
    return display_show(frame, REFRESH_FOR_SWITCH);
}

void app_main(void)
{
    bool update_requested = recovery_ota_take_handoff(esp_reset_reason() == ESP_RST_SW);
    ESP_LOGI("recovery", "entry: %s", update_requested ? "update" : "recovery");
    board_init();
    gpio_config_t buttons = { .pin_bit_mask = (1ULL << OK_GPIO) | (1ULL << BACK_GPIO),
                              .mode = GPIO_MODE_INPUT };
    ESP_ERROR_CHECK(gpio_config(&buttons));
    deadlines_start(due);
    refresh_policy_cfg_t policy = { .min_data_ms = 1000, .min_button_ms = 0,
                                    .full_every = 12, .keepalive_ms = 86400000 };
    display_start(&policy);
    start_network();
    recovery_fonts_start();
    words_use(settings.language);
    int shown = -1;
    unsigned shown_generation = 0;
    recovery_keys_t buttons_state = {0};
    bool shown_online = false;
    recovery_wait_t shown_waiting = RECOVERY_WAIT_NETWORK;
    uint32_t next_connection = 0;
    for (;;) {
        unsigned ticket = atomic_load(&control.ticket);
        recovery_state_t current_state = ticket & RECOVERY_STATE_MASK;
        unsigned keys = (gpio_get_level(OK_GPIO) == 0 ? 1u : 0u) |
                        (gpio_get_level(BACK_GPIO) == 0 ? 2u : 0u);
        unsigned generation = ticket >> RECOVERY_STATE_BITS;
        unsigned pressed = recovery_keys_pressed(&buttons_state, keys, current_state, generation,
                                                  shown == CONFIRM && shown_generation == generation);
        if (current_state == CONFIRM && (pressed & 2)) {
            recovery_control_change_ticket(&control, ticket, CANCELLED);
        } else if (current_state == CONFIRM && (pressed & 1)) {
            recovery_control_change_ticket(&control, ticket, ACCEPTED);
        } else if (shown == current_state &&
                   (current_state == FAILED ||
                    (current_state == WAITING && shown_waiting != RECOVERY_WAIT_PREPARING)) &&
                   (pressed & 2) &&
                   recovery_control_claim_ticket(&control, ticket, RETRYING)) {
            recovery_control_set(&control, recovery_ota_retry() ? RESTART : FAILED);
        }
        bool connected = atomic_load(&online);
        if (station && !connected && now_seconds() >= next_connection) {
            esp_wifi_connect();
            next_connection = now_seconds() + NETWORK_RETRY_SECONDS;
        }
        if (current_state != WAITING) { update_requested = false; }
        recovery_wait_t waiting = recovery_screen_wait(update_requested, station != NULL,
                                                       connected, now_seconds());
        bool network_help = current_state == FAILED ||
                            (current_state == WAITING && waiting == RECOVERY_WAIT_NETWORK);
        if (shown != current_state || (current_state == WAITING && shown_waiting != waiting) ||
            (network_help && shown_online != connected) || shown_generation != generation) {
            display_result_t result = draw(current_state, waiting);
            /* A new request may have the same visible version as the last one. */
            if (result == DISPLAY_SHOWN || result == DISPLAY_UNCHANGED) {
                shown = current_state;
                shown_generation = generation;
                shown_online = connected;
                shown_waiting = waiting;
                ESP_LOGI("recovery", "screen: state=%d waiting=%d online=%d",
                         current_state, waiting, connected);
            }
        }
        deadline_id_t deadline;
        while (deadlines_take_due(&deadline)) {
            if (deadline.kind == DEADLINE_PANEL_SLEEP) { display_rest(); }
            if (deadline.kind == DEADLINE_PANEL_KEEPALIVE) { display_keepalive(); }
        }
        if (current_state == RESTART) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            display_rest();
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
