/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "sdkconfig.h"

#include "appconf.h"
#include "board.h"
#include "buttons.h"
#include "devconsole.h"
#include "display.h"
#include "events.h"
#include "host.h"
#include "reset.h"
#include "settings.h"
#include "storage.h"
#include "sysinfo.h"
#include "timers.h"

static const char *TAG = "main";

static appconf_t s_cfg;

/* Runs in the buttons task: only hands the press over to the host. */
static void on_button_pressed(uint8_t index, btn_event_t press)
{
    events_post_button(index, press == BTN_LONG);
}

void app_main(void)
{
    ESP_LOGI(TAG, "perINKpheral firmware %s (idf %s, built %s %s)",
             esp_app_get_description()->version, esp_get_idf_version(),
             __DATE__, __TIME__);

    reset_early_init();
    settings_nvs_init();
    board_init();

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    char device_id[5];
    appconf_device_id(mac, device_id);

    appconf_t *cfg = &s_cfg;
    settings_load(cfg, device_id);
    ESP_LOGI(TAG, "device %s: name \"%s\", hostname %s.local",
             device_id, cfg->device_name, cfg->hostname);

    /* The screen and the buttons come first: the device works without a
     * network. */
    events_start();
    timers_start();
    display_start();
    buttons_start(on_button_pressed);
    sysinfo_start(cfg->device_name);
    storage_start();
#ifdef CONFIG_APP_DEV_CONSOLE
    devconsole_start();
#endif

    /* The image booted and drives its hardware: good enough to keep. */
    esp_ota_mark_app_valid_cancel_rollback();

    host_run();
}
