/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app.h"

#include <stdbool.h>
#include <string.h>

#include "host_api.h"
#include "probe_wasm.h"
#include "tinytest.h"

/* A real module (apps/probe) in a real interpreter, with the device
 * replaced by the recorders below. */

#define SURFACE_W 64
#define SURFACE_H 64

/* The rows apps/probe paints, as it names them. */
#define ROW_PRESSES       0
#define ROW_TIMER         2
#define ROW_NOT_DRAWING   4
#define ROW_BAD_ARGUMENTS 6
#define ROW_MEASURES      8
#define ROW_MORE_REFUSALS 10
#define ROW_BITMAP        12
#define ROW_STORE         16
#define ROW_LINE          14
#define ROW_DATA          18
#define ROW_TEXT          20
#define ROW_COLOR         60
#define ROW_LOCAL_TIME    62

static uint8_t bits[SURFACE_W / 8 * SURFACE_H];
static gfx_t surface;

static char logged[64];
static int  timer_set_id;
static int  timer_set_ms;

/* 0: the device does not know the time, as at start. */
static int64_t fake_now;

static int64_t fake_unix_time(void)
{
    return fake_now;
}

static int32_t fake_time_zone_offset(void)
{
    return 3600;
}

/* Stands still, so that the probe can check what it reads, unless a test
 * lets it run: then every look at it is a millisecond later. */
#define FAKE_UPTIME_MS 1234
static bool    clock_runs;
static int64_t clock_ms;

static int64_t fake_uptime_ms(void)
{
    if (clock_runs) {
        clock_ms++;
    }
    return clock_ms;
}

static void record_log(int level, const char *text, int length)
{
    (void)level;
    strncat(logged, text, (size_t)length);
    strcat(logged, ";");
}

static void record_timer(int id, int32_t milliseconds)
{
    timer_set_id = id;
    timer_set_ms = milliseconds;
}

/* One application's store, in memory: a few keys, small values. */
#define FAKE_KV_SLOTS 4
static struct {
    char    app_name[16];
    char    key[HOST_KV_KEY_MAX + 1];
    uint8_t value[HOST_KV_VALUE_MAX];
    int32_t length;
} kv_slots[FAKE_KV_SLOTS];

static int kv_slot_of(const char *app_name, const char *key)
{
    for (int slot = 0; slot < FAKE_KV_SLOTS; slot++) {
        if (kv_slots[slot].length > 0 && strcmp(kv_slots[slot].app_name, app_name) == 0 &&
            strcmp(kv_slots[slot].key, key) == 0) {
            return slot;
        }
    }
    return -1;
}

static int32_t fake_kv_get(const char *app_name, const char *key, void *value, int32_t capacity)
{
    int slot = kv_slot_of(app_name, key);
    if (slot < 0) {
        return HOST_ERR_NOT_FOUND;
    }
    if (kv_slots[slot].length > capacity) {
        return HOST_ERR_NO_SPACE;
    }
    memcpy(value, kv_slots[slot].value, (size_t)kv_slots[slot].length);
    return kv_slots[slot].length;
}

static int32_t fake_kv_set(const char *app_name, const char *key, const void *value,
                           int32_t value_length)
{
    int slot = kv_slot_of(app_name, key);
    if (value_length == 0) {
        if (slot >= 0) {
            kv_slots[slot].length = 0;
        }
        return HOST_OK;
    }
    for (int free_slot = 0; slot < 0 && free_slot < FAKE_KV_SLOTS; free_slot++) {
        if (kv_slots[free_slot].length == 0) {
            slot = free_slot;
        }
    }
    if (slot < 0) {
        return HOST_ERR_NO_SPACE;
    }
    strcpy(kv_slots[slot].app_name, app_name);
    strcpy(kv_slots[slot].key, key);
    memcpy(kv_slots[slot].value, value, (size_t)value_length);
    kv_slots[slot].length = value_length;
    return HOST_OK;
}

static const app_services_t SERVICES = {
    .unix_time = fake_unix_time,
    .time_zone_offset = fake_time_zone_offset,
    .uptime_ms = fake_uptime_ms,
    .log = record_log,
    .timer_set = record_timer,
    .kv_get = fake_kv_get,
    .kv_set = fake_kv_set,
};

