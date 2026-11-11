/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Not an application anyone would use: test_app.c loads it into a real
 * interpreter and reads what it did off the pixels. Every row it paints
 * answers one question about the host API.
 */
#include "app_values.h"
#include "host_api.h"

#define ROW_PRESSES        0  /* as many pixels as the buttons pressed add up to */
#define ROW_TIMER          2  /* timer id + 1 pixels, once a timer fired */
#define ROW_NOT_DRAWING    4  /* one pixel: drawing outside app_on_draw was refused */
#define ROW_BAD_ARGUMENTS  6  /* pixel 0: a wild pointer refused; pixel 2: an unknown font */
#define ROW_MEASURES       8  /* pixels 0 2 4 6 8: text width, font height, uptime, wall time, three font sizes */
#define ROW_MORE_REFUSALS   10 /* pixels 0..10: see refusals[] in app_on_draw */
#define ROW_BITMAP         12
#define ROW_LINE           14
#define ROW_STORE          16 /* pixels 0..12: see app_on_draw; pixel 20: a value from an earlier life */
#define ROW_DATA           18 /* pixel 0: a response arrived whole and in order; pixel 2: a failed one was told apart */
#define ROW_TEXT           20
#define ROW_COLOR          60 /* pixels 0 1 4..7: a bar with a hole of paper; pixel 10: a wrong colour refused */
#define ROW_VALUES         63 /* pixel 0: picked values read right; 2: a failed request told apart; 4: no values outside the handler; 6: numbers parse */
#define ROW_LOCAL_TIME     62 /* pixel 0: no time yet; pixel 2: the time taken apart; pixel 4: a wild pointer refused */

#define BUTTON_DRAW_OUTSIDE 3
#define BUTTON_TRAP         4
/* Not on the device: test_app.c presses these three. */
#define BUTTON_EXIT          7
#define BUTTON_SPIN_FOREVER  8
#define BUTTON_RECURSE       9
#define BUTTON_REDRAW_SOON   10
#define BUTTON_REDRAW_CLEAN  11
#define BUTTON_REDRAW_STREAM 12
#define TIMER_ON_FOCUS      1
#define UNKNOWN_FONT        7
#define FAKE_UPTIME_MS      1234 /* what test_app.c's clocks answer */
#define FAKE_ZONE_OFFSET    3600
#define OFF_THE_SURFACE     300
#define HUGE                2000000000

static int32_t presses;
static int32_t fired_timer = -1;
static int32_t result_outside_draw;
static int32_t found_on_start;

/* What app_on_data saw of request 1: the lengths add up, the bytes too. */
static int32_t data_bytes, data_sum, data_pieces, data_status;
static int32_t data_began, data_ended, data_failed;

static void mark(int32_t x, int32_t row)
{
    gfx_rect(x, row, 1, 1, HOST_RECT_FILLED);
}

/* Never true, but the compiler cannot know: with it the recursion below
 * is not provably endless, which the compiler would refuse to build. */
static volatile int32_t reached_the_bottom;

/* Built so that the compiler cannot turn the recursion into a loop: the
 * result is multiplied and then added to after the call returns. */
static int32_t descend_forever(int32_t depth)
{
    if (reached_the_bottom) {
        return depth;
    }
    return descend_forever(depth + 1) * depth + 1;
}

/* One pixel of ROW_STORE for every answer of the store that is right. */
static void draw_store_checks(void)
{
    static const uint8_t written[] = { 1, 2, 3, 4, 5 };
    uint8_t read_back[8] = { 0 };
    uint8_t too_small[2];
    static uint8_t too_large[HOST_KV_VALUE_MAX + 1];

    if (found_on_start) {
        mark(20, ROW_STORE);
    }
    kv_set("fresh", 5, written, 0); /* so that every life starts the same */
    const int32_t answers[] = {
        kv_get("fresh", 5, read_back, sizeof read_back) == HOST_ERR_NOT_FOUND,
        kv_set("fresh", 5, written, sizeof written) == HOST_OK &&
            kv_get("fresh", 5, read_back, sizeof read_back) == (int32_t)sizeof written &&
            read_back[0] == 1 && read_back[4] == 5,
        kv_get("fresh", 5, too_small, sizeof too_small) == HOST_ERR_NO_SPACE,
        kv_set("fresh", 5, written, 0) == HOST_OK &&
            kv_get("fresh", 5, read_back, sizeof read_back) == HOST_ERR_NOT_FOUND,
        kv_set("", 0, written, 1) == HOST_ERR_ARGUMENT,
        kv_set("with a gap", 10, written, 1) == HOST_ERR_ARGUMENT,
        kv_set("large", 5, too_large, sizeof too_large) == HOST_ERR_ARGUMENT,
    };
    for (int32_t i = 0; i < (int32_t)(sizeof answers / sizeof answers[0]); i++) {
        if (answers[i]) {
            mark(2 * i, ROW_STORE);
        }
    }
    static const uint8_t for_the_next_life[] = { 7 };
    kv_set("kept", 4, for_the_next_life, sizeof for_the_next_life);
}

