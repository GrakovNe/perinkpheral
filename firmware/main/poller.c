/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "poller.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/task.h"

#include "app_manifest.h"
#include "app_store.h"
#include "app_kv.h"
#include "deadlines.h"
#include "events.h"
#include "host.h"
#include "host_api.h"
#include "launcher.h"
#include "memory.h"
#include "poll_schedule.h"
#include "poller_net.h"
#include "power.h"
#include "status.h"
#include "storage.h"
#include "text_template.h"
#include "wifi.h"

/* What ties the polls to the host: which are wanted (poll_schedule.c),
 * when each is due (a deadline), the one task that asks the network
 * (poller_net.c) and the radio it needs for that. The task never waits on
 * a clock of its own: it sleeps until it is woken, and what it has to
 * look at again by a certain time is a deadline too. */

static const char *TAG = "poller";

#define TASK_STACK_BYTES     8192 /* the TLS handshake runs on it */
#define TASK_PRIORITY        4
#define RADIO_WAIT_MS        20000 /* for the network to be joined before the radio is given back */
#define MEMORY_LOOK_AGAIN_MS 1000  /* no memory for a session just now */
/* A TLS session takes about 45 KB while it is open; below this the
 * request waits for a better moment instead of taking the device down. */
/* And its largest buffer is one piece of 16 KB: the sum is not enough. */
#define NO_APP               0 /* no application has this id */
#define MS_PER_SECOND        1000

static poll_schedule_t   s_schedule;
/* The installed applications as the poller last took them: its own copy,
 * so that nothing here looks at a list another task is rewriting. An
 * application is its id everywhere below. */
static app_manifest_t    s_known[LAUNCHER_INSTALLED_MAX];
static uint32_t pace_the_user_chose(const app_manifest_t *known, int poll);
static int               s_known_count;
/* The schedule is changed from other tasks too: an application asks for a
 * poll, the web page saves settings. */
static SemaphoreHandle_t s_lock;
static TaskHandle_t      s_task;

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

/* How long ago something was, not when something is due: the clock that
 * only goes forward. */
