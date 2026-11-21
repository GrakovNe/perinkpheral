/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The stand-in host: Perinkpheral OS as an application sees it, for this
 * computer. It defines every call of sdk/host_api.h an application may
 * import (the system API apart), so an application's C is compiled as it
 * is, with the sanitizers, and its handlers are called directly.
 *
 * It is a second reading of the contract, not the firmware's code: the
 * store, the session, the clock, timers, alarms, files and the delivery of
 * answers behave as the contract says; the drawing calls draw nothing and
 * record what they were asked, with text measured as Departure Mono is.
 * What the panel shows is the simulator's to say (tools/perinkpheral
 * sim-test), which runs the firmware's own code.
 *
 * Two users. The capability checks (checks.c) run any application through
 * it knowing nothing of the application. An application's own tests
 * (the .c files of its test folder) include the application's source, call its
 * handlers and read standin() for what the host saw.
 *
 * Nothing here moves by itself: the clock stands until it is passed, a
 * timer comes when the test fires it, an answer arrives as the test cuts it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host_api.h"

/* ---- What the tools know of the application: its manifest and what is kept beside it ---- */

#define STANDIN_SETTING_PLAIN 0
#define STANDIN_SETTING_USERS 1 /* "url" and "link": kv_set is refused */
#define STANDIN_SETTING_LIST  2

typedef struct {
    const char *key;
    int32_t     kind;
    const char *otherwise; /* the manifest's default, a list's items with line breaks; NULL: none */
    const char *longest;   /* a value as long as the setting can hold, for the store's check */
} standin_setting_t;

typedef struct {
    const char        *url;   /* as the manifest writes it, {{key}} in it */
    const char *const *picks; /* NULL: the answer is handed on in pieces */
    int32_t            pick_count;
    const char        *each;   /* the list setting it is asked over, or NULL */
    const char        *prefix; /* the items it claims, or NULL */
} standin_poll_t;

/* An answer kept for an address, as the simulator's mocks are: an address
 * ending in '*' answers every address that begins with the rest. */
typedef struct {
    const char *address;
    int32_t     status;
    const char *file; /* the body's file, or NULL when `body` is it */
    const char *body;
} standin_answer_t;

typedef struct {
    const char *name; /* on the device */
    const char *file; /* on this computer */
} standin_file_t;

typedef struct {
    const char *key, *value;
} standin_pair_t;

/* From `at` on, the zone's offset is `offset`: the changes of its rules, written by the tools. */
typedef struct {
    int64_t at;
    int32_t offset;
} standin_zone_change_t;

typedef struct {
    const char              *name;
    bool                     is_fullscreen, writes_files;
    const standin_setting_t *settings;
    int32_t                  setting_count;
    const standin_poll_t    *polls;
    int32_t                  poll_count;
    const char *const       *hosts;
    int32_t                  host_count;
    uint32_t                 intents; /* a bit a HOST_INTENT_* */
    const standin_file_t    *files;   /* the package's `files` folder */
    int32_t                  file_count;
    /* test/check.json beside the application: where and when it is checked. */
    int64_t                  at;
    int32_t                  zone_offset; /* before the first change */
    const standin_zone_change_t *zone_changes;
    int32_t                  zone_change_count;
    int32_t                  arena_bytes; /* what the module's page leaves for app_alloc(); 0: 64 KB */
    const standin_pair_t    *store;
    int32_t                  store_count;
    const standin_answer_t  *answers;
    int32_t                  answer_count;
} standin_subject_t;

/* Made by the tools for every program linked with the stand-in. */
extern const standin_subject_t standin_subject;

/* The application's handlers; NULL for one it does not export. The checks
 * are given them by the tools; a test of an application sets those it
 * wants the stand-in to call. */
typedef struct {
    void (*on_load)(void);
    void (*on_focus)(void);
    void (*on_blur)(void);
    void (*on_terminate)(void);
    void (*on_installed)(void);
    void (*on_updated)(void);
    void (*on_settings)(void);
    void (*on_intent)(int32_t intent);
    void (*on_back)(void);
    void (*on_button)(int32_t event);
    void (*on_timer)(int32_t id);
    void (*on_alarm)(int32_t id);
    void (*on_draw)(void);
    void (*on_data)(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t http_status);
    void (*on_picked)(int32_t request, int32_t http_status);
} standin_handlers_t;
extern standin_handlers_t standin_handlers;

/* ---- The host's state: what a test reads, and what it may set ---- */

#define STANDIN_STORE_VALUES 32
#define STANDIN_FILES        24 /* the package's files and what it writes: 16 blocks and 8 */
#define STANDIN_FILE_BYTES   HOST_FILES_MAX
#define STANDIN_MARKS        64
#define STANDIN_NOTE_BYTES   200
#define STANDIN_NOTES        16
#define STANDIN_URL_BYTES    512

