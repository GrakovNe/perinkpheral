/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The stand-in host held to the contract's own numbers: what the store
 * counts and refuses, what the session keeps and for whom, what the clock
 * and the alarms do when time passes, and how an answer is cut into
 * pieces. It is the specimen's test only by where it lies: an
 * application's own test is found in its test folder and this is how the
 * tools' test finds one.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../specimen.c"
#include "standin.h"

static void test_store(void)
{
    char read[HOST_KV_VALUE_MAX];
    uint8_t bytes[HOST_KV_VALUE_MAX + 1] = { 0 };
    standin_reset();
    /* A setting with nothing stored reads as its default and takes no room. */
    assert(kv_get("place", 5, read, sizeof read) == 4 && memcmp(read, "here", 4) == 0);
    assert(standin_store_bytes() == 0 && standin_value("place", NULL) == NULL);
    assert(kv_get("absent", 6, read, sizeof read) == HOST_ERR_NOT_FOUND);
    /* 64 bytes a value and its length rounded up to 32. */
    assert(kv_set("a", 1, bytes, 1) == HOST_OK && standin_store_bytes() == 96);
    assert(kv_set("a", 1, bytes, 32) == HOST_OK && standin_store_bytes() == 96);
    assert(kv_set("a", 1, bytes, 33) == HOST_OK && standin_store_bytes() == 128);
    assert(kv_get("a", 1, read, 32) == HOST_ERR_NO_SPACE && kv_get("a", 1, read, 33) == 33);
    assert(kv_set("a", 1, bytes, 0) == HOST_OK && standin_store_bytes() == 0);
    /* 1 KB a value, 2 KB an application: 1024 takes 1088, and 896 more is 960 - the whole 2048. */
    assert(kv_set("big", 3, bytes, HOST_KV_VALUE_MAX + 1) == HOST_ERR_ARGUMENT);
    assert(kv_set("big", 3, bytes, HOST_KV_VALUE_MAX) == HOST_OK && standin_store_bytes() == 1088);
    assert(kv_set("more", 4, bytes, 897) == HOST_ERR_NO_SPACE);
    assert(kv_set("more", 4, bytes, 896) == HOST_OK && standin_store_bytes() == HOST_KV_TOTAL_MAX);
    assert(kv_set("one", 3, bytes, 1) == HOST_ERR_NO_SPACE);
    /* Replacing a value counts the room without what it replaces. */
    assert(kv_set("more", 4, bytes, 10) == HOST_OK && kv_set("one", 3, bytes, 1) == HOST_OK);
    assert(standin()->store_writes == 8 && standin()->refused_writes == 3);
    /* A key is 1-15 printable characters; a "url" setting is the user's. */
    assert(kv_set("", 0, bytes, 1) == HOST_ERR_ARGUMENT);
    assert(kv_set("sixteen-letters!", 16, bytes, 1) == HOST_ERR_ARGUMENT);
    assert(kv_set("a b", 3, bytes, 1) == HOST_ERR_ARGUMENT);
    assert(kv_set("server", 6, "http://a", 8) == HOST_ERR_ARGUMENT && kv_set("server", 6, bytes, 0) == HOST_ERR_ARGUMENT);
    /* A setting saved empty stays empty over its default; forgotten, the default is back. */
    standin_set("place", "");
    assert(kv_get("place", 5, read, sizeof read) == 0);
    standin_set("place", NULL);
    assert(kv_get("place", 5, read, sizeof read) == 4);
    /* A refusal a test asks for reaches every write and changes nothing. */
    standin()->refuse_writes = HOST_ERR_FAILED;
    assert(kv_set("one", 3, bytes, 2) == HOST_ERR_FAILED && standin_value("one", NULL) != NULL);
    int32_t length = 0;
    standin_value("one", &length);
    assert(length == 1);
    /* The specimen writes no files, so the host lets it write none. */
    standin()->refuse_writes = HOST_OK;
    assert(file_write("cache.bin", 9, 0, bytes, 4) == HOST_ERR_ARGUMENT && file_size("cache.bin", 9) == HOST_ERR_NOT_FOUND);
}

