/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The SDK's library, what every application links: text, settings read from
 * the store, paces, places, dates, centring by ink. The host's calls it makes
 * are stood in for here.
 */
#define HOST_IMPORT(name, ...) __VA_ARGS__
#define APP_EXPORT(name, ...) __VA_ARGS__
#include "host_api.h"

#include "tinytest.h"

/* ---- the host, stood in for ---- */

#define STORE_MAX 8
static struct { char key[16]; char value[256]; int32_t length; } store[STORE_MAX];
static int store_count;

static void store_put(const char *key, const char *value)
{
    snprintf(store[store_count].key, sizeof store[0].key, "%s", key);
    snprintf(store[store_count].value, sizeof store[0].value, "%s", value);
    store[store_count].length = (int32_t)strlen(value);
    store_count++;
}

int32_t kv_get(const char *key, int32_t key_length, void *value, int32_t capacity)
{
    for (int i = 0; i < store_count; i++) {
        if ((int32_t)strlen(store[i].key) == key_length && memcmp(store[i].key, key, (size_t)key_length) == 0) {
            if (store[i].length > capacity) {
                return HOST_ERR_ARGUMENT;
            }
            memcpy(value, store[i].value, (size_t)store[i].length);
            return store[i].length;
        }
    }
    return 0;
}

int32_t kv_set(const char *key, int32_t key_length, const void *value, int32_t length)
{
    (void)key; (void)key_length; (void)value; (void)length;
    return HOST_OK;
}

/* The session as the host keeps it: one block, copied in and out whole. */
static uint8_t session[HOST_SESSION_MAX];
static int32_t session_length;

int32_t session_set(const void *data, int32_t length)
{
    if (length < 0 || length > HOST_SESSION_MAX) {
        return HOST_ERR_ARGUMENT;
    }
    memcpy(session, data, (size_t)length);
    session_length = length;
    return HOST_OK;
}

int32_t session_get(void *buffer, int32_t capacity)
{
    if (session_length > capacity) {
        return HOST_ERR_NO_SPACE;
    }
    memcpy(buffer, session, (size_t)session_length);
    return session_length;
}

int32_t time_text(int64_t unix_time, int32_t style, char *text, int32_t capacity)
{
    (void)unix_time; (void)style;
    static const char SAID[] = "Fri 13 Nov";
    int32_t length = (int32_t)sizeof SAID - 1 < capacity ? (int32_t)sizeof SAID - 1 : capacity;
    memcpy(text, SAID, (size_t)length);
    return length;
}

/* A typeface whose ink stands two pixels in from the left and ends four before the room's end. */
static int ink_answers = 1;
int32_t gfx_text_width(int32_t font, const char *text, int32_t length) { (void)font; (void)text; return 10 * length; }
int32_t gfx_text_ink(int32_t font, const char *text, int32_t length, host_text_ink_t *ink)
{
    (void)font; (void)text;
    if (!ink_answers) {
        return HOST_ERR_ARGUMENT;
    }
    ink->first_x = 2;
    ink->end_x = 10 * length - 4;
    return HOST_OK;
}
int32_t gfx_draw_text(int32_t x, int32_t y, int32_t font, const char *text, int32_t length)
{
    (void)x; (void)y; (void)font; (void)text; return 10 * length;
}
int32_t ui_draw_actions(const char *ok, int32_t ok_length, const char *back, int32_t back_length)
{
    (void)ok; (void)ok_length; (void)back; (void)back_length; return HOST_OK;
}
/* A cell fits in the size of its value's length, longer values in larger numbers - smaller
 * type; what was drawn is kept to be looked at. */
