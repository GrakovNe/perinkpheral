/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The device without the device: one application, run by the firmware's
 * own interpreter, host API, kit and window manager, on a computer. The
 * panel is a frame in memory, the buttons and the screen are the device's
 * development API (/api/dev) on localhost, the store and the files are
 * folders, the network is the computer's - asked through
 * tools/simulator/fetch.py, so that this program needs no HTTP library -
 * and answers can be replaced by mocks.
 *
 *     perinkpheral-sim --apps <folder> --app <name> --state <folder>
 *                      [--port 8401] [--language en] [--font departure|jersey]
 *                      [--glyphs <folder>] [--mocks <file.json>] [--clock]
 *                      [--python python3] [--fetcher <tools/simulator/fetch.py>]
 *
 * <apps>/<name> holds the application as mkapp builds it: app.wasm,
 * manifest.json and its files. <state> keeps what the application stores.
 *
 * Beyond the device's API, /api/sim: background and foreground (the
 * application leaves the screen and is loaded only for what comes to it),
 * intent?kind=started|time_changed|network_up, alarm?id=N (now),
 * forward?seconds=N (the clock and every deadline move on).
 */
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "app_delivery.h"
#include "app_manifest.h"
#include "app_scene.h"
#include "app_store.h"
#include "deadline_table.h"
#include "gfx.h"
#include "gfx_icons.h"
#include "host_api.h"
#include "json_pick.h"
#include "json_tree.h"
#include "notice.h"
#include "scene.h"
#include "screens.h"
#include "text_template.h"
#include "units.h"
#include "wm.h"
#include "words.h"

#define SCREEN_WIDTH  400
#define SCREEN_HEIGHT 300
#define FRAME_BYTES   (SCREEN_WIDTH / BITS_PER_BYTE * SCREEN_HEIGHT)

#define LOG_KEPT_BYTES     (256 * 1024)
#define REQUEST_HEAD_MAX   8192
#define REQUEST_BODY_MAX   (2 * 1024 * 1024)
#define PATH_BYTES         1024
#define NOTICES_MAX        4
#define FIRST_POLLS_MS     2000 /* after a start, as the device spreads them */
#define REDIRECTS_MAX      3
#define REQUEST_TIMEOUT_S  15
#define WAIT_MAX_MS        1000

typedef enum { DUE_TIMER, DUE_ALARM, DUE_POLL, DUE_MINUTE } due_kind_t;

/* ---- what the simulator was asked to be ---- */

static struct {
    const char *apps;
    const char *app;
    const char *state;
    const char *language;
    const char *glyphs;
    const char *mocks;
    const char *python;
    const char *fetcher;
    int         port;
    bool        jersey;
    bool        clock;
} s_options = { .language = "en", .port = 8401, .python = "python3" };

/* ---- time: the computer's, moved on by /api/sim/forward ---- */

static int64_t s_forward_ms;
static int64_t s_started_ms;
static uint32_t s_boot_id;

static int64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * MS_PER_SECOND + now.tv_nsec / 1000000 + s_forward_ms;
}

static int64_t unix_now(void)
{
    return (int64_t)time(NULL) + s_forward_ms / MS_PER_SECOND;
}

/* ---- the log: what /api/log gives, and the console ---- */

static char    *s_log;
static size_t   s_log_length;
static uint64_t s_log_written; /* every byte ever written: where an answer ended */

static void log_text(const char *text, size_t length)
{
    fwrite(text, 1, length, stdout);
    fflush(stdout);
    if (length > LOG_KEPT_BYTES) {
        text += length - LOG_KEPT_BYTES;
        length = LOG_KEPT_BYTES;
    }
    if (s_log_length + length > LOG_KEPT_BYTES) {
        size_t dropped = s_log_length + length - LOG_KEPT_BYTES;
        memmove(s_log, s_log + dropped, s_log_length - dropped);
        s_log_length -= dropped;
    }
    memcpy(s_log + s_log_length, text, length);
    s_log_length += length;
    s_log_written += length;
}

static void say(char level, const char *tag, const char *format, ...)
{
    char line[1024];
    int  at = snprintf(line, sizeof line, "%c (%lld) %s: ", level,
                       (long long)(monotonic_ms() - s_started_ms), tag);
    va_list arguments;
    va_start(arguments, format);
    at += vsnprintf(line + at, sizeof line - (size_t)at, format, arguments);
    va_end(arguments);
    if (at > (int)sizeof line - 2) {
        at = (int)sizeof line - 2;
    }
    line[at++] = '\n';
    log_text(line, (size_t)at);
}

/* ---- the application ---- */

static app_manifest_t       s_manifest;
static char                *s_manifest_json;
static scene_t             *s_app_scene;
static app_delivery_state_t s_delivery;
static wm_t                 s_wm;
static deadline_table_t     s_deadlines;
static uint32_t             s_poll_every_s[APP_POLLS_MAX];
static int32_t              s_alarm_every_s[HOST_ALARM_COUNT];
static uint8_t              s_bits[FRAME_BYTES];
static gfx_t                s_screen;
static uint32_t             s_frames;

static struct {
    bool is_set;
    int  icon;
    char text[HOST_STATUS_TEXT_MAX + 1];
} s_header_status[2];

static void set_due(due_kind_t kind, int number, int64_t in_ms)
{
    deadline_id_t id = { .kind = (int)kind, .owner = 0, .number = number };
    deadline_table_set(&s_deadlines, id, monotonic_ms() + (in_ms > 0 ? in_ms : 0), true);
}

static void cancel_due(due_kind_t kind, int number)
{
    deadline_id_t id = { .kind = (int)kind, .owner = 0, .number = number };
    deadline_table_cancel(&s_deadlines, id);
}

static void deliver(const app_delivery_t *delivery)
{
    if (s_app_scene != NULL && (s_wm.current != s_app_scene || s_wm.suspended) &&
        !delivery->only_on_screen) {
        say('I', "sim", "%s is off the screen: loaded for this, and unloaded after it", s_options.app);
    }
    if (s_app_scene != NULL) {
        app_deliver(&s_delivery, &s_wm, s_app_scene, delivery, monotonic_ms());
    }
}

/* ---- the store: a folder of the application's, a file a key ---- */

static void store_path(char path[PATH_BYTES], const char *app_name, const char *key)
{
    /* A key may hold any printable character, a slash too: its name on
     * disk is its bytes in hex. */
    int at = snprintf(path, PATH_BYTES, "%s/store/%s/", s_options.state, app_name);
    for (const char *c = key; *c != '\0' && at < PATH_BYTES - 3; c++) {
        at += snprintf(path + at, (size_t)(PATH_BYTES - at), "%02x", (unsigned char)*c);
    }
}

static void make_folders(const char *path)
{
    char made[PATH_BYTES];
    snprintf(made, sizeof made, "%s", path);
    for (char *slash = made + 1; *slash != '\0'; slash++) {
        if (*slash == '/') {
            *slash = '\0';
            mkdir(made, 0755);
            *slash = '/';
        }
    }
    mkdir(made, 0755);
}

/* As the device counts it: two entries of 32 bytes, and one for every 32
 * bytes of the value. */
static int32_t taken_by(int32_t length)
{
    return (2 + (length + 31) / 32) * 32;
}

static int32_t kv_get(const char *app_name, const char *key, void *value, int32_t capacity)
{
    char path[PATH_BYTES];
    store_path(path, app_name, key);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length > capacity) {
        fclose(file);
        return HOST_ERR_NO_SPACE;
    }
    size_t read = fread(value, 1, (size_t)length, file);
    fclose(file);
    return (int32_t)read;
}

