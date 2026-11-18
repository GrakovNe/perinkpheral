// SPDX-License-Identifier: MIT
#include "settings.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static bool open_read_only_storage(void)
{
    /* IDF may repair NVS during initialization, before a handle is opened.
     * Give it a read-only partition descriptor that lives as long as NVS. */
    static esp_partition_t read_only;
    static bool prepared, initialized;
    if (!prepared) {
        const esp_partition_t *partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NVS_DEFAULT_PART_NAME);
        if (partition == NULL) {
            return false;
        }
        read_only = *partition;
        read_only.readonly = true;
        prepared = true;
    }
    if (!initialized) {
        initialized = nvs_flash_init_partition_ptr(&read_only) == ESP_OK;
    }
    return initialized;
}

void recovery_settings_read(appconf_t *settings)
{
    uint8_t mac[APPCONF_MAC_BYTES];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    char device_id[APPCONF_DEVICE_ID_SIZE];
    appconf_device_id(mac, device_id);
    appconf_defaults(settings, device_id);
    if (!open_read_only_storage()) {
        return;
    }
    nvs_handle_t handle;
    if (nvs_open_from_partition(NVS_DEFAULT_PART_NAME, "cfg", NVS_READONLY, &handle) != ESP_OK) {
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
    char language[sizeof settings->language];
    size = sizeof language;
    if (nvs_get_str(handle, "language", language, &size) == ESP_OK &&
        (strcmp(language, "en") == 0 || strcmp(language, "ru") == 0)) {
        strcpy(settings->language, language);
    }
    nvs_close(handle);
    if (!appconf_validate(settings, error, sizeof error)) {
        settings->wifi_ssid[0] = settings->wifi_password[0] = '\0';
    }
    appconf_derive(settings, device_id);
}
