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
#include "app_host.h"
#include "gfx_icons.h"
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

/* What applications keep in the header while they are away: on each side
 * an icon and a line, the application's that set it last. */
#define HEADER_SIDES           2 /* HOST_HEADER_LEFT, HOST_HEADER_RIGHT of host_api.h */
#define HEADER_LEFT            0
#define HEADER_RIGHT           1
#define HEADER_STATUS_TEXT_MAX 15
#define HEADER_NO_ICON         (-1)
static struct {
    uint32_t app_id; /* 0: nobody has this side */
    int      icon;
    char     text[HEADER_STATUS_TEXT_MAX + 1];
} s_header_status[HEADER_SIDES];

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
    if (s_header_status[HEADER_LEFT].app_id != 0) {
        header.left_icon = s_header_status[HEADER_LEFT].icon;
        header.left = s_header_status[HEADER_LEFT].text;
    }
    if (s_header_status[HEADER_RIGHT].app_id != 0) {
        header.right_icon = s_header_status[HEADER_RIGHT].icon;
        header.right = s_header_status[HEADER_RIGHT].text;
    }
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
static wm_t                 wm;
static app_delivery_state_t delivery_state;

/* An application taking an answer off the screen, piece by piece, gives
 * way to what the screen is needed for now. The rest of that answer is
 * dropped; when it was a poll's, the poll is asked again in a while. */
static void give_way_to_the_screen(void)
{
    app_abandoned_t abandoned;
    app_delivery_give_way(&delivery_state, &wm, esp_timer_get_time() / 1000, &abandoned);
    if (abandoned.owner != NULL) {
        const char *app_name = launcher_app_name(abandoned.owner);
        if (app_name != NULL) {
            poller_answer_was_abandoned(app_name, abandoned.request);
        }
    }
}

/* The scene whose picture the panel shows: what a person pressing a
 * button is looking at. Not the scene that has the screen while its first
 * frame is still being put on the panel. */
static const scene_t *volatile s_scene_on_panel;
/* The buttons that went down while a scene that asks once was on the
 * panel: only their presses are that scene's to take. */
static uint8_t s_went_down_on_question;

/* Whether the press is one the scene on the screen may have: every press,
 * unless one press there does what cannot be taken back - then only a
 * press that began with the question in front of whoever made it. The
 * panel takes seconds to show a question, and a press made for the
 * picture before it must not answer it. */