#define STANDIN_ALARM_NONE  0
#define STANDIN_ALARM_IN    1 /* alarm_set: by the uptime */
#define STANDIN_ALARM_AT    2 /* alarm_set_at: by the clock */
#define STANDIN_ALARM_EVERY 3

typedef struct {
    char    key[HOST_KV_KEY_MAX + 1];
    bool    is_stored;
    int32_t length;
    uint8_t value[HOST_KV_VALUE_MAX];
} standin_value_t;

typedef struct {
    char    name[HOST_FILE_NAME_MAX + 1];
    bool    is_stored;
    int32_t size;
    uint8_t bytes[STANDIN_FILE_BYTES];
} standin_stored_file_t;

typedef struct {
    int32_t kind;
    int64_t due_uptime_ms; /* IN and EVERY */
    int64_t at;            /* AT */
    int32_t every;
} standin_alarm_t;

typedef struct {
    int32_t tag, method;
    char    url[STANDIN_URL_BYTES];
    char    headers[HOST_HTTP_HEADERS_MAX + 1];
    int32_t headers_length;
    uint8_t body[HOST_HTTP_BODY_MAX + 1];
    int32_t body_length;
} standin_request_t;

typedef struct {
    /* The clock. Set by standin_clock(), moved by standin_pass(). */
    int64_t  now;         /* Unix seconds; 0: the device does not know the time */
    int32_t  zone_offset;
    int64_t  uptime_ms;
    uint32_t boot_id;
    int64_t  clock_ms_part; /* of the second the clock is in */

    /* The store and the files, with the device's accounting. */
    standin_value_t       values[STANDIN_STORE_VALUES];
    standin_stored_file_t files[STANDIN_FILES];
    int32_t refuse_writes;   /* a HOST_ERR_*: every write of the store and the files answers it */
    int32_t store_writes;    /* kv_set calls the store took */
    int32_t store_changes;   /* those that changed what it holds: the device's store writes the flash
                                for no other (NVS compares first) */
    int32_t file_writes;
    int32_t refused_writes;  /* answered with an error: the limits, or refuse_writes */
    int32_t handler_writes;  /* attempts since the handler began */
    int32_t most_handler_writes;
    char    refused_what[STANDIN_NOTE_BYTES]; /* the first write the limits refused */

    /* The session: kept while the application is in the stack. */
    uint8_t session[HOST_SESSION_MAX];
    int32_t session_length;
    bool    session_inverted; /* the screen's inversion, kept with the session */

    /* Where the instance is. */
    bool is_new; /* installed and never loaded: its first load is told app_on_installed() */
    bool is_loaded, is_loading, is_on_screen, is_session_writable, is_drawing, is_delivering;
    const char *handler; /* the one running, for what is said of a fault */

    /* Timers of the instance on the screen, alarms of the application. */
    int64_t         timer_due_ms[HOST_TIMER_COUNT]; /* by the uptime; 0: not set */
    standin_alarm_t alarms[HOST_ALARM_COUNT];

    /* What the handlers left for when they return. */
    int32_t redraws, redraw_flags;
    bool    is_leaving, wants_screen, is_inverted;
    int32_t notices;
    host_notice_t last_notice;
    char    header_text[2][HOST_HEADER_TEXT_MAX + 1];
    char    status_text[2][HOST_STATUS_TEXT_MAX + 1];
    int32_t status_icon[2];
    int32_t logs, error_logs;
    char    last_log[HOST_LOG_TEXT_MAX + 1];

    /* The network. */
    standin_request_t requests[HOST_HTTP_PENDING_MAX];
    int32_t request_count;
    int32_t refuse_requests; /* a HOST_ERR_*: http_send answers it */
    char    refused_request[STANDIN_NOTE_BYTES]; /* the first request the device refuses for what it asks */
    int32_t sends;
    int32_t polls_run[4];
    int32_t poll_interval[4];
    int32_t answer_item;

    /* Drawing: what the frame was made of, not its pixels. */
    uint64_t picture;      /* of the last app_on_draw() */
    int32_t  draw_calls;   /* in it */
    int32_t  draws;        /* frames drawn */
    int32_t  refused_draws; /* drawing calls the host answered with an error, in a frame */
    char     refused_draw[STANDIN_NOTE_BYTES];
    int32_t  stray_draws;  /* drawing calls outside app_on_draw() */
    char     stray_draw[STANDIN_NOTE_BYTES];

    /* A run started by standin_live() leaves its results here for who ran it. */
    uint64_t marks[STANDIN_MARKS];
    char     notes[STANDIN_NOTES][STANDIN_NOTE_BYTES];
    int32_t  note_count;
} standin_t;

/* The host's state. One for the program, kept where a forked run shares it. */
standin_t *standin(void);

/* A device as it is when the application was just installed: an empty
 * store, the package's files, no session, the clock at the subject's `at`. */
