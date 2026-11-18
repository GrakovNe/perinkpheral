/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "words.h"
#include "priorities.h"
#include "ota.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdatomic.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "ota_health.h"
#include "host.h"
#include "image_layout.h"
#include "ota_stream.h"
#include "ota_handoff.h"
#include "power.h"
#include "web_server.h"
#include "wifi.h"

static const char *TAG = "ota";

#define HEALTH_CHECK_PERIOD_MS 1000
#define MICROSECONDS_PER_SECOND 1000000

#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "Updates over the network rely on the bootloader's rollback: without it a new image is never on probation and a bad one stays."
#endif

/* ota_stream.c finds the application description by these numbers. */
_Static_assert(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) == 24 + 8,
               "the application description is not where ota_stream looks for it");
_Static_assert(offsetof(esp_app_desc_t, project_name) == 48 && sizeof(esp_app_desc_t) == 256,
               "the application description is not laid out as ota_stream expects");

/* True from start until a freshly installed image has proved itself. */
static atomic_bool s_on_probation;

_Static_assert(sizeof(esp_ota_select_entry_t) <= OTA_HANDOFF_OFFSET,
               "recovery hint overlaps the boot record");

const char *ota_enter_recovery(void)
{
    if (s_on_probation) { return "Previous update is being verified."; }
    const esp_partition_t *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!factory || esp_ota_set_boot_partition(factory) != ESP_OK) {
        return "Recovery image unavailable. Restore it over USB.";
    }
    const esp_partition_t *metadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                        ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    const ota_handoff_t hint = { OTA_HANDOFF_MAGIC, ~OTA_HANDOFF_MAGIC };
    /* Failure only loses the quiet update screen; recovery remains bootable. */
    if (!metadata || metadata->erase_size <= OTA_HANDOFF_OFFSET + sizeof hint ||
        esp_partition_write(metadata, OTA_HANDOFF_OFFSET, &hint, sizeof hint) != ESP_OK) {
        ESP_LOGW(TAG, "recovery update hint unavailable");
    }
    return NULL;
}

/* ---- the probation of a freshly installed image ---- */

static bool s_network_stored;

static void probation_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(HEALTH_CHECK_PERIOD_MS));
        wifi_status_t wifi;
        wifi_get_status(&wifi);
        ota_health_input_t input = {
            .network_stored = s_network_stored,
            .online = wifi.phase == WIFI_PHASE_ONLINE,
            .access_point_up = wifi_phase_has_access_point(wifi.phase),
            .web_server_up = web_server_is_up(),
            .uptime_s = (uint32_t)(esp_timer_get_time() / MICROSECONDS_PER_SECOND),
        };
        ota_health_t verdict = ota_health(&input);
        if (verdict == OTA_HEALTH_WAIT) {
            continue;
        }
        if (verdict == OTA_HEALTH_ROLL_BACK) {
            ESP_LOGE(TAG, "not reachable after %" PRIu32 " s on the new image: rolling back",
                     input.uptime_s);
            esp_ota_mark_app_invalid_rollback_and_reboot();
            /* Only reached when there is nothing to roll back to. */
            ESP_LOGE(TAG, "recovery unavailable; image remains unconfirmed");
            continue;
        }
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        /* By its number: a release build has no names for errors, and would
         * say "UNKNOWN ERROR" of ESP_OK. */
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "image confirmed after %" PRIu32 " s", input.uptime_s);
        } else {
            ESP_LOGE(TAG, "image not confirmed after %" PRIu32 " s: error 0x%x", input.uptime_s,
                     (unsigned)err);
        }
        if (err == ESP_OK) {
            s_on_probation = false;
            vTaskDelete(NULL);
        }
    }
}

void ota_start(bool network_stored)
{
    s_network_stored = network_stored;
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    s_on_probation = running != NULL && esp_ota_get_state_partition(running, &state) == ESP_OK &&
                     state == ESP_OTA_IMG_PENDING_VERIFY;
    ESP_LOGI(TAG, "running %s from %s%s", esp_app_get_description()->version,
             running != NULL ? running->label : "?", s_on_probation ? ", on probation" : "");
    if (s_on_probation) {
        BaseType_t ok = xTaskCreate(probation_task, "ota-probation", STACK_OTA_PROBATION, NULL, PRIORITY_BACKGROUND, NULL);
        configASSERT(ok == pdPASS);
    }
}

bool ota_is_on_probation(void)
{
    return s_on_probation;
}
