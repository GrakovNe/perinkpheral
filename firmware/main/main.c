/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "appconf.h"
#include "board.h"
#include "buttons.h"
#include "demo.h"
#include "display.h"
#include "reset.h"
#include "settings.h"

static const char *TAG = "main";

static appconf_t s_cfg;

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
    display_start();
    buttons_start();

    /* The image booted and drives its hardware: good enough to keep. */
    esp_ota_mark_app_valid_cancel_rollback();

    demo_run();
}
