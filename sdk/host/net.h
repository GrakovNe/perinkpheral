/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The network: answers to polls and requests as pieces or picked values,
 * polls run and paced, requests from code.
 */
#pragma once

#include "base.h"

/* One piece of the answer to a poll or to http_send(). Perinkpheral OS does
 * not hold a complete response in memory.
 *
 * request:     poll number in the manifest, or the tag of http_send().
 * data:        the piece, in the buffer returned by app_data_buffer().
 *              Valid until the handler returns.
 * flags:       HOST_DATA_FIRST on the first piece, HOST_DATA_LAST on the
 *              last one. A short answer is one piece with both flags.
 *              HOST_DATA_FAILED: a call without data; the request was
 *              not made or failed, possibly after earlier pieces. Only
 *              LAST makes an answer whole; FAILED discards all preceding
 *              pieces. Piece boundaries are unspecified, even within a
 *              character or a document marker.
 * http_status: HTTP status, delivered with the first piece. With
 *              HOST_DATA_FAILED: a HOST_FAILED_* code.
 *
 * Delivery can stop without HOST_DATA_LAST and without HOST_DATA_FAILED:
 * a background instance can be unloaded by a button press, screen switch,
 * application-list reload or delivery to another application. Only
 * app_on_terminate() is called then. The next HOST_DATA_FIRST starts a
 * new answer. Perinkpheral OS repeats an interrupted poll after one minute, or after
 * its interval when that is shorter; an
 * interrupted answer to http_send() is dropped, and the application
 * asks again if it still needs it. Discard partially read data.
 *
 * A poll with "each" in the manifest is one request for every item of a
 * list setting: answer_item() tells which item the answer is for.
 *
 * Called on and off the screen. Off the screen the application is loaded
 * for the delivery and unloaded after it; a poll that cannot start at
 * all - no network, no memory - is told only to an application on the
 * screen. Save the result to the store.
 * Call screen_request() to request the screen. */
#define HOST_DATA_FIRST     1
#define HOST_DATA_LAST      2
#define HOST_DATA_FAILED    4
#define HOST_DATA_CHUNK_MAX 1024
/* Failure codes, delivered in place of the HTTP status: in app_on_data()
 * with HOST_DATA_FAILED, and in app_on_picked(). */
#define HOST_FAILED_NO_NETWORK  (-1) /* no network connection */
#define HOST_FAILED_NO_ANSWER   (-2) /* no answer from the server, or an incomplete answer */
#define HOST_FAILED_NOT_READY   (-3) /* a {{setting}} used by the request is empty */
#define HOST_FAILED_NOT_ALLOWED (-4) /* destination outside the current manifest "hosts" list */
#define HOST_FAILED_TIMEOUT     (-5) /* timeout after the server was reached */
#define HOST_FAILED_NOT_TRUSTED (-6) /* HTTPS certificate not trusted: self-signed, expired,
                                        or issued for another name */
#define HOST_FAILED_TOO_LARGE   (-7) /* answer larger than HOST_ANSWER_MAX, or larger than
                                        HOST_ANSWER_PIECES_MAX when streamed */
#define HOST_FAILED_NO_MEMORY   (-8) /* insufficient device memory for the request */
/* Maximum answer size: with value extraction ("pick"), and streamed. */
#define HOST_ANSWER_MAX        (256 * 1024)
#define HOST_ANSWER_PIECES_MAX (4 * 1024 * 1024)
APP_EXPORT(app_on_data, void app_on_data(int32_t request, const uint8_t *data, int32_t length,
                                         int32_t flags, int32_t http_status));

/* The answer to a poll with "pick" in the manifest. "pick" is a list of
 * JSON paths, for example "current.temperature_2m" or
 * "daily.weather_code[0]". Perinkpheral OS reads the answer and extracts the
 * values. The application receives this single call instead of pieces.
 *
 * request:     poll number in the manifest.
 * http_status: HTTP status, or a HOST_FAILED_* code.
 *
 * answer_value() is valid inside the handler. It copies the value of path
 * number `index` as text without a terminator and returns its length.
 * Numbers, true, false and null are copied as written; strings are copied
 * without quotes. A value is at most HOST_VALUE_MAX bytes; a longer one
 * is cut between characters.
 * Returns HOST_ERR_NOT_FOUND if the answer does not contain the path,
 * HOST_ERR_NO_SPACE if the buffer is too small.
 *
 * A list path has "[*]" in place of one index - "close[*]",
 * "items[*].title" - and gives every element of that array, in order.
 * answer_count() returns the number of elements, answer_element() copies one
 * the way answer_value() copies a value; answer_value() has none for it. Any
 * other path has one element, its value, or none. A null element is kept
 * as "null", so that two lists of one array stay in step. The elements of
 * one answer take at most HOST_LIST_BYTES as text, each its length plus
 * 2 bytes; elements beyond that are dropped and the log says so.
 * sdk/lib/app_values.h converts values to numbers and text. */