static int64_t uptime_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* There is something for the task to look at. From any task. */
static void wake(void)
{
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

void poller_look(void)
{
    wake();
}

/* ---- what the network part tells the host ---- */

/* An answer for an application is the host loop's to give: asked of it,
 * and waited for - the data is the poller's and stays where it is only
 * that long. */
typedef struct {
    const char           *app_name;
    const app_delivery_t *delivery;
} handed_t;

static void deliver_in_host_loop(void *argument)
{
    const handed_t *handed = argument;
    host_deliver(handed->app_name, handed->delivery);
}

static void hand_over(const char *app_name, const app_delivery_t *delivery)
{
    handed_t handed = { .app_name = app_name, .delivery = delivery };
    events_call_patiently(deliver_in_host_loop, &handed);
}

/* Whether the last time the network was needed it was there: what the
 * header's icon says. Unknown until something was asked. */
static volatile bool s_last_use_failed;

static void note_network_use(bool worked)
{
    if (s_last_use_failed == worked) {
        s_last_use_failed = !worked;
        status_check();
    }
}

bool poller_last_use_failed(void)
{
    return s_last_use_failed;
}

static const poller_net_host_t NET_HOST = {
    .deliver = hand_over,
    .network_worked = note_network_use,
};

/* ---- the applications, by id ---- */

/* The application's id, or NO_APP. With the lock held, or before the
 * poller's task is there. */
static int id_of(const char *app_name)
{
    for (int i = 0; i < s_known_count; i++) {
        if (strcmp(s_known[i].name, app_name) == 0) {
            return (int)s_known[i].id;
        }
    }
    return NO_APP;
}

/* The name of the application with the id, for whoever is called by name;
 * false when it is not installed any more. With the lock held. */
static bool name_of(int app, char name[APP_NAME_MAX + 1])
{
    for (int i = 0; i < s_known_count; i++) {
        if ((int)s_known[i].id == app) {
            strcpy(name, s_known[i].name);
            return true;
        }
    }
    return false;
}

static int app_number(const char *app_name)
{
    if (s_lock == NULL) {
        return NO_APP; /* asked before the poller started: nothing is scheduled yet */
    }
    lock();
    int app = id_of(app_name);
    unlock();
    return app;
}

/* ---- what the task has to look at again, and when ---- */

#define NEVER INT64_MAX

/* The task's alone: the nearest of the moments its last look at things
 * found worth coming back for. */
static int64_t s_look_again_in_ms;

static void look_again_in(int64_t milliseconds)
{
    if (milliseconds < s_look_again_in_ms) {
        s_look_again_in_ms = milliseconds > 0 ? milliseconds : 0;
    }
}

/* The task is asleep with nothing it means to come back to. */
static volatile bool s_is_resting;

/* Nothing to do now: until somebody wakes the task, or the moment it
 * asked to look again comes. */
static void rest(void)
{
    if (s_look_again_in_ms == NEVER) {
        deadlines_cancel(DEADLINE_POLLER_LOOK, 0, 0);
    } else {
        deadlines_set_in(DEADLINE_POLLER_LOOK, 0, 0, s_look_again_in_ms);
    }
    s_is_resting = s_look_again_in_ms == NEVER;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    s_is_resting = false;
}

/* ---- requests the applications make themselves ---- */

#define PENDING_MAX 2 /* for the whole device */

typedef struct {
    bool         is_waiting;
    int          app;
    int          number; /* what the answer is delivered under: the application's tag */
    uint32_t     order;  /* the oldest goes first */
    int64_t      queued_ms;
    int          method;
    char         url[APP_URL_MAX + 1];
    app_header_t headers[APP_HEADERS_MAX];
    int          header_count;
    uint8_t      body[HOST_HTTP_BODY_MAX];
    int          body_length;
} pending_t;

static pending_t s_pending[PENDING_MAX];
static uint32_t  s_next_order;

/* Makes the oldest request an application queued, if there is one. */
/* Takes the request that has waited longest and makes it - or, with
 * `why_not` a HOST_FAILED_*, answers it with that instead. */
static bool run_pending_request(int why_not)
{
    static pending_t taken; /* only this task makes requests; too large for its stack */
    char app_name[APP_NAME_MAX + 1]; /* its own: the list may change while the request runs */
    bool is_taken = false, is_installed = false;
    lock();
    pending_t *oldest = NULL;
    for (int i = 0; i < PENDING_MAX; i++) {
        if (s_pending[i].is_waiting && (oldest == NULL || s_pending[i].order < oldest->order)) {
            oldest = &s_pending[i];
        }
    }
    if (oldest != NULL) {
        taken = *oldest;
        oldest->is_waiting = false;
        is_taken = true;
        is_installed = name_of(taken.app, app_name);
    }
    unlock();
    if (is_installed) {
        poller_ask_t ask = {
            .number = taken.number, .method = taken.method, .url = taken.url,
            .headers = taken.headers, .header_count = taken.header_count,
            .body = taken.body, .body_length = taken.body_length,
        };
        if (why_not != 0) {
            poller_net_tell_failure(app_name, ask.number, why_not);
        } else {
            poller_net_request(app_name, &ask);
        }
    }
    return is_taken;
}

int32_t poller_request(const char *app_name, const app_request_t *asked)
{
    int app = app_number(app_name);
    if (app == NO_APP) {
        return HOST_ERR_FAILED;
    }
    /* Only where the manifest said the application would go - and that is
     * asked of the address as it will be, the user's own server put in. */
    char url[APP_URL_MAX + 1];
    if (!text_template_fill_url(asked->url, poller_net_setting_of, (void *)app_name, url,
                                sizeof url)) {
        return HOST_ERR_ARGUMENT;
    }
    char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
    bool  allowed = manifest != NULL &&
                    app_manifest_allows_url(manifest, url, poller_net_setting_of,
                                            (void *)app_name);
    free(manifest);
    if (!allowed) {
        ESP_LOGW(TAG, "%s: a request to a host its manifest does not list", app_name);
        return HOST_ERR_ARGUMENT;
    }
    int32_t result = HOST_ERR_NO_SPACE;
    lock();
    int        own_waiting = 0;
    pending_t *free_slot = NULL;
    for (int i = 0; i < PENDING_MAX; i++) {
        if (!s_pending[i].is_waiting) {
            free_slot = &s_pending[i];
        } else if (s_pending[i].app == app) {
            own_waiting++;
        }
    }
    if (free_slot != NULL && own_waiting < HOST_HTTP_PENDING_MAX) {
        free_slot->app = app;
        free_slot->number = asked->tag;
        free_slot->order = s_next_order++;
        free_slot->queued_ms = uptime_ms();
        free_slot->method = asked->method;
        strcpy(free_slot->url, url);
        memcpy(free_slot->headers, asked->headers, sizeof free_slot->headers);
        free_slot->header_count = asked->header_count;
        free_slot->body_length = asked->body_length;
        memcpy(free_slot->body, asked->body, (size_t)asked->body_length);
        free_slot->is_waiting = true;
        result = HOST_OK;
    }
    unlock();
    wake();
    return result;
}

/* A request waits for the network only so long: then the application is
 * told there is none, and decides what to show. */
static bool fail_request_without_network(void)
{
    if (wifi_is_online()) {
        return false;
    }
    char app_name[APP_NAME_MAX + 1];
    int  number = 0;
    bool found = false;
    lock();
    for (int i = 0; i < PENDING_MAX && !found; i++) {
        if (!s_pending[i].is_waiting) {
            continue;
        }
        int64_t waited_ms = uptime_ms() - s_pending[i].queued_ms;
        if (waited_ms > RADIO_WAIT_MS) {
            number = s_pending[i].number;
            s_pending[i].is_waiting = false;
            found = name_of(s_pending[i].app, app_name);
        } else {
            look_again_in(RADIO_WAIT_MS - waited_ms + 1);
        }
    }
    unlock();
    if (found) {
        ESP_LOGW(TAG, "%s/%d: no network", app_name, number);
        note_network_use(false);
        poller_net_tell_failure(app_name, number, HOST_FAILED_NO_NETWORK);
    }
    return found;
}

static bool has_pending_request(void)
{
    bool has = false;
    lock();
    for (int i = 0; i < PENDING_MAX && !has; i++) {
        has = s_pending[i].is_waiting;
    }
    unlock();
    return has;
}

/* ---- a file for somebody else: the catalog, an application being installed ---- */

typedef struct {
    const char    *url;
    size_t         max_bytes;
    poller_take_fn take;
    void          *context;
    int            http_status;
    bool           is_whole;
} fetch_job_t;

static fetch_job_t      *s_fetch_job;  /* the one waiting to be done, or NULL */
static SemaphoreHandle_t s_fetch_done; /* given when it is */
static SemaphoreHandle_t s_fetch_turn; /* one caller at a time */

static bool run_fetch_job(void)
{
    lock();
    fetch_job_t *job = s_fetch_job;
    s_fetch_job = NULL;
    unlock();
    if (job == NULL) {
        return false;
    }
    job->is_whole = poller_net_fetch(job->url, job->max_bytes, job->take, job->context,
                                     &job->http_status);
    xSemaphoreGive(s_fetch_done);
    return true;
}

bool poller_fetch(const char *url, size_t max_bytes, poller_take_fn take, void *context,
                  int *http_status)
{
    *http_status = 0;
    if (s_lock == NULL || !wifi_is_online()) {
        return false;
    }
    fetch_job_t job = { .url = url, .max_bytes = max_bytes, .take = take, .context = context };
    xSemaphoreTake(s_fetch_turn, portMAX_DELAY);
    lock();
    s_fetch_job = &job;
    unlock();
    wake();
    xSemaphoreTake(s_fetch_done, portMAX_DELAY);
    xSemaphoreGive(s_fetch_turn);
    *http_status = job.http_status;
    return job.is_whole;
}

/* ---- the polls ---- */

#define ABANDONED_AGAIN_MS (60 * 1000)

/* The poll whose answer the application was not there to take to its end:
 * the one being fetched - there is one request at a time. With the lock. */
static struct {
    bool is_one;
    int  app;
    int  poll;
} s_abandoned;

void poller_answer_was_abandoned(const char *app_name, int request)
{
    if (s_lock == NULL || request >= HOST_REQUEST_FIRST) {
        return; /* a request of the application's own is not the host's to repeat */
    }
    lock();
    s_abandoned.app = id_of(app_name);
    s_abandoned.poll = request;
    s_abandoned.is_one = s_abandoned.app != NO_APP;
    unlock();
}

void poller_poll_is_due(uint32_t app, int poll)
{
    lock();
    poll_schedule_want(&s_schedule, (int)app, poll);
    unlock();
    wake();
}

void poller_network_is_back(void)
{
    note_network_use(true);
    /* What could not be asked while it was away is asked now, not when
     * each poll's turn comes round again. */
    if (s_lock == NULL) {
        return;
    }
    /* Not when the radio is only on for a poll: the one it was raised for
     * is wanted already, and the others keep the pace they asked for. */
    if (!power_is_saving()) {
        lock();
        poll_schedule_want_all(&s_schedule);
        unlock();
    }
    wake();
}

/* A request waits for memory this many looks and is then told there is
 * none: an answer, which waiting for ever is not. */
#define MEMORY_LOOKS_MAX 6

static int s_memory_looks;

static bool has_memory_for_a_session(void)
{
    if (memory_has(MEMORY_FOR_A_SESSION)) {
        s_memory_looks = 0;
        return true;
    }
    s_memory_looks++;
    return false;
}

/* Waited long enough: whatever was to be asked is answered "no memory". */
static bool has_waited_for_memory_enough(void)
{
    if (s_memory_looks < MEMORY_LOOKS_MAX) {
        return false;
    }
    s_memory_looks = 0;
    return true;
}

/* Whether anything is waiting for the network now. */
static bool wants_network(void)
{
    lock();
    bool wanted = s_fetch_job != NULL || poll_schedule_next_wanted(&s_schedule) != NULL;
    unlock();
    return wanted || has_pending_request();
}

static bool s_is_holding_radio; /* the task's alone: it asked for the radio and has it */

/* Saving power, the radio is on only while there is something to ask:
 * taken when something is wanted, given back when nothing is. A network
 * that does not answer is not waited for with the radio on: the next try
 * is a poll's least distance away. */
static void keep_radio_for_work(void)
{
    static int64_t held_since_ms, try_again_at_ms;
    bool wanted = power_is_saving() && wants_network();
    if (wanted && !s_is_holding_radio) {
        if (uptime_ms() >= try_again_at_ms) {
            power_hold_radio();
            s_is_holding_radio = true;
            held_since_ms = uptime_ms();
        } else {
            look_again_in(try_again_at_ms - uptime_ms());
        }
    } else if (s_is_holding_radio && !wanted) {
        power_release_radio();
        s_is_holding_radio = false;
    }
    if (s_is_holding_radio && !wifi_is_online()) {
        int64_t waited_ms = uptime_ms() - held_since_ms;
        if (waited_ms > RADIO_WAIT_MS) {
            ESP_LOGW(TAG, "no network in %d s: the radio is given back", RADIO_WAIT_MS / 1000);
            power_release_radio();
            s_is_holding_radio = false;
            try_again_at_ms = uptime_ms() + (int64_t)POWER_SAVING_POLL_MIN_S * MS_PER_SECOND;
        } else {
            look_again_in(RADIO_WAIT_MS - waited_ms + 1);
        }
    }
}

/* Asks the poll that has waited longest, if one is wanted and can be
 * asked now. */
static bool run_wanted_poll(void)
{
    /* The entry may be moved by another task once the lock is gone, and the
     * list of applications read anew while the request runs: what is needed
     * of both is taken now. */
    char name[APP_NAME_MAX + 1] = "";
    lock();
    const poll_entry_t *wanted = poll_schedule_next_wanted(&s_schedule);
    int  app = wanted != NULL ? wanted->app : NO_APP, poll = wanted != NULL ? wanted->poll : 0;
    bool is_installed = wanted != NULL && name_of(app, name);
    unlock();
    if (wanted == NULL) {
        return false;
    }
    bool has_network = wifi_is_online();
    int  why_not = has_network ? 0 : HOST_FAILED_NO_NETWORK;
    if (has_network && !has_memory_for_a_session()) {
        if (!has_waited_for_memory_enough()) {
            look_again_in(MEMORY_LOOK_AGAIN_MS);
            return false;
        }
        why_not = HOST_FAILED_NO_MEMORY;
    }
    if (!has_network && s_is_holding_radio) {
        /* The radio is being brought up for this: woken when it is, and
         * looked at again if it never is. */
        look_again_in(RADIO_WAIT_MS);
        return false;
    }
    if (is_installed) {
        if (why_not == 0) {
            poller_net_poll(name, poll);
        } else {
            poller_net_poll_not_made(name, poll, why_not);
        }
    }
    /* By id: if the application is gone meanwhile, so is its poll. */
    lock();
    poll_schedule_answered(&s_schedule, app, poll);
    uint32_t pace_ms = poll_schedule_pace_ms(&s_schedule, app, poll);
    if (s_abandoned.is_one && s_abandoned.app == app && s_abandoned.poll == poll) {
        /* Its answer was cut short by somebody at the device: again when
         * they are likely done, not a whole period later. */
        s_abandoned.is_one = false;
        if (pace_ms > ABANDONED_AGAIN_MS) {
            pace_ms = ABANDONED_AGAIN_MS;
        }
    }
    unlock();
    if (pace_ms > 0) {
        deadlines_set_in(DEADLINE_POLL, (uint32_t)app, poll, pace_ms);
    }
    return true;
}

static void poller_task(void *arg)
{
    (void)arg;
    for (;;) {
        s_look_again_in_ms = NEVER;
        keep_radio_for_work();
        /* Somebody is waiting for this one: before the polls. */
        if (run_fetch_job()) {
            continue;
        }
        bool has_network = wifi_is_online();
        if (has_network && has_pending_request()) {
            if (has_memory_for_a_session()) {
                run_pending_request(0);
                continue;
            }
            if (has_waited_for_memory_enough()) {
                run_pending_request(HOST_FAILED_NO_MEMORY);
                continue;
            }
            look_again_in(MEMORY_LOOK_AGAIN_MS);
        }
        if (fail_request_without_network()) {
            continue;
        }
        if (run_wanted_poll()) {
            continue;
        }
        rest();
    }
}

/* Takes the installed applications as the launcher has them now. What
 * was scheduled, queued or set for an application that is still there
 * stays as it is; what was for one that is gone goes; the polls of a new
 * one are scheduled. Returns the polls there are. In the host loop, with
 * the lock held or before the poller's task. */
static int schedule_installed(void)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    s_known_count = count < LAUNCHER_INSTALLED_MAX ? count : LAUNCHER_INSTALLED_MAX;
    memcpy(s_known, installed, (size_t)s_known_count * sizeof s_known[0]);

    int      apps[LAUNCHER_INSTALLED_MAX];
    uint32_t owners[LAUNCHER_INSTALLED_MAX];
    for (int i = 0; i < s_known_count; i++) {
        apps[i] = (int)s_known[i].id;
        owners[i] = s_known[i].id;
    }
    poll_schedule_keep_only(&s_schedule, apps, s_known_count);
    deadlines_keep_only(DEADLINE_POLL, owners, s_known_count);
    for (int i = 0; i < PENDING_MAX; i++) {
        char name[APP_NAME_MAX + 1];
        if (s_pending[i].is_waiting && !name_of(s_pending[i].app, name)) {
            s_pending[i].is_waiting = false;
        }
    }
    /* From a battery no poll is closer to its last than this. */
    poll_schedule_set_floor(&s_schedule, power_is_saving() ? POWER_SAVING_POLL_MIN_S : 0);
    for (int i = 0; i < s_known_count; i++) {
        for (int poll = 0; poll < s_known[i].poll_count; poll++) {
            uint32_t first_in_ms;
            if (poll_schedule_has(&s_schedule, apps[i], poll) ||
                !poll_schedule_add(&s_schedule, apps[i], poll, s_known[i].poll_every_s[poll],
                                   &first_in_ms)) {
                continue;
            }
            /* The pace the application asked for before the restart, if it
             * asked for one: the first fetch is at once all the same. */
            uint32_t kept_s = pace_the_user_chose(&s_known[i], poll);
            if (kept_s == 0) {
                kept_s = app_kv_poll_pace(s_known[i].name, poll);
            }
            if (kept_s != 0) {
                bool is_another;
                poll_schedule_set_interval(&s_schedule, apps[i], poll, kept_s, &is_another);
            }
            if (!deadlines_has(DEADLINE_POLL, owners[i], poll)) {
                deadlines_set_in(DEADLINE_POLL, owners[i], poll, first_in_ms);
            }
        }
    }
    return s_schedule.count;
}

