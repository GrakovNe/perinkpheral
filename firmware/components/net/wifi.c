/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wifi.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "captive_dns.h"
#include "deadlines.h"

static const char *TAG = "wifi";

#define PASSWORD_MAX            64
#define ACCESS_POINT_CHANNEL    1
#define ACCESS_POINT_CLIENTS    4
#define EVENT_QUEUE_LENGTH      8
#define TASK_STACK_BYTES        4096
#define MICROSECONDS_PER_MS     1000

static struct {
    char ssid[WIFI_SSID_MAX + 1];
    char password[PASSWORD_MAX + 1];
    char access_point_name[WIFI_SSID_MAX + 1];
    void (*on_change)(void);

    wifi_plan_t        plan; /* the Wi-Fi task's alone */
    QueueHandle_t      events;
    bool               access_point_up;
    bool               radio_off; /* the Wi-Fi task's alone */

    /* What other tasks may ask about, under the lock. */
    SemaphoreHandle_t lock;
    wifi_phase_t      phase;
    char              address[16];
    bool              is_radio_off;
    bool              was_reached;
} s_wifi;

/* Whether the network was reached last time, through the chip's deep
 * sleep: the icon that says so stays on the panel through it, and a start
 * that forgot would redraw the panel for nothing. */
static RTC_DATA_ATTR bool s_was_reached_before_sleep;

/* Asked of the Wi-Fi task through its queue, beside the plan's events. */
#define RADIO_OFF_ASKED ((wifi_plan_event_t)100)
#define RADIO_ON_ASKED  ((wifi_plan_event_t)101)

static int64_t now_ms(void)
{
    return esp_timer_get_time() / MICROSECONDS_PER_MS;
}

/* From an event handler or a timer: hands the event to the Wi-Fi task. */
static void post(wifi_plan_event_t event)
{
    if (xQueueSend(s_wifi.events, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event %d dropped: the queue is full", (int)event);
    }
}

/* Where the network was found last: the access point and its channel.
 * With it the next connection looks in one place instead of scanning
 * every channel - a second or two less of each time the radio comes up
 * for a poll, which on a battery is most of what a poll costs. Kept
 * through the chip's deep sleep. */
static RTC_DATA_ATTR struct {
    bool    is_known;
    uint8_t access_point[6];
    uint8_t channel;
    char    ssid[WIFI_SSID_MAX + 1];
} s_found_at;

/* The attempt under way looks only where the network was last: if it
 * fails, that place is forgotten and the next one looks everywhere. */
static bool s_is_looking_in_one_place;

static void remember_where_found(void)
{
    wifi_ap_record_t found;
    if (esp_wifi_sta_get_ap_info(&found) != ESP_OK) {
        return;
    }
    memcpy(s_found_at.access_point, found.bssid, sizeof s_found_at.access_point);
    s_found_at.channel = found.primary;
    strlcpy(s_found_at.ssid, s_wifi.ssid, sizeof s_found_at.ssid);
    s_found_at.is_known = true;
}

static void on_got_address(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const ip_event_got_ip_t *got = data;
    xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
    esp_ip4addr_ntoa(&got->ip_info.ip, s_wifi.address, sizeof s_wifi.address);
    xSemaphoreGive(s_wifi.lock);
    s_is_looking_in_one_place = false;
    remember_where_found();
    post(WIFI_EVENT_CONNECTED);
}

static void on_station_disconnected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const wifi_event_sta_disconnected_t *lost = data;
    ESP_LOGW(TAG, "not connected to \"%s\", reason %d", s_wifi.ssid, lost->reason);
    if (s_is_looking_in_one_place) {
        /* Not there any more - another channel, another access point. */
        s_is_looking_in_one_place = false;
        s_found_at.is_known = false;
    }
    post(WIFI_EVENT_DISCONNECTED);
}

/* The radio is running: only now can it be told to connect. */
static void on_station_started(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    post(WIFI_EVENT_STARTED);
}

void wifi_retry_is_due(void)
{
    post(WIFI_EVENT_RETRY_DUE);
}

