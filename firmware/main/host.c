/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "host.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "app_scene.h"
#include "app_manifest.h"
#include "devconsole.h"
#include "app_delivery.h"
#include "devtools.h"
#include "display.h"
#include "events.h"
#include "launcher.h"
#include "poller.h"
#include "power.h"
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

/* How many events in a row are handled before the panel is shown what came of them. */
#define EVENTS_BEFORE_SHOWING 24

/* The back buffer: frames are put together here and handed over whole. */
static uint8_t s_frame[DISPLAY_FRAME_SIZE];

static void draw_header(gfx_t *surface, const scene_t *scene, void *context)
{
    (void)context;
    status_t status;
    status_read(&status);
    /* The host's own: nothing on the left, the date on the right. */
    screens_header_t header = {
        .wifi = status.wifi_connected,
        .left = scene->header_left,
        .right = scene->header_right != NULL ? scene->header_right
                 : status.date_known         ? status.date
                                             : NULL,
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
    case WM_CHANGED_URGENTLY:
        return REFRESH_FOR_REQUEST;
    case WM_CHANGED_IN_A_ROW:
        return REFRESH_FOR_ROW;
    case WM_CHANGED_CLEANLY:
        return REFRESH_FOR_CLEAN;
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
    power_note_press(); /* the user is here: the device stays reachable for a while */
    wm_button(wm, BUTTON_ROLE[index], SCENE_PRESS_OF[press]);
}

/* How many times the panel was refreshed since start: lets a tool tell a
 * new picture from the one it has already seen. */
static uint32_t s_frames_shown;

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
        s_frames_shown++;
        break;
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
        /* A firmware without the screen that says how leaves the user at the
         * menu: Device has the access point's address. */
        return launcher_setup() != NULL ? launcher_setup() : menu;
    }
    char last_app[APP_NAME_MAX + 1];
    settings_load_last_app(last_app, sizeof last_app);
    scene_t *last_scene = launcher_app_scene(last_app);
    return last_scene != NULL ? last_scene : menu;
}

/* The About application as the splash: only while it is loaded and has
 * not stopped, or the splash would be its message of a crash. */
static bool about_is_shown(scene_t *about)
{
    return app_scene_app(about) != NULL;
}

/* An application took the screen: that is where to return after a restart. */
static void remember_current_app(const wm_t *wm)
{
    const char *app_name = launcher_app_name(wm->current);
    if (app_name != NULL) {
        settings_save_last_app(app_name);
    }
}

/* An application's scene is about to be freed, the list of them being
 * read anew: nobody may be left holding it. */
typedef struct {
    wm_t                 *wm;
    app_delivery_state_t *delivery;
} reload_context_t;

static void drop_app_scene(scene_t *scene, void *context)
{
    reload_context_t *reload = context;
    wm_drop(reload->wm, scene);
    if (reload->delivery->in_background == scene) {
        /* In the middle of an answer that now has nobody to go to; the scene
         * that gave way to it comes back. */
        app_delivery_init(reload->delivery);
        app_scene_end_background(scene); /* unloaded first: one instance at a time */
        wm_resume(reload->wm);
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
    /* The splash is the About application; a firmware without one starts
     * where the splash would have led. */
    scene_t *after_boot = scene_after_boot(menu);
    scene_t *first = launcher_about() != NULL
                         ? scene_boot_splash(after_boot, timers_set, launcher_about(),
                                             about_is_shown)
                         : after_boot;
    wm_init(&wm, menu, first, draw_header, NULL);

    app_delivery_state_t delivery_state;
    app_delivery_init(&delivery_state);
    poller_start();

    int events_since_shown = 0;
    for (;;) {
        /* A refresh takes a second and the queue does not wait for it: with
         * events in it they are answered first and the panel shows where
         * they led, not every step. Not for ever, though - a queue that
         * never empties must not keep the picture back. */
        uint32_t wait_ms = 0;
        if (!events_are_waiting() || events_since_shown >= EVENTS_BEFORE_SHOWING) {
            wait_ms = show_pending_frame(&wm, &screen);
            events_since_shown = 0;
            events_report_dropped();
        }
        events_since_shown++;

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
        case EVENT_APP_DELIVERY: {
            poller_delivery_t *handed = event.delivery;
            scene_t *owner = launcher_app_scene(handed->app_name);
            if (owner != NULL) {
                app_deliver(&delivery_state, &wm, owner, &handed->delivery,
                            esp_timer_get_time() / 1000);
            }
            xSemaphoreGive(handed->handed_over);
            break;
        }
        case EVENT_APPS_CHANGED: {
            reload_context_t context = { &wm, &delivery_state };
            poller_hold();
            launcher_reload(drop_app_scene, &context);
            poller_release_reloaded();
            menu_entries = launcher_entries(&menu_entry_count);
            scene_menu(menu_entries, menu_entry_count);
            /* Where the splash leads may have been one of them. */
            scene_boot_splash(scene_after_boot(menu), timers_set, launcher_about(),
                              about_is_shown);
            wm_invalidate(&wm, WM_CHANGED_URGENTLY);
            break;
        }
        case EVENT_DEV_REQUEST:
#ifdef CONFIG_APP_DEV_CONSOLE
            devtools_serve(event.dev_request, &wm, s_frames_shown,
                           wm_pending_change(&wm) != WM_CLEAN);
#endif
            break;
        }
        if (wm.current != scene_before) {
            remember_current_app(&wm);
        }
    }
}