bool poller_is_idle(void)
{
    /* Resting, and not about to be woken by something already asked. */
    return s_is_resting && !wants_network();
}

void poller_reschedule(void)
{
    if (s_lock == NULL) {
        return;
    }
    /* Only the least distance between polls is another: everything else
     * is as it was. */
    lock();
    poll_schedule_set_floor(&s_schedule, power_is_saving() ? POWER_SAVING_POLL_MIN_S : 0);
    unlock();
    wake(); /* the radio may be the poller's to hold now, or to give back */
}

void poller_hold(void)
{
    lock();
}

void poller_reload(void)
{
    lock();
    int polls = schedule_installed();
    unlock();
    ESP_LOGI(TAG, "applications changed: %d polls of %d applications", polls, s_known_count);
}

void poller_start(void)
{
    poller_net_start(&NET_HOST);
    poll_schedule_init(&s_schedule);
    int polls = schedule_installed();
    s_fetch_done = xSemaphoreCreateBinary();
    s_fetch_turn = xSemaphoreCreateMutex();
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL && s_fetch_done != NULL && s_fetch_turn != NULL);
    /* Started even with nothing to fetch: an application may ask later. */
    BaseType_t ok = xTaskCreate(poller_task, "poller", TASK_STACK_BYTES, NULL, TASK_PRIORITY,
                                &s_task);
    configASSERT(ok == pdPASS);
    ESP_LOGI(TAG, "%d polls of the installed applications are scheduled", polls);
}