static int32_t stored_bytes(const char *app_name, const char *except_path)
{
    char folder[PATH_BYTES];
    snprintf(folder, sizeof folder, "%s/store/%s", s_options.state, app_name);
    DIR *directory = opendir(folder);
    int32_t total = 0;
    struct dirent *entry;
    while (directory != NULL && (entry = readdir(directory)) != NULL) {
        char path[PATH_BYTES];
        struct stat found;
        snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
        if (entry->d_name[0] != '.' && strcmp(path, except_path) != 0 && stat(path, &found) == 0) {
            total += taken_by((int32_t)found.st_size);
        }
    }
    if (directory != NULL) {
        closedir(directory);
    }
    return total;
}

static int32_t kv_set(const char *app_name, const char *key, const void *value, int32_t length)
{
    char path[PATH_BYTES], folder[PATH_BYTES];
    store_path(path, app_name, key);
    if (length == 0) {
        remove(path);
        return HOST_OK;
    }
    if (s_manifest_json != NULL && app_manifest_is_server_setting(s_manifest_json, key)) {
        say('W', "sim", "%s: \"%s\" is the user's to set", app_name, key);
        return HOST_ERR_ARGUMENT;
    }
    if (stored_bytes(app_name, path) + taken_by(length) > HOST_KV_TOTAL_MAX) {
        say('W', "sim", "%s: \"%s\" is over what an application may keep", app_name, key);
        return HOST_ERR_NO_SPACE;
    }
    snprintf(folder, sizeof folder, "%s/store/%s", s_options.state, app_name);
    make_folders(folder);
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return HOST_ERR_FAILED;
    }
    bool is_written = fwrite(value, 1, (size_t)length, file) == (size_t)length;
    return fclose(file) == 0 && is_written ? HOST_OK : HOST_ERR_FAILED;
}

/* A setting as the host puts it into an address: what is stored, else the
 * manifest's default. */
static bool setting_of(void *context, const char *key, char *value, size_t value_size)
{
    (void)context;
    int32_t length = kv_get(s_options.app, key, value, (int32_t)value_size - 1);
    if (length > 0) {
        value[length] = '\0';
        return true;
    }
    json_t *root = json_parse(s_manifest_json);
    const json_t *setting;
    bool found = false;
    json_for_each(setting, json_get(root, "settings"))
    {
        const json_t *named = json_get(setting, "key");
        const json_t *preset = json_get(setting, "default");
        if (json_is_string(named) && strcmp(named->text, key) == 0 && json_is_string(preset) &&
            strlen(preset->text) < value_size) {
            strcpy(value, preset->text);
            found = true;
        }
    }
    json_free(root);
    return found;
}

/* ---- the network: the computer's, on a thread, answers replaced by mocks ---- */

typedef struct job {
    struct job *next;
    bool        is_poll;
    bool        wants_values;
    int         number; /* the poll's index, or the request's tag */
    int         method;
    char        url[APP_URL_MAX + 1];
    char       *headers; /* "Name: value\n" each */
    uint8_t    *body;
    size_t      body_length;
    char        pick[JSON_PICK_PATHS_MAX][JSON_PICK_PATH_MAX + 1];
    int         pick_count;
    /* the answer */
    int             status; /* HTTP, or a HOST_FAILED_* */
    uint8_t        *answer;
    size_t          answer_length;
    host_response_t response;
} job_t;

static pthread_mutex_t s_jobs_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_jobs_waiting = PTHREAD_COND_INITIALIZER;
static job_t          *s_jobs_asked;
static int             s_jobs_done[2]; /* a pipe: the thread writes a finished job's pointer */
static json_t         *s_mocks;
static char            s_mocks_folder[PATH_BYTES];

static const char *const METHODS[] = { "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD",
                                       "PROPFIND", "REPORT" };

/* A mock for the address: an exact key, or a key ending in "*" the address begins with. */
static const json_t *mock_for(const char *url)
{
    const json_t *mock;
    json_for_each(mock, s_mocks)
    {
        size_t length = strlen(mock->name);
        bool   is_prefix = length > 0 && mock->name[length - 1] == '*';
        if (is_prefix ? strncmp(url, mock->name, length - 1) == 0 : strcmp(url, mock->name) == 0) {
            return mock;
        }
    }
    return NULL;
}

static void answer_from_mock(job_t *job, const json_t *mock)
{
    const json_t *status = json_get(mock, "status");
    const json_t *file = json_get(mock, "file");
    const json_t *body = json_get(mock, "body");
    const json_t *type = json_get(mock, "content_type");
    job->status = json_is_number(status) ? (int)status->number : 200;
    job->response.status = job->status;
    snprintf(job->response.content_type, sizeof job->response.content_type, "%s",
             json_is_string(type) ? type->text : "application/json");
    if (json_is_string(file)) {
        char path[PATH_BYTES];
        snprintf(path, sizeof path, "%s%s", file->text[0] == '/' ? "" : s_mocks_folder, file->text);
        FILE *opened = fopen(path, "rb");
        if (opened == NULL) {
            job->status = HOST_FAILED_NO_ANSWER;
            return;
        }
        fseek(opened, 0, SEEK_END);
        long length = ftell(opened);
        fseek(opened, 0, SEEK_SET);
        job->answer = malloc((size_t)length + 1);
        job->answer_length = fread(job->answer, 1, (size_t)length, opened);
        fclose(opened);
    } else if (json_is_string(body)) {
        job->answer_length = strlen(body->text);
        job->answer = malloc(job->answer_length + 1);
        memcpy(job->answer, body->text, job->answer_length);
    }
    job->response.length = (int32_t)job->answer_length;
}

/* ---- base64, for bodies on their way to fetch.py and back ---- */

static const char BASE64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const uint8_t *bytes, size_t length)
{
    char  *text = malloc((length + 2) / 3 * 4 + 1);
    size_t out = 0;
    for (size_t i = 0; i < length; i += 3) {
        uint32_t group = (uint32_t)bytes[i] << 16 | (i + 1 < length ? (uint32_t)bytes[i + 1] << 8 : 0) |
                         (i + 2 < length ? bytes[i + 2] : 0);
        text[out++] = BASE64[group >> 18 & 63];
        text[out++] = BASE64[group >> 12 & 63];
        text[out++] = i + 1 < length ? BASE64[group >> 6 & 63] : '=';
        text[out++] = i + 2 < length ? BASE64[group & 63] : '=';
    }
    text[out] = '\0';
    return text;
}

static uint8_t *base64_decode(const char *text, size_t *length)
{
    size_t   size = strlen(text);
    uint8_t *bytes = malloc(size / 4 * 3 + 1);
    uint32_t group = 0;
    int      bits = 0;
    *length = 0;
    for (const char *at = text; *at != '\0' && *at != '='; at++) {
        const char *found = strchr(BASE64, *at);
        if (found == NULL) {
            continue;
        }
        group = group << 6 | (uint32_t)(found - BASE64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes[(*length)++] = (uint8_t)(group >> bits);
        }
    }
    return bytes;
}

/* ---- fetch.py: started once, a request a line in, an answer a line out ---- */

static FILE *s_fetcher_in;
static FILE *s_fetcher_out;