static app_t *load_probe_as(const char *name)
{
    logged[0] = '\0';
    timer_set_id = timer_set_ms = -1;
    clock_runs = false;
    clock_ms = FAKE_UPTIME_MS;
    app_t *app = NULL;
    CHECK(app_load(name, probe_wasm, probe_wasm_len, &SERVICES, &app) == APP_OK);
    return app;
}

static app_t *load_probe(void)
{
    return load_probe_as("probe");
}

static void draw(app_t *app)
{
    gfx_init(&surface, bits, SURFACE_W, SURFACE_H);
    gfx_clear(&surface, false);
    app_draw(app, &surface);
}

static int black_in_row(int row)
{
    int count = 0;
    for (int x = 0; x < SURFACE_W; x++) {
        count += gfx_get(&surface, x, row);
    }
    return count;
}

static void test_lifecycle(void)
{
    app_t *app = load_probe();
    CHECK_STR(logged, "init;");

    app_focus(app);
    CHECK(timer_set_id == 1 && timer_set_ms == 500);

    app_blur(app);
    CHECK_STR(logged, "init;blur;");
    CHECK(!app_has_stopped(app));
    app_unload(app);
}

static void test_buttons_and_redraw(void)
{
    app_t *app = load_probe();
    CHECK(!app_take_redraw_request(app));

    app_button(app, 2, HOST_PRESS_SHORT);
    CHECK(app_take_redraw_request(app));
    CHECK(!app_take_redraw_request(app)); /* asked once, told once */

    app_button(app, 1, HOST_PRESS_SHORT);
    draw(app);
    CHECK(black_in_row(ROW_PRESSES) == 3); /* 2 + 1: the state lives in the module */
    app_unload(app);
}

static void test_exit(void)
{
    app_t *app = load_probe();
    CHECK(!app_take_exit_request(app));
    app_button(app, 7, HOST_PRESS_SHORT);
    CHECK(app_take_exit_request(app));
    CHECK(!app_take_exit_request(app)); /* asked once, told once */
    app_unload(app);
}

static void test_timer(void)
{
    app_t *app = load_probe();
    draw(app);
    CHECK(black_in_row(ROW_TIMER) == 0);
    app_timer(app, 2);
    CHECK(app_take_redraw_request(app));
    draw(app);
    CHECK(black_in_row(ROW_TIMER) == 3);
    app_unload(app);
}

static void test_drawing(void)
{
    app_t *app = load_probe();
    draw(app);
    CHECK(gfx_get(&surface, 0, ROW_BITMAP) && !gfx_get(&surface, 1, ROW_BITMAP) &&
          gfx_get(&surface, 2, ROW_BITMAP));
    CHECK(black_in_row(ROW_LINE) == 4);
    /* y is the top of the line: the glyph is under it, not above */
    CHECK(black_in_row(ROW_TEXT - 1) == 0);
    int text_ink = 0;
    for (int row = ROW_TEXT; row < ROW_TEXT + gfx_font_height(gfx_font_system()); row++) {
        text_ink += black_in_row(row);
    }
    CHECK(text_ink > 0);
    app_unload(app);
}

static void test_refusals(void)
{
    app_t *app = load_probe();
    /* a wild pointer and an unknown font come back as errors, not crashes */
    draw(app);
    CHECK(gfx_get(&surface, 0, ROW_BAD_ARGUMENTS) && gfx_get(&surface, 2, ROW_BAD_ARGUMENTS));
    CHECK(black_in_row(ROW_NOT_DRAWING) == 0);

    /* drawing from a button handler is refused and paints nothing */
    app_button(app, 3, HOST_PRESS_SHORT);
    CHECK(!app_take_redraw_request(app));
    draw(app);
    CHECK(black_in_row(ROW_NOT_DRAWING) == 1);
    CHECK(black_in_row(ROW_PRESSES) == 0);
    app_unload(app);
}

static void test_measures_and_time(void)
{
    app_t *app = load_probe();
    draw(app);
    /* text width adds up, a font has a height, both clocks answer, the
     * three fonts are three sizes */
    CHECK(black_in_row(ROW_MEASURES) == 5);
    app_unload(app);
}