static void start_access_point(void)
{
    wifi_config_t config = { 0 };
    strlcpy((char *)config.ap.ssid, s_wifi.access_point_name, sizeof config.ap.ssid);
    config.ap.ssid_len = (uint8_t)strlen(s_wifi.access_point_name);
    config.ap.channel = ACCESS_POINT_CHANNEL;
    config.ap.authmode = WIFI_AUTH_OPEN;
    config.ap.max_connection = ACCESS_POINT_CLIENTS;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &config);
    }
    if (err != ESP_OK) {
        /* Without it the device cannot be set up, but it must not go
         * into a restart loop over that either. */
        ESP_LOGE(TAG, "cannot bring the setup access point up: %s", esp_err_to_name(err));
        return;
    }
    s_wifi.access_point_up = true;
    captive_dns_enable(true);
    ESP_LOGI(TAG, "setup access point \"%s\" is up", s_wifi.access_point_name);
}

static void stop_access_point(void)
{
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot take the setup access point down: %s", esp_err_to_name(err));
        return;
    }
    s_wifi.access_point_up = false;
    captive_dns_enable(false);
    ESP_LOGI(TAG, "setup access point is down");
}

static void connect(void)
{
    wifi_config_t config = { 0 };
    strlcpy((char *)config.sta.ssid, s_wifi.ssid, sizeof config.sta.ssid);
    strlcpy((char *)config.sta.password, s_wifi.password, sizeof config.sta.password);
    config.sta.threshold.authmode =
        s_wifi.password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    s_is_looking_in_one_place =
        s_found_at.is_known && strcmp(s_found_at.ssid, s_wifi.ssid) == 0;
    if (s_is_looking_in_one_place) {
        config.sta.bssid_set = true;
        memcpy(config.sta.bssid, s_found_at.access_point, sizeof config.sta.bssid);
        config.sta.channel = s_found_at.channel;
        config.sta.scan_method = WIFI_FAST_SCAN;
    }
    esp_err_t err = esp_wifi_set_mode(s_wifi.access_point_up ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &config);
    }
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    if (err == ESP_ERR_WIFI_CONN) {
        return; /* an attempt is under way already: its outcome will come */
    }
    if (err != ESP_OK) {
        /* The plan hears of it as of any other failed attempt. */
        ESP_LOGW(TAG, "cannot start connecting: %s", esp_err_to_name(err));
        /* If it was the place that the radio would not take, not again. */
        s_is_looking_in_one_place = false;
        s_found_at.is_known = false;
        post(WIFI_EVENT_DISCONNECTED);
    }
}

static void carry_out(const wifi_plan_actions_t *actions)
{
    if (actions->start_access_point) {
        start_access_point();
    }
    if (actions->stop_access_point) {
        stop_access_point();
    }
    if (actions->connect) {
        connect();
    }
    if (actions->retry_in_ms > 0) {
        deadlines_set_in(DEADLINE_WIFI_RETRY, 0, 0, actions->retry_in_ms);
    }
}

/* `failed_last`: the event just handled was an attempt that failed. */
static void publish_phase(bool failed_last)
{
    xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
    /* With the radio off nobody is online, whatever the plan was left at;
     * whether the network was reached last time is what it was. */
    wifi_phase_t phase = s_wifi.radio_off ? WIFI_PHASE_CONNECTING : s_wifi.plan.phase;
    bool was_reached = s_wifi.radio_off ? s_wifi.was_reached
                                        : s_wifi.plan.phase == WIFI_PHASE_ONLINE ||
                                              (s_wifi.was_reached && !failed_last);
    bool changed = s_wifi.phase != phase || s_wifi.was_reached != was_reached ||
                   s_wifi.is_radio_off != s_wifi.radio_off;
    s_wifi.phase = phase;
    s_wifi.was_reached = was_reached;
    s_was_reached_before_sleep = was_reached;
    s_wifi.is_radio_off = s_wifi.radio_off;
    if (s_wifi.phase != WIFI_PHASE_ONLINE) {
        s_wifi.address[0] = '\0';
    }
    xSemaphoreGive(s_wifi.lock);
    if (changed && s_wifi.on_change != NULL) {
        s_wifi.on_change();
    }
}

