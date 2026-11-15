/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "priorities.h"
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
#include "units.h"

/* What ties the polls to the host: which are wanted (poll_schedule.c),
 * when each is due (a deadline), the one task that asks the network
 * (poller_net.c) and the radio it needs for that. The task never waits on
 * a clock of its own: it sleeps until it is woken, and what it has to
 * look at again by a certain time is a deadline too. */

static const char *TAG = "poller";

#define TASK_STACK_BYTES     STACK_POLLER
#define TASK_PRIORITY        PRIORITY_BACKGROUND
#define RADIO_WAIT_MS        20000 /* for the network to be joined before the radio is given back */
#define MEMORY_LOOK_AGAIN_MS 1000  /* no memory for a session just now */
/* A TLS session takes about 45 KB while it is open; below this the
 * request waits for a better moment instead of taking the device down. */
/* And its largest buffer is one piece of 16 KB: the sum is not enough. */
#define NO_APP               0 /* no application has this id */

static poll_schedule_t   s_schedule;
/* The installed applications as the poller last took them: its own copy,
 * so that nothing here looks at a list another task is rewriting. An
 * application is its id everywhere below. */
static app_manifest_t    s_known[LAUNCHER_INSTALLED_MAX];
static uint32_t user_interval_seconds(const app_manifest_t *known, int poll);
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
    return esp_timer_get_time() / US_PER_MS;
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

/* Whose request is under way, by id: an application updated meanwhile has
 * another id and the same name, and the answer was asked for by the
 * version that is gone. One request at a time, so one is enough. */
static volatile uint32_t s_answering_app;

