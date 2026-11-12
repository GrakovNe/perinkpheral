/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "poller.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
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
#include "host.h"
#include "launcher.h"
#include "poll_schedule.h"
#include "storage.h"
#include "text_template.h"
#include "power.h"
#include "status.h"
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
#define FREE_HEAP_NEEDED    (60 * 1024)
/* And its largest buffer is one piece of 16 KB: the sum is not enough. */
#define FREE_BLOCK_NEEDED   (17 * 1024)
#define USER_AGENT          "perinkpheral"
#define HTTP_OK_FIRST       200
#define HTTP_OK_LAST        299
#define NO_APP              0 /* no application has this id */

static poll_schedule_t       s_schedule;
/* The installed applications as the poller last took them: its own copy,
 * so that nothing here looks at a list another task is rewriting. An
 * application is its id everywhere below. */
static app_manifest_t        s_known[LAUNCHER_INSTALLED_MAX];
static int                   s_known_count;
static bool s_is_holding_radio; /* the poller's task alone: it asked for the radio and has it */
static app_alarms_t          s_alarms;
/* The schedule and the alarms are changed from other tasks too: an
 * application asks for a poll, the web page saves settings. */
static SemaphoreHandle_t     s_lock;

static void lock(void);
static void unlock(void);

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

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

#define QUEUE_FULL_RETRY_MS 50 /* the host loop is busy with the panel: not for long */

static void hand_over(const char *app_name, const app_delivery_t *delivery)
{
    handed_t handed = { .app_name = app_name, .delivery = delivery };
    while (!events_call(deliver_in_host_loop, &handed)) {
        vTaskDelay(pdMS_TO_TICKS(QUEUE_FULL_RETRY_MS));
    }
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
        http_status = HOST_FAILED_NO_ANSWER;
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
    hand_over_piece(app_name, poll_number, HOST_FAILED_NO_ANSWER, NULL, 0, HOST_DATA_FAILED);
}

/* `why` is a HOST_FAILED_*, where the HTTP status would have been.
 * `only_on_screen`: not something to load a sleeping application for. */
static void tell_failure_to(const char *app_name, int poll_number, bool wants_values, int why,
                            bool only_on_screen)
{
    app_delivery_t delivery = { .request = poll_number, .http_status = why,
                                .only_on_screen = only_on_screen };
    if (wants_values) {
        delivery.kind = APP_DELIVERY_VALUES;
    } else {
        delivery.kind = APP_DELIVERY_PIECE;
        delivery.flags = HOST_DATA_FAILED;
    }
    hand_over(app_name, &delivery);
}