static void test_session(void)
{
    int32_t kept = 7, read = 0;
    uint8_t large[HOST_SESSION_MAX + 1] = { 0 };
    standin_reset();
    /* Attached after app_on_load(): it can be neither set nor read inside it. */
    standin()->is_loading = true;
    assert(session_set(&kept, sizeof kept) == HOST_ERR_ARGUMENT && session_get(&read, sizeof read) == 0);
    standin()->is_loading = false;
    /* An instance off the screen reads it and cannot change it. */
    assert(session_set(&kept, sizeof kept) == HOST_ERR_ARGUMENT);
    standin()->is_session_writable = true;
    assert(session_set(large, sizeof large) == HOST_ERR_ARGUMENT && session_set(large, HOST_SESSION_MAX) == HOST_OK);
    assert(session_set(&kept, sizeof kept) == HOST_OK);
    assert(session_get(&read, 3) == HOST_ERR_NO_SPACE && session_get(&read, sizeof read) == 4 && read == 7);
    /* Gone when the application leaves the stack, and at a restart. */
    standin_forget_session();
    assert(session_get(&read, sizeof read) == 0);
    assert(session_set(&kept, sizeof kept) == HOST_OK);
    standin_restart();
    assert(session_get(&read, sizeof read) == 0);
}

static void test_time(void)
{
    host_local_time_t parts;
    char text[32];
    int64_t moment = 0;
    standin_reset();
    /* 2026-11-21 12:00 UTC, a Saturday: the specimen's "at". */
    assert(time_now() == 1795262400 && time_local(&parts) == HOST_OK);
    assert(parts.year == 2026 && parts.month == 11 && parts.day == 21 && parts.weekday == 6 && parts.hour == 12);
    assert(time_text(time_now(), HOST_TIME_DATE, text, sizeof text) == 10 && memcmp(text, "Sat 21 Nov", 10) == 0);
    assert(time_from_local(&parts, &moment) == HOST_OK && moment == time_now());
    parts.day = 32; /* carries over: 2 December */
    assert(time_from_local(&parts, &moment) == HOST_OK && moment == time_now() + 11 * 86400);
    assert(time_local_at(HOST_TIME_MIN - 1, &parts) == HOST_ERR_ARGUMENT);
    /* Nothing moves until time is passed; then the uptime and the clock move together. */
    uint32_t boot = time_boot_id();
    assert(time_uptime_ms() == 0);
    standin_pass(1500);
    standin_pass(500);
    assert(time_uptime_ms() == 2000 && time_now() == 1795262402 && boot != 0);
    /* A device that does not know the time: 0, and no local time. */
    standin_clock(0);
    assert(time_now() == 0 && time_zone_offset() == 0 && time_local(&parts) == HOST_ERR_NOT_FOUND);
    standin_restart();
    assert(time_uptime_ms() == 0 && time_boot_id() != boot && time_boot_id() != 0);
}

static void test_timers_and_alarms(void)
{
    standin_reset();
    assert(timer_set(HOST_TIMER_COUNT, 10) == HOST_ERR_ARGUMENT && timer_set(0, -1) == HOST_ERR_ARGUMENT);
    assert(timer_set(1, 250) == HOST_OK && standin_timers() == 2u && standin_timer_left(1) == 250);
    standin_pass(100);
    assert(standin_timer_left(1) == 150);
    assert(timer_set(1, 0) == HOST_OK && standin_timers() == 0);
    assert(alarm_set(HOST_ALARM_COUNT, 5) == HOST_ERR_ARGUMENT && alarm_set_every(0, 59, 0) == HOST_ERR_ARGUMENT);
    assert(alarm_set(2, 90) == HOST_OK && alarm_set_every(0, 60, 15) == HOST_OK && alarm_set_at(1, time_now() + 30) == HOST_OK);
    assert(standin_alarms() == 7u && standin_alarm_left(2) == 90 && standin_alarm_left(0) == 15 && standin_alarm_left(1) == 30);
    /* An alarm by the clock follows the clock; one counted in seconds does not. */
    standin_clock(time_now() + 20);
    assert(standin_alarm_left(1) == 10 && standin_alarm_left(2) == 90);
    /* A repeating alarm that came is due a period on; a single one is gone. */
    standin_handlers.on_alarm = NULL;
    standin_alarm(0);
    standin_alarm(2);
    assert(standin_alarm_left(0) == 60 && standin_alarm_left(2) == 0);
    /* A restart clears them all; an update too. */
    standin_updated();
    assert(standin_alarms() == 0);
}

static int32_t pieces, bytes_seen, last_flags, last_status;

static void count_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t http_status)
{
    (void)data;
    assert(request == HOST_REQUEST_FIRST && answer_item() == -1);
    assert(pieces > 0 || (flags & (HOST_DATA_FIRST | HOST_DATA_FAILED)) != 0);
    pieces++;
    bytes_seen += length;
    last_flags = flags;
    last_status = http_status;
}

static int32_t last_picked;
static void picked_status(int32_t request, int32_t http_status)
{
    (void)request;
    last_picked = http_status;
}

