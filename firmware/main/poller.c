/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "poller.h"

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/task.h"

#include "app_kv.h"
#include "app_manifest.h"
#include "app_store.h"
#include "events.h"
#include "host_api.h"
#include "json_pick.h"
#include "launcher.h"
#include "poll_schedule.h"
#include "storage.h"
#include "text_template.h"
#include "power.h"
#include "wifi.h"

static const char *TAG = "poller";

#define TASK_STACK_BYTES    8192 /* the TLS handshake runs on it */
#define TASK_PRIORITY       4
#define IDLE_CHECK_MS       250
/* Saving power, every look is the chip waking up: fewer of them. */
#define IDLE_CHECK_SAVING_MS 2000
#define RADIO_WAIT_MS       20000 /* for the network to be joined before the radio is given back */
#define REQUEST_TIMEOUT_MS  15000
#define ANSWER_MAX_BYTES    (256 * 1024)
/* A TLS session takes about 45 KB while it is open; below this the
 * request waits for a better moment instead of taking the device down. */
#define FREE_HEAP_NEEDED    (70 * 1024)
#define USER_AGENT          "perinkpheral"
#define HTTP_OK_FIRST       200
#define HTTP_OK_LAST        299
#define NO_APP              (-1)

static poll_schedule_t       s_schedule;
static const app_manifest_t *s_apps;
static int                   s_app_count;
static app_alarms_t          s_alarms;
static uint32_t              s_generation; /* of the list of applications: goes up when it is read anew */
/* The schedule and the alarms are changed from other tasks too: an
 * application asks for a poll, the web page saves settings. */
static SemaphoreHandle_t     s_lock;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void poller_hand_over(const char *app_name, const app_delivery_t *delivery)
{
    /* A semaphore of its own for every hand-over: the poller and the web
     * server may both be waiting, each for its own. */
    SemaphoreHandle_t handed_over = xSemaphoreCreateBinary();
    if (handed_over == NULL) {
        ESP_LOGW(TAG, "%s: no memory to hand anything over", app_name);
        return;
    }
    poller_delivery_t handed = { .app_name = app_name, .delivery = *delivery,
                                 .handed_over = handed_over };
    event_t event = { .kind = EVENT_APP_DELIVERY, .delivery = &handed };
    while (!events_post(&event)) {
        vTaskDelay(pdMS_TO_TICKS(50)); /* the queue is full: the host is busy with the panel */
    }
    xSemaphoreTake(handed_over, portMAX_DELAY);
    vSemaphoreDelete(handed_over);
}

static void hand_over(const char *app_name, const app_delivery_t *delivery)
{
    poller_hand_over(app_name, delivery);
}

static void hand_over_values(const char *app_name, int request, int http_status,
                             const char *const *values, int count)
{
    app_delivery_t delivery = { .kind = APP_DELIVERY_VALUES, .request = request,
                                .http_status = http_status, .values = values,
                                .value_count = count };
    hand_over(app_name, &delivery);
}

static void hand_over_piece(const char *app_name, int request, int http_status,
                            const uint8_t *data, size_t length, int flags)
{
    app_delivery_t delivery = { .kind = APP_DELIVERY_PIECE, .request = request,
                                .http_status = http_status, .data = data, .length = length,
                                .flags = flags };
    hand_over(app_name, &delivery);
}

/* The settings of an application are values in its own store; a poll's
 * {{key}} is one of them. */
static bool setting_of(void *context, const char *key, char *value, size_t value_size)
{
    const char *app_name = context;
    int32_t length = app_kv_get(app_name, key, value, (int32_t)value_size - 1);
    if (length < 0) {
        return false;
    }
    value[length] = '\0';
    return true;
}

/* Everything one request needs, in one block: none of it fits a stack. */
typedef struct {
    app_poll_t poll;
    char       url[APP_URL_MAX + 1];
    char       header_value[APP_HEADER_VALUE_MAX + 1];
    const char *paths[JSON_PICK_PATHS_MAX];
    const char *values[JSON_PICK_PATHS_MAX];
    json_pick_t pick;
    uint8_t     piece[HOST_DATA_CHUNK_MAX];
    uint8_t     next_piece[HOST_DATA_CHUNK_MAX];
} request_t;