void standin_reset(void);

/* ---- The world around the application ---- */

void standin_clock(int64_t unix_time);     /* the clock is set; no intent is delivered */
void standin_pass(int64_t milliseconds);   /* time goes by: the uptime and, when known, the clock */
void standin_restart(void);                /* another boot: uptime 0, no alarms, no session, no requests */
void standin_forget_session(void);         /* the application left the stack */

/* A value as a setting is saved; NULL forgets it. */
void standin_set(const char *key, const char *text);
/* What is stored under the key, or NULL; a default is not stored. */
const uint8_t *standin_value(const char *key, int32_t *length);
int32_t standin_store_bytes(void); /* as the device counts them */
void standin_file_put(const char *name, const void *bytes, int32_t size);
const uint8_t *standin_file(const char *name, int32_t *size);

/* Timers and alarms that wait, as bits of their ids; what is left of one. */
uint32_t standin_timers(void);
uint32_t standin_alarms(void);
int32_t standin_timer_left(int32_t id);          /* milliseconds; 0: not set */
int32_t standin_alarm_left(int32_t id);          /* seconds; 0: not set */
bool standin_alarm_due(int32_t id);              /* set, and its time has come */

/* The address poll `poll` asks for item `item` of its list (-1: it has
 * none): {{key}} filled from the store. False when a key it needs is empty,
 * which the device delivers as HOST_FAILED_NOT_READY. */
bool standin_poll_url(int32_t poll, int32_t item, char *url, int32_t capacity);
/* The same for an address a request of the application's own was sent to. */
bool standin_fill_url(const char *written, char *url, int32_t capacity);
/* Whether the manifest's "hosts" lets a request of the application's own go to the address. */
bool standin_url_allowed(const char *url);
/* Whether every value of a queued request's headers can be filled now. */
bool standin_headers_ready(const standin_request_t *request);
/* How many items a poll with "each" is asked for; -1 for a poll without. */
int32_t standin_poll_items(int32_t poll);
/* Item `index` of those the poll claims: its number in the list, or -1. */
int32_t standin_poll_item(int32_t poll, int32_t index);

/* ---- The application's handlers, called as the device calls them ----
 *
 * Each has the device's three seconds: a handler still running then stops
 * the program. A missing handler is skipped. */

void standin_load(void);       /* a new instance: app_on_load(), and app_on_installed() the first time since standin_reset() */
void standin_focus(void);      /* it has the screen: app_on_focus() */
void standin_leave(void);      /* app_on_blur(), app_on_terminate(); its timers stop */
void standin_terminate(void);  /* an instance loaded off the screen ends */
void standin_press(int32_t button);      /* KEYDOWN, KEYUP, PRESSED; BACK as the device takes it */
void standin_long_press(int32_t button); /* KEYDOWN, LONG_PRESSED, KEYUP */
void standin_timer(int32_t id);          /* app_on_timer(), the timer no longer waiting */
void standin_alarm(int32_t id);
void standin_intent(int32_t intent);     /* when the manifest asks for it */
void standin_settings(void);
void standin_updated(void);              /* another package: the alarms of the old one are gone, app_on_updated() */
uint64_t standin_draw(void);             /* app_on_draw(): what the frame was made of */
uint64_t standin_screen(uint64_t frame); /* the frame with the header and the inversion around it */

/* ---- Answers ---- */

#define STANDIN_WHOLE  0 /* every piece, the last with HOST_DATA_LAST */
#define STANDIN_CUT    1 /* the first half and no more: an answer that just stops */
#define STANDIN_FAILED 2 /* the first half, then HOST_DATA_FAILED with `failure` */

/* An answer in pieces of `piece` bytes through app_data_buffer() and
 * app_on_data(). A negative `status` is a request that failed: one call,
 * HOST_DATA_FAILED, no data. */
void standin_data(int32_t request, int32_t item, int32_t status, const void *body, int32_t length,
                  int32_t piece, int32_t ending, int32_t failure);
/* An answer to a poll with "pick": read by the SDK's picker, app_on_picked(). */
void standin_picked(int32_t poll, int32_t item, int32_t status, const void *body, int32_t length);

/* ---- Runs ----
 *
 * An instance of an application is its memory, and a new instance starts
 * with that memory as the module has it. Here that is a new process: `run`
 * runs in a fork of this program, taken before any handler ran, and shares
 * standin() with it. False when it ended by a fault, a sanitizer's finding
 * or a handler that did not return; standin_fault() then says which. */
bool standin_live(void (*run)(void *context), void *context);
const char *standin_fault(void);
/* The same, with everything the run did to the host undone but its marks
 * and notes: to ask what the application would do without it having done it. */
bool standin_try(void (*run)(void *context), void *context);
void standin_note(const char *format, ...) __attribute__((format(printf, 1, 2)));