static void test_answers(void)
{
    static const char BODY[] = "0123456789";
    standin_reset();
    standin_handlers.on_data = count_data;
    pieces = bytes_seen = 0;
    standin_data(HOST_REQUEST_FIRST, -1, 200, BODY, 10, 3, STANDIN_WHOLE, 0);
    assert(pieces == 4 && bytes_seen == 10 && last_flags == HOST_DATA_LAST && last_status == 200);
    pieces = bytes_seen = 0;
    standin_data(HOST_REQUEST_FIRST, -1, 200, BODY, 10, 10, STANDIN_WHOLE, 0);
    assert(pieces == 1 && last_flags == (HOST_DATA_FIRST | HOST_DATA_LAST));
    /* An answer that stops: half of it, and neither LAST nor FAILED. */
    pieces = bytes_seen = 0;
    standin_data(HOST_REQUEST_FIRST, -1, 200, BODY, 10, 1, STANDIN_CUT, 0);
    assert(pieces == 5 && bytes_seen == 5 && last_flags == 0);
    /* One that fails after pieces: FAILED last, with the reason for a status. */
    pieces = bytes_seen = 0;
    standin_data(HOST_REQUEST_FIRST, -1, 200, BODY, 10, 2, STANDIN_FAILED, HOST_FAILED_NO_ANSWER);
    assert(pieces == 4 && bytes_seen == 6 && last_flags == HOST_DATA_FAILED && last_status == HOST_FAILED_NO_ANSWER);
    /* A request that failed is one call and no data. */
    pieces = bytes_seen = 0;
    standin_data(HOST_REQUEST_FIRST, -1, HOST_FAILED_NO_NETWORK, NULL, 0, 1, STANDIN_WHOLE, 0);
    assert(pieces == 1 && bytes_seen == 0 && last_flags == HOST_DATA_FAILED && last_status == HOST_FAILED_NO_NETWORK);

    /* A poll's address is filled from the store, as text_template.c fills it: a value in the
     * path or the query percent-encoded, a list's items with commas, a setting saved empty
     * empty; a key with no value and no default makes it a request not ready. */
    char url[STANDIN_URL_BYTES];
    assert(standin_poll_url(0, -1, url, sizeof url) && strcmp(url, "https://example.test/now?place=here") == 0);
    standin_set("place", "Saint Petersburg/1,2\nRu");
    assert(standin_poll_url(0, -1, url, sizeof url) &&
           strcmp(url, "https://example.test/now?place=Saint%20Petersburg%2F1%2C2,Ru") == 0);
    standin_set("place", "");
    assert(standin_poll_url(0, -1, url, sizeof url) && strcmp(url, "https://example.test/now?place=") == 0);
    assert(!standin_fill_url("https://example.test/{{absent}}", url, sizeof url));

    /* A request is filled and held to "hosts" when it is queued (poller.c): an address of a
     * key that is not set, or of a host not listed, is refused there and has no answer. */
    standin()->request_count = 0;
    assert(http_send(HOST_REQUEST_FIRST, HOST_HTTP_GET, "https://example.test/{{absent}}", 31, NULL, 0, NULL, 0) ==
           HOST_ERR_ARGUMENT);
    assert(standin()->request_count == 0 && standin()->refused_request[0] == '\0');
    assert(http_send(HOST_REQUEST_FIRST, HOST_HTTP_GET, "https://elsewhere.test/", 23, NULL, 0, NULL, 0) ==
           HOST_ERR_ARGUMENT);
    assert(standin()->request_count == 0 && strstr(standin()->refused_request, "hosts") != NULL);
    /* A header's value is filled when the request is sent; a key with no value makes it not ready. */
    assert(!standin_header_lines_ready("Authorization: Bearer {{token}}", 31));
    standin_set("token", "t");
    assert(standin_header_lines_ready("Authorization: Bearer {{token}}", 31));
    standin_set("token", NULL);
    /* Hosts as app_manifest.c compares them: a name in any case, at its scheme's port. */
    assert(standin_url_allowed("https://EXAMPLE.test/a") && standin_url_allowed("http://example.test:80/"));
    assert(!standin_url_allowed("https://example.test:8443/") && !standin_url_allowed("ftp://example.test/") &&
           !standin_url_allowed("https://user@example.test/"));
    standin_set("place", "a b");
    assert(http_send(HOST_REQUEST_FIRST, HOST_HTTP_GET, "https://example.test/{{place}}", 30, NULL, 0, NULL, 0) ==
           HOST_OK);
    /* What it was filled with stays, whatever the store holds later. */
    standin_set("place", "c");
    assert(standin()->request_count == 1 && strcmp(standin()->requests[0].url, "https://example.test/a%20b") == 0);
    standin()->request_count = 0;
    standin_set("place", NULL);
    /* The specimen's own handler takes a picked value from the SDK's picker. */
    standin_handlers.on_picked = app_on_picked;
    standin_picked(0, -1, 200, "{\"value\": \"21\"}", 15);
    assert(strcmp(value, "21") == 0 && standin()->store_writes == 1);
    standin_picked(0, -1, 200, "{\"value\": \"21\"}", 15);
    assert(standin()->store_writes == 1);
    standin_picked(0, -1, HOST_FAILED_NO_NETWORK, NULL, 0);
    assert(strcmp(value, "21") == 0);
    /* As poller_net.c hands it over: an answer the picker cannot read is no answer, whatever its
     * status; an empty one is a document of no values and keeps its status. */
    standin_handlers.on_picked = picked_status;
    standin_picked(0, -1, 503, "Service Unavailable", 19);
    assert(last_picked == HOST_FAILED_NO_ANSWER);
    standin_picked(0, -1, 200, "{\"value\": ", 10);
    assert(last_picked == HOST_FAILED_NO_ANSWER);
    standin_picked(0, -1, 503, "", 0);
    assert(last_picked == 503);
    standin_handlers.on_picked = app_on_picked;
}