/* Reads poll number `poll_number` of the manifest into the request. */
static bool load_poll(request_t *request, const char *app_name, int poll_number)
{
    char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
    bool  parsed = manifest != NULL && app_manifest_parse_poll(manifest, poll_number,
                                                               &request->poll) == APP_MANIFEST_OK;
    free(manifest);
    return parsed;
}

static const esp_http_client_method_t METHODS[] = {
    [HOST_HTTP_GET] = HTTP_METHOD_GET,
    [HOST_HTTP_POST] = HTTP_METHOD_POST,
    [HOST_HTTP_PUT] = HTTP_METHOD_PUT,
    [HOST_HTTP_DELETE] = HTTP_METHOD_DELETE,
};

/* The request of request->poll with the settings put in. False when a
 * setting it needs is not there yet. */
static bool prepare(request_t *request, const char *app_name, int method,
                    esp_http_client_handle_t *client)
{
    if (!text_template_fill_url(request->poll.url, setting_of, (void *)app_name, request->url,
                                sizeof request->url)) {
        return false;
    }
    esp_http_client_config_t config = {
        .url = request->url,
        .method = METHODS[method],
        .timeout_ms = REQUEST_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = USER_AGENT,
        .buffer_size = HOST_DATA_CHUNK_MAX,
    };
    *client = esp_http_client_init(&config);
    if (*client == NULL) {
        return false;
    }
    for (int i = 0; i < request->poll.header_count; i++) {
        const app_header_t *header = &request->poll.headers[i];
        if (!text_template_fill(header->value, setting_of, (void *)app_name,
                                request->header_value, sizeof request->header_value)) {
            esp_http_client_cleanup(*client);
            *client = NULL;
            return false;
        }
        esp_http_client_set_header(*client, header->name, request->header_value);
    }
    return true;
}

/* Up to a whole piece, however the network cuts it; less only at the end.
 * Negative when the connection broke. */
static int read_piece(esp_http_client_handle_t client, uint8_t *piece)
{
    int length = 0;
    while (length < HOST_DATA_CHUNK_MAX) {
        int count = esp_http_client_read(client, (char *)piece + length,
                                         HOST_DATA_CHUNK_MAX - length);
        if (count < 0) {
            return count;
        }
        if (count == 0) {
            break;
        }
        length += count;
    }
    return length;
}

/* The answer is read to its end, the connection closed, and only then
 * is the application called: off the screen it would have to be loaded
 * next to an open TLS session otherwise. */
static void fetch_values(request_t *request, esp_http_client_handle_t client,
                         const char *app_name, int poll_number, int http_status)
{
    int path_count = request->poll.pick_count;
    for (int i = 0; i < path_count; i++) {
        request->paths[i] = request->poll.pick[i];
    }
    json_pick_init(&request->pick, request->paths, path_count);
    size_t total = 0;
    int    length;
    bool   is_whole = true;
    while ((length = read_piece(client, request->piece)) > 0) {
        total += (size_t)length;
        if (total > ANSWER_MAX_BYTES || !json_pick_feed(&request->pick, (char *)request->piece,
                                                        (size_t)length)) {
            is_whole = false;
            break;
        }
    }
    json_pick_finish(&request->pick);
    esp_http_client_close(client);
    if (length < 0 || !is_whole) {
        /* Broke off, too long, or not JSON: what was read so far is not
         * an answer, and half of one must not pass for a whole one. */
        http_status = 0;
    }
    for (int i = 0; i < path_count; i++) {
        request->values[i] = json_pick_value(&request->pick, i);
    }
    hand_over_values(app_name, poll_number, http_status, request->values, path_count);
}

/* The answer as it comes. A piece is handed over once the next one is in
 * hand, so that the last one can be told to be the last. */
