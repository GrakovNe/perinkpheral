/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "host.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "app_host.h"
#include "app_scene.h"
#include "app_store.h"
#include "devconsole.h"
#include "display.h"
#include "events.h"
#include "scenes.h"
#include "screens.h"
#include "status.h"
#include "storage.h"
#include "timers.h"
#include "wm.h"

static const char *TAG = "host";

/* A frame the panel failed to take is offered again, but not at once: the
 * panel has just timed out. */
#define RETRY_FAILED_SHOW_MS (30 * 1000)

#define NOTHING_TO_SHOW UINT32_MAX

/* The back buffer: frames are put together here and handed over whole. */
static uint8_t s_frame[DISPLAY_FRAME_SIZE];

static void draw_header(gfx_t *surface, void *context)
{
    (void)context;
    status_t status;
    status_read(&status);
    screens_header_t header = {
        .wifi = status.wifi_connected,
        .date = status.date_known ? status.date : NULL,
    };
    screens_header(surface, &header);
}

static refresh_reason_t refresh_reason(wm_change_t change)
{
    switch (change) {
    case WM_CHANGED_BY_SWITCH:
        return REFRESH_FOR_SWITCH;
    case WM_CHANGED_BY_INPUT:
        return REFRESH_FOR_BUTTON;
    default:
        return REFRESH_FOR_DATA;
    }
}

/* The buttons in the order they sit on the device. */
static const scene_button_t BUTTON_ROLE[] = {
    SCENE_BUTTON_UP, SCENE_BUTTON_OK, SCENE_BUTTON_DOWN, SCENE_BUTTON_BACK,
};

static void on_button(wm_t *wm, uint8_t index, bool long_press)
{
    ESP_LOGI(TAG, "button %d %s", index + 1, long_press ? "long" : "short");
    wm_button(wm, BUTTON_ROLE[index], long_press ? SCENE_PRESS_LONG : SCENE_PRESS_SHORT);
}

/* Puts the frame together and offers it to the display if anything
 * changed. Returns how long to wait before offering again, or
 * NOTHING_TO_SHOW when the panel is up to date. */
static uint32_t show_pending_frame(wm_t *wm, gfx_t *screen)
{
    wm_change_t change = wm_pending_change(wm);
    if (change == WM_CLEAN) {
        return NOTHING_TO_SHOW;
    }
    refresh_reason_t reason = refresh_reason(change);
    wm_draw(wm, screen);
    switch (display_show(s_frame, reason)) {
    case DISPLAY_DEFERRED:
        return display_wait_ms(reason);
    case DISPLAY_FAILED:
        ESP_LOGW(TAG, "the panel did not take the frame");
        return RETRY_FAILED_SHOW_MS;
    case DISPLAY_SHOWN:
    case DISPLAY_UNCHANGED:
        break;
    }
    wm_shown(wm);
    return NOTHING_TO_SHOW;
}

#define INSTALLED_APPS_MAX 24
#define BUILT_IN_PAGES     2

/* What the menu opens: the installed applications, then the host's own
 * pages. Returns how many entries it filled. */
static int list_menu_entries(menu_entry_t *entries)
{
    /* Titles are shown for as long as the menu lives. */
    static app_manifest_t installed[INSTALLED_APPS_MAX];
    int installed_count = app_store_list(STORAGE_APPS_ROOT, installed, INSTALLED_APPS_MAX);
    ESP_LOGI(TAG, "%d applications installed", installed_count);

    int count = 0;
    for (int i = 0; i < installed_count; i++) {
        scene_t *scene = app_scene_create(&installed[i], STORAGE_APPS_ROOT, app_host_services());
        if (scene != NULL) {
            entries[count++] = (menu_entry_t){ installed[i].title, installed[i].version, scene };
        }
    }
    entries[count++] = (menu_entry_t){ "Device", "name, firmware", scene_device() };
    entries[count++] = (menu_entry_t){ "About", "logo, license", scene_splash() };
    return count;
}

void host_run(void)
{
    static menu_entry_t menu_entries[INSTALLED_APPS_MAX + BUILT_IN_PAGES];
    int menu_entry_count = list_menu_entries(menu_entries);

    gfx_t screen;
    gfx_init(&screen, s_frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    wm_t wm;
    wm_init(&wm, scene_menu(menu_entries, menu_entry_count), scene_splash(), draw_header, NULL);

    for (;;) {
        uint32_t wait_ms = show_pending_frame(&wm, &screen);

        event_t event;
        if (!events_wait(&event, wait_ms)) {
            continue;
        }
        switch (event.kind) {
        case EVENT_BUTTON:
            on_button(&wm, event.button.index, event.button.long_press);
            break;
        case EVENT_TIMER:
            if (timers_is_current(event.timer.id, event.timer.generation)) {
                wm_timer(&wm, event.timer.id);
            }
            break;
        case EVENT_DUMP_FRAME:
#ifdef CONFIG_APP_DEV_CONSOLE
            devconsole_dump_frame(display_shown_frame(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
#endif
            break;
        }
    }
}
