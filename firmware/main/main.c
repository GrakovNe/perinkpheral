/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "esp_app_desc.h"
#include "esp_log.h"
#include "mdns.h"
#include "esp_mac.h"
#include "esp_system.h"

#include "sdkconfig.h"

#include "app_code.h"
#include "app_host.h"
#include "app_system.h"
#include "m3_area.h"
#include "poller.h"
#include "power.h"
#include "app_kv.h"
#include "appconf.h"
#include "board.h"
#include "buttons.h"
#include "devconsole.h"
#include "display.h"
#include "events.h"
#include "host.h"
#include "log_capture.h"
#include "reset.h"
#include "net_time.h"
#include "ota.h"
#include "settings.h"
#include "status.h"
#include "storage.h"
#include "sysinfo.h"
#include "timers.h"
#include "web_server.h"
#include "wifi.h"

static const char *TAG = "main";

#define WEB_PORT 80


/* The device answers to its name: <hostname>.local, and says it has a page.
 * Without this the name on the page and in the log is one nobody can open.
 * Not worth failing to start over: the address still works. */
static void announce_name(const appconf_t *settings)
{
    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        err = mdns_hostname_set(settings->hostname);
    }
    if (err == ESP_OK) {
        err = mdns_instance_name_set(settings->device_name);
    }
    if (err == ESP_OK) {
        err = mdns_service_add(NULL, "_http", "_tcp", WEB_PORT, NULL, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "the name is not announced: %s", esp_err_to_name(err));
    }
}

/* The wall clock was set to another time, by the network or by hand.
 * Alarms for a time of day follow it; applications are told only of a
 * change somebody could notice, not of every correction by a few
 * milliseconds. */
#define CLOCK_CHANGE_WORTH_TELLING_MS 2000

static void on_clock_moved(int64_t delta_ms)
{
    status_check(); /* the day may be another, and midnight is elsewhere */
    if (delta_ms >= CLOCK_CHANGE_WORTH_TELLING_MS || delta_ms <= -CLOCK_CHANGE_WORTH_TELLING_MS) {
        app_host_tell_time_changed();
    }
}

/* Runs in the Wi-Fi task whenever the connection comes or goes. */
static void on_wifi_changed(void)
{
    if (wifi_is_online()) {
        /* The clock was asked before there was a network to answer. */
        net_time_sync_now();
        poller_network_is_back();
        /* News only when the network was away, not when the device raised
         * its own radio for a poll. */
        if (!power_is_saving()) {
            app_host_tell_network_is_up();
        }
    }
    status_check();
}

/* Runs in the buttons task: only hands the press over to the host. */
static void on_button_event(uint8_t index, btn_event_t event)
{
    static const event_press_t PRESS_OF[] = {
        [BTN_DOWN] = EVENT_PRESS_DOWN,
        [BTN_UP] = EVENT_PRESS_UP,
        [BTN_SHORT] = EVENT_PRESS_SHORT,
        [BTN_LONG] = EVENT_PRESS_LONG,
    };
    events_post_button(index, PRESS_OF[event]);
}

void app_main(void)
{
    log_capture_start();
    ESP_LOGI(TAG, "perINKpheral firmware %s (idf %s, built %s %s)",
             esp_app_get_description()->version, esp_get_idf_version(),
             __DATE__, __TIME__);

    reset_early_init();
    /* First of all: the heap will not be this whole again. */
    m3_area_start();

    settings_nvs_init();
    app_kv_start();
    board_init();

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    char device_id[5];
    appconf_device_id(mac, device_id);

    settings_start(device_id);
    static appconf_t settings; /* as they are at start: what is told once is told from here */
    settings_read(&settings);
    const appconf_t *cfg = &settings;
    ESP_LOGI(TAG, "device %s: name \"%s\", hostname %s.local",
             device_id, cfg->device_name, cfg->hostname);

    /* The screen and the buttons come first: the device works without a
     * network. */
    events_start();
    host_deadlines_start();
    const refresh_policy_cfg_t refresh_policy = settings_refresh_policy(cfg);
    display_start(&refresh_policy);
    app_host_set_dithering(cfg->dither != 0);
    gfx_font_use((gfx_family_t)cfg->font);
    buttons_start(on_button_event);
    sysinfo_start(cfg->device_name);
    storage_start();
    app_code_start();

    wifi_config_in_t wifi = {
        .ssid = cfg->wifi_ssid,
        .password = cfg->wifi_password,
        .hostname = cfg->hostname,
        .access_point_name = cfg->device_name,
        .on_change = on_wifi_changed,
    };
    status_start();
    wifi_start(&wifi);
    power_start(cfg->power_saving != APPCONF_POWER_PLUGGED);
    announce_name(cfg);
    net_time_on_change(on_clock_moved);
    net_time_start(cfg->ntp_server, cfg->tz, cfg->time_auto != 0);
    web_server_start();
    /* An image installed over the network has to prove that the network
     * still works before it is kept. */
    ota_start(cfg->wifi_ssid[0] != '\0');
#ifdef CONFIG_APP_DEV_CONSOLE
    devconsole_start();
#endif

    host_run();
}
