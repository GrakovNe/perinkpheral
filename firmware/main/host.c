/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "host.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "app_manifest.h"
#include "devconsole.h"
#include "display.h"
#include "events.h"
#include "launcher.h"
#include "scenes.h"
#include "screens.h"
#include "settings.h"
#include "status.h"
#include "timers.h"
#include "wifi.h"
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

static void on_button(wm_t *wm, uint8_t index, event_press_t press)
{
    static const scene_press_t SCENE_PRESS_OF[] = {
        [EVENT_PRESS_DOWN] = SCENE_PRESS_DOWN,
        [EVENT_PRESS_UP] = SCENE_PRESS_UP,
        [EVENT_PRESS_SHORT] = SCENE_PRESS_SHORT,
        [EVENT_PRESS_LONG] = SCENE_PRESS_LONG,
    };
    if (press == EVENT_PRESS_SHORT || press == EVENT_PRESS_LONG) {
        ESP_LOGI(TAG, "button %d %s", index + 1, press == EVENT_PRESS_LONG ? "long" : "short");
    }
    wm_button(wm, BUTTON_ROLE[index], SCENE_PRESS_OF[press]);
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

/* Where the device goes after the splash at power-up: to be set up when
 * it has no network, else back to the application it showed last, else to
 * the menu. */
static scene_t *scene_after_boot(scene_t *menu)
{
    wifi_status_t wifi;
    wifi_get_status(&wifi);
    if (wifi.ssid[0] == '\0') {
        return scene_setup();
    }
    char last_app[APP_NAME_MAX + 1];
    settings_load_last_app(last_app, sizeof last_app);
    scene_t *last_scene = launcher_app_scene(last_app);
    return last_scene != NULL ? last_scene : menu;
}

/* An application took the screen: that is where to return after a restart. */
static void remember_current_app(const wm_t *wm)
{
    const char *app_name = launcher_app_name(wm->current);
    if (app_name != NULL) {
        settings_save_last_app(app_name);
    }
}

void host_run(void)
{
    launcher_start();
    int menu_entry_count;
    const menu_entry_t *menu_entries = launcher_entries(&menu_entry_count);
    scene_t *menu = scene_menu(menu_entries, menu_entry_count);

    gfx_t screen;
    gfx_init(&screen, s_frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    wm_t wm;
    wm_init(&wm, menu, scene_boot_splash(scene_after_boot(menu), timers_set), draw_header, NULL);

    for (;;) {
        uint32_t wait_ms = show_pending_frame(&wm, &screen);

        event_t event;
        if (!events_wait(&event, wait_ms)) {
            continue;
        }
        const scene_t *scene_before = wm.current;
        switch (event.kind) {
        case EVENT_BUTTON:
            on_button(&wm, event.button.index, event.button.press);
            break;
        case EVENT_TIMER:
            if (timers_take_due(event.timer.id)) {
                wm_timer(&wm, event.timer.id);
            }
            break;
        case EVENT_STATUS_CHANGED:
            wm_invalidate(&wm, WM_CHANGED_BY_DATA);
            break;
        case EVENT_DUMP_FRAME:
#ifdef CONFIG_APP_DEV_CONSOLE
            devconsole_dump_frame(display_shown_frame(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
#endif
            break;
        }
        if (wm.current != scene_before) {
            remember_current_app(&wm);
        }
    }
}
