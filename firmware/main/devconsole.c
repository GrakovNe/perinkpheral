/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "devconsole.h"

#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "events.h"

static const char *TAG = "devconsole";

#define SHORT_PRESS_KEYS "1234"
#define LONG_PRESS_KEYS  "qwer"
#define DUMP_FRAME_KEY   'p'
#define MEMORY_KEY       'm'
#define READ_RETRY_MS    100

/* The position of key in keys, or -1. */
static int key_index(const char *keys, char key)
{
    const char *found = key != '\0' ? strchr(keys, key) : NULL;
    return found != NULL ? (int)(found - keys) : -1;
}

static void handle_key(char key)
{
    int button = key_index(SHORT_PRESS_KEYS, key);
    if (button >= 0) {
        events_post_button((uint8_t)button, EVENT_PRESS_SHORT);
        return;
    }
    button = key_index(LONG_PRESS_KEYS, key);
    if (button >= 0) {
        events_post_button((uint8_t)button, EVENT_PRESS_LONG);
        return;
    }
    if (key == DUMP_FRAME_KEY) {
        event_t event = { .kind = EVENT_DUMP_FRAME };
        events_post(&event);
    } else if (key == MEMORY_KEY) {
        /* The host loop is the main task: applications run on its stack. */
        printf("MEMORY free %u least %u host-stack-least %u\n",
               (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
               (unsigned)uxTaskGetStackHighWaterMark(xTaskGetHandle("main")));
    }
}

static void devconsole_task(void *arg)
{
    (void)arg;
    for (;;) {
        char key;
        if (usb_serial_jtag_read_bytes(&key, 1, portMAX_DELAY) == 1) {
            handle_key(key);
        } else {
            /* Nothing on the other end: do not spin on it. */
            vTaskDelay(pdMS_TO_TICKS(READ_RETRY_MS));
        }
    }
}

void devconsole_dump_frame(const uint8_t *frame, int width, int height)
{
    const int bytes_per_row = width / 8;
    printf("FRAME BEGIN %d %d\n", width, height);
    for (int row = 0; row < height; row++) {
        for (int byte = 0; byte < bytes_per_row; byte++) {
            printf("%02x", frame[row * bytes_per_row + byte]);
        }
        printf("\n");
    }
    printf("FRAME END\n");
}

void devconsole_start(void)
{
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    /* A frame dump is 30 KB of text and the host waits while it goes out:
     * a larger buffer makes that wait shorter. */
    config.tx_buffer_size = 4096;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    usb_serial_jtag_vfs_use_driver();

    BaseType_t ok = xTaskCreate(devconsole_task, "devconsole", 3072, NULL, 2, NULL);
    configASSERT(ok == pdPASS);
    ESP_LOGW(TAG, "development console is on: 1-4 short, q/w/e/r long, p frame, m memory");
}
