/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "poller_net.h"

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_kv.h"
#include "app_manifest.h"
#include "app_store.h"
#include "host_api.h"
#include "json_pick.h"
#include "storage.h"
#include "text_template.h"
#include "wifi.h"

/* The part of the poller that talks to the network: one request at a
 * time, on the poller's task. What it learns goes to the host through
 * the two calls it was given, and it knows nothing of schedules. */

static const char *TAG = "poller";

#define REQUEST_TIMEOUT_MS  15000
#define REQUEST_WITH_REDIRECTS_MS (2 * REQUEST_TIMEOUT_MS) /* the request and all it is sent on to */
#define ANSWER_MAX_BYTES    (256 * 1024)
#define USER_AGENT          "perinkpheral"
#define HTTP_OK_FIRST       200
#define HTTP_OK_LAST        299

static const poller_net_host_t *s_host;

void poller_net_start(const poller_net_host_t *host)
{
    s_host = host;
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void hand_over(const char *app_name, const app_delivery_t *delivery)
{
    s_host->deliver(app_name, delivery);
}

static void note_network_use(bool worked)
{
    s_host->network_worked(worked);
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
bool poller_net_setting_of(void *context, const char *key, char *value, size_t value_size)
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
    if (!text_template_fill_url(request->poll.url, poller_net_setting_of, (void *)app_name, request->url,
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
        if (!text_template_fill(header->value, poller_net_setting_of, (void *)app_name,
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
            bool  allowed = manifest != NULL && app_manifest_allows_url(manifest, next_url, poller_net_setting_of,
                                                      (void *)app_name);
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
        /* The hops share the time one request has: a chain of slow servers
         * is not waited for four times over, with the radio held. */
        int64_t left_ms = REQUEST_WITH_REDIRECTS_MS - (now_ms() - started_ms);
        if (left_ms <= 0) {
            err = ESP_ERR_TIMEOUT;
            break;
        }
        if (left_ms < REQUEST_TIMEOUT_MS) {
            esp_http_client_set_timeout_ms(client, (int)left_ms);
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

void poller_net_poll_without_network(const char *app_name, int poll_number)
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

void poller_net_poll(const char *app_name, int poll_number)
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

void poller_net_request(const char *app_name, const poller_ask_t *ask)
{
    request_t *request = calloc(1, sizeof *request);
    if (request == NULL) {
        ESP_LOGW(TAG, "%s/%d: no memory for the request", app_name, ask->number);
        tell_failure(app_name, ask->number, false, HOST_FAILED_NO_ANSWER);
        return;
    }
    /* A request is a poll nobody scheduled: the same URL and headers. */
    strlcpy(request->poll.url, ask->url, sizeof request->poll.url);
    memcpy(request->poll.headers, ask->headers,
           (size_t)ask->header_count * sizeof request->poll.headers[0]);
    request->poll.header_count = ask->header_count;
    perform(request, app_name, ask->number, ask->method, ask->body, ask->body_length, true);
    free(request);
}

void poller_net_tell_failure(const char *app_name, int number, int why)
{
    tell_failure(app_name, number, false, why);
}

#define FETCH_HEADER_BYTES  4096
bool poller_net_fetch(const char *url, size_t max_bytes, poller_net_take_fn take, void *context,
                      int *http_status)
{
    *http_status = 0;
    static uint8_t piece[HOST_DATA_CHUNK_MAX]; /* only this task fetches */
    esp_http_client_config_t config = {
        .url = url,
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
        return false;
    }
    for (int hop = 0; hop <= REDIRECTS_MAX; hop++) {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            break;
        }
        esp_http_client_fetch_headers(client);
        *http_status = esp_http_client_get_status_code(client);
        if (!is_redirect(*http_status) || esp_http_client_set_redirection(client) != ESP_OK) {
            break;
        }
        forget_answer(client);
    }
    bool   is_ok = *http_status >= HTTP_OK_FIRST && *http_status <= HTTP_OK_LAST;
    size_t total = 0;
    int    length = 0;
    while (is_ok && (length = read_piece(client, piece)) > 0) {
        total += (size_t)length;
        if (total > max_bytes || !take(context, piece, (size_t)length)) {
            is_ok = false;
        }
    }
    esp_http_client_cleanup(client);
    return is_ok && length == 0;
}

