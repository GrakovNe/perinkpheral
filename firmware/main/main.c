/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"

#include "sdkconfig.h"

#include "app_host.h"
#include "app_system.h"
#include "m3_reserve.h"
#include "power.h"
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

static appconf_t s_cfg;

/* Runs in the Wi-Fi task whenever the connection comes or goes. */
static void on_wifi_changed(void)
{
    if (wifi_is_online()) {
        /* The clock was asked before there was a network to answer. */
        net_time_sync_now();
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
    m3_reserve();

    settings_nvs_init();
    board_init();

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    /* Kept: the system API derives names from it for as long as the device runs. */
    static char device_id[5];
    appconf_device_id(mac, device_id);

    appconf_t *cfg = &s_cfg;
    settings_load(cfg, device_id);
    ESP_LOGI(TAG, "device %s: name \"%s\", hostname %s.local",
             device_id, cfg->device_name, cfg->hostname);

    /* The screen and the buttons come first: the device works without a
     * network. */
    events_start();
    timers_start();
    const refresh_policy_cfg_t refresh_policy = settings_refresh_policy(cfg);
    display_start(&refresh_policy);
    app_host_use(cfg);
    app_system_use(cfg, device_id);
    buttons_start(on_button_event);
    sysinfo_start(cfg->device_name);
    storage_start();

    wifi_config_in_t wifi = {
        .ssid = cfg->wifi_ssid,
        .password = cfg->wifi_password,
        .hostname = cfg->hostname,
        .access_point_name = cfg->device_name,
        .on_change = on_wifi_changed,
    };
    status_start();
    wifi_start(&wifi);
    power_start(cfg->power_saving != 0);
    net_time_start(cfg->ntp_server, cfg->tz, cfg->time_auto != 0);
    web_server_start(cfg, device_id);
    /* An image installed over the network has to prove that the network
     * still works before it is kept. */
    ota_start(cfg->wifi_ssid[0] != '\0');
#ifdef CONFIG_APP_DEV_CONSOLE
    devconsole_start();
#endif

    host_run();
}
