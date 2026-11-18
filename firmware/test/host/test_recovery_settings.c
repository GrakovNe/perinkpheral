// SPDX-License-Identifier: MIT
#include <string.h>

#include "../../../recovery/main/settings.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tinytest.h"

static const esp_partition_t original = { .subtype = ESP_PARTITION_SUBTYPE_DATA_NVS, .size = 0x6000,
                                          .address = 0x9000, .label = NVS_DEFAULT_PART_NAME };
static const esp_partition_t *descriptor;
static bool has_partition, init_fails, open_fails;
static int init_calls, opens, closes;
static const char *stored_language;

esp_err_t esp_read_mac(uint8_t *mac, int interface)
{
    CHECK(interface == ESP_MAC_WIFI_STA);
    memset(mac, 0x12, APPCONF_MAC_BYTES);
    return ESP_OK;
}

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{
    CHECK(type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_DATA_NVS);
    CHECK_STR(label, original.label);
    return has_partition ? &original : NULL;
}

esp_err_t nvs_flash_init_partition_ptr(const esp_partition_t *partition)
{
    init_calls++;
    CHECK(partition != &original && partition->readonly && !original.readonly);
    CHECK(partition->address == original.address && partition->size == original.size);
    CHECK_STR(partition->label, original.label);
    CHECK(descriptor == NULL || partition == descriptor);
    descriptor = partition;
    return init_fails ? ESP_FAIL : ESP_OK;
}

esp_err_t nvs_open_from_partition(const char *partition, const char *name, int mode, nvs_handle_t *handle)
{
    opens++;
    CHECK_STR(partition, descriptor->label);
    CHECK_STR(name, "cfg");
    CHECK(mode == NVS_READONLY);
    *handle = 1;
    return open_fails ? ESP_FAIL : ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *value, size_t *length)
{
    CHECK(handle == 1);
    const char *text = strcmp(key, "language") == 0 ? stored_language :
                       strcmp(key, "wssid") == 0 ? "Saved Wi-Fi" :
                       strcmp(key, "wpass") == 0 ? "example-password" : "Desk";
    if (text == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    size_t bytes = strlen(text) + 1;
    if (bytes > *length) {
        *length = bytes;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(value, text, bytes);
    *length = bytes;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    CHECK(handle == 1);
    closes++;
}

int main(void)
{
    appconf_t settings;
    recovery_settings_read(&settings);
    CHECK(init_calls == 0 && opens == 0);
    CHECK_STR(settings.language, "en");
    CHECK_STR(settings.wifi_ssid, "");

    has_partition = init_fails = true;
    recovery_settings_read(&settings);
    CHECK(init_calls == 1 && opens == 0);
    CHECK_STR(settings.wifi_ssid, "");

    init_fails = false;
    stored_language = "ru";
    recovery_settings_read(&settings);
    CHECK(init_calls == 2 && opens == 1 && closes == 1);
    CHECK_STR(settings.language, "ru");
    CHECK_STR(settings.wifi_ssid, "Saved Wi-Fi");

    static const char *const FALLBACKS[] = { NULL, "uk", "x-overlong" };
    for (size_t index = 0; index < sizeof FALLBACKS / sizeof FALLBACKS[0]; index++) {
        stored_language = FALLBACKS[index];
        recovery_settings_read(&settings);
        CHECK_STR(settings.language, "en");
        CHECK_STR(settings.wifi_ssid, "Saved Wi-Fi");
        CHECK_STR(settings.wifi_password, "example-password");
    }
    CHECK(init_calls == 2 && closes == 4);

    open_fails = true;
    recovery_settings_read(&settings);
    CHECK(opens == 5 && closes == 4);
    CHECK_STR(settings.language, "en");
    CHECK_STR(settings.wifi_ssid, "");
    REPORT();
}