static int32_t drawn_sizes[4], drawn_cells;
int32_t ui_cell_fit(const host_cell_t *cell, const uint8_t *picture, int32_t icon)
{
    (void)picture; (void)icon;
    return 1 + (int32_t)strlen(cell->value) / 4;
}
int32_t ui_draw_cell(const host_cell_t *cell, const uint8_t *picture, int32_t icon)
{
    (void)picture; (void)icon;
    if (drawn_cells < 4) {
        drawn_sizes[drawn_cells++] = cell->size;
    }
    return HOST_OK;
}
int32_t answer_value(int32_t index, char *text, int32_t capacity) { (void)index; (void)text; (void)capacity; return -1; }
int32_t answer_count(int32_t index) { (void)index; return -1; }
int32_t answer_element(int32_t index, int32_t element, char *text, int32_t capacity)
{
    (void)index; (void)element; (void)text; (void)capacity; return -1;
}

#include "../../../sdk/lib/app_text.c"
#include "../../../sdk/lib/app_fixed.c"
#include "../../../sdk/lib/app_values.c"
#include "../../../sdk/lib/app_state.c"
#include "../../../sdk/lib/app_days.c"
#include "../../../sdk/lib/app_draw.c"

static void test_text(void)
{
    CHECK(app_text_equal("min", "min") && !app_text_equal("min", "mint") && !app_text_equal("", "a"));
    CHECK(app_text_equal("", ""));
    CHECK(app_text_starts("MOEX:SBER", "MOEX:") && app_text_starts("a", "") && !app_text_starts("MO", "MOEX"));

    /* A copy too long is cut between characters, never inside one. */
    char cut[6];
    app_text_copy(cut, sizeof cut, "ab\xD0\x96\xD0\x96");
    CHECK_STR(cut, "ab\xD0\x96");

    char said[32];
    CHECK(app_time_text(0, HOST_TIME_DATE, said, sizeof said) == 10);
    CHECK_STR(said, "Fri 13 Nov");
    char tight[4];
    CHECK(app_time_text(0, HOST_TIME_DATE, tight, sizeof tight) == 3 && tight[3] == '\0');
    CHECK(app_time_text(0, HOST_TIME_DATE, tight, 0) == 0);
}

static void test_cells_in_one_size(void)
{
    host_cell_t cells[3] = { { .value = "1" }, { .value = "12345678" }, { .value = "12" } };
    drawn_cells = 0;
    app_draw_cells(cells, 0, 0, 3);
    CHECK(drawn_cells == 3);
    CHECK(drawn_sizes[0] == 3 && drawn_sizes[1] == 3 && drawn_sizes[2] == 3); /* the longest's size */
}

static void test_session(void)
{
    typedef struct {
        int32_t page, row;
    } place_t;
    APP_SESSION_FITS(place_t);
    place_t here = { 2, 7 }, back = { -1, -1 };
    CHECK(!app_session_load(&back, sizeof back)); /* nothing kept yet */
    CHECK(back.page == -1);
    CHECK(app_session_save(&here, sizeof here));
    CHECK(app_session_load(&back, sizeof back) && back.page == 2 && back.row == 7);
    int32_t other = 9;
    CHECK(!app_session_load(&other, sizeof other)); /* another size is another struct */
    CHECK(other == 9);
}

static void test_fixed_numbers_at_the_edges(void)
{
    int64_t value = 7;
    CHECK(!app_fixed_parse("1234567890123456789", 0, &value)); /* a 19th digit would be lost */
    CHECK(!app_fixed_parse("9223372036854775808", 0, &value));
    CHECK(value == 7);
    CHECK(!app_fixed_parse("9.223372036854775808e18", 0, &value));
    CHECK(!app_fixed_parse("-9.223372036854775808e18", 0, &value));
    CHECK(!app_fixed_parse("1.234567890123456789e16", 2, &value));
    CHECK(!app_fixed_parse("123456789012345678.6", 0, &value));
    CHECK(value == 7);
    CHECK(app_fixed_parse("9.223372036854775800e18", 0, &value) && value == 9223372036854775800LL);
    CHECK(app_fixed_parse("123456789012345678.4", 0, &value) && value == 123456789012345678LL);
    CHECK(app_fixed_parse("1.234567890123456789", 2, &value) && value == 123);
    CHECK(app_fixed_parse("1.235000000000000009", 2, &value) && value == 124);
    CHECK(app_fixed_parse("123456789012345678", 0, &value) && value == 123456789012345678LL);
    CHECK(app_fixed_ratio(4000000000000000000LL, 8000000000000000000LL, 1) == 5);
    CHECK(app_fixed_ratio(1, 3, 2) == 33);
}