static bool start_fetcher(void)
{
    if (s_options.fetcher == NULL) {
        return false;
    }
    int to_child[2], from_child[2];
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        return false;
    }
    pid_t child = fork();
    if (child == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[1]);
        close(from_child[0]);
        execlp(s_options.python, s_options.python, s_options.fetcher, (char *)NULL);
        _exit(127);
    }
    close(to_child[0]);
    close(from_child[1]);
    if (child < 0) {
        return false;
    }
    s_fetcher_in = fdopen(to_child[1], "w");
    s_fetcher_out = fdopen(from_child[0], "r");
    return s_fetcher_in != NULL && s_fetcher_out != NULL;
}

static void stop_fetcher(void)
{
    if (s_fetcher_in != NULL) {
        fclose(s_fetcher_in);
    }
    if (s_fetcher_out != NULL) {
        fclose(s_fetcher_out);
    }
    s_fetcher_in = s_fetcher_out = NULL;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

static void copy_answer_header(const json_t *headers, const char *name, char *to, size_t size)
{
    const json_t *value = json_get(headers, name);
    if (json_is_string(value)) {
        snprintf(to, size, "%s", value->text);
    }
}

static json_t *headers_of(char *lines)
{
    json_t *headers = json_new_object();
    for (char *line = lines; line != NULL && *line != '\0';) {
        char *end = strchr(line, '\n');
        if (end != NULL) {
            *end = '\0';
        }
        char *colon = strchr(line, ':');
        if (colon != NULL) {
            *colon = '\0';
            json_add_string(headers, line, colon[1] == ' ' ? colon + 2 : colon + 1);
            *colon = ':';
        }
        if (end != NULL) {
            *end = '\n';
        }
        line = end != NULL ? end + 1 : NULL;
    }
    return headers;
}

static void fetch(job_t *job)
{
    job->response.length = -1;
    const json_t *mock = mock_for(job->url);
    if (mock != NULL) {
        answer_from_mock(job, mock);
        return;
    }
    if (s_fetcher_in == NULL && !start_fetcher()) {
        job->status = HOST_FAILED_NO_NETWORK;
        return;
    }
    json_t *asked = json_new_object();
    json_add_string(asked, "method", METHODS[job->method]);
    json_add_string(asked, "url", job->url);
    json_add_item(asked, "headers", headers_of(job->headers));
    char *body = base64_encode(job->body, job->body_length);
    json_add_string(asked, "body", body);
    free(body);
    /* A GET is asked again where a redirect says; one with a body is not sent twice. */
    json_add_bool(asked, "follow", job->method == HOST_HTTP_GET);
    json_add_number(asked, "max", job->wants_values ? HOST_ANSWER_MAX : HOST_ANSWER_PIECES_MAX);
    json_add_number(asked, "timeout", REQUEST_TIMEOUT_S);
    char *line = json_print(asked);
    json_free(asked);
    bool is_sent = fprintf(s_fetcher_in, "%s\n", line) > 0 && fflush(s_fetcher_in) == 0;
    free(line);
    char   *answer_line = NULL;
    size_t  capacity = 0;
    ssize_t got = is_sent ? getline(&answer_line, &capacity, s_fetcher_out) : -1;
    json_t *answer = got > 0 ? json_parse(answer_line) : NULL;
    free(answer_line);
    if (answer == NULL) {
        /* fetch.py is gone: it is started again for the next request. */
        stop_fetcher();
        job->status = HOST_FAILED_NO_ANSWER;
        return;
    }
    const json_t *status = json_get(answer, "status");
    job->status = json_is_number(status) ? (int)status->number : HOST_FAILED_NO_ANSWER;
    host_response_t *response = &job->response;
    response->status = job->status;
    const json_t *got_headers = json_get(answer, "headers");
    copy_answer_header(got_headers, "content-type", response->content_type, sizeof response->content_type);
    copy_answer_header(got_headers, "etag", response->etag, sizeof response->etag);
    copy_answer_header(got_headers, "last-modified", response->last_modified,
                       sizeof response->last_modified);
    copy_answer_header(got_headers, "location", response->location, sizeof response->location);
    copy_answer_header(got_headers, "link", response->link, sizeof response->link);
    char number[24] = "";
    copy_answer_header(got_headers, "content-length", number, sizeof number);
    if (number[0] != '\0') {
        response->length = atoi(number);
    }
    number[0] = '\0';
    copy_answer_header(got_headers, "retry-after", number, sizeof number);
    if (number[0] >= '0' && number[0] <= '9') {
        response->retry_after = atoi(number);
    }
    const json_t *content = json_get(answer, "body");
    if (json_is_string(content)) {
        job->answer = base64_decode(content->text, &job->answer_length);
    }
    json_free(answer);
}

static void *network_thread(void *unused)
{
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&s_jobs_lock);
        while (s_jobs_asked == NULL) {
            pthread_cond_wait(&s_jobs_waiting, &s_jobs_lock);
        }
        job_t *job = s_jobs_asked;
        s_jobs_asked = job->next;
        pthread_mutex_unlock(&s_jobs_lock);
        fetch(job);
        if (write(s_jobs_done[1], &job, sizeof job) != (ssize_t)sizeof job) {
            free(job);
        }
    }
    return NULL;
}

/* One at a time for the whole device, as the device asks them. */
static void ask(job_t *job)
{
    pthread_mutex_lock(&s_jobs_lock);
    job_t **last = &s_jobs_asked;
    while (*last != NULL) {
        last = &(*last)->next;
    }
    *last = job;
    pthread_cond_signal(&s_jobs_waiting);
    pthread_mutex_unlock(&s_jobs_lock);
}

static void free_job(job_t *job)
{
    free(job->headers);
    free(job->body);
    free(job->answer);
    free(job);
}

/* An answer to the application, as the device gives one: the values it
 * picked, or the answer in pieces of HOST_DATA_CHUNK_MAX. */
static void hand_over(job_t *job)
{
    const char *kind = job->is_poll ? "poll" : "request";
    if (job->status < 0) {
        say('W', "sim", "%s/%s %d: failed (%d)", s_options.app, kind, job->number, job->status);
        app_delivery_t failed = {
            .kind = job->wants_values ? APP_DELIVERY_VALUES : APP_DELIVERY_PIECE,
            .request = job->number, .http_status = job->status, .flags = HOST_DATA_FAILED,
        };
        deliver(&failed);
        return;
    }
    say('I', "sim", "%s/%s %d: HTTP %d, %zu bytes", s_options.app, kind, job->number, job->status,
        job->answer_length);
    if (job->wants_values) {
        const char *paths[JSON_PICK_PATHS_MAX];
        for (int i = 0; i < job->pick_count; i++) {
            paths[i] = job->pick[i];
        }
        static json_pick_t pick;
        json_pick_init(&pick, paths, job->pick_count);
        json_pick_feed(&pick, (const char *)job->answer, job->answer_length);
        json_pick_finish(&pick);
        const char *values[JSON_PICK_PATHS_MAX];
        for (int i = 0; i < job->pick_count; i++) {
            values[i] = json_pick_value(&pick, i);
        }
        app_delivery_t delivery = {
            .kind = APP_DELIVERY_VALUES, .request = job->number, .http_status = job->status,
            .response = &job->response, .values = values, .value_count = job->pick_count,
        };
        deliver(&delivery);
        return;
    }
    size_t at = 0;
    do {
        size_t piece = job->answer_length - at;
        if (piece > HOST_DATA_CHUNK_MAX) {
            piece = HOST_DATA_CHUNK_MAX;
        }
        int flags = (at == 0 ? HOST_DATA_FIRST : 0) |
                    (at + piece == job->answer_length ? HOST_DATA_LAST : 0);
        app_delivery_t delivery = {
            .kind = APP_DELIVERY_PIECE, .request = job->number, .http_status = job->status,
            .response = &job->response, .data = job->answer + at, .length = piece, .flags = flags,
        };
        deliver(&delivery);
        at += piece;
    } while (at < job->answer_length);
}