static void fetch_pieces(request_t *request, esp_http_client_handle_t client,
                         const char *app_name, int poll_number, int http_status)
{
    uint8_t *piece = request->piece, *next = request->next_piece;
    int      length = read_piece(client, piece);
    int      flags = HOST_DATA_FIRST;
    size_t   total = 0;
    while (length >= 0) {
        total += (size_t)length;
        int next_length = length == HOST_DATA_CHUNK_MAX ? read_piece(client, next) : 0;
        if (next_length < 0 || total > ANSWER_MAX_BYTES) {
            break;
        }
        if (next_length == 0) {
            flags |= HOST_DATA_LAST;
        }
        hand_over_piece(app_name, poll_number, http_status, piece, (size_t)length, flags);
        if (flags & HOST_DATA_LAST) {
            return;
        }
        flags = 0;
        uint8_t *swapped = piece;
        piece = next;
        next = swapped;
        length = next_length;
    }
    hand_over_piece(app_name, poll_number, 0, NULL, 0, HOST_DATA_FAILED);
}

static void tell_failure(const request_t *request, const char *app_name, int poll_number,
                         bool wants_values)
{
    if (wants_values) {
        hand_over_values(app_name, poll_number, 0, NULL, 0);
    } else {
        hand_over_piece(app_name, poll_number, 0, NULL, 0, HOST_DATA_FAILED);
    }
    (void)request;
}

/* Makes the request that request->poll describes and hands the answer
 * to the application under `number`. */
static void perform(request_t *request, const char *app_name, int number, int method,
                    const uint8_t *body, int body_length)
{
    bool wants_values = request->poll.pick_count > 0;
    esp_http_client_handle_t client = NULL;
    if (!prepare(request, app_name, method, &client)) {
        /* Most likely a setting that is not there yet: the application is
         * told, so that it can say what it is waiting for. */
        ESP_LOGI(TAG, "%s/%d: cannot be asked yet", app_name, number);
        tell_failure(request, app_name, number, wants_values);
        return;
    }
    int64_t   started_ms = now_ms();
    esp_err_t err = esp_http_client_open(client, body_length);
    if (err == ESP_OK && body_length > 0 &&
        esp_http_client_write(client, (const char *)body, body_length) != body_length) {
        err = ESP_FAIL;
    }
    int http_status = 0;
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        http_status = esp_http_client_get_status_code(client);
    }
    if (err != ESP_OK || http_status == 0) {
        ESP_LOGW(TAG, "%s/%d: no answer: %s", app_name, number, esp_err_to_name(err));
        tell_failure(request, app_name, number, wants_values);
    } else {
        if (wants_values) {
            fetch_values(request, client, app_name, number, http_status);
        } else {
            fetch_pieces(request, client, app_name, number, http_status);
        }
        bool is_ok = http_status >= HTTP_OK_FIRST && http_status <= HTTP_OK_LAST;
        ESP_LOG_LEVEL(is_ok ? ESP_LOG_INFO : ESP_LOG_WARN, TAG, "%s/%d: HTTP %d in %d ms",
                      app_name, number, http_status, (int)(now_ms() - started_ms));
    }
    esp_http_client_cleanup(client);
}

static void run_poll(const char *app_name, int poll_number)
{
    request_t *request = calloc(1, sizeof *request);
    if (request == NULL) {
        ESP_LOGW(TAG, "%s/%d: no memory for the request", app_name, poll_number);
        return;
    }
    if (load_poll(request, app_name, poll_number)) {
        perform(request, app_name, poll_number, HOST_HTTP_GET, NULL, 0);
    }
    free(request);
}

/* ---- requests the applications make themselves ---- */

#define PENDING_MAX 4 /* for the whole device */

typedef struct {
    bool         is_waiting;
    int          app;
    int          number; /* what the answer is delivered under */
    int          method;
    char         url[APP_URL_MAX + 1];
    app_header_t headers[APP_HEADERS_MAX];
    int          header_count;
    uint8_t      body[HOST_HTTP_BODY_MAX];
    int          body_length;
} pending_t;

