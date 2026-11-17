/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The device without the device: one application, run by the firmware's
 * own interpreter, host API, kit and window manager, on a computer. The
 * panel is a frame in memory, the buttons and the screen are the device's
 * development API (/api/dev) on localhost, the store and the files are
 * folders, the network is that of the program that started it - asked a
 * request a line on --fetcher-port, so that this program needs no HTTP
 * library - and answers can be replaced by mocks.
 *
 *     perinkpheral-sim --apps <folder> --app <name> --state <folder>
 *                      [--port 8401] [--language en] [--font departure|jersey]
 *                      [--glyphs <folder>] [--mocks <file.json>] [--clock]
 *                      [--at <unix time>] [--fetcher-port <port>]
 *
 * <apps>/<name> holds the application as tools/perinkpheral builds it: app.wasm,
 * manifest.json and its files. <state> keeps what the application stores.
 *
 * Beyond the device's API, /api/sim: background and foreground (the
 * application leaves the screen and is loaded only for what comes to it),
 * intent?kind=started|time_changed|network_up, alarm?id=N (now),
 * forward?seconds=N (the clock and every deadline move on).
 *
 * --at starts the clock at a moment of its own and lets it run from there:
 * the same screens at the same moment, to set a picture beside another.
 */
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "app_delivery.h"
#include "app_manifest.h"
#include "poll_schedule.h"
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
#include "appconf.h"
#include "header.h"
#include "scenes.h"
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
#define PATH_BYTES         PATH_MAX /* what realpath() writes: 1024 on macOS, 4096 on Linux */
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
    int         fetcher_port; /* the network of whoever started the simulator, on this port */
    int         port;
    bool        jersey;
    bool        clock;
    int64_t     at; /* where the clock starts; 0: the computer's */
} s_options = { .language = "en", .port = 8401 };

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

static int64_t s_at_started; /* the computer's time when the clock was set to --at */