static void add_header(char **headers, const char *name, const char *value)
{
    size_t had = *headers != NULL ? strlen(*headers) : 0;
    size_t more = strlen(name) + strlen(value) + 3;
    char  *grown = realloc(*headers, had + more + 1);
    if (grown != NULL) {
        snprintf(grown + had, more + 1, "%s: %s\n", name, value);
        *headers = grown;
    }
}

static void start_poll(int number)
{
    app_poll_t poll;
    if (app_manifest_parse_poll(s_manifest_json, number, &poll) != APP_MANIFEST_OK) {
        return;
    }
    job_t *job = calloc(1, sizeof *job);
    job->is_poll = true;
    job->number = number;
    job->method = HOST_HTTP_GET;
    job->wants_values = poll.pick_count > 0;
    job->pick_count = poll.pick_count;
    memcpy(job->pick, poll.pick, sizeof poll.pick);
    bool is_ready = text_template_fill_url(poll.url, setting_of, NULL, job->url, sizeof job->url);
    for (int i = 0; is_ready && i < poll.header_count; i++) {
        char value[APP_HEADER_VALUE_MAX + 1];
        is_ready = text_template_fill(poll.headers[i].value, setting_of, NULL, value, sizeof value) &&
                   strpbrk(value, "\r\n") == NULL;
        add_header(&job->headers, poll.headers[i].name, value);
    }
    if (!is_ready) {
        job->status = HOST_FAILED_NOT_READY;
        hand_over(job);
        free_job(job);
        return;
    }
    ask(job);
}

/* ---- the services the application's host calls ---- */

static int64_t services_unix_time(void) { return unix_now(); }

static int32_t offset_at(int64_t unix_time)
{
    time_t    moment = (time_t)unix_time;
    struct tm local;
    localtime_r(&moment, &local);
    return (int32_t)local.tm_gmtoff;
}

static int32_t services_offset(void) { return offset_at(unix_now()); }
static int64_t services_uptime(void) { return monotonic_ms() - s_started_ms; }
static uint32_t services_boot_id(void) { return s_boot_id; }

static void services_log(int level, const char *text, int length)
{
    static const char LEVELS[] = "EWID";
    say(level >= 0 && level < 4 ? LEVELS[level] : 'I', s_options.app, "%.*s", length, text);
}

static void services_timer(int id, int32_t milliseconds)
{
    if (milliseconds <= 0) {
        cancel_due(DUE_TIMER, id);
    } else {
        set_due(DUE_TIMER, id, milliseconds);
    }
}

static bool services_dithers(void) { return true; }

static bool services_poll_now(const char *app_name, int poll)
{
    (void)app_name;
    if (poll < 0 || poll >= s_manifest.poll_count) {
        return false;
    }
    set_due(DUE_POLL, poll, 0);
    return true;
}

static bool services_poll_interval(const char *app_name, int poll, uint32_t every_s)
{
    (void)app_name;
    if (poll < 0 || poll >= s_manifest.poll_count) {
        return false;
    }
    s_poll_every_s[poll] = every_s;
    set_due(DUE_POLL, poll, (int64_t)every_s * MS_PER_SECOND);
    return true;
}

static int32_t services_request(const char *app_name, const app_request_t *request)
{
    char url[APP_URL_MAX + 1];
    if (!text_template_fill_url(request->url, setting_of, NULL, url, sizeof url) ||
        !app_manifest_allows_url(s_manifest_json, url, setting_of, NULL)) {
        say('W', "sim", "%s: a request to a host its manifest does not list", app_name);
        return HOST_ERR_ARGUMENT;
    }
    job_t *job = calloc(1, sizeof *job);
    job->number = request->tag;
    job->method = request->method;
    snprintf(job->url, sizeof job->url, "%s", url);
    for (int i = 0; i < request->header_count; i++) {
        char value[APP_HEADER_VALUE_MAX + 1];
        if (text_template_fill(request->headers[i].value, setting_of, NULL, value, sizeof value)) {
            add_header(&job->headers, request->headers[i].name, value);
        }
    }
    if (request->body_length > 0) {
        job->body = malloc((size_t)request->body_length);
        memcpy(job->body, request->body, (size_t)request->body_length);
        job->body_length = (size_t)request->body_length;
    }
    ask(job);
    return HOST_OK;
}

static bool services_alarm(const char *app_name, int alarm, int64_t when, bool is_absolute)
{
    (void)app_name;
    s_alarm_every_s[alarm] = 0;
    if (!is_absolute && when == 0) {
        cancel_due(DUE_ALARM, alarm);
        return true;
    }
    int64_t in_ms = is_absolute ? (when - unix_now()) * MS_PER_SECOND : when * MS_PER_SECOND;
    set_due(DUE_ALARM, alarm, in_ms);
    return true;
}

static bool services_alarm_every(const char *app_name, int alarm, int32_t every_s, int32_t first_in_s)
{
    (void)app_name;
    s_alarm_every_s[alarm] = every_s;
    set_due(DUE_ALARM, alarm, (int64_t)first_in_s * MS_PER_SECOND);
    return true;
}

static void services_header_status(const char *app_name, int side, int icon, const char *text)
{
    (void)app_name;
    if (side < 0 || side > 1) {
        return;
    }
    s_header_status[side].is_set = icon >= 0 || text[0] != '\0';
    s_header_status[side].icon = icon;
    snprintf(s_header_status[side].text, sizeof s_header_status[side].text, "%s", text);
    wm_invalidate(&s_wm, WM_CHANGED_BY_DATA);
}

static const char *services_language(void) { return s_options.language; }

/* ---- notifications: a scene over whatever has the screen ---- */

static struct {
    char first[64], second[64], detail[96];
    int  icon;
} s_notices[NOTICES_MAX];
static int  s_notice_count;
static bool s_notice_is_open;

static scene_reaction_t notice_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press == SCENE_PRESS_SHORT && (button == SCENE_BUTTON_OK || button == SCENE_BUTTON_BACK)) {
        reaction.kind = SCENE_BACK;
        s_notice_is_open = false;
        memmove(&s_notices[0], &s_notices[1], sizeof s_notices[0] * (size_t)(NOTICES_MAX - 1));
        s_notice_count--;
    }
    return reaction;
}

static void notice_scene_draw(scene_t *scene, gfx_t *surface)
{
    (void)scene;
    const notice_t notice = {
        .first = s_notices[0].first,
        .second = s_notices[0].second[0] != '\0' ? s_notices[0].second : NULL,
        .detail = s_notices[0].detail[0] != '\0' ? s_notices[0].detail : NULL,
        .icon = s_notices[0].icon,
        .ok = word(WORD_DISMISS),
    };
    notice_draw(surface, &notice);
}

static const scene_ops_t NOTICE_OPS = { .button = notice_button, .draw = notice_scene_draw };
static scene_t           s_notice_scene = { .ops = &NOTICE_OPS };