static bool is_press_for(const scene_t *scene, uint8_t index, event_press_t press)
{
    if (!scene->asks_once) {
        s_went_down_on_question = 0;
        return true;
    }
    uint8_t button = (uint8_t)(1u << index);
    if (press == EVENT_PRESS_DOWN) {
        if (s_scene_on_panel == scene) {
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
    give_way_to_the_screen();
    /* The long press on BACK is the user's own way out of anything. */
    bool is_way_out = index == BUTTON_BACK_INDEX && press == EVENT_PRESS_LONG;
    if (!is_way_out && !is_press_for(wm->current, index, press)) {
        return;
    }
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
    s_scene_on_panel = wm->current;
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
    wm_close(reload->wm, scene);
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
    if (why == HOST_STOP_FACTORY_RESET) {
        reset_mark_factory();
    }
    esp_restart();
}

void host_set_header_status(uint32_t app_id, int side, int icon, const char *text)
{
    if (side != HEADER_LEFT && side != HEADER_RIGHT) {
        return;
    }
    bool is_empty = icon == HEADER_NO_ICON && text[0] == '\0';
    if (is_empty) {
        if (s_header_status[side].app_id != app_id) {
            return; /* not its to give back */
        }
        s_header_status[side].app_id = 0;
    } else {
        s_header_status[side].app_id = app_id;
        s_header_status[side].icon = icon;
        strlcpy(s_header_status[side].text, text, sizeof s_header_status[side].text);
    }
    wm_invalidate(&wm, WM_CHANGED_BY_DATA);
}

void host_redraw(void)
{
    wm_invalidate(&wm, WM_CHANGED_CLEANLY);
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
    bool              is_to_put_away; /* answered: the screen goes back once the handler returned */
    scene_t          *scene;
    char              first[CONFIRM_TEXT_MAX + 1];  /* the question, in two lines */
    char              second[CONFIRM_TEXT_MAX + 1];
    int               icon;
    char              action[CONFIRM_TEXT_MAX + 1];
} s_confirm;

/* An answer is being handed to an application that is not on the screen:
 * the scene on it is set aside for the moment and comes back. */
static bool s_is_delivering;

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
    s_confirm.is_to_put_away = true;
    deadlines_cancel(DEADLINE_CONFIRM, 0, 0);
    ESP_LOGW(TAG, "\"%s %s\" %s", s_confirm.first, s_confirm.second,
             is_confirmed ? "confirmed" : "not confirmed");
    xSemaphoreGive(s_confirm.answered);
}

/* The question is answered and its scene is still in the stack: on the
 * screen, set aside for an answer in pieces, or under the menu that was
 * opened over it. It comes out, and what was under it is there again. Not
 * from inside a scene's own handler. */
static void put_question_away(void)
{
    s_confirm.is_to_put_away = false;
    give_way_to_the_screen();
    wm_close(&wm, s_confirm.scene);
}

static void open_confirm(void *argument)
{
    (void)argument;
    /* An application taking an answer off the screen gives way, as to a press. */
    give_way_to_the_screen();
    s_confirm.is_open = true;
    s_confirm.is_to_put_away = false;
    s_confirm.scene = scene_confirm(s_confirm.first, s_confirm.second, s_confirm.icon,
                                    s_confirm.action, on_confirm_answer);
    wm_open(&wm, s_confirm.scene);
    /* The same scene with another question is another picture. */
    wm_invalidate(&wm, WM_CHANGED_BY_SWITCH);
    if (!deadlines_set_in(DEADLINE_CONFIRM, 0, 0, CONFIRM_WAIT_MS)) {
        on_confirm_answer(false); /* nothing would end the wait: not asked at all */
    }
}

/* Nobody came. */
static void close_unanswered_confirm(void)
{
    if (s_confirm.is_open) {
        on_confirm_answer(false);
    }
}

host_answer_t host_ask_at_the_device(const char *first, const char *second, int icon,
                                     const char *action)
{
    if (s_confirm.turn == NULL || xSemaphoreTake(s_confirm.turn, 0) != pdTRUE) {
        return HOST_ANSWER_BUSY;
    }
    strlcpy(s_confirm.first, first, sizeof s_confirm.first);
    strlcpy(s_confirm.second, second, sizeof s_confirm.second);
    s_confirm.icon = icon;
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

/* ---- the device is busy with something that must not be interrupted ---- */

static scene_t *s_working; /* in the stack while it is; NULL otherwise */

static void open_working(void *argument)
{
    const char *const *lines = argument;
    give_way_to_the_screen();
    /* The question that led here is put away first: it is not what the
     * screen goes back to. */
    if (s_confirm.is_to_put_away) {
        put_question_away();
    }
    s_working = scene_working(lines[0], lines[1]);
    wm_open(&wm, s_working);
}

static void close_working(void *argument)
{
    (void)argument;
    if (s_working == NULL) {
        return;
    }
    /* One instance at a time: an application taking an answer off the
     * screen goes before the one that had the screen is loaded again. */
    give_way_to_the_screen();
    wm_close(&wm, s_working);
    s_working = NULL;
}

/* Whoever is told that the work began has just pressed a button: the
 * notice is on the panel before the work - which may well keep the host
 * loop from the panel for seconds, as erasing the flash does - begins. */
#define WORKING_SHOWN_WAIT_MS 5000
#define WORKING_SHOWN_POLL_MS 50

void host_show_working(const char *first, const char *second)
{
    const char *lines[] = { first, second };
    if (!events_call(open_working, lines)) {
        return; /* a busy queue: the work goes on unannounced */
    }
    for (int waited_ms = 0; waited_ms < WORKING_SHOWN_WAIT_MS && s_scene_on_panel != s_working;
         waited_ms += WORKING_SHOWN_POLL_MS) {
        vTaskDelay(pdMS_TO_TICKS(WORKING_SHOWN_POLL_MS));
    }
}

void host_work_is_over(void)
{
    events_call_patiently(close_working, NULL);
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
            app_host_alarm_rang(app_id, alarm_number);
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
    app_host_keep_recurring_of(ids, count);
    for (int side = 0; side < HEADER_SIDES; side++) {
        bool is_owner_there = false;
        for (int i = 0; i < count; i++) {
            is_owner_there = is_owner_there || ids[i] == s_header_status[side].app_id;
        }
        if (!is_owner_there && s_header_status[side].app_id != 0) {
            s_header_status[side].app_id = 0;
            wm_invalidate(&wm, WM_CHANGED_BY_DATA);
        }
    }
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
    bool     shows_splash = launcher_about() != NULL;
    scene_t *first = shows_splash ? scene_boot_splash(after_boot, timers_set, launcher_about(),
                                                       about_is_shown)
                                  : after_boot;
    wm_init(&wm, menu, first, draw_header, NULL);
    /* A restart forgot what the applications had set going; those that
     * asked are told, and set it going again. */
    app_host_tell_started();

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
        if (s_confirm.is_to_put_away) {
            put_question_away();
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