static void test_more_refusals(void)
{
    app_t *app = load_probe();
    draw(app);
    /* a timer that does not exist, a negative delay, bits outside the
     * memory, a negative length, a rectangle and a line of two thousand
     * million pixels: six refusals; and text too long is cut to the limit */
    CHECK(black_in_row(ROW_MORE_REFUSALS) == 7);
    /* none of the refused timers reached the device */
    CHECK(timer_set_id == -1);
    app_unload(app);
}

static void test_store(void)
{
    memset(kv_slots, 0, sizeof kv_slots);
    app_t *app = load_probe();
    draw(app);
    /* nothing stored yet; the value written is read back whole; a buffer
     * too small is refused; a removed key is gone; keys that are not keys
     * and a value too large are refused */
    CHECK(black_in_row(ROW_STORE) == 7);
    app_unload(app);

    /* what one life of the application left, the next one finds... */
    app = load_probe();
    draw(app);
    CHECK(gfx_get(&surface, 20, ROW_STORE));
    app_unload(app);

    /* ...and another application does not: each has a store of its own */
    app = load_probe_as("other");
    draw(app);
    CHECK(!gfx_get(&surface, 20, ROW_STORE));
    app_unload(app);
}

static void test_data(void)
{
    app_t *app = load_probe();
    static uint8_t response[2500];
    memset(response, 3, sizeof response);

    /* a response of two and a half kilobytes, the way the host cuts it */
    CHECK(!app_take_focus_request(app));
    app_data(app, 1, response, HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 200);
    app_data(app, 1, response + 1024, HOST_DATA_CHUNK_MAX, 0, 0);
    CHECK(!app_take_focus_request(app));
    app_data(app, 1, response + 2048, 2500 - 2048, HOST_DATA_LAST, 0);
    /* the probe asks for the screen once it has all of it */
    CHECK(app_take_focus_request(app));
    CHECK(!app_take_focus_request(app));

    /* a request that could not be made */
    app_data(app, 2, NULL, 0, HOST_DATA_FAILED, 0);

    draw(app);
    CHECK(gfx_get(&surface, 0, ROW_DATA) && gfx_get(&surface, 2, ROW_DATA));

    /* a piece larger than the buffer is not delivered, let alone written */
    static uint8_t too_large[HOST_DATA_CHUNK_MAX + 1];
    app_data(app, 1, too_large, sizeof too_large, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    CHECK(!app_take_focus_request(app));
    CHECK(!app_has_stopped(app));
    app_unload(app);
}

static void test_trap_stops_the_application(void)
{
    app_t *app = load_probe();
    app_button(app, 4, HOST_PRESS_SHORT);
    CHECK(app_has_stopped(app));
    CHECK(app_stop_reason(app)[0] != '\0');
    CHECK(app_take_redraw_request(app)); /* the screen has to say so */

    /* nothing runs in it any more */
    logged[0] = '\0';
    app_blur(app);
    CHECK_STR(logged, "");
    draw(app);
    CHECK(black_in_row(ROW_LINE) == 0);
    app_unload(app);
}

static void test_runaway_handlers_are_stopped(void)
{
    /* a loop that calls nothing and never ends */
    app_t *app = load_probe();
    clock_runs = true;
    app_button(app, 8, HOST_PRESS_SHORT);
    CHECK(app_has_stopped(app));
    CHECK_STR(app_stop_reason(app), "it ran too long");
    app_unload(app);

    /* recursion without an end: stopped before it runs through the stack,
     * and by the stack, not by the clock */
    app = load_probe();
    clock_runs = true;
    app_button(app, 9, HOST_PRESS_SHORT);
    CHECK(app_has_stopped(app));
    CHECK(strcmp(app_stop_reason(app), "it ran too long") != 0);
    app_unload(app);

    /* and an application that behaves is not cut short by a slow clock */
    app = load_probe();
    clock_runs = true;
    app_button(app, 1, HOST_PRESS_SHORT);
    draw(app);
    CHECK(!app_has_stopped(app));
    app_unload(app);
}

static void test_bad_modules(void)
{
    app_t *app = NULL;
    static const uint8_t garbage[] = { 'n', 'o', 't', ' ', 'w', 'a', 's', 'm' };
    CHECK(app_load("x", garbage, sizeof garbage, &SERVICES, &app) == APP_ERR_BAD_MODULE);

    /* a valid header and then nothing sensible */
    static const uint8_t truncated[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00, 0x05, 0x7F };
    CHECK(app_load("x", truncated, sizeof truncated, &SERVICES, &app) == APP_ERR_BAD_MODULE);

    /* (module (memory 2)): more than the one page an application gets */
    static const uint8_t two_pages[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
                                         0x05, 0x03, 0x01, 0x00, 0x02 };
    CHECK(app_load("x", two_pages, sizeof two_pages, &SERVICES, &app) == APP_ERR_BAD_MODULE);
    CHECK(app == NULL);

    /* (module (type (func)) (import "host" "evil" (func (type 0)))) */
    static const uint8_t unknown_import[] = { 0x00, 'a',  's',  'm',  0x01, 0x00, 0x00, 0x00,
                                              0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x02, 0x0D,
                                              0x01, 0x04, 'h',  'o',  's',  't',  0x04, 'e',
                                              'v',  'i',  'l',  0x00, 0x00 };
    CHECK(app_load("x", unknown_import, sizeof unknown_import, &SERVICES, &app) == APP_ERR_BAD_MODULE);

    /* a name the host has, asked of another module */
    static const uint8_t other_module[] = { 0x00, 'a',  's',  'm',  0x01, 0x00, 0x00, 0x00,
                                            0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x02, 0x12,
                                            0x01, 0x03, 'e',  'n',  'v',  0x0A, 'g',  'f',
                                            'x',  '_',  'h',  'e',  'i',  'g',  'h',  't',
                                            0x00, 0x00 };
    CHECK(app_load("x", other_module, sizeof other_module, &SERVICES, &app) == APP_ERR_BAD_MODULE);

    /* an import section that says it is longer than the file */
    static const uint8_t lying_section[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
                                             0x02, 0x7F, 0x01 };
    CHECK(app_load("x", lying_section, sizeof lying_section, &SERVICES, &app) == APP_ERR_BAD_MODULE);

    app_unload(NULL); /* allowed */
}

static void test_redraw_flags(void)
{
    app_t *app = load_probe();
    app_button(app, 2, HOST_PRESS_SHORT);
    CHECK(app_take_redraw_request(app) && app_take_redraw_flags(app) == HOST_REDRAW_NORMAL);

    app_button(app, 10, HOST_PRESS_SHORT);
    CHECK(app_take_redraw_request(app) && app_take_redraw_flags(app) == HOST_REDRAW_SOON);
    CHECK(app_take_redraw_flags(app) == 0); /* asked once, told once */

    /* two requests in a row: both wishes stand */
    app_button(app, 10, HOST_PRESS_SHORT);
    app_button(app, 11, HOST_PRESS_SHORT);
    CHECK(app_take_redraw_flags(app) == (HOST_REDRAW_SOON | HOST_REDRAW_CLEAN));
    app_unload(app);
}

static void test_color_and_local_time(void)
{
    app_t *app = load_probe();
    draw(app);
    /* a bar of eight with a hole of two, and the mark for a refused colour */
    CHECK(black_in_row(ROW_COLOR) == 7);
    CHECK(!gfx_get(&surface, 2, ROW_COLOR) && !gfx_get(&surface, 3, ROW_COLOR));
    /* every draw starts in ink again */
    draw(app);
    CHECK(gfx_get(&surface, 0, ROW_COLOR));

    CHECK(gfx_get(&surface, 0, ROW_LOCAL_TIME) && !gfx_get(&surface, 2, ROW_LOCAL_TIME));
    CHECK(gfx_get(&surface, 4, ROW_LOCAL_TIME));
    fake_now = 1794439815; /* 2026-11-11 23:30:15 UTC */
    draw(app);
    CHECK(!gfx_get(&surface, 0, ROW_LOCAL_TIME) && gfx_get(&surface, 2, ROW_LOCAL_TIME));
    fake_now = 0;
    app_unload(app);
}

int main(void)
{
    test_lifecycle();
    test_buttons_and_redraw();
    test_exit();
    test_timer();
    test_redraw_flags();
    test_drawing();
    test_color_and_local_time();
    test_refusals();
    test_measures_and_time();
    test_more_refusals();
    test_store();
    test_data();
    test_trap_stops_the_application();
    test_runaway_handlers_are_stopped();
    test_bad_modules();
    REPORT();
}