static int64_t unix_now(void)
{
    int64_t now = (int64_t)time(NULL);
    if (s_options.at != 0) {
        now = s_options.at + (now - s_at_started);
    }
    return now + s_forward_ms / MS_PER_SECOND;
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
static uint32_t             s_poll_override_s[APP_POLLS_MAX];
static int32_t              s_alarm_every_s[HOST_ALARM_COUNT];
static int64_t              s_alarm_at[HOST_ALARM_COUNT];
static bool                 s_time_changed;
static uint8_t              s_bits[FRAME_BYTES];
static gfx_t                s_screen;
static uint32_t             s_frames;

static header_kept_t        s_header_kept;
static appconf_t            s_settings; /* the host's settings, by the device's table */
/* The simulator stands for one application, which is its id here. */
#define SIM_APP_ID 1u

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
    bool is_more_of_an_answer = delivery->kind == APP_DELIVERY_PIECE &&
                                (delivery->flags & (HOST_DATA_FIRST | HOST_DATA_FAILED)) == 0;
    if (s_app_scene != NULL && (s_wm.current != s_app_scene || s_wm.suspended) &&
        !delivery->only_on_screen && !is_more_of_an_answer) {
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


/* A setting of the manifest that has nothing stored reads as its default, as
 * on the device. */
static int32_t default_of(const char *key, void *value, int32_t capacity)
{
    char preset[APP_SETTING_VALUE_MAX + 1];
    int  length = s_manifest_json != NULL
                      ? app_manifest_setting_default(s_manifest_json, key, preset, sizeof preset)
                      : -1;
    if (length < 0) {
        return HOST_ERR_NOT_FOUND;
    }
    if (length > capacity) {
        return HOST_ERR_NO_SPACE;
    }
    memcpy(value, preset, (size_t)length);
    return length;
}

static int32_t kv_get(const char *app_name, const char *key, void *value, int32_t capacity)
{
    char path[PATH_BYTES];
    store_path(path, app_name, key);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return default_of(key, value, capacity);
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

/* `is_users`: written as the page writes a setting, which may set the server; the application may not. */
static int32_t kv_store(const char *app_name, const char *key, const void *value, int32_t length, bool is_users)
{
    char path[PATH_BYTES], folder[PATH_BYTES];
    store_path(path, app_name, key);
    if (length == 0) {
        remove(path);
        return HOST_OK;
    }
    if (!is_users && s_manifest_json != NULL && app_manifest_is_server_setting(s_manifest_json, key)) {
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

/* What the application writes. */
static int32_t kv_set(const char *app_name, const char *key, const void *value, int32_t length)
{
    return kv_store(app_name, key, value, length, false);
}

/* A setting as the host puts it into an address: what is stored, or the
 * manifest's default when nothing is, as on the device. With a context, the item of a list a poll with "each" stands for. */
typedef struct {
    const char *key;
    const char *item;
} list_item_t;

static bool setting_of(void *context, const char *key, char *value, size_t value_size)
{
    const list_item_t *each = context;
    if (each != NULL && strcmp(key, each->key) == 0) {
        return snprintf(value, value_size, "%s", each->item) < (int)value_size;
    }
    int32_t length = kv_get(s_options.app, key, value, (int32_t)value_size - 1);
    if (length > 0) {
        value[length] = '\0';
        return true;
    }
    return false;
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
    int         item; /* of the list a poll with "each" goes over; -1: none */
    /* the answer */
    int             status; /* HTTP, or a HOST_FAILED_* */
    uint8_t        *answer;
    size_t          answer_length;
    host_response_t response;
} job_t;

static bool            s_last_use_failed; /* what the header's icon says */
static pthread_mutex_t s_jobs_lock = PTHREAD_MUTEX_INITIALIZER;
static int             s_requests_waiting; /* of the application's own: HOST_HTTP_PENDING_MAX */
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

/* ---- base64, for bodies on their way to the network and back ---- */

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

/* ---- the network: a request a line out, an answer a line in ---- */

static FILE *s_fetcher_in;
static FILE *s_fetcher_out;

/* The network of the program that started the simulator, on a port of this
 * computer: the same lines over TCP. The CLion plugin answers there, in the
 * IDE's own process, which the system lets into the local network. */
static bool connect_fetcher(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_port = htons((uint16_t)s_options.fetcher_port) };
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof address) != 0) {
        if (fd >= 0) {
            close(fd);
        }
        return false;
    }
    s_fetcher_in = fdopen(dup(fd), "w");
    s_fetcher_out = fdopen(fd, "r");
    return s_fetcher_in != NULL && s_fetcher_out != NULL;
}

/* No port, no network: every request is "no network", as on a device off its Wi-Fi. */
static bool start_fetcher(void)
{
    return s_options.fetcher_port > 0 && connect_fetcher();
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

/* One exchange with the network: the answer, and where a redirect leads (`next`, empty
 * when nowhere). */
static void exchange(job_t *job, char *next, size_t next_size)
{
    next[0] = '\0';
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
    json_add_number(asked, "max", job->wants_values ? HOST_ANSWER_MAX : HOST_ANSWER_PIECES_MAX);
    /* The application's own certificate authorities, as files: trusted for its requests beside
     * the computer's, as on the device. */
    char files[MANIFEST_CERTIFICATES_MAX][MANIFEST_FILE_NAME_MAX + 1];
    int  trusted = s_manifest_json != NULL
                       ? app_manifest_certificates(s_manifest_json, files, MANIFEST_CERTIFICATES_MAX)
                       : 0;
    if (trusted > 0) {
        json_t *trust = json_new_array();
        for (int i = 0; i < trusted; i++) {
            /* Absolute: the network side runs in another process, from another folder. */
            char path[PATH_BYTES * 2], whole[PATH_MAX];
            snprintf(path, sizeof path, "%s/%s/%s", s_options.apps, s_options.app, files[i]);
            json_array_add(trust, json_new_string(realpath(path, whole) != NULL ? whole : path));
        }
        json_add_item(asked, "trust", trust);
    }
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
        /* The network is gone: it is asked again for the next request. */
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
    const json_t *where = json_get(answer, "next");
    if (json_is_string(where) && snprintf(next, next_size, "%s", where->text) >= (int)next_size) {
        next[0] = '\x01'; /* an address too long to follow: refused, as on the device */
        next[1] = '\0';
    }
    json_free(answer);
}

#define REDIRECTS_MAX 3

/* A request as the device makes it (poller_net.c): a GET that is answered with a redirect is
 * asked again where it says, three times at most; one with a body is not sent twice. A
 * request of the application's own stays inside its manifest's "hosts", and the headers it
 * gave do not travel to another host than the one it named. */
static void fetch(job_t *job)
{
    if (job->status < 0) {
        job->response.status = job->status;
        job->response.length = -1;
        return;
    }
    char first[APP_URL_MAX + 1];
    snprintf(first, sizeof first, "%s", job->url);
    for (int hop = 0;; hop++) {
        char next[APP_URL_MAX + 1];
        exchange(job, next, sizeof next);
        int code = job->status;
        bool is_redirect = code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
        if (job->method != HOST_HTTP_GET || !is_redirect || next[0] == '\0' || hop == REDIRECTS_MAX) {
            return;
        }
        bool is_refused = next[0] == '\x01';
        if (!is_refused && !app_manifest_is_same_host(first, next)) {
            is_refused = !job->is_poll && !app_manifest_allows_url(s_manifest_json, next, setting_of, NULL);
            free(job->headers);
            job->headers = NULL;
        }
        free(job->answer);
        job->answer = NULL;
        job->answer_length = 0;
        memset(&job->response, 0, sizeof job->response);
        if (is_refused) {
            /* Said in the log by hand_over(), on the thread the log belongs to. */
            job->status = HOST_FAILED_NOT_ALLOWED;
            job->response.status = job->status;
            job->response.length = -1;
            return;
        }
        snprintf(job->url, sizeof job->url, "%s", next);
    }
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
    if (job->status == HOST_FAILED_NOT_ALLOWED) {
        say('W', "sim", "%s: redirected to a host its manifest does not list", s_options.app);
    }
    bool failed = job->status == HOST_FAILED_NO_NETWORK || job->status == HOST_FAILED_NO_ANSWER ||
                  job->status == HOST_FAILED_TIMEOUT;
    if (failed != s_last_use_failed && job->status != HOST_FAILED_NOT_READY) {
        s_last_use_failed = failed;
        wm_invalidate(&s_wm, WM_CHANGED_BY_DATA);
    }
    const char *kind = job->is_poll ? "poll" : "request";
    char        item[16] = "";
    if (job->item >= 0) {
        snprintf(item, sizeof item, ", item %d", job->item);
    }
    if (!job->is_poll) {
        s_requests_waiting--;
    }
    if (job->status < 0) {
        say('W', "sim", "%s/%s %d%s: failed (%d)", s_options.app, kind, job->number, item,
            job->status);
        app_delivery_t failed = {
            .kind = job->wants_values ? APP_DELIVERY_VALUES : APP_DELIVERY_PIECE,
            .request = job->number, .http_status = job->status, .flags = HOST_DATA_FAILED,
            .has_item = job->item >= 0, .item = job->item,
        };
        deliver(&failed);
        return;
    }
    say('I', "sim", "%s/%s %d%s: HTTP %d, %zu bytes", s_options.app, kind, job->number, item,
        job->status, job->answer_length);
    if (job->wants_values) {
        const char *paths[JSON_PICK_PATHS_MAX];
        for (int i = 0; i < job->pick_count; i++) {
            paths[i] = job->pick[i];
        }
        static json_pick_t pick;
        static char        list[HOST_LIST_BYTES];
        json_pick_init(&pick, paths, job->pick_count);
        json_pick_lists(&pick, list, sizeof list);
        bool is_json = json_pick_feed(&pick, (const char *)job->answer, job->answer_length);
        is_json = json_pick_finish(&pick) && is_json; /* as the device: a document that ends whole */
        if (json_pick_is_cut(&pick)) {
            say('W', "sim", "%s/%s %d: list elements beyond %d bytes were dropped", s_options.app,
                kind, job->number, HOST_LIST_BYTES);
        }
        app_delivery_t delivery = {
            .kind = APP_DELIVERY_VALUES, .request = job->number,
            .http_status = is_json ? job->status : HOST_FAILED_NO_ANSWER,
            .response = &job->response, .picked = &pick,
            .has_item = job->item >= 0, .item = job->item,
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
            .has_item = job->item >= 0, .item = job->item,
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

/* One request of a poll: for item `item` of the list it goes over, or -1. */
static void start_poll_request(const app_poll_t *poll, int number, const list_item_t *each,
                               int item)
{
    job_t *job = calloc(1, sizeof *job);
    job->is_poll = true;
    job->number = number;
    job->item = item;
    job->method = HOST_HTTP_GET;
    job->wants_values = poll->pick_count > 0;
    job->pick_count = poll->pick_count;
    memcpy(job->pick, poll->pick, sizeof poll->pick);
    bool is_ready = text_template_fill_url(poll->url, setting_of, (void *)each, job->url,
                                           sizeof job->url);
    for (int i = 0; is_ready && i < poll->header_count; i++) {
        char value[APP_HEADER_VALUE_MAX + 1];
        is_ready = text_template_fill(poll->headers[i].value, setting_of, (void *)each, value,
                                      sizeof value) &&
                   strpbrk(value, "\r\n") == NULL;
        if (is_ready) {
            add_header(&job->headers, poll->headers[i].name, value);
        }
    }
    if (!is_ready) {
        job->status = HOST_FAILED_NOT_READY;
        hand_over(job);
        free_job(job);
        return;
    }
    ask(job);
}

/* A poll: one request, or one for every item of the list it goes over. */
static void start_poll(int number)
{
    static app_poll_t poll;
    if (app_manifest_parse_poll(s_manifest_json, number, &poll) != APP_MANIFEST_OK) {
        return;
    }
    char list[APP_SETTING_VALUE_MAX + 1];
    if (poll.each_key[0] == '\0' || !setting_of(NULL, poll.each_key, list, sizeof list)) {
        start_poll_request(&poll, number, NULL, -1);
        return;
    }
    char *at = list;
    for (int item = 0; at != NULL; item++) {
        char *end = strchr(at, '\n');
        if (end != NULL) {
            *end = '\0';
        }
        /* An item another poll of the list claims by its prefix is passed over; {{key}} is
         * the item without this poll's prefix. */
        const char *value;
        if (app_manifest_poll_takes(&poll, at, &value)) {
            list_item_t each = { .key = poll.each_key, .item = value };
            start_poll_request(&poll, number, &each, item);
        }
        at = end != NULL ? end + 1 : NULL;
    }
}

/* ---- the services the application's host calls ---- */

static int64_t services_unix_time(void)
{
    int64_t now = unix_now();
    return now >= HOST_TIME_MIN && now <= HOST_TIME_MAX ? now : 0;
}

static int32_t offset_at(int64_t unix_time)
{
    if (unix_time < HOST_TIME_MIN || unix_time > HOST_TIME_MAX) {
        return 0;
    }
    time_t    moment = (time_t)unix_time;
    struct tm local;
    if (localtime_r(&moment, &local) == NULL) {
        return 0;
    }
    return (int32_t)local.tm_gmtoff;
}

static int32_t services_offset(void) { return offset_at(unix_now()); }
static int64_t services_uptime(void) { return monotonic_ms() - s_started_ms; }
static uint32_t services_boot_id(void) { return s_boot_id; }

static void services_log(int level, const char *text, int length)
{
    static const char LEVELS[] = "EWID";
    /* HOST_LOG_ERROR is 1. */
    say(level >= HOST_LOG_ERROR && level <= HOST_LOG_DEBUG ? LEVELS[level - HOST_LOG_ERROR] : 'I',
        "app", "%.*s", length, text); /* as the device tags it: what --app and the page's filter look for */
}

static void services_timer(int id, int32_t milliseconds)
{
    if (milliseconds <= 0) {
        cancel_due(DUE_TIMER, id);
    } else {
        set_due(DUE_TIMER, id, milliseconds);
    }
}

static bool services_dithers(void) { return s_settings.dither != 0; }

static bool services_poll_now(const char *app_name, int poll)
{
    (void)app_name;
    if (poll < 0 || poll >= s_manifest.poll_count) {
        return false;
    }
    set_due(DUE_POLL, poll, 0);
    return true;
}

static uint32_t manifest_poll_seconds(int poll)
{
    char pace[32];
    if (s_manifest.poll_every_key[poll][0] != '\0' &&
        setting_of(NULL, s_manifest.poll_every_key[poll], pace, sizeof pace)) {
        uint32_t chosen = poll_schedule_normalize_interval(app_manifest_interval_seconds(pace));
        if (chosen > 0) {
            return chosen;
        }
    }
    return s_manifest.poll_every_s[poll];
}

/* Host records stay outside the application's store and its quota. */
static void poll_override_path(char path[PATH_BYTES], const char *app_name, int poll)
{
    snprintf(path, PATH_BYTES, "%s/host/%s-poll-%d", s_options.state, app_name, poll);
}

static int32_t services_poll_interval(const char *app_name, int poll, uint32_t every_s)
{
    if (poll < 0 || poll >= s_manifest.poll_count ||
        (every_s != 0 && (every_s < APP_POLL_EVERY_MIN_S || every_s > APP_POLL_EVERY_MAX_S))) {
        return HOST_ERR_ARGUMENT;
    }
    if (every_s == 0 || s_poll_override_s[poll] != every_s) {
        char path[PATH_BYTES], folder[PATH_BYTES];
        poll_override_path(path, app_name, poll);
        if (every_s == 0) {
            if (remove(path) != 0 && errno != ENOENT) {
                return HOST_ERR_FAILED;
            }
        } else {
            snprintf(folder, sizeof folder, "%s/host", s_options.state);
            make_folders(folder);
            FILE *file = fopen(path, "wb");
            if (file == NULL) {
                return HOST_ERR_FAILED;
            }
            bool written = fwrite(&every_s, sizeof every_s, 1, file) == 1;
            if (fclose(file) != 0 || !written) {
                return HOST_ERR_FAILED;
            }
        }
        s_poll_override_s[poll] = every_s;
    }
    uint32_t effective_s = every_s != 0 ? every_s : manifest_poll_seconds(poll);
    if (s_poll_every_s[poll] != effective_s) {
        s_poll_every_s[poll] = effective_s;
        set_due(DUE_POLL, poll, (int64_t)effective_s * MS_PER_SECOND);
    }
    return HOST_OK;
}

static int32_t services_request(const char *app_name, const app_request_t *request)
{
    if (s_requests_waiting >= HOST_HTTP_PENDING_MAX) {
        return HOST_ERR_NO_SPACE;
    }
    char url[APP_URL_MAX + 1];
    if (!text_template_fill_url(request->url, setting_of, NULL, url, sizeof url) ||
        !app_manifest_allows_url(s_manifest_json, url, setting_of, NULL)) {
        say('W', "sim", "%s: a request to a host its manifest does not list", app_name);
        return HOST_ERR_ARGUMENT;
    }
    job_t *job = calloc(1, sizeof *job);
    job->number = request->tag;
    job->item = -1;
    job->method = request->method;
    snprintf(job->url, sizeof job->url, "%s", url);
    for (int i = 0; i < request->header_count; i++) {
        char value[APP_HEADER_VALUE_MAX + 1];
        if (!text_template_fill(request->headers[i].value, setting_of, NULL, value, sizeof value) ||
            strpbrk(value, "\r\n") != NULL) {
            job->status = HOST_FAILED_NOT_READY;
            break;
        }
        add_header(&job->headers, request->headers[i].name, value);
    }
    if (request->body_length > 0) {
        job->body = malloc((size_t)request->body_length);
        memcpy(job->body, request->body, (size_t)request->body_length);
        job->body_length = (size_t)request->body_length;
    }
    s_requests_waiting++;
    ask(job);
    return HOST_OK;
}

static bool services_alarm(const char *app_name, int alarm, int64_t when, bool is_absolute)
{
    (void)app_name;
    s_alarm_every_s[alarm] = 0;
    s_alarm_at[alarm] = is_absolute ? when : 0;
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
    s_alarm_at[alarm] = 0;
    set_due(DUE_ALARM, alarm, (int64_t)first_in_s * MS_PER_SECOND);
    return true;
}

static void services_header_status(const char *app_name, int side, int icon, const char *text)
{
    (void)app_name;
    if (header_keep(&s_header_kept, SIM_APP_ID, side, icon, text)) {
        wm_invalidate(&s_wm, WM_CHANGED_BY_DATA);
    }
}

static const char *services_language(void) { return s_settings.language; }

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

/* Off the screen however it went - a long BACK empties the stack without a press for it - so the
 * next one, or this one again, is shown when there is room. */
static void notice_leave(scene_t *scene)
{
    (void)scene;
    s_notice_is_open = false;
}

static const scene_ops_t NOTICE_OPS = { .button = notice_button, .draw = notice_scene_draw,
                                        .leave = notice_leave };
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

/* ---- the host's settings, by key: the device's own table and rules, kept
 * in memory - the simulator's options are where they start ---- */

static int32_t services_setting_get(const char *key, char *value, int32_t capacity)
{
    const appconf_field_t *field = appconf_field(key, strlen(key));
    if (field == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    if ((field->flags & APPCONF_SECRET) != 0) {
        return HOST_ERR_ARGUMENT;
    }
    char text[APPCONF_VALUE_MAX];
    appconf_format(&s_settings, field, text, sizeof text);
    size_t length = strlen(text);
    if (length > (size_t)capacity) {
        return HOST_ERR_NO_SPACE;
    }
    memcpy(value, text, length);
    return (int32_t)length;
}

static int32_t services_setting_range(const char *key, int32_t *value, int32_t *least, int32_t *most)
{
    const appconf_field_t *field = appconf_field(key, strlen(key));
    if (field == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    if (field->kind == APPCONF_TEXT) {
        return HOST_ERR_ARGUMENT;
    }
    *value = appconf_number(&s_settings, field);
    *least = field->least;
    *most = field->most;
    return HOST_OK;
}

/* What the simulator draws follows the store, as the device does: the
 * typeface, the language, the clock in the header. */
static int32_t services_setting_set(const char *key, const char *value)
{
    const appconf_field_t *field = appconf_field(key, strlen(key));
    appconf_t wanted = s_settings;
    char      problem[APPCONF_PROBLEM_MAX];
    if (field == NULL || (field->flags & APPCONF_OWN_WAY) != 0 || !appconf_put(&wanted, field, value) ||
        !appconf_validate(&wanted, problem, sizeof problem)) {
        return HOST_ERR_ARGUMENT;
    }
    s_settings = wanted;
    say('I', "sim", "setting %s = %s", key, value);
    gfx_font_use((gfx_family_t)s_settings.font);
    words_use(s_settings.language);
    s_options.clock = s_settings.header_clock != 0;
    if (strcmp(key, "tz") == 0) {
        /* The simulator's local time is the C library's: the zone is its. */
        setenv("TZ", s_settings.tz, 1);
        tzset();
    }
    wm_invalidate(&s_wm, WM_CHANGED_BY_DATA);
    return HOST_OK;
}

static int32_t services_time_set(int64_t unix_seconds)
{
    if (unix_seconds < HOST_TIME_MIN || unix_seconds > HOST_TIME_MAX) {
        return HOST_ERR_ARGUMENT;
    }
    if (s_settings.time_auto != 0) {
        return HOST_ERR_FAILED;
    }
    s_options.at = unix_seconds - s_forward_ms / MS_PER_SECOND;
    s_at_started = (int64_t)time(NULL);
    for (int alarm = 0; alarm < HOST_ALARM_COUNT; alarm++) {
        if (s_alarm_at[alarm] != 0) {
            int64_t left_s = s_alarm_at[alarm] - unix_seconds;
            set_due(DUE_ALARM, alarm, left_s > 0 ? left_s * MS_PER_SECOND : 0);
        }
    }
    s_time_changed = true;
    set_due(DUE_MINUTE, 0, (SECONDS_PER_MINUTE - unix_seconds % SECONDS_PER_MINUTE) * MS_PER_SECOND);
    wm_invalidate(&s_wm, WM_CHANGED_BY_DATA);
    return HOST_OK;
}

static const app_services_t SERVICES = {
    .system_setting_get = services_setting_get,
    .system_setting_set = services_setting_set,
    .system_setting_range = services_setting_range,
    .system_time_set = services_time_set,
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

/* ---- the menu: the device's own, with the application as its one row ---- */

static menu_entry_t s_menu_entry;
static scene_t     *s_menu;

/* ---- the header, as the host draws it ---- */

static void draw_header(gfx_t *surface, const scene_t *scene, void *context)
{
    (void)context;
    time_t       now = (time_t)unix_now();
    header_now_t header_now = {
        /* As on the device: whether the last use of the network worked. */
        .network_ok = !s_last_use_failed,
        .time_known = true,
        .shows_clock = s_options.clock,
        .scene_left = scene->header_left,
        .scene_right = scene->header_right,
    };
    localtime_r(&now, &header_now.local);
    char             when[HEADER_WHEN_MAX + 1];
    screens_header_t header = header_compose(&s_header_kept, &header_now, when, sizeof when);
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

/* An application that has not run here before, or ran as another version,
 * is told so, as the device tells it: what ran last is kept in the state. */
static void tell_new_version(void)
{
    /* What the device calls the version: its module and its manifest. */
    uint32_t id = 2166136261u;
    char     path[PATH_BYTES];
    snprintf(path, sizeof path, "%s/%s/app.wasm", s_options.apps, s_options.app);
    FILE *file = fopen(path, "rb");
    for (int byte; file != NULL && (byte = fgetc(file)) != EOF;) {
        id = (id ^ (uint8_t)byte) * 16777619u;
    }
    if (file != NULL) {
        fclose(file);
    }
    for (const char *at = s_manifest_json; *at != '\0'; at++) {
        id = (id ^ (uint8_t)*at) * 16777619u;
    }
    snprintf(path, sizeof path, "%s/seen", s_options.state);
    uint32_t seen = 0;
    file = fopen(path, "rb");
    if (file != NULL) {
        if (fread(&seen, sizeof seen, 1, file) != 1) {
            seen = 0;
        }
        fclose(file);
    }
    if (seen == id) {
        return;
    }
    file = fopen(path, "wb");
    if (file != NULL) {
        fwrite(&id, sizeof id, 1, file);
        fclose(file);
    }
    say('I', "sim", "%s: %s", s_options.app, seen != 0 ? "updated" : "installed");
    app_delivery_t delivery = { .kind = seen != 0 ? APP_DELIVERY_UPDATED : APP_DELIVERY_INSTALLED };
    deliver(&delivery);
}

static bool load_app(void)
{
    if (s_app_scene != NULL) {
        wm_close(&s_wm, s_app_scene);
        app_delivery_init(&s_delivery);
        app_scene_destroy(s_app_scene);
        s_app_scene = NULL;
        /* Nothing in the menu until a new one loads: a load that fails
         * leaves no row that opens the scene just freed. */
        s_menu = scene_menu(&s_menu_entry, 0);
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
    char *translated = app_store_read_translation(s_options.apps, s_options.app, s_settings.language);
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
    memset(s_alarm_at, 0, sizeof s_alarm_at);
    s_time_changed = false;
    memset(&s_header_kept, 0, sizeof s_header_kept);
    for (int poll = 0; poll < s_manifest.poll_count; poll++) {
        char path[PATH_BYTES];
        poll_override_path(path, s_options.app, poll);
        FILE *file = fopen(path, "rb");
        uint32_t kept_s = 0;
        if (file != NULL) {
            if (fread(&kept_s, sizeof kept_s, 1, file) != 1 ||
                kept_s < APP_POLL_EVERY_MIN_S || kept_s > APP_POLL_EVERY_MAX_S) {
                kept_s = 0;
            }
            fclose(file);
        }
        s_poll_override_s[poll] = kept_s;
        s_poll_every_s[poll] = kept_s != 0 ? kept_s : manifest_poll_seconds(poll);
        set_due(DUE_POLL, poll, FIRST_POLLS_MS);
    }
    set_due(DUE_MINUTE, 0, (SECONDS_PER_MINUTE - unix_now() % SECONDS_PER_MINUTE) * MS_PER_SECOND);
    say('I', "sim", "%s %s loaded", s_manifest.name, s_manifest.version);
    /* As after a restart of the device: news for whoever asked, then the screen. */
    tell_new_version();
    intent(HOST_INTENT_STARTED);
    s_menu_entry = (menu_entry_t){ .title = s_manifest.title,
                                   .icon = s_manifest.has_icon ? s_manifest.icon : NULL,
                                   .scene = s_app_scene,
                                   .name = s_manifest.name };
    s_menu = scene_menu(&s_menu_entry, 1);
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
        s_alarm_at[id->number] = 0;
        if (s_alarm_every_s[id->number] > 0) {
            set_due(DUE_ALARM, id->number, (int64_t)s_alarm_every_s[id->number] * MS_PER_SECOND);
        }
        app_delivery_t delivery = { .kind = APP_DELIVERY_ALARM, .request = id->number };
        deliver(&delivery);
        break;
    }
    case DUE_POLL:
        /* The next time first: what the poll brings - an answer of "not ready" is delivered
         * at once - may ask for it sooner (poll_now), and that is not to be undone. */
        set_due(DUE_POLL, id->number, (int64_t)s_poll_every_s[id->number] * MS_PER_SECOND);
        start_poll(id->number);
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
                                "Access-Control-Expose-Headers: X-Frame, X-Pending, X-Log-End, X-Log-Boot\r\n"
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
    char headers[80];
    snprintf(headers, sizeof headers, "X-Log-End: %llu\r\nX-Log-Boot: %lu\r\n", (unsigned long long)s_log_written,
             (unsigned long)s_boot_id);
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

/* The page of the tools (tools/cli: its screen, buttons and log), made into page.inc by the
 * Makefile: what `perinkpheral serve` shows, here at the simulator's own address. */
static const unsigned char PAGE[] = {
#include "page.inc"
};

static void answer_page(int socket_fd)
{
    /* No application to build and run from here: the Run button stays hidden. */
    static const char PLACE[] = "{{app}}";
    const char *page = (const char *)PAGE;
    size_t      length = sizeof PAGE;
    const char *at = memmem(page, length, PLACE, sizeof PLACE - 1);
    char       *body = malloc(length);
    size_t      before = at != NULL ? (size_t)(at - page) : length;
    memcpy(body, page, before);
    size_t written = before;
    if (at != NULL) {
        size_t after = length - before - (sizeof PLACE - 1);
        memcpy(body + written, at + sizeof PLACE - 1, after);
        written += after;
    }
    answer(socket_fd, 200, "text/html; charset=utf-8", "Cache-Control: no-store\r\n", body, written);
    free(body);
}

static void handle(int socket_fd, const request_t *request)
{
    char value[PATH_BYTES], name[PATH_BYTES];
    bool is_post = strcmp(request->method, "POST") == 0;
    const char *path = request->path;
    if (!is_post && strcmp(path, "/") == 0) {
        answer_page(socket_fd);
    } else if (strcmp(path, "/api/status") == 0) {
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
                             ? kv_store(s_options.app, name, request->body, (int32_t)request->body_length, true)
                             : kv_store(s_options.app, name, "", 0, true);
        app_delivery_t settings = { .kind = APP_DELIVERY_SETTINGS };
        deliver(&settings);
        for (int poll = 0; poll < s_manifest.poll_count; poll++) {
            if (s_poll_override_s[poll] == 0) {
                s_poll_every_s[poll] = manifest_poll_seconds(poll);
            }
            set_due(DUE_POLL, poll, 0);
        }
        answer_text(socket_fd, stored == HOST_OK ? 200 : 400, stored == HOST_OK ? "stored" : "refused");
    } else if (is_post && strcmp(path, "/api/sim/intent") == 0) {
        query_value(request, "kind", name, sizeof name);
        int kind = strcmp(name, "started") == 0         ? HOST_INTENT_STARTED
                   : strcmp(name, "time_changed") == 0  ? HOST_INTENT_TIME_CHANGED
                   : strcmp(name, "network_up") == 0    ? HOST_INTENT_NETWORK_UP
                                                        : 0;
        bool is_installed = strcmp(name, "installed") == 0, is_updated = strcmp(name, "updated") == 0;
        if (is_installed || is_updated) {
            app_delivery_t delivery = { .kind = is_installed ? APP_DELIVERY_INSTALLED
                                                             : APP_DELIVERY_UPDATED };
            deliver(&delivery);
            answer_text(socket_fd, 200, "told");
            return;
        }
        if (kind == 0) {
            answer_text(socket_fd, 400, "kind=started|time_changed|network_up|installed|updated");
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

/* A connection that says nothing this long after it was made is one a browser opened ahead of a
 * request it may never make: it is closed, and the browser opens another when it has one. */
#define IDLE_CONNECTION_MS 250
/* A request that stops halfway is given up after this. */
#define REQUEST_STALL_S 5

/* One request on one connection: the device's server answers so, and closes. The simulator
 * serves one connection at a time on its only thread, so none may keep it waiting. */
static void serve(int listening)
{
    int connection = accept(listening, NULL, NULL);
    if (connection < 0) {
        return;
    }
    struct pollfd first = { .fd = connection, .events = POLLIN };
    if (poll(&first, 1, IDLE_CONNECTION_MS) <= 0) {
        close(connection);
        return;
    }
    struct timeval stall = { .tv_sec = REQUEST_STALL_S };
    setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &stall, sizeof stall);
    setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &stall, sizeof stall);
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
                    "                 [--mocks <file.json>] [--clock] [--at <unix time>]\n"
                    "                 [--fetcher-port <port>]\n");
    exit(2);
}

static volatile sig_atomic_t s_is_stopping;

static void on_stop(int signal_number)
{
    (void)signal_number;
    s_is_stopping = 1;
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
        else if (strcmp(argv[i], "--at") == 0 && next) s_options.at = atoll(argv[++i]);
        else if (strcmp(argv[i], "--fetcher-port") == 0 && next) s_options.fetcher_port = atoi(argv[++i]);
        else usage();
    }
    if (s_options.apps == NULL || s_options.app == NULL || s_options.state == NULL) {
        usage();
    }
    s_at_started = (int64_t)time(NULL);
    /* The store's paths are as short as the device's ("/apps/<name>/<file>"):
     * the folder of applications becomes the working one, and the other
     * folders are made absolute before that. */
    static char state[PATH_BYTES], mocks[PATH_BYTES], glyphs[PATH_BYTES];
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
    /* Stopped, the application is unloaded as on a restart of the device:
     * app_on_terminate() saves what it has. */
    signal(SIGINT, on_stop);
    signal(SIGTERM, on_stop);
    s_log = malloc(LOG_KEPT_BYTES);
    s_started_ms = monotonic_ms();
    srand((unsigned)time(NULL));
    s_boot_id = (uint32_t)rand() | 1u;
    appconf_defaults(&s_settings, "0000");
    s_settings.font = s_options.jersey ? GFX_FAMILY_JERSEY : GFX_FAMILY_DEPARTURE_MONO;
    s_settings.header_clock = s_options.clock ? 1 : 0;
    snprintf(s_settings.language, sizeof s_settings.language, "%s", s_options.language);
    /* The zone is the computer's until Settings sets another: as TZ names
     * it, or - with no TZ - as the offset it has now, in the POSIX form the
     * device keeps ("<+0300>-3"), which then becomes TZ, so that what the
     * store says and what the clock does are one. */
    const char *zone = getenv("TZ");
    if (zone != NULL && zone[0] != '\0' && strlen(zone) < sizeof s_settings.tz) {
        snprintf(s_settings.tz, sizeof s_settings.tz, "%s", zone);
    } else {
        int32_t offset = offset_at(unix_now());
        int32_t east = offset < 0 ? -offset : offset;
        int32_t hours = east / 3600, minutes = east % 3600 / 60;
        char    sign = offset < 0 ? '-' : '+', posix_sign = offset < 0 ? '+' : '-';
        if (minutes == 0) {
            snprintf(s_settings.tz, sizeof s_settings.tz, "<%c%02d>%c%d", sign, (int)hours,
                     posix_sign, (int)hours);
        } else {
            snprintf(s_settings.tz, sizeof s_settings.tz, "<%c%02d%02d>%c%d:%02d", sign,
                     (int)hours, (int)minutes, posix_sign, (int)hours, (int)minutes);
        }
        setenv("TZ", s_settings.tz, 1);
        tzset();
    }
    char problem[APPCONF_PROBLEM_MAX];
    if (!appconf_validate(&s_settings, problem, sizeof problem)) {
        /* A language the device does not know: the store stays one the
         * device could have, and every setting can still be changed. */
        say('W', "sim", "%s: %s", s_options.language, problem);
        snprintf(s_settings.language, sizeof s_settings.language, "en");
    }
    words_use(s_settings.language);
    gfx_font_use(s_options.jersey ? GFX_FAMILY_JERSEY : GFX_FAMILY_DEPARTURE_MONO);
    load_glyphs();
    load_mocks();
    if (pipe(s_jobs_done) != 0) {
        return 1;
    }
    pthread_t thread;
    pthread_create(&thread, NULL, network_thread, NULL);

    gfx_init(&s_screen, s_bits, SCREEN_WIDTH, SCREEN_HEIGHT);
    s_menu = scene_menu(&s_menu_entry, 0);
    wm_init(&s_wm, s_menu, s_menu, draw_header, NULL);
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

    while (!s_is_stopping) {
        /* System calls can be made inside an application handler. Deliver
         * the clock change after it returns, without reentering the module. */
        if (s_time_changed) {
            s_time_changed = false;
            intent(HOST_INTENT_TIME_CHANGED);
        }
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
    say('I', "sim", "stopping: %s is unloaded", s_options.app);
    if (s_app_scene != NULL) {
        wm_close(&s_wm, s_app_scene);
        app_scene_destroy(s_app_scene);
    }
    stop_fetcher();
    return 0;
}