#define HOST_VALUE_MAX  63
#define HOST_LIST_BYTES 8192
APP_EXPORT(app_on_picked, void app_on_picked(int32_t request, int32_t http_status));
HOST_IMPORT(answer_value, int32_t answer_value(int32_t index, char *text, int32_t capacity));
HOST_IMPORT(answer_count, int32_t answer_count(int32_t index));
HOST_IMPORT(answer_element,
            int32_t answer_element(int32_t index, int32_t element, char *text, int32_t capacity));

/* The item of the list setting the answer being delivered is for: its
 * number in the list, from 0, for a poll with "each"; -1 for any other
 * answer. Valid inside app_on_data() and app_on_picked(). */
HOST_IMPORT(answer_item, int32_t answer_item(void));

/* Response metadata. Valid inside app_on_data() and app_on_picked(), for
 * the answer being delivered. The same for every piece of one answer.
 * A header that the server did not send is an empty string. A header
 * longer than its field is cut. A terminal HOST_DATA_FAILED callback may
 * have no metadata: status is the failure code, length is -1 and header
 * strings are empty. Invalid or incomplete picked JSON is reported as
 * HOST_FAILED_NO_ANSWER even when an HTTP response arrived. */
typedef struct host_answer_info {
    int32_t status;            /* HTTP status, or a HOST_FAILED_* code */
    int32_t length;            /* Content-Length; -1 if absent */
    int32_t retry_after;       /* Retry-After in seconds; 0 if absent or given as a date */
    char    content_type[48];  /* "application/json; charset=utf-8" */
    char    etag[64];          /* for If-None-Match */
    char    last_modified[32]; /* for If-Modified-Since */
    char    location[128];     /* target of a redirect that was not followed */
    char    link[192];         /* Link header: address of the next page */
} host_answer_info_t;
HOST_LAYOUT(host_answer_info_t, 476);
/* Copies the first min(capacity, sizeof(host_answer_info_t)) bytes of the
 * Perinkpheral OS's struct, including zeroed absent fields. Returns bytes written,
 * HOST_ERR_ARGUMENT for a negative capacity or invalid destination, or
 * HOST_ERR_NOT_FOUND outside a data handler. Bytes beyond Perinkpheral OS's
 * struct are untouched; zero the destination first when accepting a
 * shorter struct. Capacity 0 writes nothing and returns 0 in a handler. */
HOST_IMPORT(answer_info, int32_t answer_info(host_answer_info_t *response, int32_t capacity));

/* Returns the buffer for one piece of data: HOST_DATA_CHUNK_MAX bytes of
 * application memory. Provided by sdk/lib; applications do not define it. */
APP_EXPORT(app_data_buffer, uint8_t *app_data_buffer(void));

/* ---- Network ---- */

/* Network availability is reported in the answer: a request made without
 * a network fails with HOST_FAILED_NO_NETWORK. The radio can be off
 * between requests. */

/* Requests poll number `poll` through the shared queue without waiting for
 * a response. Repeated pending requests are coalesced. Perinkpheral OS performs
 * one request at a time. Calls within a second of the last accepted
 * poll_run() are delayed until that second has elapsed; this does not
 * guarantee a one-second gap between request starts. */
HOST_IMPORT(poll_run, int32_t poll_run(int32_t poll));