static pending_t s_pending[PENDING_MAX];
static int       s_next_number = HOST_REQUEST_FIRST;

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

/* The application's number as the schedule counts them, or NO_APP. */
static int app_number(const char *app_name)
{
    if (s_lock == NULL) {
        return NO_APP; /* asked before the poller started: nothing is scheduled yet */
    }
    for (int app = 0; app < s_app_count; app++) {
        if (strcmp(s_apps[app].name, app_name) == 0) {
            return app;
        }
    }
    return NO_APP;
}

/* Makes the oldest request an application queued, if there is one. */
static bool run_pending_request(void)
{
    static uint8_t body[HOST_HTTP_BODY_MAX]; /* only this task makes requests */
    int app = NO_APP, number = 0, method = 0, body_length = 0;
    char app_name[APP_NAME_MAX + 1]; /* its own: the list may change while the request runs */
    lock();
    pending_t *oldest = NULL;
    for (int i = 0; i < PENDING_MAX; i++) {
        if (s_pending[i].is_waiting && (oldest == NULL || s_pending[i].number < oldest->number)) {
            oldest = &s_pending[i];
        }
    }
    /* Nothing waiting is the usual case, and costs no memory. */
    request_t *request = oldest != NULL ? calloc(1, sizeof *request) : NULL;
    if (request != NULL) {
        app = oldest->app;
        strcpy(app_name, s_apps[app].name);
        number = oldest->number;
        method = oldest->method;
        body_length = oldest->body_length;
        memcpy(body, oldest->body, (size_t)body_length);
        strcpy(request->poll.url, oldest->url);
        memcpy(request->poll.headers, oldest->headers, sizeof oldest->headers);
        request->poll.header_count = oldest->header_count;
        oldest->is_waiting = false;
    }
    unlock();
    if (app != NO_APP) {
        perform(request, app_name, number, method, body, body_length);
    }
    free(request);
    return app != NO_APP;
}

int32_t poller_request(const char *app_name, const app_request_t *asked)
{
    int app = app_number(app_name);
    if (app == NO_APP) {
        return HOST_ERR_FAILED;
    }
    /* Only where the manifest said the application would go. */
    char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
    bool  allowed = manifest != NULL && app_manifest_allows_url(manifest, asked->url);
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
        free_slot->number = s_next_number++;
        free_slot->method = asked->method;
        strcpy(free_slot->url, asked->url);
        memcpy(free_slot->headers, asked->headers, sizeof free_slot->headers);
        free_slot->header_count = asked->header_count;
        free_slot->body_length = asked->body_length;
        memcpy(free_slot->body, asked->body, (size_t)asked->body_length);
        free_slot->is_waiting = true;
        result = free_slot->number;
    }
    unlock();
    return result;
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

#define FETCH_HEADER_BYTES  4096
#define FETCH_REDIRECTS_MAX 3

static void do_fetch(fetch_job_t *job)
{
    static uint8_t piece[HOST_DATA_CHUNK_MAX]; /* only this task fetches */
    esp_http_client_config_t config = {
        .url = job->url,
        .timeout_ms = REQUEST_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = USER_AGENT,
        /* A file of a release is behind a redirect to an address a kilobyte
         * and more long, and a header has to fit here in one piece. */
        .buffer_size = FETCH_HEADER_BYTES,
        /* And the request for that address has to fit into this one. */
        .buffer_size_tx = FETCH_HEADER_BYTES,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return;
    }
    for (int hop = 0; hop <= FETCH_REDIRECTS_MAX; hop++) {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            break;
        }
        esp_http_client_fetch_headers(client);
        job->http_status = esp_http_client_get_status_code(client);
        bool is_redirect = job->http_status == 301 || job->http_status == 302 ||
                           job->http_status == 303 || job->http_status == 307 ||
                           job->http_status == 308;
        if (!is_redirect || esp_http_client_set_redirection(client) != ESP_OK) {
            break;
        }
        esp_http_client_close(client);
    }
    bool   is_ok = job->http_status >= HTTP_OK_FIRST && job->http_status <= HTTP_OK_LAST;
    size_t total = 0;
    int    length = 0;
    while (is_ok && (length = read_piece(client, piece)) > 0) {
        total += (size_t)length;
        if (total > job->max_bytes || !job->take(job->context, piece, (size_t)length)) {
            is_ok = false;
        }
    }
    job->is_whole = is_ok && length == 0;
    esp_http_client_cleanup(client);
}

