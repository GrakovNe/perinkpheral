/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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
    esp_timer_handle_t retry_timer;
    bool               access_point_up;

    /* What other tasks may ask about, under the lock. */
    SemaphoreHandle_t lock;
    wifi_phase_t      phase;
    char              address[16];
} s_wifi;

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

static void on_got_address(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const ip_event_got_ip_t *got = data;
    xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
    esp_ip4addr_ntoa(&got->ip_info.ip, s_wifi.address, sizeof s_wifi.address);
    xSemaphoreGive(s_wifi.lock);
    post(WIFI_EVENT_CONNECTED);
}

static void on_station_disconnected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const wifi_event_sta_disconnected_t *lost = data;
    ESP_LOGW(TAG, "not connected to \"%s\", reason %d", s_wifi.ssid, lost->reason);
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

static void on_retry_timer(void *arg)
{
    (void)arg;
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
    ESP_LOGI(TAG, "setup access point is down");
}

static void connect(void)
{
    wifi_config_t config = { 0 };
    strlcpy((char *)config.sta.ssid, s_wifi.ssid, sizeof config.sta.ssid);
    strlcpy((char *)config.sta.password, s_wifi.password, sizeof config.sta.password);
    config.sta.threshold.authmode =
        s_wifi.password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
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
        esp_timer_stop(s_wifi.retry_timer); /* not running is fine */
        ESP_ERROR_CHECK(esp_timer_start_once(
            s_wifi.retry_timer, (uint64_t)actions->retry_in_ms * MICROSECONDS_PER_MS));
    }
}

static void publish_phase(void)
{
    xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
    bool changed = s_wifi.phase != s_wifi.plan.phase;
    s_wifi.phase = s_wifi.plan.phase;
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
        wifi_plan_actions_t actions = wifi_plan_step(&s_wifi.plan, event, now_ms());
        carry_out(&actions);
        publish_phase();
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
    s_wifi.events = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(wifi_plan_event_t));
    configASSERT(s_wifi.lock != NULL && s_wifi.events != NULL);
    esp_timer_create_args_t retry = { .callback = on_retry_timer, .name = "wifi-retry" };
    ESP_ERROR_CHECK(esp_timer_create(&retry, &s_wifi.retry_timer));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
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
    strlcpy(status->address, s_wifi.address, sizeof status->address);
    xSemaphoreGive(s_wifi.lock);
}

bool wifi_is_online(void)
{
    wifi_status_t status;
    wifi_get_status(&status);
    return status.phase == WIFI_PHASE_ONLINE;
}