/* Queues a request from code.
 *
 * Destination: only hosts listed under "hosts" in the manifest.
 * Queue:       Perinkpheral OS performs one request at a time for the whole
 *              device. An application has at most HOST_HTTP_PENDING_MAX
 *              requests waiting; with more to ask, it queues the next
 *              ones as answers arrive. The applications take turns, and
 *              polls take turns with requests, so every request is made
 *              in the end. A request is about 6 s over HTTPS.
 * Answer:      delivered through app_on_data() in pieces, with `tag` as
 *              the request number, on or off the screen. The instance
 *              that receives the answer may not be the one that made the
 *              request: one that left the screen meanwhile is loaded anew.
 * tag:         a number chosen by the application, HOST_REQUEST_FIRST to
 *              HOST_REQUEST_LAST, distinct from poll numbers:
 *
 *                  enum { REQUEST_TOKEN = HOST_REQUEST_FIRST, REQUEST_EVENTS };
 *
 * headers:     lines "Name: value" separated by '\n', at most four. The
 *              Perinkpheral OS sends "User-Agent: perinkpheral"; a User-Agent header
 *              given here replaces it. The same for polls. Each name is
 *              at most 31 bytes and each raw value at most 255 bytes.
 * {{key}}      reads the application's KV store, including keys not
 *              declared as settings. An absent declared setting uses its
 *              manifest default. URL values are percent encoded (except
 *              a leading url/link setting); list values use commas.
 *              URL substitution and the initial host check happen before
 *              queuing: an unset URL value returns HOST_ERR_ARGUMENT with
 *              no callback. The expanded destination stays queued. At send
 *              time it is authorized again and headers are expanded from
 *              the same current KV snapshot. After a settings change a
 *              queued request whose destination is no longer allowed fails
 *              with HOST_FAILED_NOT_ALLOWED; it is never silently retargeted.
 *              Missing/invalid header values give HOST_FAILED_NOT_READY.
 *              Once prepared, a request keeps that snapshot even if settings
 *              change during transmission. Body bytes are copied at queue time.
 *              Expanded URLs fit in 511 bytes; expanded header values fit
 *              in 255 bytes. Expansion failure gives HOST_FAILED_NOT_READY,
 *              except for the initial URL check.
 *
 * Redirects: a GET is followed up to three times; the application
 * receives the last answer only. A redirect to a host outside "hosts"
 * fails with HOST_FAILED_NOT_ALLOWED. Request headers are not sent to
 * another host. A request with any other method is sent once; its 3xx
 * status is delivered as the answer. Polls are followed the same way,
 * to any host.
 *
 * A JSON answer can be parsed with sdk/lib/json_pick.h: feed every piece,
 * read the values after the last one.
 *
 * Returns HOST_OK; HOST_ERR_ARGUMENT for a URL outside the manifest hosts
 * or malformed arguments; HOST_ERR_NO_SPACE if HOST_HTTP_PENDING_MAX of the
 * application's requests are waiting, or the device has no memory for
 * another. All queued requests share a 16 KB budget, including queue
 * records, URLs, headers and bodies. The per-application ceiling reserves
 * no slots; HOST_ERR_NO_SPACE can occur before sixteen requests wait. */
#define HOST_HTTP_GET    0
#define HOST_HTTP_POST   1
#define HOST_HTTP_PUT    2
#define HOST_HTTP_DELETE 3
#define HOST_HTTP_PATCH    4
#define HOST_HTTP_HEAD     5 /* the answer is one piece without data; use answer_info() */
#define HOST_HTTP_PROPFIND 6 /* CalDAV */
#define HOST_HTTP_REPORT   7 /* CalDAV */
#define HOST_HTTP_METHOD_LAST HOST_HTTP_REPORT
#define HOST_HTTP_HEADERS_MAX 1023
#define HOST_HTTP_BODY_MAX    512
#define HOST_HTTP_PENDING_MAX 16
#define HOST_REQUEST_FIRST    16
#define HOST_REQUEST_LAST     255
HOST_IMPORT(http_send,
            int32_t http_send(int32_t tag, int32_t method, const char *url, int32_t url_length,
                                 const char *headers, int32_t headers_length,
                                 const void *body, int32_t body_length));

/* Overrides the interval of poll number `poll`, in seconds, kept across
 * restarts and updates. Positive values must be 10..86400; anything else
 * except 0 returns HOST_ERR_ARGUMENT, as does an unknown poll. 0 forgets
 * the stored code override and resumes the manifest's interval or its
 * interval setting. A code override takes precedence over that setting.
 * A changed effective interval restarts the deadline from now; setting
 * the same interval does not. Returns HOST_OK, or HOST_ERR_FAILED if the
 * override could not be kept or forgotten. */
HOST_IMPORT(poll_set_interval, int32_t poll_set_interval(int32_t poll, int32_t seconds));