static void test_drawing(void)
{
    char fitted[16];
    standin_reset();
    /* Measuring works anywhere; drawing outside app_on_draw() is refused and counted. */
    assert(gfx_text_width(HOST_FONT_TEXT, "abc", 3) == 42 && gfx_font_height(HOST_FONT_SMALL) == 15);
    assert(gfx_fit_text(HOST_FONT_TEXT, "abcdefgh", 8, 84, fitted, sizeof fitted) == 6 && memcmp(fitted, "abc...", 6) == 0);
    assert(gfx_draw_text(0, 0, HOST_FONT_TEXT, "a", 1) == HOST_ERR_NOT_DRAWING && standin()->stray_draws == 1);
    /* The same state draws the same picture, another state another; a refused call is counted. */
    standin_handlers.on_draw = app_on_draw;
    page = 0;
    uint64_t first_page = standin_draw();
    assert(standin()->draw_calls == 2 && standin_draw() == first_page);
    page = 1;
    assert(standin_draw() != first_page && standin()->refused_draws == 0);
    standin()->is_drawing = true;
    assert(gfx_draw_rect(0, 0, 5000, 10, HOST_RECT_FILLED) == HOST_ERR_ARGUMENT && standin()->refused_draws == 1);
    assert(gfx_width() == 400 && gfx_height() == 260);
    /* What the kit and the fonts measure on the device (kit.c, gfx.c): a title, a button, a
     * value with its unit, a text's lines, the largest font's ascent. */
    int32_t calls = standin()->draw_calls;
    assert(gfx_width() == 400 && standin()->draw_calls == calls); /* measuring is no drawing */
    assert(ui_draw_title("Title", 5) == 62 && gfx_font_ascent(HOST_FONT_HUGE) == 110);
    assert(ui_draw_button(0, 0, "OK", 2, "Go", 2, 0) == 76 && ui_draw_button(0, 0, "OK", 2, "", 0, 0) == 40);
    assert(ui_value_width(HOST_FONT_LARGE, "1", 1, "s", 1) == 54 && ui_value_width(HOST_FONT_TEXT, "1", 1, "s", 1) == 26);
    assert(ui_draw_text_lines(0, 0, HOST_UI_TEXT, "", 0, 100, 4) == 0);
    assert(ui_draw_text_lines(0, 0, HOST_UI_TEXT, "one two three", 13, 100, 4) == 2);
    /* A picture of no size is refused; a bitmap file that is not there is no argument wrong. */
    static const uint8_t nothing[] = { 0, 1 };
    int32_t refused = standin()->refused_draws;
    assert(gfx_draw_picture(0, 0, nothing, 1) == HOST_ERR_ARGUMENT && standin()->refused_draws == refused + 1);
    assert(gfx_draw_bitmap_file(0, 0, 8, 8, "absent", 6, 0) == HOST_ERR_NOT_FOUND &&
           standin()->refused_draws == refused + 1);
    standin()->is_drawing = false;
    /* A port too long for one is no port: refused, and read without overflowing. */
    assert(http_send(HOST_REQUEST_FIRST, HOST_HTTP_GET, "https://example.test:2147483648/", 32, NULL, 0, NULL, 0) ==
           HOST_ERR_ARGUMENT);
}

int main(void)
{
    test_store();
    test_session();
    test_time();
    test_timers_and_alarms();
    test_answers();
    test_drawing();
    puts("the stand-in host: store, session, time, timers and alarms, answers, drawing");
    return 0;
}