static void wifi_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* The first event is the radio saying it has started. */
        wifi_plan_event_t event;
        xQueueReceive(s_wifi.events, &event, portMAX_DELAY);
        if (event == RADIO_OFF_ASKED) {
            /* Only a device that is where it should be may go dark. */
            if (!s_wifi.radio_off && s_wifi.plan.has_network &&
                !wifi_phase_has_access_point(s_wifi.plan.phase)) {
                s_wifi.radio_off = true;
                deadlines_cancel(DEADLINE_WIFI_RETRY, 0, 0);
                esp_wifi_stop();
            }
            publish_phase(false);
            continue;
        }
        if (event == RADIO_ON_ASKED) {
            if (s_wifi.radio_off) {
                s_wifi.radio_off = false;
                /* A new run of attempts: the old failures are not counted. */
                wifi_plan_init(&s_wifi.plan, true);
                esp_wifi_start(); /* and the radio says when it has started */
            }
            publish_phase(false);
            continue;
        }
        if (s_wifi.radio_off) {
            continue; /* the last words of a radio that was turned off */
        }
        bool failed = event == WIFI_EVENT_DISCONNECTED;
        wifi_plan_actions_t actions = wifi_plan_step(&s_wifi.plan, event, now_ms());
        carry_out(&actions);
        publish_phase(failed);
    }
}

void wifi_start(const wifi_config_in_t *config)
{
    strlcpy(s_wifi.ssid, config->ssid, sizeof s_wifi.ssid);
    strlcpy(s_wifi.password, config->password, sizeof s_wifi.password);
    strlcpy(s_wifi.access_point_name, config->access_point_name,
            sizeof s_wifi.access_point_name);
    s_wifi.on_change = config->on_change;
    wifi_plan_init(&s_wifi.plan, s_wifi.ssid[0] != '\0');
    s_wifi.phase = s_wifi.plan.phase;

    s_wifi.lock = xSemaphoreCreateMutex();
    s_wifi.was_reached = s_was_reached_before_sleep;
    s_wifi.events = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(wifi_plan_event_t));
    configASSERT(s_wifi.lock != NULL && s_wifi.events != NULL);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    /* A phone that joins the access point is told that every name is the
     * device, so it opens the device's page without being asked. */
    static const uint8_t SETUP_ADDRESS[4] = { 192, 168, 4, 1 };
    captive_dns_start(SETUP_ADDRESS);
    esp_netif_t *station = esp_netif_create_default_wifi_sta();
    /* A name the network stack will not take is not worth refusing to
     * start over: the device then goes by its default name. */
    esp_err_t hostname_result = esp_netif_set_hostname(station, config->hostname);
    if (hostname_result != ESP_OK) {
        ESP_LOGW(TAG, "hostname \"%s\" not taken: %s", config->hostname,
                 esp_err_to_name(hostname_result));
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START,
                                                        on_station_started, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_station_disconnected, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        on_got_address, NULL, NULL));
    /* The settings are the host's to keep, not the Wi-Fi library's. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    /* The task first, so that it is there to take the start event. */
    BaseType_t ok = xTaskCreate(wifi_task, "wifi", TASK_STACK_BYTES, NULL, 5, NULL);
    configASSERT(ok == pdPASS);
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_get_status(wifi_status_t *status)
{
    memset(status, 0, sizeof *status);
    strlcpy(status->ssid, s_wifi.ssid, sizeof status->ssid);
    strlcpy(status->access_point_name, s_wifi.access_point_name,
            sizeof status->access_point_name);
    if (s_wifi.lock == NULL) {
        return; /* not started: everything stays empty, the phase is setup */
    }
    xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
    status->phase = s_wifi.phase;
    status->is_radio_off = s_wifi.is_radio_off;
    status->was_reached = s_wifi.was_reached;
    strlcpy(status->address, s_wifi.address, sizeof status->address);
    xSemaphoreGive(s_wifi.lock);
}

void wifi_set_radio(bool on)
{
    if (s_wifi.events != NULL) {
        post(on ? RADIO_ON_ASKED : RADIO_OFF_ASKED);
    }
}

bool wifi_is_online(void)
{
    wifi_status_t status;
    wifi_get_status(&status);
    return status.phase == WIFI_PHASE_ONLINE;
}
