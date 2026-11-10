/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "demo.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "buttons.h"
#include "display.h"
#include "screens.h"

static const char *TAG = "demo";

typedef enum {
    PAGE_SPLASH, /* at boot: any press opens the menu */
    PAGE_MENU,
    PAGE_DEVICE,
    PAGE_ABOUT,
} page_t;

static const screens_menu_item_t MENU[] = {
    { "Device", "name, firmware" },
    { "About", "logo, license" },
};
static const page_t MENU_PAGE[] = { PAGE_DEVICE, PAGE_ABOUT };
#define MENU_COUNT ((int)(sizeof MENU / sizeof MENU[0]))

/* No network and no clock yet: Wi-Fi is crossed out, the date is blank.
 * The splash is a full-screen page and has no header. */
static const screens_header_t HEADER = { false, NULL };

static const char *s_device_name;
static int s_selected;

static void draw_device(gfx_t *g)
{
    char heap[16], uptime[24];
    snprintf(heap, sizeof heap, "%" PRIu32 " KB", esp_get_free_heap_size() / 1024);
    uint32_t min = (uint32_t)(esp_timer_get_time() / 60000000);
    if (min >= 60) {
        snprintf(uptime, sizeof uptime, "%" PRIu32 " h %" PRIu32 " min", min / 60, min % 60);
    } else {
        snprintf(uptime, sizeof uptime, "%" PRIu32 " min", min);
    }
    const esp_partition_t *run = esp_ota_get_running_partition();
    const screens_field_t fields[] = {
        { "Name", s_device_name },
        { "Firmware", esp_app_get_description()->version },
        { "ESP-IDF", esp_get_idf_version() },
        { "Slot", run != NULL ? run->label : "?" },
        { "Free memory", heap },
        { "Uptime", uptime },
    };
    screens_fields(g, fields, (int)(sizeof fields / sizeof fields[0]), &HEADER);
}

static void draw(gfx_t *g, page_t page)
{
    switch (page) {
    case PAGE_SPLASH:
    case PAGE_ABOUT:
        screens_splash(g);
        break;
    case PAGE_MENU:
        screens_menu(g, MENU, MENU_COUNT, s_selected, &HEADER);
        break;
    case PAGE_DEVICE:
        draw_device(g);
        break;
    }
}

void demo_run(const char *device_name)
{
    gfx_t *g = display_gfx();
    s_device_name = device_name;

    page_t page = PAGE_SPLASH;
    draw(g, page);
    display_present(REFRESH_FOR_SWITCH);
    ESP_LOGI(TAG, "splash shown; any press -> menu, long press 4 -> menu from anywhere");

    /* Presses faster than the panel may be redrawn are taken in and
     * shown by the next refresh the policy allows. */
    bool dirty = false;
    for (;;) {
        buttons_event_t ev;
        if (buttons_wait(&ev, dirty ? 500 : UINT32_MAX)) {
            ESP_LOGI(TAG, "button %d %s", ev.index + 1, ev.event == BTN_LONG ? "long" : "short");
            page_t next = page;
            if (ev.event == BTN_LONG && ev.index == 3) {
                next = PAGE_MENU;
            } else if (ev.event != BTN_SHORT) {
                continue;
            } else if (page == PAGE_SPLASH) {
                next = PAGE_MENU;
            } else if (page == PAGE_MENU) {
                if (ev.index == 0) {
                    s_selected = (s_selected + MENU_COUNT - 1) % MENU_COUNT;
                    dirty = true;
                } else if (ev.index == 2) {
                    s_selected = (s_selected + 1) % MENU_COUNT;
                    dirty = true;
                } else if (ev.index == 1) {
                    next = MENU_PAGE[s_selected];
                }
            }
            if (next != page) {
                /* The built-in pages are one program: moving between
                 * them is a press like any other, not a scene change. */
                page = next;
                dirty = true;
            }
        }
        if (dirty) {
            draw(g, page);
            if (display_present(REFRESH_FOR_BUTTON)) {
                dirty = false;
            }
        }
    }
}