static bool run_fetch_job(void)
{
    lock();
    fetch_job_t *job = s_fetch_job;
    s_fetch_job = NULL;
    unlock();
    if (job == NULL) {
        return false;
    }
    do_fetch(job);
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
    xSemaphoreTake(s_fetch_done, portMAX_DELAY);
    xSemaphoreGive(s_fetch_turn);
    *http_status = job.http_status;
    return job.is_whole;
}

/* An alarm that has come is handed to its application like an answer. */
static bool ring_due_alarm(void)
{
    char app_name[APP_NAME_MAX + 1];
    lock();
    int app = app_alarms_take_due(&s_alarms, now_ms());
    if (app >= 0) {
        strcpy(app_name, s_apps[app].name);
    }
    unlock();
    if (app < 0) {
        return false;
    }
    app_delivery_t alarm = { .kind = APP_DELIVERY_ALARM };
    poller_hand_over(app_name, &alarm);
    return true;
}

/* Whether anything is waiting for the network now. */
static bool wants_network(void)
{
    lock();
    uint32_t wait_ms;
    bool wanted = s_fetch_job != NULL || poll_schedule_due(&s_schedule, now_ms(), &wait_ms) != NULL;
    for (int i = 0; i < PENDING_MAX && !wanted; i++) {
        wanted = s_pending[i].is_waiting;
    }
    unlock();
    return wanted;
}

/* Saving power, the radio is on only while there is something to ask:
 * taken when there is, given back when there is not. A network that does
 * not answer is not waited for with the radio on: the next try is a poll's
 * least distance away. */
static void keep_radio_for_work(void)
{
    static bool    is_holding;
    static int64_t held_since_ms, try_again_at_ms;
    bool wanted = power_is_saving() && wants_network();
    if (wanted && !is_holding && now_ms() >= try_again_at_ms) {
        power_hold_radio();
        is_holding = true;
        held_since_ms = now_ms();
    } else if (is_holding && !wanted) {
        power_release_radio();
        is_holding = false;
    } else if (is_holding && !wifi_is_online() && now_ms() - held_since_ms > RADIO_WAIT_MS) {
        ESP_LOGW(TAG, "no network in %d s: the radio is given back", RADIO_WAIT_MS / 1000);
        power_release_radio();
        is_holding = false;
        try_again_at_ms = now_ms() + (int64_t)POWER_SAVING_POLL_MIN_S * 1000;
    }
}

static void poller_task(void *arg)
{
    (void)arg;
    for (;;) {
        keep_radio_for_work();
        if (ring_due_alarm()) {
            continue;
        }
        /* Somebody is waiting for this one: before the polls. */
        if (run_fetch_job()) {
            continue;
        }
        bool can_ask = wifi_is_online() && esp_get_free_heap_size() >= FREE_HEAP_NEEDED;
        if (can_ask && run_pending_request()) {
            continue;
        }
        lock();
        uint32_t            wait_ms;
        const poll_entry_t *due = poll_schedule_due(&s_schedule, now_ms(), &wait_ms);
        /* The entry may be moved by another task once the lock is gone:
         * what is needed of it is taken now. */
        int due_app = due != NULL ? due->app : NO_APP, due_poll = due != NULL ? due->poll : 0;
        /* And the list of applications may be read anew while the request
         * runs: the name is kept, and the numbers are good only for the
         * list they were taken from. */
        char     due_name[APP_NAME_MAX + 1] = "";
        uint32_t generation = s_generation;
        if (due != NULL) {
            strcpy(due_name, s_apps[due_app].name);
        }
        unlock();
        if (due == NULL) {
            uint32_t idle_ms = power_is_saving() ? IDLE_CHECK_SAVING_MS : IDLE_CHECK_MS;
            vTaskDelay(pdMS_TO_TICKS(wait_ms < idle_ms ? wait_ms : idle_ms));
            continue;
        }
        /* Without a network, or without the memory for a session, the
         * poll stays due and is looked at again in a moment. */
        if (!wifi_is_online() || esp_get_free_heap_size() < FREE_HEAP_NEEDED) {
            vTaskDelay(pdMS_TO_TICKS(IDLE_CHECK_MS));
            continue;
        }
        run_poll(due_name, due_poll);
        lock();
        if (generation == s_generation) {
            poll_schedule_finished(&s_schedule, due_app, due_poll, now_ms());
        }
        unlock();
    }
}