static void test_paces(void)
{
    CHECK(app_seconds_of("30 s", -1) == 30);
    CHECK(app_seconds_of("15 min", -1) == 900);
    CHECK(app_seconds_of("1 h", -1) == 3600);
    CHECK(app_seconds_of("3 hours", -1) == 3 * 3600);
    CHECK(app_seconds_of("1 d", -1) == 86400);
    CHECK(app_seconds_of("45", -1) == 45);   /* a number alone is seconds, as the host reads it */
    CHECK(app_seconds_of("Off", -1) == -1);
    CHECK(app_seconds_of("25 h", -1) == -1);       /* more than a day is no pace, as on the host */
    CHECK(app_seconds_of("999999 d", -1) == -1);   /* and does not overflow */
    CHECK(app_seconds_of("At start", 0) == 0);
    CHECK(app_seconds_of("", 7) == 7);
    CHECK(app_seconds_of("5 weeks", -1) == -1);
    CHECK(app_seconds_of("1234567 s", -1) == -1); /* past the digits a pace has */

    store_count = 0;
    store_put("every", "5 min");
    CHECK(app_setting_seconds("every", 60) == 300);
    CHECK(app_setting_seconds("missing", 60) == 60);
}

static void test_places(void)
{
    store_count = 0;
    store_put("city", "Saint Petersburg, Saint Petersburg, Russia");
    store_put("town", "Lisbon");
    store_put("long", "\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0, Russia");
    char name[32];
    CHECK(app_setting_place("city", name, sizeof name) == 16);
    CHECK_STR(name, "Saint Petersburg");
    CHECK(app_setting_place("town", name, sizeof name) == 6);
    CHECK_STR(name, "Lisbon");
    CHECK(app_setting_place("none", name, sizeof name) == 0 && name[0] == '\0');
    char small[6]; /* two letters of Cyrillic and no half of a third */
    CHECK(app_setting_place("long", small, sizeof small) == 4);
    CHECK_STR(small, "\xD0\x9C\xD0\xBE");
}

static void test_days(void)
{
    CHECK(app_days_from_date(1970, 1, 1) == 0);
    CHECK(app_days_from_date(1969, 12, 31) == -1);
    CHECK(app_days_from_date(2000, 3, 1) == 11017);
    CHECK(app_days_from_date(2026, 11, 16) == 20773);
    CHECK(app_days_from_date(2024, 3, 1) - app_days_from_date(2024, 2, 28) == 2); /* a leap year */
    CHECK(app_days_from_date(2100, 3, 1) - app_days_from_date(2100, 2, 28) == 1); /* a century that is not */
    int wrong = 0;
    for (int32_t days = -800000; days <= 800000; days += 7) {
        int32_t year, month, day;
        app_date_from_days(days, &year, &month, &day);
        wrong += app_days_from_date(year, month, day) != days || month < 1 || month > 12 || day < 1 || day > 31;
    }
    CHECK(wrong == 0);
}

static void test_centring(void)
{
    /* "abc" takes 30 pixels and has ink from 2 to 26: its middle is at 14, not 15. */
    ink_answers = 1;
    CHECK(app_text_center_x(100, HOST_FONT_TEXT, "abc") == 86);
    /* A host that cannot say where the ink is: the room is centred. */
    ink_answers = 0;
    CHECK(app_text_center_x(100, HOST_FONT_TEXT, "abc") == 85);
    ink_answers = 1;
}

int main(void)
{
    test_text();
    test_paces();
    test_fixed_numbers_at_the_edges();
    test_session();
    test_cells_in_one_size();
    test_places();
    test_days();
    test_centring();
    REPORT();
}
