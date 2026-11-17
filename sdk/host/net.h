/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The network: answers to polls and requests as pieces or picked values,
 * polls run and paced, requests from code.
 */
#pragma once

#include "base.h"

/* One piece of the answer to a poll or to http_request(). The host does
 * not hold a complete response in memory.
 *
 * request:     poll number in the manifest, or the tag of http_request().
 * data:        the piece, in the buffer returned by app_data_buffer().
 *              Valid until the handler returns.
 * flags:       HOST_DATA_FIRST on the first piece, HOST_DATA_LAST on the
 *              last one. A short answer is one piece with both flags.
 *              HOST_DATA_FAILED: single call without data; the request
 *              was not made or was interrupted.
 * http_status: HTTP status, delivered with the first piece. With
 *              HOST_DATA_FAILED: a HOST_FAILED_* code.
 *
 * Delivery can stop without HOST_DATA_LAST and without HOST_DATA_FAILED:
 * a background instance is unloaded when the user presses a button. Only
 * app_on_terminate() is called then. The next HOST_DATA_FIRST starts a
 * new answer. The host repeats an interrupted poll after one minute, or after
 * its interval when that is shorter; an
 * interrupted answer to http_request() is dropped, and the application
 * asks again if it still needs it. Discard partially read data.
 *
 * A poll with "each" in the manifest is one request for every item of a
 * list setting: data_item() tells which item the answer is for.
 *
 * Called on and off the screen. Off the screen the application is loaded
 * for the delivery and unloaded after it; a poll that cannot start at
 * all - no network, no memory - is told only to an application on the
 * screen. Save the result to the store.
 * Call request_focus() to request the screen. */
#define HOST_DATA_FIRST     1
#define HOST_DATA_LAST      2
#define HOST_DATA_FAILED    4
#define HOST_DATA_CHUNK_MAX 1024
/* Failure codes, delivered in place of the HTTP status: in app_on_data()
 * with HOST_DATA_FAILED, and in app_on_values(). */
#define HOST_FAILED_NO_NETWORK  (-1) /* no network connection */
#define HOST_FAILED_NO_ANSWER   (-2) /* no answer from the server, or an incomplete answer */
#define HOST_FAILED_NOT_READY   (-3) /* a {{setting}} used by the request is empty */
#define HOST_FAILED_NOT_ALLOWED (-4) /* redirect to a host outside the manifest "hosts" list */
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
 * "daily.weather_code[0]". The host reads the answer and extracts the
 * values. The application receives this single call instead of pieces.
 *
 * request:     poll number in the manifest.
 * http_status: HTTP status, or a HOST_FAILED_* code.
 *
 * data_value() is valid inside the handler. It copies the value of path
 * number `index` as text without a terminator and returns its length.
 * Numbers, true, false and null are copied as written; strings are copied
 * without quotes. A value is at most HOST_VALUE_MAX bytes; a longer one
 * is cut between characters.
 * Returns HOST_ERR_NOT_FOUND if the answer does not contain the path,
 * HOST_ERR_NO_SPACE if the buffer is too small.
 *
 * A list path has "[*]" in place of one index - "close[*]",
 * "items[*].title" - and gives every element of that array, in order.
 * data_count() returns the number of elements, data_element() copies one
 * the way data_value() copies a value; data_value() has none for it. Any
 * other path has one element, its value, or none. A null element is kept
 * as "null", so that two lists of one array stay in step. The elements of
 * one answer take at most HOST_LIST_BYTES as text, each its length plus
 * 2 bytes; elements beyond that are dropped and the log says so.
 * sdk/lib/app_values.h converts values to numbers and text. */
#define HOST_VALUE_MAX  63
#define HOST_LIST_BYTES 8192
APP_EXPORT(app_on_values, void app_on_values(int32_t request, int32_t http_status));
HOST_IMPORT(data_value, int32_t data_value(int32_t index, char *text, int32_t capacity));
HOST_IMPORT(data_count, int32_t data_count(int32_t index));
HOST_IMPORT(data_element,
            int32_t data_element(int32_t index, int32_t element, char *text, int32_t capacity));

/* The item of the list setting the answer being delivered is for: its
 * number in the list, from 0, for a poll with "each"; -1 for any other
 * answer. Valid inside app_on_data() and app_on_values(). */
HOST_IMPORT(data_item, int32_t data_item(void));

/* Response metadata. Valid inside app_on_data() and app_on_values(), for
 * the answer being delivered. The same for every piece of one answer.
 * A header that the server did not send is an empty string. A header
 * longer than its field is cut. */
typedef struct host_response {
    int32_t status;            /* HTTP status, or a HOST_FAILED_* code */
    int32_t length;            /* Content-Length; -1 if absent */
    int32_t retry_after;       /* Retry-After in seconds; 0 if absent or given as a date */
    char    content_type[48];  /* "application/json; charset=utf-8" */
    char    etag[64];          /* for If-None-Match */
    char    last_modified[32]; /* for If-Modified-Since */
    char    location[128];     /* target of a redirect that was not followed */
    char    link[192];         /* Link header: address of the next page */
} host_response_t;
HOST_LAYOUT(host_response_t, 476);
HOST_IMPORT(data_response, int32_t data_response(host_response_t *response));

/* Returns the buffer for one piece of data: HOST_DATA_CHUNK_MAX bytes of
 * application memory. Provided by sdk/lib; applications do not define it. */
APP_EXPORT(app_data_buffer, uint8_t *app_data_buffer(void));

/* ---- Network ---- */

/* Network availability is reported in the answer: a request made without
 * a network fails with HOST_FAILED_NO_NETWORK. The radio can be off
 * between requests. */

/* Runs poll number `poll` of the manifest immediately. The answer is
 * delivered as usual. The host performs one request at a time, at most
 * one per second: one asked sooner is made when the second is up, and calls
 * made before the request starts are one. */
HOST_IMPORT(poll_now, int32_t poll_now(int32_t poll));

/* Queues a request from code.
 *
 * Destination: only hosts listed under "hosts" in the manifest.
 * Queue:       the host performs one request at a time for the whole
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
 *              host sends "User-Agent: perinkpheral"; a User-Agent header
 *              given here replaces it. The same for polls.
 * {{key}}      in the URL and in header values is replaced by the setting
 *              with that key, as in a poll.
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
 * another. */
#define HOST_HTTP_GET    0
#define HOST_HTTP_POST   1
#define HOST_HTTP_PUT    2
#define HOST_HTTP_DELETE 3
#define HOST_HTTP_PATCH    4
#define HOST_HTTP_HEAD     5 /* the answer is one piece without data; use data_response() */
#define HOST_HTTP_PROPFIND 6 /* CalDAV */
#define HOST_HTTP_REPORT   7 /* CalDAV */
#define HOST_HTTP_METHOD_LAST HOST_HTTP_REPORT
#define HOST_HTTP_HEADERS_MAX 1023
#define HOST_HTTP_BODY_MAX    512
#define HOST_HTTP_PENDING_MAX 16
#define HOST_REQUEST_FIRST    16
#define HOST_REQUEST_LAST     255
HOST_IMPORT(http_request,
            int32_t http_request(int32_t tag, int32_t method, const char *url, int32_t url_length,
                                 const char *headers, int32_t headers_length,
                                 const void *body, int32_t body_length));

/* Sets the interval of poll number `poll`, overriding the manifest:
 * 10 seconds to 1 day, counted from now. The interval persists across
 * restarts and updates until it is set again or the application is
 * removed. */
HOST_IMPORT(poll_set_interval, int32_t poll_set_interval(int32_t poll, int32_t seconds));