void app_init(void)
{
    uint8_t kept;
    found_on_start = kv_get("kept", 4, &kept, sizeof kept) == 1 && kept == 7;
    host_log(HOST_LOG_INFO, "init", 4);
}

void app_on_focus(void)
{
    timer_set(TIMER_ON_FOCUS, 500);
}

void app_on_blur(void)
{
    host_log(HOST_LOG_INFO, "blur", 4);
}

void app_on_button(int32_t button, int32_t press)
{
    (void)press;
    if (button == BUTTON_TRAP) {
        __builtin_trap();
    }
    if (button == BUTTON_EXIT) {
        request_exit();
        return;
    }
    if (button == BUTTON_SPIN_FOREVER) {
        for (volatile int32_t turns = 0;; turns++) {
        }
    }
    if (button == BUTTON_RECURSE) {
        presses = descend_forever(1);
    }
    if (button == BUTTON_REDRAW_SOON || button == BUTTON_REDRAW_CLEAN) {
        request_redraw(button == BUTTON_REDRAW_SOON ? HOST_REDRAW_SOON : HOST_REDRAW_CLEAN);
        return;
    }
    if (button == BUTTON_REDRAW_STREAM) {
        request_redraw(HOST_REDRAW_NO_BLINK);
        return;
    }
    if (button == BUTTON_DRAW_OUTSIDE) {
        result_outside_draw = gfx_rect(0, 0, 1, 1, HOST_RECT_FILLED);
        return;
    }
    presses += button;
    request_redraw(HOST_REDRAW_NORMAL);
}

void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags,
                 int32_t http_status)
{
    if (flags & HOST_DATA_FAILED) {
        data_failed = request;
        return;
    }
    if (request != 1) {
        return;
    }
    if (flags & HOST_DATA_FIRST) {
        data_began++;
        data_status = http_status;
    }
    for (int32_t i = 0; i < length; i++) {
        data_sum += data[i];
    }
    data_bytes += length;
    data_pieces++;
    if (flags & HOST_DATA_LAST) {
        data_ended++;
        request_focus();
    }
}

static int32_t values_read_right, values_failed;

