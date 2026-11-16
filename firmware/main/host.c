/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <string.h>

#include "priorities.h"
#include "host.h"
#include "host_private.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "app_scene.h"
#include "app_manifest.h"
#include "app_delivery.h"
#include "host_api.h"
#include "deadlines.h"
#include "app_host.h"
#include "gfx_icons.h"
#include "display.h"
#include "events.h"
#include "launcher.h"
#include "memory.h"
#include "poller.h"
#include "reset.h"
#include "power.h"
#include "scenes.h"
#include "screens.h"
#include "settings.h"
#include "app_code.h"
#include "storage.h"
#include "status.h"
#include "timers.h"
#include "wifi.h"
#include "wm.h"
#include "units.h"

static const char *TAG = "host";

/* A frame the panel failed to take is offered again, but not at once: the
 * panel has just timed out. */
#define RETRY_FAILED_SHOW_MS (30 * 1000)

#define NOTHING_TO_SHOW UINT32_MAX

/* How many events in a row are handled before the panel is shown what came of them. */
#define EVENTS_BEFORE_SHOWING 24

/* The back buffer: frames are put together here and handed over whole. */
static uint8_t s_frame[DISPLAY_FRAME_SIZE];

/* Each side is, in this order: what an application keeps there, what the
 * scene on the screen gave, the host's own - the Wi-Fi icon on the left,
 * the date on the right. */
static void draw_header(gfx_t *surface, const scene_t *scene, void *context)
{
    (void)context;
    status_t status;
    status_read(&status);
    screens_header_t header = {
        .left_icon = status.wifi_connected ? GFX_ICON_SIGNAL : GFX_ICON_NO_SIGNAL,
        .left = scene->header_left,
        .right = scene->header_right != NULL ? scene->header_right
                 : status.date_known         ? status.date
                                             : NULL,
        .right_icon = SCREENS_NO_ICON,
    };
    host_header_status(HOST_HEADER_LEFT, &header.left_icon, &header.left);
    host_header_status(HOST_HEADER_RIGHT, &header.right_icon, &header.right);
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

/* The host loop's own, and nobody else's: what is on the screen and whose
 * answer is being taken. Other tasks reach them through events_call(). */
wm_t                 host_wm;
app_delivery_state_t host_delivery;

/* An application taking an answer off the screen, piece by piece, gives
 * way to what the screen is needed for now. The rest of that answer is
 * dropped; when it was a poll's, the poll is asked again in a while. */
void host_end_background_answer(void)
{
    app_abandoned_t abandoned;
    app_delivery_abort(&host_delivery, &host_wm, esp_timer_get_time() / US_PER_MS, &abandoned);
    if (abandoned.owner != NULL) {
        const char *app_name = launcher_app_name(abandoned.owner);
        if (app_name != NULL) {
            poller_answer_was_abandoned(app_name, abandoned.request);
        }
    }
}

/* Whatever is to enter the screen - a question, a notice, the default
 * application, a scene the development tools open - needs the room the
 * application taking an answer off the screen is in: it goes first. One
 * place for every way the screen changes, so that none can forget it. */
static void before_switch(void *context)
{
    (void)context;
    host_end_background_answer();
}

/* The scene whose picture the panel shows: what a person pressing a
 * button is looking at. Not the scene that has the screen while its first
 * frame is still being put on the panel. */
static const scene_t *volatile s_scene_on_panel;

const scene_t *host_scene_on_panel(void)
{
    return s_scene_on_panel;
}
/* The buttons that went down while a scene that asks once was on the
 * panel: only their presses are that scene's to take. */
static uint8_t s_went_down_on_question;
/* When the scene on the panel got there: the end of the refresh that put
 * its first picture on it. A press made before this was made for another
 * picture, though it is taken off the queue only now. */
static int64_t s_scene_shown_at_ms;

/* Whether the press is one the scene on the screen may have: every press,
 * unless one press there does what cannot be taken back - then only a
 * press that began with the question in front of whoever made it. The
 * panel takes seconds to show a question, and a press made for the
 * picture before it must not answer it. */
static bool is_press_for(const scene_t *scene, uint8_t index, event_press_t press, int64_t at_ms)
{
    if (!scene->asks_once) {
        s_went_down_on_question = 0;
        return true;
    }
    uint8_t button = (uint8_t)(1u << index);
    if (press == EVENT_PRESS_DOWN) {
        if (s_scene_on_panel == scene && at_ms >= s_scene_shown_at_ms) {
            s_went_down_on_question |= button;
        } else {
            s_went_down_on_question &= (uint8_t)~button;
        }
        return false; /* a question has no use for the way down itself */
    }
    bool began_here = (s_went_down_on_question & button) != 0;
    if (press == EVENT_PRESS_SHORT || press == EVENT_PRESS_LONG) {
        s_went_down_on_question &= (uint8_t)~button;
    }
    return began_here;
}

#define BUTTON_BACK_INDEX 3

/* The buttons in the order they sit on the device. */
static const scene_button_t BUTTON_ROLE[] = {
    SCENE_BUTTON_UP, SCENE_BUTTON_OK, SCENE_BUTTON_DOWN, SCENE_BUTTON_BACK,
};

static void on_button(wm_t *wm, uint8_t index, event_press_t press, int64_t at_ms)
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
    /* A press is for what is on the screen, and that is not loaded while
     * another application takes an answer piece by piece: the answer gives
     * way, the press is not lost. */
    host_end_background_answer();
    /* The long press on BACK is the user's own way out of anything. */
    bool is_home_press = index == BUTTON_BACK_INDEX && press == EVENT_PRESS_LONG;
    if (!is_home_press && !is_press_for(wm->current, index, press, at_ms)) {
        return;
    }
    wm_button(wm, BUTTON_ROLE[index], SCENE_PRESS_OF[press]);
}