static bool services_notify(const char *first, const char *second, const char *detail, int icon)
{
    if (s_notice_count >= NOTICES_MAX) {
        return false;
    }
    snprintf(s_notices[s_notice_count].first, sizeof s_notices[0].first, "%s", first ? first : "");
    snprintf(s_notices[s_notice_count].second, sizeof s_notices[0].second, "%s", second ? second : "");
    snprintf(s_notices[s_notice_count].detail, sizeof s_notices[0].detail, "%s", detail ? detail : "");
    s_notices[s_notice_count].icon = icon;
    s_notice_count++;
    return true;
}

static void show_notices(void)
{
    if (!s_notice_is_open && s_notice_count > 0 && !s_wm.suspended) {
        s_notice_is_open = true;
        wm_open(&s_wm, &s_notice_scene);
    }
}

static int32_t services_file_size(const char *app_name, const char *file)
{
    long size = app_store_file_size(s_options.apps, app_name, file);
    return size < 0 ? HOST_ERR_NOT_FOUND : (int32_t)size;
}

static int32_t services_file_read(const char *app_name, const char *file, int32_t offset, void *bytes,
                                  int32_t capacity)
{
    long read = app_store_read_file(s_options.apps, app_name, file, offset, bytes, (size_t)capacity);
    return read < 0 ? HOST_ERR_NOT_FOUND : (int32_t)read;
}

static int32_t services_file_write(const char *app_name, const char *file, int32_t offset,
                                   const void *bytes, int32_t length)
{
    switch (app_store_write_file(s_options.apps, app_name, file, offset, bytes, (size_t)length)) {
    case APP_WRITE_OK:
        return HOST_OK;
    case APP_WRITE_NO_SPACE:
        return HOST_ERR_NO_SPACE;
    case APP_WRITE_REFUSED:
        return HOST_ERR_ARGUMENT;
    default:
        return HOST_ERR_FAILED;
    }
}

static const app_services_t SERVICES = {
    .unix_time = services_unix_time,
    .time_zone_offset = services_offset,
    .time_zone_offset_at = offset_at,
    .uptime_ms = services_uptime,
    .boot_id = services_boot_id,
    .log = services_log,
    .timer_set = services_timer,
    .kv_get = kv_get,
    .kv_set = kv_set,
    .dithers = services_dithers,
    .poll_now = services_poll_now,
    .http_request = services_request,
    .poll_set_interval = services_poll_interval,
    .alarm_set = services_alarm,
    .alarm_set_every = services_alarm_every,
    .header_status = services_header_status,
    .language = services_language,
    .notify = services_notify,
    .file_size = services_file_size,
    .file_read = services_file_read,
    .file_write = services_file_write,
};

/* ---- the menu: what is under the application, with it as its one row ---- */

static void menu_row(void *context, int index, screens_menu_row_t *row)
{
    (void)context;
    (void)index;
    row->title = s_manifest.title;
    row->icon = s_manifest.has_icon ? s_manifest.icon : NULL;
}

static void menu_draw(scene_t *scene, gfx_t *surface)
{
    (void)scene;
    screens_menu(surface, menu_row, NULL, 1, 0);
}

static scene_reaction_t menu_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press == SCENE_PRESS_SHORT && button == SCENE_BUTTON_OK && s_app_scene != NULL) {
        reaction.kind = SCENE_OPEN;
        reaction.open = s_app_scene;
    }
    return reaction;
}

static const scene_ops_t MENU_OPS = { .button = menu_button, .draw = menu_draw };
static scene_t           s_menu = { .ops = &MENU_OPS };

/* ---- the header, as the host draws it ---- */

static void draw_header(gfx_t *surface, const scene_t *scene, void *context)
{
    (void)context;
    char      when[48];
    time_t    now = (time_t)unix_now();
    struct tm local;
    localtime_r(&now, &local);
    if (s_options.clock) {
        snprintf(when, sizeof when, "%02d:%02d", local.tm_hour, local.tm_min);
    } else {
        snprintf(when, sizeof when, "%s %d %s", word((word_t)(WORD_SUN + local.tm_wday)),
                 local.tm_mday, word((word_t)(WORD_JAN + local.tm_mon)));
    }
    screens_header_t header = {
        .left_icon = GFX_ICON_SIGNAL,
        .left = scene->header_left,
        .right = scene->header_right != NULL ? scene->header_right : when,
        .right_icon = SCREENS_NO_ICON,
    };
    if (s_header_status[HOST_HEADER_LEFT].is_set) {
        header.left_icon = s_header_status[HOST_HEADER_LEFT].icon;
        header.left = s_header_status[HOST_HEADER_LEFT].text;
    }
    if (s_header_status[HOST_HEADER_RIGHT].is_set) {
        header.right_icon = s_header_status[HOST_HEADER_RIGHT].icon;
        header.right = s_header_status[HOST_HEADER_RIGHT].text;
    }
    screens_header(surface, &header);
}

static void show(void)
{
    show_notices();
    if (wm_pending_change(&s_wm) == WM_CLEAN) {
        return;
    }
    wm_draw(&s_wm, &s_screen);
    s_frames++;
    wm_shown(&s_wm);
}

/* ---- loading, and loading again after an upload ---- */

static void intent(int kind)
{
    if ((s_manifest.intents & (1u << kind)) == 0) {
        say('I', "sim", "%s: intent %d not asked for in the manifest", s_options.app, kind);
        return;
    }
    app_delivery_t delivery = { .kind = APP_DELIVERY_INTENT, .request = kind };
    deliver(&delivery);
}

static bool load_app(void)
{
    if (s_app_scene != NULL) {
        wm_close(&s_wm, s_app_scene);
        app_delivery_init(&s_delivery);
        app_scene_destroy(s_app_scene);
        s_app_scene = NULL;
    }
    free(s_manifest_json);
    s_manifest_json = app_store_read_manifest(s_options.apps, s_options.app);
    if (s_manifest_json == NULL) {
        say('E', "sim", "%s/%s/manifest.json cannot be read", s_options.apps, s_options.app);
        return false;
    }
    app_manifest_result_t parsed = app_manifest_parse(s_manifest_json, &s_manifest);
    if (parsed != APP_MANIFEST_OK) {
        say('E', "sim", "manifest.json: %s", app_manifest_result_text(parsed));
        return false;
    }
    s_manifest.id = 1;
    /* The menu shows the title in the device's language, as the host takes it. */
    char *translated = app_store_read_translation(s_options.apps, s_options.app, s_options.language);
    json_t *words = json_parse(translated);
    const json_t *title = json_get(words, "title");
    if (json_is_string(title)) {
        snprintf(s_manifest.title, sizeof s_manifest.title, "%s", title->text);
    }
    json_free(words);
    free(translated);
    s_app_scene = app_scene_create(&s_manifest, s_options.apps, &SERVICES);
    if (s_app_scene == NULL) {
        say('E', "sim", "%s cannot be loaded", s_options.app);
        return false;
    }
    deadline_table_init(&s_deadlines);
    memset(s_alarm_every_s, 0, sizeof s_alarm_every_s);
    memset(s_header_status, 0, sizeof s_header_status);
    for (int poll = 0; poll < s_manifest.poll_count; poll++) {
        char pace[32];
        uint32_t every = s_manifest.poll_every_s[poll];
        if (s_manifest.poll_every_key[poll][0] != '\0' &&
            setting_of(NULL, s_manifest.poll_every_key[poll], pace, sizeof pace)) {
            uint32_t chosen = app_manifest_interval_seconds(pace);
            every = chosen > 0 ? chosen : every;
        }
        s_poll_every_s[poll] = every;
        set_due(DUE_POLL, poll, FIRST_POLLS_MS);
    }
    set_due(DUE_MINUTE, 0, (SECONDS_PER_MINUTE - unix_now() % SECONDS_PER_MINUTE) * MS_PER_SECOND);
    say('I', "sim", "%s %s loaded", s_manifest.name, s_manifest.version);
    /* As after a restart of the device: news for whoever asked, then the screen. */
    intent(HOST_INTENT_STARTED);
    wm_open_from_home(&s_wm, s_app_scene);
    return true;
}