static void deliver_in_host_loop(void *argument)
{
    const handed_t       *handed = argument;
    const app_manifest_t *app = launcher_find(handed->app_name);
    if (app != NULL && app->id == s_answering_app) {
        host_deliver(handed->app_name, handed->delivery);
    }
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
static int64_t s_recheck_in_ms;

static void schedule_recheck(int64_t milliseconds)
{
    if (milliseconds < s_recheck_in_ms) {
        s_recheck_in_ms = milliseconds > 0 ? milliseconds : 0;
    }
}

/* The task is asleep with nothing it means to come back to. */
static volatile bool s_is_resting;

/* Nothing to do now: until somebody wakes the task, or the moment it
 * asked to look again comes. */
static void rest(void)
{
    if (s_recheck_in_ms == NEVER) {
        deadlines_cancel(DEADLINE_POLLER_LOOK, 0, 0);
    } else {
        deadlines_set_in(DEADLINE_POLLER_LOOK, 0, 0, s_recheck_in_ms);
    }
    s_is_resting = s_recheck_in_ms == NEVER;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    s_is_resting = false;
}

/* ---- requests the applications make themselves ---- */

/* Each application has HOST_HTTP_PENDING_MAX waiting at most; a request is
 * kept in a block of its own size, and all of them together in no more
 * than PENDING_BYTES_MAX - a request is a URL and a few headers, a hundred
 * bytes or two, and a body is rare. */
#define PENDING_PER_APP   HOST_HTTP_PENDING_MAX
#define PENDING_SLOTS     (LAUNCHER_INSTALLED_MAX * PENDING_PER_APP)
#define PENDING_BYTES_MAX (16 * 1024)

typedef struct {
    int      app;
    int      number; /* what the answer is delivered under: the application's tag */
    uint32_t order;  /* the oldest goes first */
    int64_t  queued_ms;
    int      method;
    int      header_count;
    int      body_length;
    size_t   bytes;  /* of the whole block */
    /* The URL, terminated; each header's name and value, terminated; the body. */
    char     text[];
} pending_t;

static pending_t *s_pending[PENDING_SLOTS];
static size_t     s_pending_bytes;
static uint32_t   s_next_order;
static int        s_last_app_served = NO_APP; /* the applications take turns */

/* With the lock. */
static void drop_pending(int slot)
{
    s_pending_bytes -= s_pending[slot]->bytes;
    free(s_pending[slot]);
    s_pending[slot] = NULL;
}

/* The slot of the request to make next: the oldest of an application that
 * was not served last, so that one with sixteen waiting does not keep the
 * others waiting for all of them; -1 when none waits. With the lock. */
static int next_pending(void)
{
    int oldest = -1, oldest_of_another = -1;
    for (int i = 0; i < PENDING_SLOTS; i++) {
        const pending_t *request = s_pending[i];
        if (request == NULL) {
            continue;
        }
        if (oldest < 0 || request->order < s_pending[oldest]->order) {
            oldest = i;
        }
        if (request->app != s_last_app_served &&
            (oldest_of_another < 0 || request->order < s_pending[oldest_of_another]->order)) {
            oldest_of_another = i;
        }
    }
    return oldest_of_another >= 0 ? oldest_of_another : oldest;
}

/* Takes the request that is next and makes it - or, with `why_not` a
 * HOST_FAILED_*, answers it with that instead. */
static bool run_pending_request(int why_not)
{
    /* Only this task makes requests, and this is too large for its stack. */
    static struct {
        int          app;
        int          number;
        int          method;
        char         url[APP_URL_MAX + 1];
        app_header_t headers[APP_HEADERS_MAX];
        int          header_count;
        uint8_t      body[HOST_HTTP_BODY_MAX];
        int          body_length;
    } taken;
    char app_name[APP_NAME_MAX + 1]; /* its own: the list may change while the request runs */
    bool is_taken = false, is_installed = false;
    lock();
    int slot = next_pending();
    if (slot >= 0) {
        const pending_t *request = s_pending[slot];
        const char      *at = request->text;
        taken.app = request->app;
        taken.number = request->number;
        taken.method = request->method;
        strlcpy(taken.url, at, sizeof taken.url);
        at += strlen(at) + 1;
        taken.header_count = request->header_count;
        for (int i = 0; i < request->header_count; i++) {
            strlcpy(taken.headers[i].name, at, sizeof taken.headers[i].name);
            at += strlen(at) + 1;
            strlcpy(taken.headers[i].value, at, sizeof taken.headers[i].value);
            at += strlen(at) + 1;
        }
        taken.body_length = request->body_length;
        memcpy(taken.body, at, (size_t)request->body_length);
        is_taken = true;
        is_installed = name_of(request->app, app_name);
        s_last_app_served = request->app;
        drop_pending(slot);
    }
    unlock();
    if (is_installed) {
        poller_ask_t ask = {
            .number = taken.number, .method = taken.method, .url = taken.url,
            .headers = taken.headers, .header_count = taken.header_count,
            .body = taken.body, .body_length = taken.body_length,
        };
        s_answering_app = (uint32_t)taken.app;
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
    size_t bytes = sizeof(pending_t) + strlen(url) + 1 + (size_t)asked->body_length;
    for (int i = 0; i < asked->header_count; i++) {
        bytes += strlen(asked->headers[i].name) + 1 + strlen(asked->headers[i].value) + 1;
    }
    int32_t result = HOST_ERR_NO_SPACE;
    lock();
    int own_waiting = 0, free_slot = -1;
    for (int i = 0; i < PENDING_SLOTS; i++) {
        if (s_pending[i] == NULL) {
            free_slot = i;
        } else if (s_pending[i]->app == app) {
            own_waiting++;
        }
    }
    pending_t *request = NULL;
    if (free_slot >= 0 && own_waiting < PENDING_PER_APP &&
        s_pending_bytes + bytes <= PENDING_BYTES_MAX && memory_has(MEMORY_FOR_A_PAGE)) {
        request = malloc(bytes);
    }
    if (request != NULL) {
        request->app = app;
        request->number = asked->tag;
        request->order = s_next_order++;
        request->queued_ms = uptime_ms();
        request->method = asked->method;
        request->header_count = asked->header_count;
        request->body_length = asked->body_length;
        request->bytes = bytes;
        char *at = request->text;
        at = stpcpy(at, url) + 1;
        for (int i = 0; i < asked->header_count; i++) {
            at = stpcpy(at, asked->headers[i].name) + 1;
            at = stpcpy(at, asked->headers[i].value) + 1;
        }
        memcpy(at, asked->body, (size_t)asked->body_length);
        s_pending[free_slot] = request;
        s_pending_bytes += bytes;
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
    for (int i = 0; i < PENDING_SLOTS && !found; i++) {
        if (s_pending[i] == NULL) {
            continue;
        }
        int64_t waited_ms = uptime_ms() - s_pending[i]->queued_ms;
        if (waited_ms > RADIO_WAIT_MS) {
            number = s_pending[i]->number;
            found = name_of(s_pending[i]->app, app_name);
            s_answering_app = (uint32_t)s_pending[i]->app;
            drop_pending(i);
        } else {
            schedule_recheck(RADIO_WAIT_MS - waited_ms + 1);
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
    for (int i = 0; i < PENDING_SLOTS && !has; i++) {
        has = s_pending[i] != NULL;
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
    /* The poller may itself be waiting for the host loop to take an answer. */
    configASSERT(!events_is_host_loop());
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

static bool has_memory_for_session(void)
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

/* A poll with "each" is a round of requests, an item of the list each,
 * made one at a time between the others: the one under way. The task's
 * alone. */
static struct {
    bool is_on;
    int  app;
    int  poll;
    int  item; /* the next to ask */
} s_round;

/* Whether anything is waiting for the network now. */
static bool wants_network(void)
{
    lock();
    bool wanted = s_fetch_job != NULL || poll_schedule_next_wanted(&s_schedule) != NULL ||
                  s_round.is_on;
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
    static int64_t held_since_ms, retry_at_ms;
    bool wanted = power_is_saving() && wants_network();
    if (wanted && !s_is_holding_radio) {
        if (uptime_ms() >= retry_at_ms) {
            power_hold_radio();
            s_is_holding_radio = true;
            held_since_ms = uptime_ms();
        } else {
            schedule_recheck(retry_at_ms - uptime_ms());
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
            retry_at_ms = uptime_ms() + (int64_t)POWER_SAVING_POLL_MIN_S * MS_PER_SECOND;
        } else {
            schedule_recheck(RADIO_WAIT_MS - waited_ms + 1);
        }
    }
}


/* Asks the poll that has waited longest, if one is wanted and can be
 * asked now - or the next item of the round under way. */
static bool run_wanted_poll(void)
{
    /* The entry may be moved by another task once the lock is gone, and the
     * list of applications read anew while the request runs: what is needed
     * of both is taken now. */
    char name[APP_NAME_MAX + 1] = "";
    lock();
    const poll_entry_t *wanted = poll_schedule_next_wanted(&s_schedule);
    bool is_round = s_round.is_on;
    int  app = is_round ? s_round.app : wanted != NULL ? wanted->app : NO_APP;
    int  poll = is_round ? s_round.poll : wanted != NULL ? wanted->poll : 0;
    int  item = is_round ? s_round.item : 0;
    bool is_installed = (is_round || wanted != NULL) && name_of(app, name);
    unlock();
    if (!is_round && wanted == NULL) {
        return false;
    }
    if (is_round && !is_installed) {
        s_round.is_on = false; /* removed meanwhile, its round with it */
        return true;
    }
    bool has_network = wifi_is_online();
    int  why_not = has_network ? 0 : HOST_FAILED_NO_NETWORK;
    if (has_network && !has_memory_for_session()) {
        if (!has_waited_for_memory_enough()) {
            schedule_recheck(MEMORY_LOOK_AGAIN_MS);
            return false;
        }
        why_not = HOST_FAILED_NO_MEMORY;
    }
    if (!has_network && s_is_holding_radio) {
        /* The radio is being brought up for this: woken when it is, and
         * looked at again if it never is. */
        schedule_recheck(RADIO_WAIT_MS);
        return false;
    }
    int items = 0;
    if (!is_round) {
        /* Taken now, before its answer: what the application does with that
         * answer - poll_now() of this same poll, when it was not ready - asks
         * for the next one, and is not to be undone when this one ends. */
        lock();
        poll_schedule_answered(&s_schedule, app, poll);
        unlock();
    }
    if (is_installed) {
        s_answering_app = (uint32_t)app;
        if (why_not == 0) {
            items = poller_net_poll(name, poll, item);
        } else {
            poller_net_poll_not_made(name, poll, why_not);
        }
    }
    /* More items to ask: the round goes on, between whatever else waits. */
    s_round.is_on = item + 1 < items;
    if (s_round.is_on) {
        s_round.app = app;
        s_round.poll = poll;
        s_round.item = item + 1;
        return true;
    }
    /* By id: if the application is gone meanwhile, so is its poll. */
    lock();
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
        s_recheck_in_ms = NEVER;
        keep_radio_for_work();
        /* Somebody is waiting for this one: before the polls. */
        if (run_fetch_job()) {
            continue;
        }
        bool has_network = wifi_is_online();
        /* Polls and requests take turns: a queue of requests does not keep
         * the polls of every application waiting, nor the other way round. */
        static bool was_request;
        if (was_request && has_network && run_wanted_poll()) {
            was_request = false;
            continue;
        }
        if (has_network && has_pending_request()) {
            if (has_memory_for_session()) {
                run_pending_request(0);
                was_request = true;
                continue;
            }
            if (has_waited_for_memory_enough()) {
                run_pending_request(HOST_FAILED_NO_MEMORY);
                continue;
            }
            schedule_recheck(MEMORY_LOOK_AGAIN_MS);
        }
        if (fail_request_without_network()) {
            continue;
        }
        if (run_wanted_poll()) {
            was_request = false;
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
    for (int i = 0; i < PENDING_SLOTS; i++) {
        char name[APP_NAME_MAX + 1];
        if (s_pending[i] != NULL && !name_of(s_pending[i]->app, name)) {
            drop_pending(i);
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
            uint32_t kept_s = user_interval_seconds(&s_known[i], poll);
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
static uint32_t user_interval_seconds(const app_manifest_t *known, int poll)
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
    return app_manifest_interval_seconds(chosen);
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
            uint32_t pace_s = user_interval_seconds(&s_known[i], poll);
            bool     is_another;
            lock();
            poll_schedule_set_interval(&s_schedule, app, poll,
                                       pace_s != 0 ? pace_s : s_known[i].poll_every_s[poll],
                                       &is_another);
            unlock();
            if (is_another) {
                ESP_LOGI(TAG, "%s/%d: every %u s by its setting", app_name, poll,
                         (unsigned)(pace_s != 0 ? pace_s : s_known[i].poll_every_s[poll]));
            }
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