/* How many times the panel was refreshed since start: lets a tool tell a
 * new picture from the one it has already seen. */
static uint32_t s_frames_shown;

uint32_t host_frames_shown(void)
{
    return s_frames_shown;
}

bool host_frame_is_pending(void)
{
    return wm_pending_change(&host_wm) != WM_CLEAN;
}

void host_open_from_menu(scene_t *scene)
{
    wm_open_from_home(&host_wm, scene);
}

/* A wait shorter than this is the panel's own pace, not news. */
#define TOLD_WAIT_FROM_MS 2000
static bool s_wait_logged;


/* Puts the frame together and offers it to the display if anything
 * changed. Returns how long to wait before offering again, or
 * NOTHING_TO_SHOW when the panel is up to date. */
static uint32_t show_pending_frame(wm_t *wm, gfx_t *screen)
{
    wm_change_t change = wm_pending_change(wm);
    /* While an application takes an answer off the screen the one on it is
     * not loaded and has nothing to draw: its picture stays, and what
     * changed is shown when it is back. */
    if (change == WM_CLEAN || wm->suspended) {
        return NOTHING_TO_SHOW;
    }
    refresh_reason_t reason = refresh_reason(change);
    /* Too soon for the panel: nothing is drawn for it to turn down, an
     * application's picture least of all. */
    uint32_t too_soon_ms = display_wait_ms(reason);
    if (too_soon_ms > 0) {
        /* Said once a frame: whoever writes an application takes a
         * picture that waits its minute for one that never came. */
        if (!s_wait_logged && too_soon_ms > TOLD_WAIT_FROM_MS) {
            s_wait_logged = true;
            ESP_LOGI(TAG, "a redraw waits %u s: the refresh policy", (unsigned)(too_soon_ms / 1000));
        }
        return too_soon_ms;
    }
    s_wait_logged = false;
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
    if (s_scene_on_panel != wm->current) {
        s_scene_shown_at_ms = esp_timer_get_time() / US_PER_MS;
    }
    s_scene_on_panel = wm->current;
    wm_shown(wm);
    return NOTHING_TO_SHOW;
}

/* The file system is empty when the device is back. */
static void format_storage(void)
{
    storage_format();
    app_code_erase_all(); /* the code of applications that are no more */
    host_stop_soon(HOST_STOP_RESTART);
}

/* The application the user chose to see when nothing else is asked for;
 * NULL when none is chosen or the chosen one is not installed. */
static scene_t *default_scene(void)
{
    appconf_t settings;
    settings_read(&settings);
    return settings.default_app[0] != '\0' ? launcher_app_scene(settings.default_app) : NULL;
}

/* Where the device goes after the splash at power-up: to be set up when
 * it has no network, else to the default application, else back to the
 * application it showed last, else to the menu. */
static scene_t *scene_with_storage(scene_t *menu)
{
    wifi_status_t wifi;
    wifi_get_status(&wifi);
    if (wifi.ssid[0] == '\0') {
        /* A firmware without the screen that says how leaves the user at the
         * menu: Device has the access point's address. */
        return launcher_setup() != NULL ? launcher_setup() : menu;
    }
    scene_t *chosen = default_scene();
    if (chosen != NULL) {
        return chosen;
    }
    char last_app[APP_NAME_MAX + 1];
    settings_load_last_app(last_app, sizeof last_app);
    scene_t *last_scene = launcher_app_scene(last_app);
    return last_scene != NULL ? last_scene : menu;
}

