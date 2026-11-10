/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "ota.h"

#include <inttypes.h>
#include <stddef.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ota_health.h"
#include "ota_stream.h"
#include "web_server.h"
#include "wifi.h"

static const char *TAG = "ota";

#define HEALTH_CHECK_PERIOD_MS 1000
#define MICROSECONDS_PER_SECOND 1000000

/* ota_stream.c finds the application description by these numbers. */
_Static_assert(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) == 24 + 8,
               "the application description is not where ota_stream looks for it");
_Static_assert(offsetof(esp_app_desc_t, project_name) == 48 && sizeof(esp_app_desc_t) == 256,
               "the application description is not laid out as ota_stream expects");

/* ---- writing an uploaded image into the spare slot ---- */

static struct {
    ota_stream_t           stream;
    bool                   in_progress;
    const esp_partition_t *slot;
    esp_ota_handle_t       handle;
} s_upload;

static bool slot_begin(void *context, size_t image_size)
{
    (void)context;
    esp_err_t err = esp_ota_begin(s_upload.slot, image_size, &s_upload.handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

static bool slot_write(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    return esp_ota_write(s_upload.handle, data, length) == ESP_OK;
}

static bool slot_finish(void *context)
{
    (void)context;
    /* esp_ota_end checks the image and lets go of the handle either way. */
    esp_err_t err = esp_ota_end(s_upload.handle);
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(s_upload.slot);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "the uploaded image was rejected: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

static void slot_abort(void *context)
{
    (void)context;
    esp_ota_abort(s_upload.handle);
}

static const ota_writer_t SLOT_WRITER = { slot_begin, slot_write, slot_finish, slot_abort };

/* NULL when the stream is content, its error otherwise - and then the
 * upload is over. */
static const char *upload_result(bool ok)
{
    if (ok) {
        return NULL;
    }
    s_upload.in_progress = false;
    ESP_LOGW(TAG, "upload: %s", ota_stream_error(&s_upload.stream));
    return ota_stream_error(&s_upload.stream);
}

const char *ota_upload_begin(size_t image_size)
{
    if (s_upload.in_progress) {
        return "another upload is in progress";
    }
    s_upload.slot = esp_ota_get_next_update_partition(NULL);
    if (s_upload.slot == NULL) {
        return "there is no spare firmware slot";
    }
    s_upload.in_progress = true;
    ESP_LOGI(TAG, "upload of %u bytes into %s", (unsigned)image_size, s_upload.slot->label);
    return upload_result(ota_stream_start(&s_upload.stream, &SLOT_WRITER, NULL,
                                          esp_app_get_description()->project_name, image_size,
                                          s_upload.slot->size));
}

const char *ota_upload_data(const uint8_t *data, size_t length)
{
    return upload_result(ota_stream_feed(&s_upload.stream, data, length));
}

const char *ota_upload_end(void)
{
    const char *error = upload_result(ota_stream_end(&s_upload.stream));
    s_upload.in_progress = false;
    if (error == NULL) {
        ESP_LOGW(TAG, "the uploaded image is in place");
    }
    return error;
}

void ota_upload_abort(void)
{
    if (s_upload.in_progress) {
        ota_stream_abort(&s_upload.stream);
        s_upload.in_progress = false;
    }
}

/* ---- the probation of a freshly installed image ---- */

static bool          s_network_stored;
static volatile bool s_on_probation;

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
            ESP_LOGE(TAG, "nothing to roll back to, keeping this image");
        }
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "image confirmed after %" PRIu32 " s: %s", input.uptime_s,
                 esp_err_to_name(err));
        s_on_probation = false;
        vTaskDelete(NULL);
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
        BaseType_t ok = xTaskCreate(probation_task, "ota-probation", 3072, NULL, 4, NULL);
        configASSERT(ok == pdPASS);
    }
}

bool ota_is_on_probation(void)
{
    return s_on_probation;
}
