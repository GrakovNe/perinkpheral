/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The application the capability checks are tried on: it uses every
 * capability and keeps the contract in each, so the checks pass on it, and
 * the tools' test breaks a copy of it a line at a time to see each check
 * fail. Not installed anywhere.
 *
 * Two pages. The first shows a value a poll picks and a note it asks for
 * itself; OK opens the second, where OK counts. The page is the session's,
 * the value, the note and the count are the store's.
 */
#include <stddef.h>

#include "app_text.h"
#include "host_api.h"

#define MARGIN         HOST_UI_MARGIN
#define TICK_TIMER     0
#define TICK_MS        1000
#define ASK_ALARM      0
#define ASK_EVERY_S    60
#define NOTE_REQUEST   HOST_REQUEST_FIRST
#define TEXT_BYTES     96

static const char NOTE_URL[] = "https://example.test/note";

static int32_t page;                /* the session */
static int32_t count;               /* the store, "count" */
static char value[HOST_VALUE_MAX + 1]; /* the store, "value" */
static char note[TEXT_BYTES];       /* the store, "note" */
static char arriving[TEXT_BYTES];   /* a note not whole yet */
static int32_t arriving_length;
static bool value_is_unsaved, note_is_unsaved; /* its write was refused: the next answer tries again */

static int32_t length_of(const char *text)
{
    return app_text_length(text);
}

static void read_text(const char *key, char *text, int32_t size)
{
    int32_t length = kv_get(key, length_of(key), text, size - 1);
    text[length > 0 ? length : 0] = '\0';
}

static bool is_same(const char *one, const char *other)
{
    int32_t at = 0;
    while (one[at] != '\0' && one[at] == other[at]) {
        at++;
    }
    return one[at] == other[at];
}

/* Writes of the store are writes of the flash: only what changed, or what a refused write left unsaved. */
static void keep_text(const char *key, char *kept, const char *text, bool *is_unsaved)
{
    if (is_same(kept, text) && !*is_unsaved) {
        return;
    }
    int32_t at = 0;
    for (; text[at] != '\0'; at++) {
        kept[at] = text[at];
    }
    kept[at] = '\0';
    *is_unsaved = kv_set(key, length_of(key), kept, at) != HOST_OK;
    screen_redraw(HOST_REDRAW_NORMAL);
}

static void ask_note(void)
{
    http_send(NOTE_REQUEST, HOST_HTTP_GET, NOTE_URL, length_of(NOTE_URL), NULL, 0, NULL, 0);
}

void app_on_load(void)
{
    kv_get("count", 5, &count, sizeof count);
    read_text("value", value, sizeof value);
    read_text("note", note, sizeof note);
}

void app_on_focus(void)
{
    session_get(&page, sizeof page);
    timer_set(TICK_TIMER, TICK_MS);
    alarm_set_every(ASK_ALARM, ASK_EVERY_S, 0);
    ask_note();
}

void app_on_blur(void)
{
    session_set(&page, sizeof page);
}

void app_on_updated(void)
{
    alarm_set_every(ASK_ALARM, ASK_EVERY_S, 0);
}

void app_on_alarm(int32_t id)
{
    if (id == ASK_ALARM) {
        ask_note();
    }
}

void app_on_timer(int32_t id)
{
    if (id == TICK_TIMER) {
        timer_set(TICK_TIMER, TICK_MS);
    }
}

void app_on_button(int32_t event)
{
    if (event != BTN_OK_PRESSED) {
        return;
    }
    if (page == 0) {
        page = 1;
    } else {
        count++;
        kv_set("count", 5, &count, sizeof count);
    }
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_back(void)
{
    if (page == 0) {
        screen_leave();
        return;
    }
    page = 0;
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_picked(int32_t request, int32_t http_status)
{
    char picked[HOST_VALUE_MAX + 1];
    if (request != 0 || http_status != 200) {
        return; /* what it shows stays until an answer comes whole */
    }
    int32_t length = answer_value(0, picked, sizeof picked - 1);
    if (length > 0) {
        picked[length] = '\0';
        keep_text("value", value, picked, &value_is_unsaved);
    }
}

void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t http_status)
{
    if (request != NOTE_REQUEST) {
        return;
    }
    if ((flags & HOST_DATA_FAILED) != 0 || http_status != 200) {
        arriving_length = 0;
        return;
    }
    if ((flags & HOST_DATA_FIRST) != 0) {
        arriving_length = 0;
    }
    for (int32_t at = 0; at < length && arriving_length < TEXT_BYTES - 1; at++) {
        arriving[arriving_length++] = (char)data[at];
    }
    if ((flags & HOST_DATA_LAST) != 0) {
        arriving[arriving_length] = '\0';
        keep_text("note", note, arriving, &note_is_unsaved);
        arriving_length = 0;
    }
}

void app_on_draw(void)
{
    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);
    if (page == 0) {
        gfx_draw_text(MARGIN, MARGIN, HOST_FONT_TEXT, value, length_of(value));
        gfx_draw_text(MARGIN, MARGIN + line_height, HOST_FONT_TEXT, note, length_of(note));
        return;
    }
    char counter_buffer[32];
    app_text_t counter;
    app_text_start(&counter, counter_buffer, sizeof counter_buffer);
    app_text_add(&counter, "Count: ");
    app_text_add_number(&counter, count);
    gfx_draw_text(MARGIN, MARGIN, HOST_FONT_TEXT, counter_buffer, length_of(counter_buffer));
}