static void answer_due(const deadline_id_t *id)
{
    switch ((due_kind_t)id->kind) {
    case DUE_TIMER:
        wm_timer(&s_wm, id->number);
        break;
    case DUE_ALARM: {
        if (s_alarm_every_s[id->number] > 0) {
            set_due(DUE_ALARM, id->number, (int64_t)s_alarm_every_s[id->number] * MS_PER_SECOND);
        }
        app_delivery_t delivery = { .kind = APP_DELIVERY_ALARM, .request = id->number };
        deliver(&delivery);
        break;
    }
    case DUE_POLL:
        start_poll(id->number);
        set_due(DUE_POLL, id->number, (int64_t)s_poll_every_s[id->number] * MS_PER_SECOND);
        break;
    case DUE_MINUTE:
        wm_invalidate(&s_wm, WM_CHANGED_BY_DATA);
        set_due(DUE_MINUTE, 0, SECONDS_PER_MINUTE * MS_PER_SECOND);
        break;
    }
}

/* ---- the development API on localhost ---- */

typedef struct {
    char   method[8];
    char   path[PATH_BYTES];
    char   query[PATH_BYTES];
    char  *body;
    size_t body_length;
} request_t;

static void send_all(int socket_fd, const void *bytes, size_t length)
{
    const char *at = bytes;
    while (length > 0) {
        ssize_t sent = send(socket_fd, at, length, 0);
        if (sent <= 0) {
            return;
        }
        at += sent;
        length -= (size_t)sent;
    }
}

static void answer(int socket_fd, int status, const char *type, const char *extra_headers,
                   const void *body, size_t length)
{
    char head[1024];
    int  head_length = snprintf(head, sizeof head,
                                "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                                "Access-Control-Allow-Origin: *\r\n"
                                "Access-Control-Expose-Headers: X-Frame, X-Pending, X-Log-End\r\n"
                                "%sConnection: close\r\n\r\n",
                                status, status == 200 ? "OK" : "Error", type, length,
                                extra_headers != NULL ? extra_headers : "");
    send_all(socket_fd, head, (size_t)head_length);
    send_all(socket_fd, body, length);
}

static void answer_text(int socket_fd, int status, const char *text)
{
    answer(socket_fd, status, "text/plain; charset=utf-8", NULL, text, strlen(text));
}

static bool query_value(const request_t *request, const char *key, char *value, size_t size)
{
    size_t key_length = strlen(key);
    for (const char *at = request->query; *at != '\0';) {
        const char *end = strchr(at, '&');
        size_t      length = end != NULL ? (size_t)(end - at) : strlen(at);
        if (length > key_length && strncmp(at, key, key_length) == 0 && at[key_length] == '=') {
            size_t out = 0;
            for (size_t i = key_length + 1; i < length && out + 1 < size; i++) {
                if (at[i] == '%' && i + 2 < length) {
                    char hex[3] = { at[i + 1], at[i + 2], '\0' };
                    value[out++] = (char)strtol(hex, NULL, 16);
                    i += 2;
                } else {
                    value[out++] = at[i] == '+' ? ' ' : at[i];
                }
            }
            value[out] = '\0';
            return true;
        }
        at = end != NULL ? end + 1 : at + length;
    }
    return false;
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    for (int i = 0; i < 4; i++) {
        bytes[i] = (uint8_t)(value >> (BITS_PER_BYTE * i));
    }
}

/* The frame as the device gives it: a 1-bit BMP, ink and paper, rows from the bottom. */
static void answer_screen(int socket_fd)
{
    enum { HEADER = 62, ROW = (SCREEN_WIDTH / 8 + 3) / 4 * 4 };
    static uint8_t bmp[HEADER + ROW * SCREEN_HEIGHT];
    memset(bmp, 0, HEADER);
    bmp[0] = 'B';
    bmp[1] = 'M';
    put_u32(bmp + 2, sizeof bmp);
    put_u32(bmp + 10, HEADER);
    put_u32(bmp + 14, 40);
    put_u32(bmp + 18, SCREEN_WIDTH);
    put_u32(bmp + 22, SCREEN_HEIGHT);
    bmp[26] = 1;
    bmp[28] = 1;
    put_u32(bmp + 34, ROW * SCREEN_HEIGHT);
    put_u32(bmp + 46, 2);
    put_u32(bmp + 54, 0x00141413);
    put_u32(bmp + 58, 0x00E9E7E1);
    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        memcpy(bmp + HEADER + (size_t)(SCREEN_HEIGHT - 1 - row) * ROW,
               s_bits + row * (SCREEN_WIDTH / 8), SCREEN_WIDTH / 8);
    }
    char headers[64];
    snprintf(headers, sizeof headers, "X-Frame: %u\r\nX-Pending: 0\r\n", s_frames);
    answer(socket_fd, 200, "image/bmp", headers, bmp, sizeof bmp);
}

static void answer_log(int socket_fd, const request_t *request)
{
    char     since_text[24];
    uint64_t kept_from = s_log_written - s_log_length;
    size_t   from = 0;
    if (query_value(request, "since", since_text, sizeof since_text)) {
        uint64_t since = strtoull(since_text, NULL, 10);
        from = since <= kept_from ? 0 : since >= s_log_written ? s_log_length : (size_t)(since - kept_from);
    }
    char headers[48];
    snprintf(headers, sizeof headers, "X-Log-End: %llu\r\n", (unsigned long long)s_log_written);
    answer(socket_fd, 200, "text/plain; charset=utf-8", headers, s_log + from, s_log_length - from);
}

static void answer_status(int socket_fd)
{
    json_t *json = json_new_object();
    json_add_string(json, "name", "Simulator");
    json_add_string(json, "firmware", "simulator");
    json_add_string(json, "slot", "simulator");
    json_add_bool(json, "on_probation", false);
    json_add_string(json, "wifi", "online");
    json_add_string(json, "app", s_options.app);
    json_add_number(json, "uptime_s", services_uptime() / MS_PER_SECOND);
    json_add_number(json, "reset_reason", 3);
    json_add_bool(json, "in_background", s_app_scene != NULL && s_wm.current != s_app_scene);
    char *text = json_print(json);
    json_free(json);
    answer(socket_fd, 200, "application/json", NULL, text, strlen(text));
    free(text);
}

static const scene_button_t BUTTONS[] = { SCENE_BUTTON_UP, SCENE_BUTTON_OK, SCENE_BUTTON_DOWN,
                                          SCENE_BUTTON_BACK };

static void press(int button, bool is_long)
{
    scene_button_t role = BUTTONS[button];
    wm_button(&s_wm, role, SCENE_PRESS_DOWN);
    show();
    if (is_long) {
        wm_button(&s_wm, role, SCENE_PRESS_LONG);
        wm_button(&s_wm, role, SCENE_PRESS_UP);
    } else {
        wm_button(&s_wm, role, SCENE_PRESS_UP);
        wm_button(&s_wm, role, SCENE_PRESS_SHORT);
    }
}