/* Before any of that, a partition of applications that cannot be read is
 * the user's to decide about. */
static scene_t *scene_after_boot(scene_t *menu)
{
    scene_t *next = scene_with_storage(menu);
    return storage_is_mounted() ? next : scene_storage_error(next, format_storage);
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

static scene_t *s_menu;

void host_renew_menu(void)
{
    int count;
    menu_entry_t *entries = launcher_entries(&count);
    scene_menu(entries, count);
    /* Where the splash leads may have been one of them. */
    scene_boot_splash(scene_after_boot(s_menu), timers_set, launcher_about(), about_is_shown);
}

/* ---- stopping ----
 *
 * The device does not just restart: whatever is loaded is told and
 * unloaded, nothing more is asked of the network, and then it goes. Every
 * step is done when its call returns; nothing is waited for on the off
 * chance. */

#define STOP_DELAY_MS 1000 /* for the answer to whoever asked to leave the device */

static host_stop_t s_stop_reason;

static void stop(host_stop_t why)
{
    static const char *const WHY[] = {
        [HOST_STOP_RESTART] = "restart",
        [HOST_STOP_FACTORY_RESET] = "factory reset",
    };
    ESP_LOGW(TAG, "stopping: %s", WHY[why]);
    /* The application on the screen: app_on_blur(), app_on_terminate(). */
    wm_suspend(&host_wm);
    /* And one that was taking an answer off the screen. */
    if (host_delivery.in_background != NULL) {
        app_scene_end_background(host_delivery.in_background);
    }
    poller_hold(); /* and never let go: nothing more is asked */
    display_rest();
    if (why == HOST_STOP_FACTORY_RESET) {
        reset_mark_factory();
    }
    esp_restart();
}

void host_redraw(bool is_clean)
{
    wm_invalidate(&host_wm, is_clean ? WM_CHANGED_CLEANLY : WM_CHANGED_URGENTLY);
}

void host_stop_soon(host_stop_t why)
{
    /* A reset asked for is not turned into a restart by a later request. */
    if (s_stop_reason != HOST_STOP_FACTORY_RESET) {
        s_stop_reason = why;
    }
    if (!deadlines_set_in(DEADLINE_STOP, 0, 0, STOP_DELAY_MS)) {
        esp_restart(); /* no way to do it properly: the restart is not lost */
    }
}

/* ---- deadlines ---- */

/* Whether the host loop was told that some deadlines have come and has not
 * looked yet: one event says it for all of them. */
static volatile bool s_deadline_event_pending;

/* From the timer's task. */
static bool tell_of_deadlines(void)
{
    if (s_deadline_event_pending) {
        return true;
    }
    /* Said before it is posted: the host loop may answer at once. */
    s_deadline_event_pending = true;
    event_t event = { .kind = EVENT_DEADLINES };
    if (!events_post(&event)) {
        s_deadline_event_pending = false;
    }
    return s_deadline_event_pending;
}

void host_deadlines_start(void)
{
    host_notices_start();
    deadlines_start(tell_of_deadlines);
}

/* So many that have come are answered in a row; with more of them the
 * queue gets its turn first - a press is not kept waiting by a timer that
 * is set anew every time it fires. */
#define DEADLINES_IN_A_ROW 8

static void on_menu_idle_timeout(void);

static void answer_deadlines(void)
{
    s_deadline_event_pending = false;
    deadline_id_t due;
    for (int answered = 0; answered < DEADLINES_IN_A_ROW; answered++) {
        if (!deadlines_take_due(&due)) {
            return;
        }
        switch ((deadline_kind_t)due.kind) {
        case DEADLINE_SCENE_TIMER:
            wm_timer(&host_wm, due.number);
            break;
        case DEADLINE_ALARM:
            host_ring_alarm(due.owner, due.number);
            break;
        case DEADLINE_AWAKE_WINDOW:
            power_awake_period_ended();
            break;
        case DEADLINE_MEMORY_LOOK:
            memory_look();
            break;
        case DEADLINE_POWER_LOOK:
            if (power_look()) {
                poller_reschedule(); /* the polls are another distance apart */
            }
            break;
        case DEADLINE_WIFI_RETRY:
            wifi_retry_is_due();
            break;
        case DEADLINE_DATE:
            status_check();
            break;
        case DEADLINE_PANEL_SLEEP:
            display_rest();
            break;
        case DEADLINE_PANEL_KEEPALIVE:
            display_keepalive();
            break;
        case DEADLINE_POLL:
            poller_poll_is_due(due.owner, due.number);
            break;
        case DEADLINE_POLLER_LOOK:
            poller_look();
            break;
        case DEADLINE_CONFIRM:
            host_confirm_timed_out();
            break;
        case DEADLINE_MENU_IDLE:
            on_menu_idle_timeout();
            break;
        case DEADLINE_STOP:
            stop(s_stop_reason);
            break;
        }
    }
    deadlines_reschedule();
}

/* The menu left alone gives the screen to the default application. Only
 * the menu: an application that took the screen - by an alarm, say - has
 * something to tell and stays until the user closes it. */
static bool s_menu_is_watched;

static void update_menu_idle_timer(bool was_touched)
{
    scene_t *chosen = default_scene();
    scene_menu_set_back_target(chosen);
    if (host_wm.current != s_menu || chosen == NULL) {
        if (s_menu_is_watched) {
            deadlines_cancel(DEADLINE_MENU_IDLE, 0, 0);
            s_menu_is_watched = false;
        }
        return;
    }
    if (!s_menu_is_watched || was_touched) {
        appconf_t settings;
        settings_read(&settings);
        s_menu_is_watched =
            deadlines_set_in(DEADLINE_MENU_IDLE, 0, 0, (int64_t)settings.menu_idle_min * MS_PER_MINUTE);
    }
}

static void on_menu_idle_timeout(void)
{
    s_menu_is_watched = false;
    scene_t *chosen = default_scene();
    if (host_wm.current == s_menu && chosen != NULL) {
        wm_open_from_home(&host_wm, chosen);
    }
}

/* The host loop draws the panel and answers the buttons, so it runs above
 * the tasks that only compute and below the ones that keep the radio
 * alive (priorities.h). As the main task it had priority 1, the lowest of
 * all: a refresh that met a TLS handshake took 2.7 s instead of 0.46. */
void host_run(void)
{
    vTaskPrioritySet(NULL, PRIORITY_HOST_LOOP);
    launcher_start();
    int menu_entry_count;
    menu_entry_t *menu_entries = launcher_entries(&menu_entry_count);
    scene_t *menu = scene_menu(menu_entries, menu_entry_count);
    scene_menu_set_on_order(launcher_keep_order);
    s_menu = menu;

    gfx_t screen;
    gfx_init(&screen, s_frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    /* The splash is the About application; a firmware without one starts
     * where the splash would have led. */
    scene_t *after_boot = scene_after_boot(menu);
    bool     shows_splash = launcher_about() != NULL;
    scene_t *first = shows_splash ? scene_boot_splash(after_boot, timers_set, launcher_about(),
                                                       about_is_shown)
                                  : after_boot;
    wm_init(&host_wm, menu, first, draw_header, NULL);
    /* A restart forgot what the applications had set going; those that
     * asked are told, and set it going again. */
    app_host_tell_started();

    app_delivery_init(&host_delivery);
    wm_before_switch(&host_wm, before_switch, NULL);
    events_set_host_loop_task();
    poller_start();

    int events_since_shown = 0;
    for (;;) {
        /* A refresh takes half a second and the queue does not wait for it: with
         * events in it they are answered first and the panel shows where
         * they led, not every step. Not for ever, though - a queue that
         * never empties must not keep the picture back. */
        uint32_t wait_ms = 0;
        if (!events_are_waiting() || events_since_shown >= EVENTS_BEFORE_SHOWING) {
            wait_ms = show_pending_frame(&host_wm, &screen);
            events_since_shown = 0;
            events_report_dropped();
        }
        events_since_shown++;

        event_t event;
        if (!events_wait(&event, wait_ms)) {
            continue;
        }
        const scene_t *scene_before = host_wm.current;
        switch (event.kind) {
        case EVENT_BUTTON:
            on_button(&host_wm, event.button.index, event.button.press, event.button.at_ms);
            break;
        case EVENT_DEADLINES:
            answer_deadlines();
            break;
        case EVENT_STATUS_CHANGED:
            wm_invalidate(&host_wm, WM_CHANGED_BY_DATA);
            break;
        case EVENT_APPS_CHANGED:
            host_reload_apps();
            break;
        case EVENT_CALL:
            events_answer_call(event.call);
            break;
        case EVENT_INTENT:
            host_tell_news(event.intent);
            break;
        }
        host_notices_after_event();
        host_apps_after_event();
        if (host_wm.current != scene_before) {
            remember_current_app(&host_wm);
        }
        update_menu_idle_timer(event.kind == EVENT_BUTTON || host_wm.current != scene_before);
    }
}
