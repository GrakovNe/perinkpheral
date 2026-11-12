/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <string.h>

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
#include "reset.h"
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
    /* Each side is the scene's if it gave one, and the host's own otherwise:
     * nothing on the left, the date on the right. */
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

/* The host loop's own, and nobody else's: what is on the screen and whose
 * answer is being taken. Other tasks reach them through events_call(). */
static wm_t                 wm;
static app_delivery_state_t delivery_state;
static scene_t             *s_menu;

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

/* ---- stopping ----
 *
 * The device does not just restart: whatever is loaded is told and
 * unloaded, nothing more is asked of the network, and then it goes. Every
 * step is done when its call returns; nothing is waited for on the off
 * chance. */

#define STOP_DELAY_US (1000 * 1000) /* for the answer to whoever asked to leave the device */
#define STOP_RETRY_US (200 * 1000)

static esp_timer_handle_t s_stop_timer;
static host_stop_t        s_stop_reason;

static void stop(host_stop_t why)
{
    ESP_LOGW(TAG, "stopping: %s", why == HOST_STOP_FACTORY_RESET ? "factory reset" : "restart");
    /* The application on the screen: app_on_blur(), app_on_terminate(). */
    wm_suspend(&wm);
    /* And one that was taking an answer off the screen. */
    if (delivery_state.in_background != NULL) {
        app_scene_end_background(delivery_state.in_background);
    }
    poller_hold(); /* and never let go: nothing more is asked */
    if (why == HOST_STOP_FACTORY_RESET) {
        reset_mark_factory();
    }
    esp_restart();
}

static void on_stop_timer(void *arg)
{
    (void)arg;
    event_t event = { .kind = EVENT_STOP, .stop = s_stop_reason };
    if (!events_post(&event)) {
        /* The queue is full now; it will not be for long. */
        esp_timer_start_once(s_stop_timer, STOP_RETRY_US);
    }
}

void host_stop_soon(host_stop_t why)
{
    esp_timer_create_args_t args = { .callback = on_stop_timer, .name = "stop" };
    if (s_stop_timer == NULL && esp_timer_create(&args, &s_stop_timer) != ESP_OK) {
        esp_restart(); /* no way to do it properly: the restart is not lost */
    }
    /* A reset asked for is not turned into a restart by a later request. */
    if (s_stop_reason != HOST_STOP_FACTORY_RESET) {
        s_stop_reason = why;
    }
    esp_timer_stop(s_stop_timer); /* not running is fine */
    esp_timer_start_once(s_stop_timer, STOP_DELAY_US);
}

void host_reload_apps(void)
{
    reload_context_t context = { &wm, &delivery_state };
    poller_hold();
    launcher_reload(drop_app_scene, &context);
    poller_release_reloaded();
    int count;
    const menu_entry_t *entries = launcher_entries(&count);
    scene_menu(entries, count);
    /* Where the splash leads may have been one of them. */
    scene_boot_splash(scene_after_boot(s_menu), timers_set, launcher_about(), about_is_shown);
    wm_invalidate(&wm, WM_CHANGED_URGENTLY);
}

static bool s_reload_is_due;

void host_reload_apps_later(void)
{
    s_reload_is_due = true;
}

void host_deliver(const char *app_name, const app_delivery_t *delivery)
{
    scene_t *owner = launcher_app_scene(app_name);
    if (owner != NULL) {
        app_deliver(&delivery_state, &wm, owner, delivery, esp_timer_get_time() / 1000);
    }
}

/* Tells every application that asked for this kind of news, loading each
 * for the call. Those that did not ask are left asleep. */
static void broadcast(int intent)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    /* Names first: a delivery does not change the list, but the list is
     * not this function's to hold on to across calls into applications. */
    char names[LAUNCHER_INSTALLED_MAX][APP_NAME_MAX + 1];
    int  asked = 0;
    for (int i = 0; i < count; i++) {
        if ((installed[i].intents & (1u << intent)) != 0) {
            strcpy(names[asked++], installed[i].name);
        }
    }
    app_delivery_t news = { .kind = APP_DELIVERY_INTENT, .request = intent };
    for (int i = 0; i < asked; i++) {
        host_deliver(names[i], &news);
    }
}

void host_run(void)
{
    launcher_start();
    int menu_entry_count;
    const menu_entry_t *menu_entries = launcher_entries(&menu_entry_count);
    scene_t *menu = scene_menu(menu_entries, menu_entry_count);
    s_menu = menu;

    gfx_t screen;
    gfx_init(&screen, s_frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    /* The splash is the About application; a firmware without one starts
     * where the splash would have led. */
    scene_t *after_boot = scene_after_boot(menu);
    scene_t *first = launcher_about() != NULL
                         ? scene_boot_splash(after_boot, timers_set, launcher_about(),
                                             about_is_shown)
                         : after_boot;
    wm_init(&wm, menu, first, draw_header, NULL);

    app_delivery_init(&delivery_state);
    events_this_is_the_host_loop();
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
        case EVENT_APPS_CHANGED:
            host_reload_apps();
            break;
        case EVENT_CALL:
            events_answer_call(event.call);
            break;
        case EVENT_INTENT:
            broadcast(event.intent);
            break;
        case EVENT_STOP:
            stop((host_stop_t)event.stop);
            break;
        case EVENT_DEV_REQUEST:
#ifdef CONFIG_APP_DEV_CONSOLE
            devtools_serve(event.dev_request, &wm, s_frames_shown,
                           wm_pending_change(&wm) != WM_CLEAN);
#endif
            break;
        }
        if (s_reload_is_due) {
            /* Asked for from inside a handler, which has returned by now. */
            s_reload_is_due = false;
            host_reload_apps();
        }
        if (wm.current != scene_before) {
            remember_current_app(&wm);
        }
    }
}