static void handle(int socket_fd, const request_t *request)
{
    char value[PATH_BYTES], name[PATH_BYTES];
    bool is_post = strcmp(request->method, "POST") == 0;
    const char *path = request->path;
    if (strcmp(path, "/api/status") == 0) {
        answer_status(socket_fd);
    } else if (strcmp(path, "/api/log") == 0) {
        answer_log(socket_fd, request);
    } else if (strcmp(path, "/api/dev/screen") == 0) {
        show();
        answer_screen(socket_fd);
    } else if (is_post && strcmp(path, "/api/dev/button") == 0) {
        int button = query_value(request, "button", value, sizeof value) ? atoi(value) : 0;
        bool is_long = query_value(request, "press", name, sizeof name) && strcmp(name, "long") == 0;
        if (button < 1 || button > 4) {
            answer_text(socket_fd, 400, "button=1..4&press=short|long");
            return;
        }
        press(button - 1, is_long);
        answer_text(socket_fd, 200, "pressed");
    } else if (is_post && (strcmp(path, "/api/dev/open") == 0 || strcmp(path, "/api/sim/foreground") == 0)) {
        if (s_app_scene != NULL) {
            wm_open_from_home(&s_wm, s_app_scene);
        }
        answer_text(socket_fd, 200, "opened");
    } else if (is_post && strcmp(path, "/api/sim/background") == 0) {
        /* The user went to the menu: the application leaves the screen and
         * is loaded from now on only for what comes to it. */
        press(3, true);
        answer_text(socket_fd, 200, "in the background");
    } else if (is_post && strcmp(path, "/api/dev/app") == 0) {
        if (!query_value(request, "name", name, sizeof name) ||
            !query_value(request, "file", value, sizeof value) || strcmp(name, s_options.app) != 0 ||
            strchr(value, '/') != NULL || value[0] == '.') {
            answer_text(socket_fd, 400, "name=<the simulated application>&file=<file>");
            return;
        }
        char folder[PATH_BYTES], file_path[PATH_BYTES * 2];
        snprintf(folder, sizeof folder, "%s/%s", s_options.apps, name);
        make_folders(folder);
        snprintf(file_path, sizeof file_path, "%s/%s", folder, value);
        FILE *file = fopen(file_path, "wb");
        bool is_written = file != NULL &&
                          fwrite(request->body, 1, request->body_length, file) == request->body_length;
        if (file != NULL) {
            fclose(file);
        }
        answer_text(socket_fd, is_written ? 200 : 500, is_written ? "stored" : "not stored");
    } else if (is_post && (strcmp(path, "/api/dev/run") == 0 || strcmp(path, "/api/dev/restart") == 0)) {
        answer_text(socket_fd, 200, "restarting");
        load_app();
    } else if (is_post && strcmp(path, "/api/dev/kv") == 0) {
        if (!query_value(request, "key", name, sizeof name)) {
            answer_text(socket_fd, 400, "app=<name>&key=<key>, the value as the body");
            return;
        }
        int32_t stored = request->body_length > 0
                             ? kv_set(s_options.app, name, request->body, (int32_t)request->body_length)
                             : kv_set(s_options.app, name, "", 0);
        app_delivery_t settings = { .kind = APP_DELIVERY_SETTINGS };
        deliver(&settings);
        for (int poll = 0; poll < s_manifest.poll_count; poll++) {
            set_due(DUE_POLL, poll, 0);
        }
        answer_text(socket_fd, stored == HOST_OK ? 200 : 400, stored == HOST_OK ? "stored" : "refused");
    } else if (is_post && strcmp(path, "/api/sim/intent") == 0) {
        query_value(request, "kind", name, sizeof name);
        int kind = strcmp(name, "started") == 0         ? HOST_INTENT_STARTED
                   : strcmp(name, "time_changed") == 0  ? HOST_INTENT_TIME_CHANGED
                   : strcmp(name, "network_up") == 0    ? HOST_INTENT_NETWORK_UP
                                                        : 0;
        if (kind == 0) {
            answer_text(socket_fd, 400, "kind=started|time_changed|network_up");
            return;
        }
        intent(kind);
        answer_text(socket_fd, 200, "told");
    } else if (is_post && strcmp(path, "/api/sim/alarm") == 0) {
        int alarm = query_value(request, "id", value, sizeof value) ? atoi(value) : -1;
        if (alarm < 0 || alarm >= HOST_ALARM_COUNT) {
            answer_text(socket_fd, 400, "id=0..3");
            return;
        }
        deadline_id_t id = { .kind = DUE_ALARM, .owner = 0, .number = alarm };
        answer_due(&id);
        answer_text(socket_fd, 200, "rang");
    } else if (is_post && strcmp(path, "/api/sim/forward") == 0) {
        long seconds = query_value(request, "seconds", value, sizeof value) ? atol(value) : 0;
        if (seconds <= 0) {
            answer_text(socket_fd, 400, "seconds=N");
            return;
        }
        s_forward_ms += (int64_t)seconds * MS_PER_SECOND;
        say('I', "sim", "the clock moved %ld s on", seconds);
        answer_text(socket_fd, 200, "moved");
    } else {
        answer_text(socket_fd, 404, "not here");
    }
}

/* One request on one connection: the device's server answers so, and closes. */
static void serve(int listening)
{
    int connection = accept(listening, NULL, NULL);
    if (connection < 0) {
        return;
    }
    static char head[REQUEST_HEAD_MAX + 1];
    size_t      have = 0;
    char       *end = NULL;
    while (end == NULL && have < REQUEST_HEAD_MAX) {
        ssize_t got = recv(connection, head + have, REQUEST_HEAD_MAX - have, 0);
        if (got <= 0) {
            close(connection);
            return;
        }
        have += (size_t)got;
        head[have] = '\0';
        end = strstr(head, "\r\n\r\n");
    }
    if (end == NULL) {
        close(connection);
        return;
    }
    request_t request = { 0 };
    char      target[PATH_BYTES * 2] = "";
    sscanf(head, "%7s %2047s", request.method, target);
    char *question = strchr(target, '?');
    if (question != NULL) {
        *question = '\0';
        snprintf(request.query, sizeof request.query, "%s", question + 1);
    }
    snprintf(request.path, sizeof request.path, "%s", target);
    size_t      declared = 0;
    const char *length = strcasestr(head, "\r\nContent-Length:");
    if (length != NULL && length < end) {
        declared = strtoul(length + strlen("\r\nContent-Length:"), NULL, 10);
    }
    if (declared > REQUEST_BODY_MAX) {
        answer_text(connection, 413, "too large");
        close(connection);
        return;
    }
    size_t already = have - (size_t)(end + 4 - head);
    request.body = malloc(declared + 1);
    memcpy(request.body, end + 4, already < declared ? already : declared);
    request.body_length = already < declared ? already : declared;
    while (request.body_length < declared) {
        ssize_t got = recv(connection, request.body + request.body_length,
                           declared - request.body_length, 0);
        if (got <= 0) {
            break;
        }
        request.body_length += (size_t)got;
    }
    request.body[request.body_length] = '\0';
    handle(connection, &request);
    free(request.body);
    close(connection);
}

/* ---- start ---- */