/* The pace of a poll that the manifest leaves to a setting: what the user
 * chose there, or 0 when the poll has no such setting or nothing is chosen. */
static uint32_t pace_the_user_chose(const app_manifest_t *known, int poll)
{
    if (known->poll_every_key[poll][0] == '\0') {
        return 0;
    }
    char    chosen[APP_SETTING_VALUE_MAX + 1];
    int32_t length = app_kv_get(known->name, known->poll_every_key[poll], chosen,
                                (int32_t)sizeof chosen - 1);
    if (length <= 0) {
        return 0;
    }
    chosen[length] = '\0';
    return app_manifest_pace_seconds(chosen);
}

/* The settings of an application were changed: its polls are fetched at
 * once, and those whose pace is a setting take the pace it has now. */
void poller_hurry(const char *app_name)
{
    int app = app_number(app_name);
    if (app == NO_APP) {
        return;
    }
    for (int i = 0; i < s_known_count; i++) {
        if ((int)s_known[i].id != app) {
            continue;
        }
        for (int poll = 0; poll < s_known[i].poll_count; poll++) {
            if (s_known[i].poll_every_key[poll][0] == '\0') {
                continue;
            }
            uint32_t pace_s = pace_the_user_chose(&s_known[i], poll);
            bool     is_another;
            lock();
            poll_schedule_set_interval(&s_schedule, app, poll,
                                       pace_s != 0 ? pace_s : s_known[i].poll_every_s[poll],
                                       &is_another);
            unlock();
        }
    }
    lock();
    poll_schedule_want_of(&s_schedule, app);
    unlock();
    wake();
}

bool poller_poll_now(const char *app_name, int poll)
{
    int app = app_number(app_name);
    if (app == NO_APP) {
        return false;
    }
    lock();
    bool found = poll_schedule_want(&s_schedule, app, poll);
    unlock();
    wake();
    return found;
}

bool poller_set_interval(const char *app_name, int poll, uint32_t every_s)
{
    int app = app_number(app_name);
    if (app == NO_APP) {
        return false;
    }
    bool is_another = false;
    lock();
    bool     found = poll_schedule_set_interval(&s_schedule, app, poll, every_s, &is_another);
    uint32_t pace_ms = poll_schedule_pace_ms(&s_schedule, app, poll);
    unlock();
    if (found && is_another) {
        /* Counted from now, and kept: a restart does not bring the
         * manifest's pace back. */
        deadlines_set_in(DEADLINE_POLL, (uint32_t)app, poll, pace_ms);
        app_kv_set_poll_pace(app_name, poll, every_s);
    }
    return found;
}