/* Takes the installed applications as the launcher has them now and
 * schedules their polls; what was scheduled, queued or set before is for
 * applications that may be gone. Returns the polls scheduled. */
static int schedule_installed(void)
{
    s_apps = launcher_installed(&s_app_count);
    s_generation++;
    poll_schedule_init(&s_schedule);
    app_alarms_init(&s_alarms);
    memset(s_pending, 0, sizeof s_pending);
    int polls = 0;
    for (int app = 0; app < s_app_count; app++) {
        for (int poll = 0; poll < s_apps[app].poll_count; poll++) {
            /* From a battery no poll is closer to the last than this. */
            uint32_t every_s = s_apps[app].poll_every_s[poll];
            if (power_is_saving() && every_s < POWER_SAVING_POLL_MIN_S) {
                every_s = POWER_SAVING_POLL_MIN_S;
            }
            if (poll_schedule_add(&s_schedule, app, poll, every_s, now_ms())) {
                polls++;
            }
        }
    }
    return polls;
}

void poller_reschedule(void)
{
    if (s_lock == NULL) {
        return;
    }
    lock();
    schedule_installed();
    unlock();
}

void poller_hold(void)
{
    lock();
}

void poller_release_reloaded(void)
{
    int polls = schedule_installed();
    unlock();
    ESP_LOGI(TAG, "applications changed: %d polls of %d applications", polls, s_app_count);
}

void poller_start(void)
{
    int polls = schedule_installed();
    s_fetch_done = xSemaphoreCreateBinary();
    s_fetch_turn = xSemaphoreCreateMutex();
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL && s_fetch_done != NULL && s_fetch_turn != NULL);
    /* Started even with nothing to fetch: alarms are rung from here too. */
    BaseType_t ok = xTaskCreate(poller_task, "poller", TASK_STACK_BYTES, NULL, TASK_PRIORITY,
                                NULL);
    configASSERT(ok == pdPASS);
    ESP_LOGI(TAG, "%d polls of the installed applications are scheduled", polls);
}

void poller_hurry(const char *app_name)
{
    int app = app_number(app_name);
    if (app != NO_APP) {
        lock();
        poll_schedule_hurry(&s_schedule, app, now_ms());
        unlock();
    }
}

bool poller_poll_now(const char *app_name, int poll)
{
    int app = app_number(app_name);
    if (app == NO_APP) {
        return false;
    }
    lock();
    bool found = poll_schedule_hurry_one(&s_schedule, app, poll, now_ms());
    unlock();
    return found;
}

bool poller_set_interval(const char *app_name, int poll, uint32_t every_s)
{
    int app = app_number(app_name);
    if (app == NO_APP) {
        return false;
    }
    lock();
    bool found = poll_schedule_set_interval(&s_schedule, app, poll, every_s, now_ms());
    unlock();
    return found;
}

bool poller_alarm_set(const char *app_name, uint32_t seconds)
{
    int app = app_number(app_name);
    if (app == NO_APP) {
        return false;
    }
    lock();
    bool set = app_alarms_set(&s_alarms, app, seconds, now_ms());
    unlock();
    return set;
}