static void load_glyphs(void)
{
    DIR *directory = s_options.glyphs != NULL ? opendir(s_options.glyphs) : NULL;
    struct dirent *entry;
    while (directory != NULL && (entry = readdir(directory)) != NULL) {
        size_t name_length = strlen(entry->d_name);
        if (name_length < 7 || strcmp(entry->d_name + name_length - 7, ".glyphs") != 0) {
            continue;
        }
        char path[PATH_BYTES];
        snprintf(path, sizeof path, "%s/%s", s_options.glyphs, entry->d_name);
        FILE *file = fopen(path, "rb");
        if (file == NULL) {
            continue;
        }
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        /* Kept for as long as the simulator runs: the fonts point into it.
         * malloc's alignment is more than the four bytes a pack needs. */
        void *pack = malloc((size_t)size);
        if (pack == NULL) {
            fclose(file);
            continue;
        }
        size_t read = fread(pack, 1, (size_t)size, file);
        fclose(file);
        if (gfx_pack_add(pack, read)) {
            say('I', "sim", "glyph pack %s", entry->d_name);
        }
    }
    if (directory != NULL) {
        closedir(directory);
    }
}

static void load_mocks(void)
{
    if (s_options.mocks == NULL) {
        return;
    }
    FILE *file = fopen(s_options.mocks, "rb");
    if (file == NULL) {
        say('E', "sim", "%s cannot be read", s_options.mocks);
        return;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = calloc(1, (size_t)size + 1);
    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[read] = '\0';
    s_mocks = json_parse(text);
    free(text);
    if (!json_is_object(s_mocks)) {
        say('E', "sim", "%s is not a JSON object of address: answer", s_options.mocks);
        json_free(s_mocks);
        s_mocks = NULL;
        return;
    }
    const char *slash = strrchr(s_options.mocks, '/');
    snprintf(s_mocks_folder, sizeof s_mocks_folder, "%.*s", slash != NULL ? (int)(slash - s_options.mocks + 1) : 0,
             s_options.mocks);
    say('I', "sim", "%d mocked addresses from %s", json_count(s_mocks), s_options.mocks);
}

static void usage(void)
{
    fprintf(stderr, "perinkpheral-sim --apps <folder> --app <name> --state <folder> [--port 8401]\n"
                    "                 [--language en] [--font departure|jersey] [--glyphs <folder>]\n"
                    "                 [--mocks <file.json>] [--clock] [--python python3]\n"
                    "                 [--fetcher tools/simulator/fetch.py]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *next = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(argv[i], "--apps") == 0 && next) s_options.apps = argv[++i];
        else if (strcmp(argv[i], "--app") == 0 && next) s_options.app = argv[++i];
        else if (strcmp(argv[i], "--state") == 0 && next) s_options.state = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && next) s_options.port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--language") == 0 && next) s_options.language = argv[++i];
        else if (strcmp(argv[i], "--glyphs") == 0 && next) s_options.glyphs = argv[++i];
        else if (strcmp(argv[i], "--mocks") == 0 && next) s_options.mocks = argv[++i];
        else if (strcmp(argv[i], "--font") == 0 && next) s_options.jersey = strcmp(argv[++i], "jersey") == 0;
        else if (strcmp(argv[i], "--clock") == 0) s_options.clock = true;
        else if (strcmp(argv[i], "--python") == 0 && next) s_options.python = argv[++i];
        else if (strcmp(argv[i], "--fetcher") == 0 && next) s_options.fetcher = argv[++i];
        else usage();
    }
    if (s_options.apps == NULL || s_options.app == NULL || s_options.state == NULL) {
        usage();
    }
    /* The store's paths are as short as the device's ("/apps/<name>/<file>"):
     * the folder of applications becomes the working one, and the other
     * folders are made absolute before that. */
    static char state[PATH_BYTES], mocks[PATH_BYTES], glyphs[PATH_BYTES], fetcher[PATH_BYTES];
    if (s_options.fetcher != NULL && realpath(s_options.fetcher, fetcher) != NULL) {
        s_options.fetcher = fetcher;
    }
    if (realpath(s_options.state, state) == NULL) {
        mkdir(s_options.state, 0755);
        if (realpath(s_options.state, state) == NULL) {
            fprintf(stderr, "%s: %s\n", s_options.state, strerror(errno));
            return 1;
        }
    }
    s_options.state = state;
    if (s_options.mocks != NULL && realpath(s_options.mocks, mocks) != NULL) {
        s_options.mocks = mocks;
    }
    if (s_options.glyphs != NULL && realpath(s_options.glyphs, glyphs) != NULL) {
        s_options.glyphs = glyphs;
    }
    if (chdir(s_options.apps) != 0) {
        fprintf(stderr, "%s: %s\n", s_options.apps, strerror(errno));
        return 1;
    }
    s_options.apps = ".";
    signal(SIGPIPE, SIG_IGN);
    s_log = malloc(LOG_KEPT_BYTES);
    s_started_ms = monotonic_ms();
    srand((unsigned)time(NULL));
    s_boot_id = (uint32_t)rand() | 1u;
    words_use(s_options.language);
    gfx_font_use(s_options.jersey ? GFX_FAMILY_JERSEY : GFX_FAMILY_DEPARTURE_MONO);
    load_glyphs();
    load_mocks();
    if (pipe(s_jobs_done) != 0) {
        return 1;
    }
    pthread_t thread;
    pthread_create(&thread, NULL, network_thread, NULL);

    gfx_init(&s_screen, s_bits, SCREEN_WIDTH, SCREEN_HEIGHT);
    wm_init(&s_wm, &s_menu, &s_menu, draw_header, NULL);
    app_delivery_init(&s_delivery);
    if (!load_app()) {
        return 1;
    }

    int listening = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(listening, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_port = htons((uint16_t)s_options.port) };
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listening, (struct sockaddr *)&address, sizeof address) != 0 || listen(listening, 8) != 0) {
        say('E', "sim", "port %d: %s", s_options.port, strerror(errno));
        return 1;
    }
    say('I', "sim", "the device is at http://127.0.0.1:%d", s_options.port);

    for (;;) {
        deadline_id_t due;
        while (deadline_table_take_due(&s_deadlines, monotonic_ms(), &due)) {
            answer_due(&due);
        }
        show();
        int64_t nearest = deadline_table_nearest(&s_deadlines);
        int64_t wait_ms = nearest == DEADLINE_NONE ? WAIT_MAX_MS : nearest - monotonic_ms();
        if (wait_ms > WAIT_MAX_MS) wait_ms = WAIT_MAX_MS;
        if (wait_ms < 0) wait_ms = 0;
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(listening, &ready);
        FD_SET(s_jobs_done[0], &ready);
        struct timeval timeout = { .tv_sec = (time_t)(wait_ms / MS_PER_SECOND),
                                   .tv_usec = (int)(wait_ms % MS_PER_SECOND) * 1000 };
        int highest = listening > s_jobs_done[0] ? listening : s_jobs_done[0];
        if (select(highest + 1, &ready, NULL, NULL, &timeout) <= 0) {
            continue;
        }
        if (FD_ISSET(s_jobs_done[0], &ready)) {
            job_t *job;
            if (read(s_jobs_done[0], &job, sizeof job) == (ssize_t)sizeof job) {
                hand_over(job);
                free_job(job);
            }
        }
        if (FD_ISSET(listening, &ready)) {
            serve(listening);
        }
    }
}
