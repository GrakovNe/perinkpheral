/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The host loop and the installed applications: the list read anew,
 * answers and alarms handed to an application wherever it is, news it
 * asked for, and what it keeps in the header while it is away.
 */
#include <string.h>

#include "host.h"
#include "host_private.h"
#include "header.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "app_host.h"
#include "app_kv.h"
#include "app_manifest.h"
#include "app_scene.h"
#include "deadlines.h"
#include "host_api.h"
#include "launcher.h"
#include "poller.h"
#include "units.h"

static const char *TAG = "host";

/* ---- the header ----
 *
 * What applications keep in the header while they are away. */
static header_kept_t s_header_kept;

void host_set_header_status(uint32_t app_id, int side, int icon, const char *text)
{
    if (header_keep(&s_header_kept, app_id, side, icon, text)) {
        wm_invalidate(&host_wm, WM_CHANGED_BY_DATA);
    }
}

const header_kept_t *host_header_kept(void)
{
    return &s_header_kept;
}

/* ---- the list of applications ---- */

/* An application's scene is about to be freed, the list of them being
 * read anew: nobody may be left holding it. */
static void drop_app_scene(scene_t *scene, void *context)
{
    (void)context;
    wm_close(&host_wm, scene);
    if (host_delivery.in_background == scene) {
        /* In the middle of an answer that now has nobody to go to; the scene
         * that gave way to it comes back. */
        app_delivery_init(&host_delivery);
        app_scene_end_background(scene); /* unloaded first: one instance at a time */
        wm_resume(&host_wm);
    }
}

/* The alarms of applications that are no more go with them. */
static void remove_orphan_alarms(void)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    uint32_t ids[LAUNCHER_INSTALLED_MAX];
    for (int i = 0; i < count; i++) {
        ids[i] = installed[i].id;
    }
    deadlines_keep_only(DEADLINE_ALARM, ids, count);
    app_host_keep_recurring_of(ids, count);
    if (header_keep_only(&s_header_kept, ids, count)) {
        wm_invalidate(&host_wm, WM_CHANGED_BY_DATA);
    }
}

void host_close_app(const char *app_name)
{
    scene_t *scene = launcher_app_scene(app_name);
    if (scene == NULL) {
        return;
    }
    /* Taking an answer off the screen it is not in the stack, and closing
     * it there changes nothing on the screen: it is ended here. */
    if (host_delivery.in_background == scene) {
        host_end_background_answer();
    }
    wm_close(&host_wm, scene);
}

static void tell_new_versions(void);

void host_reload_apps(void)
{
    /* An application loaded off the screen for an answer is unloaded
     * before any scene is closed: closing the one on the screen may enter
     * the one under it, and there is room for one instance at a time. */
    host_end_background_answer();
    /* The application on the screen comes back if it is still installed:
     * another being installed or removed is no reason to leave it. */
    char        kept[APP_NAME_MAX + 1] = "";
    const char *current = launcher_app_name(host_wm.current);
    if (current != NULL) {
        strlcpy(kept, current, sizeof kept);
    }
    /* Nothing of the poller's is held meanwhile: the applications that
     * leave the screen here run their last handlers, and those may ask the
     * poller for something. */
    launcher_reload(drop_app_scene, NULL);
    poller_reload();
    remove_orphan_alarms();
    host_renew_menu();
    scene_t *again = kept[0] != '\0' ? launcher_app_scene(kept) : NULL;
    if (again != NULL && host_wm.current != again) {
        wm_open_from_home(&host_wm, again);
    }
    wm_invalidate(&host_wm, WM_CHANGED_URGENTLY);
    tell_new_versions();
}

static bool s_reload_is_due;

void host_reload_apps_later(void)
{
    s_reload_is_due = true;
}

void host_apps_after_event(void)
{
    if (s_reload_is_due) {
        /* Asked for from inside a handler, which has returned by now. */
        s_reload_is_due = false;
        host_reload_apps();
    }
}

/* ---- what is handed to an application ---- */

/* An answer is being handed to an application that is not on the screen:
 * the scene on it is set aside for the moment and comes back. */
static bool s_is_delivering;

bool host_is_delivering(void)
{
    return s_is_delivering;
}

void host_deliver(const char *app_name, const app_delivery_t *delivery)
{
    scene_t *owner = launcher_app_scene(app_name);
    if (owner != NULL) {
        s_is_delivering = true;
        app_deliver(&host_delivery, &host_wm, owner, delivery, esp_timer_get_time() / US_PER_MS);
        s_is_delivering = false;
    }
}

/* An alarm that has come is handed to its application like an answer. */
void host_ring_alarm(uint32_t app_id, int alarm_number)
{
    const app_manifest_t *app = launcher_find_id(app_id);
    if (app != NULL) {
        app_host_alarm_rang(app_id, alarm_number);
        app_delivery_t alarm = { .kind = APP_DELIVERY_ALARM, .request = alarm_number };
        host_deliver(app->name, &alarm);
    }
}

/* An application that has not run here before, or ran as another version,
 * is told so once: loaded for the call when it is not on the screen. */
static void tell_new_versions(void)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    struct {
        char     name[APP_NAME_MAX + 1];
        uint32_t id;
        bool     is_update;
    } news[LAUNCHER_INSTALLED_MAX];
    int told = 0;
    for (int i = 0; i < count && told < LAUNCHER_INSTALLED_MAX; i++) {
        uint32_t seen = app_kv_seen_id(installed[i].name);
        if (seen != installed[i].id) {
            strcpy(news[told].name, installed[i].name);
            news[told].id = installed[i].id;
            news[told++].is_update = seen != 0;
        }
    }
    for (int i = 0; i < told; i++) {
        /* Noted first: a version that crashes in the handler is not told
         * again at every start. */
        app_kv_set_seen_id(news[i].name, news[i].id);
        ESP_LOGI(TAG, "%s: %s", news[i].name, news[i].is_update ? "updated" : "installed");
        app_delivery_t delivery = { .kind = news[i].is_update ? APP_DELIVERY_UPDATED
                                                              : APP_DELIVERY_INSTALLED };
        host_deliver(news[i].name, &delivery);
    }
}

/* Tells every application that asked for this kind of news, loading each
 * for the call. Those that did not ask are left asleep. At a start, what
 * is new is told before that the device started. */
void host_tell_news(int intent)
{
    if (intent == HOST_INTENT_STARTED) {
        tell_new_versions();
    }
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
