// SPDX-License-Identifier: MIT
#include "settings.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"

void recovery_settings_read(appconf_t *settings)
{
    uint8_t mac[APPCONF_MAC_BYTES];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    char device_id[APPCONF_DEVICE_ID_SIZE];
    appconf_device_id(mac, device_id);
    appconf_defaults(settings, device_id);
    if (nvs_flash_init() != ESP_OK) {
        return;
    }
    nvs_handle_t handle;
    if (nvs_open("cfg", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t size = sizeof settings->device_name;
    if (nvs_get_str(handle, "name", settings->device_name, &size) != ESP_OK) {
        appconf_default_name(device_id, settings->device_name, sizeof settings->device_name);
    }
    char error[96];
    if (!appconf_validate(settings, error, sizeof error)) {
        appconf_default_name(device_id, settings->device_name, sizeof settings->device_name);
    }
    size = sizeof settings->wifi_ssid;
    bool valid = nvs_get_str(handle, "wssid", settings->wifi_ssid, &size) == ESP_OK;
    size = sizeof settings->wifi_password;
    esp_err_t result = nvs_get_str(handle, "wpass", settings->wifi_password, &size);
    if (!valid || (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND)) {
        settings->wifi_ssid[0] = settings->wifi_password[0] = '\0';
    }
    nvs_close(handle);
    if (!appconf_validate(settings, error, sizeof error)) {
        settings->wifi_ssid[0] = settings->wifi_password[0] = '\0';
    }
    appconf_derive(settings, device_id);
}