static bool same_text(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

void app_on_values(int32_t request, int32_t http_status)
{
    if (http_status == 0) {
        values_failed = request;
        return;
    }
    int32_t tenths = 0, missing = 0;
    char text[8], too_small[2];
    values_read_right = request == 3 && http_status == 200 &&
                        app_value_number(0, 1, &tenths) && tenths == -35 &&
                        app_value_text(1, text, sizeof text) && same_text(text, "Rain") &&
                        !app_value_number(2, 0, &missing) &&
                        data_value(2, text, sizeof text) == HOST_ERR_NOT_FOUND &&
                        data_value(1, too_small, sizeof too_small) == HOST_ERR_NO_SPACE &&
                        data_value(1, (char *)0xFFFFFF00u, 4) == HOST_ERR_ARGUMENT &&
                        data_value(99, text, sizeof text) == HOST_ERR_NOT_FOUND;
    request_redraw(HOST_REDRAW_NORMAL);
}

/* Called through a table of functions, the way a C function pointer is. */
static int32_t add_one(int32_t number) { return number + 1; }
static int32_t add_two(int32_t number) { return number + 2; }

static bool calls_through_pointers(void)
{
    static int32_t (*volatile const steps[])(int32_t) = { add_one, add_two };
    int32_t number = 0;
    for (int32_t i = 0; i < 2; i++) {
        number = steps[i](number);
    }
    return number == 3;
}

static bool numbers_parse(void)
{
    int32_t number = 0;
    return app_parse_number("12", 0, &number) && number == 12 &&
           app_parse_number("-3.46", 1, &number) && number == -35 &&
           app_parse_number("-3.44", 1, &number) && number == -34 &&
           app_parse_number("0.05", 1, &number) && number == 1 &&
           app_parse_number("7", 2, &number) && number == 700 &&
           app_parse_number("7.5", 0, &number) && number == 8 &&
           app_parse_number("2.999", 2, &number) && number == 300 &&
           !app_parse_number("", 0, &number) && !app_parse_number("-", 0, &number) &&
           !app_parse_number("true", 0, &number) && !app_parse_number("1e5", 0, &number) &&
           !app_parse_number("12345678901", 0, &number) && !app_parse_number("1.2x", 1, &number);
}

void app_on_timer(int32_t id)
{
    fired_timer = id;
    request_redraw(HOST_REDRAW_NORMAL);
}

void app_on_draw(void)
{
    gfx_rect(0, ROW_PRESSES, presses, 1, HOST_RECT_FILLED);
    if (fired_timer >= 0) {
        gfx_rect(0, ROW_TIMER, fired_timer + 1, 1, HOST_RECT_FILLED);
    }
    if (result_outside_draw == HOST_ERR_NOT_DRAWING) {
        mark(0, ROW_NOT_DRAWING);
    }

    const char *far_outside_memory = (const char *)0xFFFFFF00u;
    if (gfx_text(0, ROW_TEXT, HOST_FONT_TEXT, far_outside_memory, 16) == HOST_ERR_ARGUMENT) {
        mark(0, ROW_BAD_ARGUMENTS);
    }
    if (gfx_text(0, ROW_TEXT, UNKNOWN_FONT, "x", 1) == HOST_ERR_ARGUMENT) {
        mark(2, ROW_BAD_ARGUMENTS);
    }

    int32_t one_glyph = gfx_text_width(HOST_FONT_TEXT, "I", 1);
    if (one_glyph > 0 && gfx_text_width(HOST_FONT_TEXT, "II", 2) == 2 * one_glyph) {
        mark(0, ROW_MEASURES);
    }
    if (gfx_font_height(HOST_FONT_TEXT) > 0 && gfx_font_height(UNKNOWN_FONT) == HOST_ERR_ARGUMENT) {
        mark(2, ROW_MEASURES);
    }
    if (time_uptime_ms() == FAKE_UPTIME_MS) {
        mark(4, ROW_MEASURES);
    }
    if (time_now() == 0 && time_zone_offset() == FAKE_ZONE_OFFSET) {
        mark(6, ROW_MEASURES);
    }
    if (gfx_font_height(HOST_FONT_HUGE) > gfx_font_height(HOST_FONT_DISPLAY) &&
        gfx_font_height(HOST_FONT_DISPLAY) > gfx_font_height(HOST_FONT_LARGE) &&
        gfx_font_height(HOST_FONT_LARGE) > gfx_font_height(HOST_FONT_MEDIUM) &&
        gfx_font_height(HOST_FONT_MEDIUM) > gfx_font_height(HOST_FONT_TEXT) &&
        gfx_font_height(HOST_FONT_TEXT) > gfx_font_ascent(HOST_FONT_TEXT) &&
        gfx_font_ascent(HOST_FONT_TEXT) > gfx_font_cap_height(HOST_FONT_TEXT) &&
        gfx_font_cap_height(HOST_FONT_TEXT) > 0 &&
        gfx_font_ascent(UNKNOWN_FONT) == HOST_ERR_ARGUMENT &&
        gfx_font_cap_height(UNKNOWN_FONT) == HOST_ERR_ARGUMENT) {
        mark(8, ROW_MEASURES);
    }

    /* Longer than the host takes at once: cut, not refused. */
    static char long_text[400];
    for (int32_t i = 0; i < (int32_t)sizeof long_text; i++) {
        long_text[i] = 'I';
    }
    const uint8_t *bits_outside_memory = (const uint8_t *)far_outside_memory;
    const int32_t refusals[] = {
        timer_set(HOST_TIMER_COUNT, 10),
        timer_set(0, -1),
        gfx_bitmap(0, 0, 8, 8, bits_outside_memory),
        gfx_text(0, 0, HOST_FONT_TEXT, "x", -1),
        gfx_rect(0, 0, HUGE, HUGE, HOST_RECT_FILLED),
        gfx_line(0, 0, HUGE, HUGE),
    };
    for (int32_t i = 0; i < (int32_t)(sizeof refusals / sizeof refusals[0]); i++) {
        if (refusals[i] == HOST_ERR_ARGUMENT) {
            mark(2 * i, ROW_MORE_REFUSALS);
        }
    }
    if (gfx_text(0, OFF_THE_SURFACE, HOST_FONT_TEXT, long_text, sizeof long_text) ==
        HOST_TEXT_MAX * one_glyph) {
        mark(12, ROW_MORE_REFUSALS);
    }

    draw_store_checks();

    /* test_app.c sends 2500 bytes, each 3, in three pieces, with status 200 */
    if (data_began == 1 && data_ended == 1 && data_pieces == 3 && data_bytes == 2500 &&
        data_sum == 7500 && data_status == 200) {
        mark(0, ROW_DATA);
    }
    if (data_failed == 2) {
        mark(2, ROW_DATA);
    }

    static const uint8_t ink_at_0_and_2[] = { 0xA0 };
    gfx_bitmap(0, ROW_BITMAP, 3, 1, ink_at_0_and_2);
    gfx_line(0, ROW_LINE, 3, ROW_LINE);
    gfx_text(0, ROW_TEXT, HOST_FONT_TEXT, "I", 1);

    gfx_rect(0, ROW_COLOR, 8, 1, HOST_RECT_FILLED);
    if (gfx_color(HOST_PAPER) == HOST_OK) {
        gfx_rect(2, ROW_COLOR, 2, 1, HOST_RECT_FILLED);
        /* a clear bit of a bitmap leaves the bar alone, a set one is paper too */
        static const uint8_t nothing_set[] = { 0x00 };
        gfx_bitmap(4, ROW_COLOR, 4, 1, nothing_set);
    }
    /* half dark: every other pixel of this row, by the host's pattern */
    if (gfx_color(8) == HOST_OK) {
        gfx_rect(16, ROW_COLOR, 16, 1, HOST_RECT_FILLED);
    }
    if (gfx_color(HOST_INK + 1) == HOST_ERR_ARGUMENT && gfx_color(-1) == HOST_ERR_ARGUMENT &&
        gfx_color(HOST_INK) == HOST_OK) {
        mark(10, ROW_COLOR);
    }

    if (values_read_right) {
        mark(0, ROW_VALUES);
    }
    if (values_failed == 5) {
        mark(2, ROW_VALUES);
    }
    char no_value[4];
    if (data_value(0, no_value, sizeof no_value) == HOST_ERR_NOT_FOUND) {
        mark(4, ROW_VALUES);
    }
    if (numbers_parse()) {
        mark(6, ROW_VALUES);
    }
    if (calls_through_pointers()) {
        mark(8, ROW_VALUES);
    }
    char piece[4] = { 0 };
    if (file_size("data.bin", 8) == 10 && file_read("data.bin", 8, 4, piece, 3) == 3 &&
        piece[0] == '4' && piece[2] == '6' && file_read("data.bin", 8, 8, piece, 4) == 2 &&
        file_read("data.bin", 8, 10, piece, 4) == 0 &&
        file_size("none.bin", 8) == HOST_ERR_NOT_FOUND &&
        file_size("../x", 4) == HOST_ERR_ARGUMENT && file_size(".hidden", 7) == HOST_ERR_ARGUMENT &&
        file_size("", 0) == HOST_ERR_ARGUMENT && file_read("data.bin", 8, -1, piece, 4) == HOST_ERR_ARGUMENT &&
        file_read("data.bin", 8, 0, (void *)0xFFFFFF00u, 4) == HOST_ERR_ARGUMENT) {
        mark(10, ROW_VALUES);
    }

    host_local_time_t now;
    int32_t local_time_result = time_local(&now);
    if (time_now() == 0 && local_time_result == HOST_ERR_NOT_FOUND) {
        mark(0, ROW_LOCAL_TIME);
    }
    /* 2026-11-11 23:30:15 UTC, an hour east of it: already Thursday the 12th */
    if (local_time_result == HOST_OK && now.year == 2026 && now.month == 11 && now.day == 12 &&
        now.weekday == 4 && now.hour == 0 && now.minute == 30 && now.second == 15) {
        mark(2, ROW_LOCAL_TIME);
    }
    if (time_local((host_local_time_t *)far_outside_memory) == HOST_ERR_ARGUMENT) {
        mark(4, ROW_LOCAL_TIME);
    }

    /* Far beyond the surface: has to vanish, not land somewhere. */
    gfx_rect(gfx_width(), gfx_height(), 5000, 5000, HOST_RECT_FILLED);
}