static void tell_failure(const char *app_name, int poll_number, bool wants_values, int why)
{
    tell_failure_to(app_name, poll_number, wants_values, why, false);
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

void poller_network_is_back(void)
{
    note_network_use(true);
    /* What could not be asked while it was away is asked now, not when
     * each poll's turn comes round again. */
    if (s_lock != NULL) {
        lock();
        poll_schedule_hurry_all(&s_schedule, now_ms());
        unlock();
    }
}

#define REDIRECTS_MAX 3

static bool is_redirect(int http_status)
{
    return http_status == 301 || http_status == 302 || http_status == 303 ||
           http_status == 307 || http_status == 308;
}

/* Closes the connection on an answer nobody wants: what the client read
 * of its body along with the headers would otherwise come out in front of
 * the next answer. */
static void forget_answer(esp_http_client_handle_t client)
{
    esp_http_client_clear_response_buffer(client);
    esp_http_client_close(client);
}

typedef enum {
    REDIRECT_NONE,     /* the answer in hand is the answer */
    REDIRECT_FOLLOWED, /* the client is set to the new address, and closed */
    REDIRECT_REFUSED,  /* to a host the application may not ask */
} redirect_t;

/* Points the client where the answer in hand sends it. A request of the
 * application's own stays inside the manifest's "hosts"; and the headers
 * an application gave - a token, most likely - were meant for the host it
 * named, so they do not travel to another. */
static redirect_t follow_redirect(request_t *request, esp_http_client_handle_t client,
                                  const char *app_name, bool only_listed_hosts)
{
    if (esp_http_client_set_redirection(client) != ESP_OK) {
        return REDIRECT_NONE; /* no Location, or one too long to read */
    }
    char *next_url = (char *)request->piece;
    if (esp_http_client_get_url(client, next_url, sizeof request->piece) != ESP_OK) {
        return REDIRECT_REFUSED;
    }
    if (!app_manifest_is_same_host(request->url, next_url)) {
        if (only_listed_hosts) {
            char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
            bool  allowed = manifest != NULL && app_manifest_allows_url(manifest, next_url);
            free(manifest);
            if (!allowed) {
                return REDIRECT_REFUSED;
            }
        }
        for (int i = 0; i < request->poll.header_count; i++) {
            esp_http_client_delete_header(client, request->poll.headers[i].name);
        }
    }
    forget_answer(client);
    return REDIRECT_FOLLOWED;
}

/* Makes the request that request->poll describes and hands the answer
 * to the application under `number`. `only_listed_hosts`: a request of
 * the application's own, which a redirect must not take out of its
 * manifest's "hosts". */
static void perform(request_t *request, const char *app_name, int number, int method,
                    const uint8_t *body, int body_length, bool only_listed_hosts)
{
    bool wants_values = request->poll.pick_count > 0;
    esp_http_client_handle_t client = NULL;
    if (!prepare(request, app_name, method, &client)) {
        /* Most likely a setting that is not there yet: the application is
         * told, so that it can say what it is waiting for. */
        ESP_LOGI(TAG, "%s/%d: cannot be asked yet", app_name, number);
        tell_failure(app_name, number, wants_values, HOST_FAILED_NOT_READY);
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
    /* Only what can be asked again as it was: a body is not sent twice. */
    bool is_refused = false;
    for (int hop = 0; err == ESP_OK && method == HOST_HTTP_GET && is_redirect(http_status) &&
                      hop < REDIRECTS_MAX; hop++) {
        redirect_t redirect = follow_redirect(request, client, app_name, only_listed_hosts);
        if (redirect == REDIRECT_NONE) {
            break;
        }
        if (redirect == REDIRECT_REFUSED) {
            is_refused = true;
            break;
        }
        err = esp_http_client_open(client, 0);
        http_status = 0;
        if (err == ESP_OK) {
            esp_http_client_fetch_headers(client);
            http_status = esp_http_client_get_status_code(client);
        }
    }
    if (is_refused) {
        ESP_LOGW(TAG, "%s/%d: redirected to a host its manifest does not list", app_name, number);
        tell_failure(app_name, number, wants_values, HOST_FAILED_NOT_ALLOWED);
    } else if (err != ESP_OK || http_status <= 0) {
        ESP_LOGW(TAG, "%s/%d: no answer: %s", app_name, number, esp_err_to_name(err));
        bool has_network = wifi_is_online();
        note_network_use(false);
        tell_failure(app_name, number, wants_values,
                     has_network ? HOST_FAILED_NO_ANSWER : HOST_FAILED_NO_NETWORK);
    } else {
        note_network_use(true);
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

/* The poll's time has come and there is no network. The application is
 * told if it is on the screen - it has something to show for it - and a
 * sleeping one is left asleep; the poll waits for its next turn, or for
 * the network. */
static void skip_poll_without_network(const char *app_name, int poll_number)
{
    request_t *request = calloc(1, sizeof *request);
    if (request == NULL) {
        return;
    }
    if (load_poll(request, app_name, poll_number)) {
        note_network_use(false);
        tell_failure_to(app_name, poll_number, request->poll.pick_count > 0,
                        HOST_FAILED_NO_NETWORK, true);
    }
    free(request);
}

static void run_poll(const char *app_name, int poll_number)
{
    request_t *request = calloc(1, sizeof *request);
    if (request == NULL) {
        ESP_LOGW(TAG, "%s/%d: no memory for the request", app_name, poll_number);
        return;
    }
    if (load_poll(request, app_name, poll_number)) {
        perform(request, app_name, poll_number, HOST_HTTP_GET, NULL, 0, false);
    }
    free(request);
}

/* ---- requests the applications make themselves ---- */

#define PENDING_MAX         2 /* for the whole device */

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

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

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

/* Makes the oldest request an application queued, if there is one. */
static bool run_pending_request(void)
{
    static uint8_t body[HOST_HTTP_BODY_MAX]; /* only this task makes requests */
    int app = NO_APP, number = 0, method = 0, body_length = 0;
    char app_name[APP_NAME_MAX + 1]; /* its own: the list may change while the request runs */
    lock();
    pending_t *oldest = NULL;
    for (int i = 0; i < PENDING_MAX; i++) {
        if (s_pending[i].is_waiting && (oldest == NULL || s_pending[i].order < oldest->order)) {
            oldest = &s_pending[i];
        }
    }
    /* Nothing waiting is the usual case, and costs no memory. */
    request_t *request = oldest != NULL ? calloc(1, sizeof *request) : NULL;
    if (request != NULL) {
        app = name_of(oldest->app, app_name) ? oldest->app : NO_APP;
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
        perform(request, app_name, number, method, body, body_length, true);
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
        free_slot->number = asked->tag;
        free_slot->order = s_next_order++;
        free_slot->queued_ms = now_ms();
        free_slot->method = asked->method;
        strcpy(free_slot->url, asked->url);
        memcpy(free_slot->headers, asked->headers, sizeof free_slot->headers);
        free_slot->header_count = asked->header_count;
        free_slot->body_length = asked->body_length;
        memcpy(free_slot->body, asked->body, (size_t)asked->body_length);
        free_slot->is_waiting = true;
        result = HOST_OK;
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
    for (int hop = 0; hop <= REDIRECTS_MAX; hop++) {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            break;
        }
        esp_http_client_fetch_headers(client);
        job->http_status = esp_http_client_get_status_code(client);
        if (!is_redirect(job->http_status) || esp_http_client_set_redirection(client) != ESP_OK) {
            break;
        }
        forget_answer(client);
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
    int  app = NO_APP, alarm_number = 0;
    lock();
    bool is_due = app_alarms_take_due(&s_alarms, now_ms(), &app, &alarm_number) &&
                  name_of(app, app_name);
    unlock();
    if (!is_due) {
        return false;
    }
    app_delivery_t alarm = { .kind = APP_DELIVERY_ALARM, .request = alarm_number };
    hand_over(app_name, &alarm);
    return true;
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
        if (s_pending[i].is_waiting && now_ms() - s_pending[i].queued_ms > RADIO_WAIT_MS) {
            number = s_pending[i].number;
            s_pending[i].is_waiting = false;
            found = name_of(s_pending[i].app, app_name);
        }
    }
    unlock();
    if (found) {
        ESP_LOGW(TAG, "%s/%d: no network", app_name, number);
        note_network_use(false);
        tell_failure(app_name, number, false, HOST_FAILED_NO_NETWORK);
    }
    return found;
}

static bool has_memory_for_a_session(void)
{
    return esp_get_free_heap_size() >= FREE_HEAP_NEEDED &&
           heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) >= FREE_BLOCK_NEEDED;
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
    static int64_t held_since_ms, try_again_at_ms;
    bool wanted = power_is_saving() && wants_network();
    if (wanted && !s_is_holding_radio && now_ms() >= try_again_at_ms) {
        power_hold_radio();
        s_is_holding_radio = true;
        held_since_ms = now_ms();
    } else if (s_is_holding_radio && !wanted) {
        power_release_radio();
        s_is_holding_radio = false;
    } else if (s_is_holding_radio && !wifi_is_online() && now_ms() - held_since_ms > RADIO_WAIT_MS) {
        ESP_LOGW(TAG, "no network in %d s: the radio is given back", RADIO_WAIT_MS / 1000);
        power_release_radio();
        s_is_holding_radio = false;
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
        bool can_ask = wifi_is_online() && has_memory_for_a_session();
        if (can_ask && run_pending_request()) {
            continue;
        }
        if (fail_request_without_network()) {
            continue;
        }
        lock();
        uint32_t            wait_ms;
        const poll_entry_t *due = poll_schedule_due(&s_schedule, now_ms(), &wait_ms);
        /* The entry may be moved by another task once the lock is gone:
         * what is needed of it is taken now. */
        int due_app = due != NULL ? due->app : NO_APP, due_poll = due != NULL ? due->poll : 0;
        /* And the list of applications may be read anew while the request
         * runs: the name is taken now. */
        char due_name[APP_NAME_MAX + 1] = "";
        if (due != NULL && !name_of(due_app, due_name)) {
            due = NULL; /* not installed any more: its poll goes at the next reload */
        }
        unlock();
        if (due == NULL) {
            uint32_t idle_ms = power_is_saving() ? IDLE_CHECK_SAVING_MS : IDLE_CHECK_MS;
            vTaskDelay(pdMS_TO_TICKS(wait_ms < idle_ms ? wait_ms : idle_ms));
            continue;
        }
        /* The radio is being brought up for this, or there is no memory for
         * a session just now: the poll stays due and is looked at again in
         * a moment. */
        bool has_network = wifi_is_online();
        if (has_network ? !has_memory_for_a_session() : s_is_holding_radio) {
            vTaskDelay(pdMS_TO_TICKS(IDLE_CHECK_MS));
            continue;
        }
        if (has_network) {
            run_poll(due_name, due_poll);
        } else {
            skip_poll_without_network(due_name, due_poll);
        }
        lock();
        /* By id: if the application is gone meanwhile, so is its poll. */
        poll_schedule_finished(&s_schedule, due_app, due_poll, now_ms());
        unlock();
    }
}

/* Takes the installed applications as the launcher has them now. What
 * was scheduled, queued or set for an application that is still there
 * stays as it is; what was for one that is gone goes; the polls of a new
 * one are scheduled. Returns the polls there are. With the lock held, in
 * the host loop or before the poller's task. */
static int schedule_installed(void)
{
    int count;
    const app_manifest_t *installed = launcher_installed(&count);
    s_known_count = count < LAUNCHER_INSTALLED_MAX ? count : LAUNCHER_INSTALLED_MAX;
    memcpy(s_known, installed, (size_t)s_known_count * sizeof s_known[0]);

    int ids[LAUNCHER_INSTALLED_MAX];
    for (int i = 0; i < s_known_count; i++) {
        ids[i] = (int)s_known[i].id;
    }
    poll_schedule_keep_only(&s_schedule, ids, s_known_count);
    app_alarms_keep_only(&s_alarms, ids, s_known_count);
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
            if (!poll_schedule_has(&s_schedule, ids[i], poll)) {
                poll_schedule_add(&s_schedule, ids[i], poll, s_known[i].poll_every_s[poll],
                                  now_ms());
            }
        }
    }
    return s_schedule.count;
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
}

void poller_clock_moved(int64_t delta_ms)
{
    if (s_lock == NULL) {
        return;
    }
    lock();
    app_alarms_clock_moved(&s_alarms, delta_ms);
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
    ESP_LOGI(TAG, "applications changed: %d polls of %d applications", polls, s_known_count);
}

void poller_start(void)
{
    poll_schedule_init(&s_schedule);
    app_alarms_init(&s_alarms);
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

bool poller_alarm_set(const char *app_name, int alarm, int64_t when, bool is_a_time)
{
    int app = app_number(app_name);
    if (app == NO_APP) {
        return false;
    }
    lock();
    bool set = true;
    if (!is_a_time && when == 0) {
        app_alarms_cancel(&s_alarms, app, alarm);
    } else {
        /* A time of day is so far from now by the wall clock, and is kept
         * on the clock that only goes forward - with a note that it moves
         * when the wall clock is set. */
        int64_t in_ms = is_a_time ? (when - (int64_t)time(NULL)) * 1000 : when * 1000;
        set = app_alarms_set(&s_alarms, app, alarm, now_ms() + in_ms, is_a_time);
    }
    unlock();
    return set;
}
