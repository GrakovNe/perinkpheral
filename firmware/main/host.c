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
#include "deadlines.h"
#include "deep_sleep.h"
#include "devtools.h"
#include "display.h"
#include "events.h"
#include "launcher.h"
#include "ota.h"
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

static app_delivery_state_t delivery_state;

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
    /* A press is for what is on the screen, and that is not loaded while
     * another application takes an answer piece by piece: the answer gives
     * way, the press is not lost. */
    app_delivery_give_way(&delivery_state, wm, esp_timer_get_time() / 1000);
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
        return too_soon_ms;
    }
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

/* The file system is empty when the device is back. */
static void format_storage(void)
{
    storage_format();
    app_code_forget_all(); /* the code of applications that are no more */
    host_stop_soon(HOST_STOP_RESTART);
}

/* Where the device goes after the splash at power-up: to be set up when
 * it has no network, else back to the application it showed last, else to
 * the menu. */
static scene_t *scene_with_storage(scene_t *menu)
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

/* The host loop's own, and nobody else's: what is on the screen and whose
 * answer is being taken. Other tasks reach them through events_call(). */
static wm_t                 wm;
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

#define STOP_DELAY_MS 1000 /* for the answer to whoever asked to leave the device */

static host_stop_t s_stop_reason;

static void stop(host_stop_t why)
{
    static const char *const WHY[] = {
        [HOST_STOP_RESTART] = "restart",
        [HOST_STOP_FACTORY_RESET] = "factory reset",
        [HOST_STOP_DEEP_SLEEP] = "deep sleep",
    };
    ESP_LOGW(TAG, "stopping: %s", WHY[why]);
    /* The application on the screen: app_on_blur(), app_on_terminate(). */
    wm_suspend(&wm);
    /* And one that was taking an answer off the screen. */
    if (delivery_state.in_background != NULL) {
        app_scene_end_background(delivery_state.in_background);
    }
    poller_hold(); /* and never let go: nothing more is asked */
    display_rest();
    if (why == HOST_STOP_DEEP_SLEEP) {
        deep_sleep_until(deadlines_next_worth_waking_for());
    }
    if (why == HOST_STOP_FACTORY_RESET) {
        reset_mark_factory();
    }
    esp_restart();
}

static bool is_question_open(void);

/* Not worth going down for less: the start after it takes a few seconds. */
#define DEEP_SLEEP_LEAST_MS (30 * 1000)

/* A device that sleeps deeply between its polls does so the moment nothing
 * is left to do: every one of these is known to be done, none is waited
 * for on the off chance. */
static bool may_sleep_deeply(void)
{
    if (!power_may_sleep_deeply() || !poller_is_idle() || ota_is_on_probation()) {
        return false;
    }
    if (is_question_open() || delivery_state.in_background != NULL || wm.suspended ||
        wm_pending_change(&wm) != WM_CLEAN) {
        return false;
    }
    /* The panel is still awake after its last refresh. */
    if (deadlines_has(DEADLINE_PANEL_SLEEP, 0, 0) || deadlines_has(DEADLINE_STOP, 0, 0)) {
        return false;
    }
    int64_t next_ms = deadlines_next_worth_waking_for();
    return next_ms == DEADLINE_NONE || next_ms - deadlines_now_ms() >= DEEP_SLEEP_LEAST_MS;
}

void host_stop_soon(host_stop_t why)
{
    /* A reset asked for is not turned into a restart by a later request. */
    if (s_stop_reason != HOST_STOP_FACTORY_RESET) {
        s_stop_reason = why;
    }
    deadlines_set_in(DEADLINE_STOP, 0, 0, STOP_DELAY_MS);
}

/* ---- a question for whoever stands at the device ---- */

#define CONFIRM_WAIT_MS   60000
#define CONFIRM_TEXT_MAX  47

/* One question at a time. The task that asks waits here for the answer;
 * the question itself lives in the host loop. */
static struct {
    SemaphoreHandle_t turn;     /* held by the task whose question is open */
    SemaphoreHandle_t answered;
    bool              is_open;  /* the host loop's */
    bool              is_confirmed;
    scene_t          *scene;
    char              question[CONFIRM_TEXT_MAX + 1];
    char              action[CONFIRM_TEXT_MAX + 1];
} s_confirm;

/* An answer is being handed to an application that is not on the screen:
 * the scene on it is set aside for the moment and comes back. */
static bool s_is_delivering;

static bool is_question_open(void)
{
    return s_confirm.is_open;
}

/* In the host loop: from the scene. The first call is the answer. */
static void on_confirm_answer(bool is_confirmed)
{
    /* Set aside for a delivery is not an answer: the question is back in a
     * moment. */
    if (!s_confirm.is_open || (s_is_delivering && !is_confirmed)) {
        return;
    }
    s_confirm.is_open = false;
    s_confirm.is_confirmed = is_confirmed;
    deadlines_cancel(DEADLINE_CONFIRM, 0, 0);
    ESP_LOGW(TAG, "\"%s\" %s", s_confirm.question, is_confirmed ? "confirmed" : "not confirmed");
    xSemaphoreGive(s_confirm.answered);
}

static void open_confirm(void *argument)
{
    (void)argument;
    /* An application taking an answer off the screen gives way, as to a press. */
    app_delivery_give_way(&delivery_state, &wm, esp_timer_get_time() / 1000);
    s_confirm.is_open = true;
    s_confirm.scene = scene_confirm(s_confirm.question, s_confirm.action, on_confirm_answer);
    wm_show(&wm, s_confirm.scene);
    deadlines_set_in(DEADLINE_CONFIRM, 0, 0, CONFIRM_WAIT_MS);
}

/* Nobody came: the question leaves the screen, which answers it. */
static void close_unanswered_confirm(void)
{
    if (s_confirm.is_open) {
        scene_reaction_t back = { SCENE_BACK, NULL };
        wm_scene_reacted(&wm, s_confirm.scene, back, WM_CHANGED_BY_SWITCH);
        on_confirm_answer(false); /* if the scene was not on the screen to be told */
    }
}

host_answer_t host_ask_at_the_device(const char *question, const char *action)
{
    if (s_confirm.turn == NULL || xSemaphoreTake(s_confirm.turn, 0) != pdTRUE) {
        return HOST_ANSWER_BUSY;
    }
    strlcpy(s_confirm.question, question, sizeof s_confirm.question);
    strlcpy(s_confirm.action, action, sizeof s_confirm.action);
    host_answer_t answer = HOST_ANSWER_BUSY;
    if (events_call(open_confirm, NULL)) {
        /* The host loop answers for certain: by a press, or by the deadline. */
        xSemaphoreTake(s_confirm.answered, portMAX_DELAY);
        answer = s_confirm.is_confirmed ? HOST_ANSWER_CONFIRMED : HOST_ANSWER_NOT_CONFIRMED;
    }
    xSemaphoreGive(s_confirm.turn);
    return answer;
}

/* ---- deadlines ---- */

/* Whether the host loop was told that some deadlines have come and has not
 * looked yet: one event says it for all of them. */
static volatile bool s_deadlines_are_told;

/* From the timer's task. */
static bool tell_of_deadlines(void)
{
    if (s_deadlines_are_told) {
        return true;
    }
    /* Said before it is posted: the host loop may answer at once. */
    s_deadlines_are_told = true;
    event_t event = { .kind = EVENT_DEADLINES };
    if (!events_post(&event)) {
        s_deadlines_are_told = false;
    }
    return s_deadlines_are_told;
}

void host_deadlines_start(void)
{
    s_confirm.turn = xSemaphoreCreateMutex();
    s_confirm.answered = xSemaphoreCreateBinary();
    configASSERT(s_confirm.turn != NULL && s_confirm.answered != NULL);
    deadlines_start(tell_of_deadlines);
}

/* An alarm that has come is handed to its application like an answer. */
static void ring_alarm(uint32_t app_id, int alarm_number)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    for (int i = 0; i < count; i++) {
        if (installed[i].id == app_id) {
            app_delivery_t alarm = { .kind = APP_DELIVERY_ALARM, .request = alarm_number };
            host_deliver(installed[i].name, &alarm);
            return;
        }
    }
}

/* So many that have come are answered in a row; with more of them the
 * queue gets its turn first - a press is not kept waiting by a timer that
 * is set anew every time it fires. */
#define DEADLINES_IN_A_ROW 8

static void answer_deadlines(void)
{
    s_deadlines_are_told = false;
    deadline_id_t due;
    for (int answered = 0; answered < DEADLINES_IN_A_ROW; answered++) {
        if (!deadlines_take_due(&due)) {
            return;
        }
        switch ((deadline_kind_t)due.kind) {
        case DEADLINE_SCENE_TIMER:
            wm_timer(&wm, due.number);
            break;
        case DEADLINE_ALARM:
            ring_alarm(due.owner, due.number);
            break;
        case DEADLINE_AWAKE_WINDOW:
            power_awake_is_over();
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
            close_unanswered_confirm();
            break;
        case DEADLINE_STOP:
            stop(s_stop_reason);
            break;
        }
    }
    deadlines_look_again();
}

/* The alarms of applications that are no more go with them. */
static void forget_alarms_of_the_removed(void)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    uint32_t ids[LAUNCHER_INSTALLED_MAX];
    for (int i = 0; i < count; i++) {
        ids[i] = installed[i].id;
    }
    deadlines_keep_only(DEADLINE_ALARM, ids, count);
}

void host_reload_apps(void)
{
    reload_context_t context = { &wm, &delivery_state };
    /* Nothing of the poller's is held meanwhile: the applications that
     * leave the screen here run their last handlers, and those may ask the
     * poller for something. */
    launcher_reload(drop_app_scene, &context);
    poller_reload();
    forget_alarms_of_the_removed();
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
        s_is_delivering = true;
        app_deliver(&delivery_state, &wm, owner, delivery, esp_timer_get_time() / 1000);
        s_is_delivering = false;
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
    /* Woken from deep sleep is not powered up: the device goes on showing
     * what it showed, with no splash in between. */
    bool     shows_splash = launcher_about() != NULL && deep_sleep_woke_by() == WOKE_NOT_FROM_SLEEP;
    scene_t *first = shows_splash ? scene_boot_splash(after_boot, timers_set, launcher_about(),
                                                       about_is_shown)
                                  : after_boot;
    wm_init(&wm, menu, first, draw_header, NULL);

    app_delivery_init(&delivery_state);
    events_this_is_the_host_loop();
    poller_start();

    int events_since_shown = 0;
    for (;;) {
        /* A refresh takes half a second and the queue does not wait for it: with
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

        if (!events_are_waiting() && may_sleep_deeply()) {
            stop(HOST_STOP_DEEP_SLEEP);
        }
        event_t event;
        if (!events_wait(&event, wait_ms)) {
            continue;
        }
        const scene_t *scene_before = wm.current;
        switch (event.kind) {
        case EVENT_BUTTON:
            on_button(&wm, event.button.index, event.button.press);
            break;
        case EVENT_DEADLINES:
            answer_deadlines();
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
