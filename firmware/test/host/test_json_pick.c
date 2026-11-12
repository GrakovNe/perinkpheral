/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "json_pick.h"

#include <string.h>

#include "tinytest.h"

static json_pick_t pick;

/* Feeds the document `piece` bytes at a time: a value must come out the
 * same wherever the network happened to cut it. */
static void read_in_pieces(const char *const *paths, int count, const char *json, size_t piece)
{
    json_pick_init(&pick, paths, count);
    size_t length = strlen(json);
    for (size_t at = 0; at < length; at += piece) {
        json_pick_feed(&pick, json + at, length - at < piece ? length - at : piece);
    }
    json_pick_finish(&pick);
}

static const char *value(int index)
{
    const char *found = json_pick_value(&pick, index);
    return found != NULL ? found : "(none)";
}

static const char WEATHER[] =
    "{\"latitude\":52.52,\"current_units\":{\"temperature_2m\":\"\\u00b0C\"},"
    "\"current\":{\"time\":\"2026-11-11T12:00\",\"temperature_2m\":-3.5,\"weather_code\":71,"
    "\"is_day\":true,\"gust\":null},"
    "\"daily\":{\"weather_code\":[3, 61 ,95],\"temperature_2m_max\":[4.1,5.0,-0.2]},"
    "\"alerts\":[{\"title\":\"Ice \\\"black\\\" ice\",\"area\":{\"name\":\"Mitte\"}},"
    "{\"title\":\"Wind\"}]}";

static void test_picks_by_path(void)
{
    static const char *const PATHS[] = {
        "current.temperature_2m", "current.weather_code",  "daily.weather_code[1]",
        "daily.temperature_2m_max[2]", "alerts[0].title",  "alerts[1].title",
        "current.is_day",         "current.gust",          "alerts[0].area.name",
        "latitude",               "current.time",          "current_units.temperature_2m",
    };
    for (size_t piece = 1; piece <= sizeof WEATHER; piece += 3) {
        read_in_pieces(PATHS, 12, WEATHER, piece);
        CHECK_STR(value(0), "-3.5");
        CHECK_STR(value(1), "71");
        CHECK_STR(value(2), "61");
        CHECK_STR(value(3), "-0.2");
        CHECK_STR(value(4), "Ice \"black\" ice");
        CHECK_STR(value(5), "Wind");
        CHECK_STR(value(6), "true");
        CHECK_STR(value(7), "null");
        CHECK_STR(value(8), "Mitte");
        CHECK_STR(value(9), "52.52");
        CHECK_STR(value(10), "2026-11-11T12:00");
        CHECK_STR(value(11), "\xC2\xB0" "C"); /* the degree sign, as UTF-8 */
    }
}

static void test_what_has_no_value(void)
{
    static const char *const PATHS[] = {
        "current",            /* an object */
        "daily.weather_code", /* an array */
        "daily.weather_code[3]",
        "nothing.here",
        "current.temperature", /* a prefix of a key is not the key */
        "alerts[2].title",
    };
    read_in_pieces(PATHS, 6, WEATHER, 7);
    for (int i = 0; i < 6; i++) {
        CHECK(json_pick_value(&pick, i) == NULL);
    }
    CHECK(json_pick_value(&pick, -1) == NULL && json_pick_value(&pick, 6) == NULL);
}

static void test_documents_that_are_not_objects(void)
{
    static const char *const FIRST[] = { "[0]", "[2].a", "[1][1]" };
    read_in_pieces(FIRST, 3, "[10,[20,21],{\"a\":\"x\"}]", 4);
    CHECK_STR(value(0), "10");
    CHECK_STR(value(1), "x");
    CHECK_STR(value(2), "21");

    /* a bare number is complete only when the document ends */
    static const char *const ROOT[] = { "" };
    read_in_pieces(ROOT, 1, "42", 1);
    CHECK(json_pick_value(&pick, 0) == NULL); /* nothing is picked outside a container */
}

static void test_strings(void)
{
    static const char *const PATHS[] = { "a", "b", "c", "d", "e" };
    read_in_pieces(PATHS, 5,
                   "{\"a\":\"line\\nbreak\\ttab\",\"b\":\"sl\\/ash \\\\ back\","
                   "\"c\":\"caf\xC3\xA9 \\u0041\\u00e9 \\ud83d\\ude00!\",\"d\":\"\",\"e\":\"{[,:]}\"}",
                   5);
    CHECK_STR(value(0), "line break tab");
    CHECK_STR(value(1), "sl/ash \\ back");
    /* text is UTF-8 and stays UTF-8: as it came, and as \u spelled it - in
     * two halves when it is beyond the first 65536 */
    CHECK_STR(value(2), "caf\xC3\xA9 A\xC3\xA9 \xF0\x9F\x98\x80!");
    CHECK_STR(value(3), "");
    CHECK_STR(value(4), "{[,:]}"); /* punctuation inside a string is text */
}

static void test_a_value_is_cut_between_characters(void)
{
    /* 62 letters and a character of two bytes: the last byte would be the
     * 64th, so the character is left out whole, and nothing after it slips
     * into the room it leaves */
    char json[200] = "{\"t\":\"";
    for (int i = 0; i < 62; i++) {
        strcat(json, "a");
    }
    strcat(json, "\xC3\xA9z\"}");
    static const char *const PATHS[] = { "t" };
    read_in_pieces(PATHS, 1, json, 7);
    CHECK(strlen(value(0)) == 62 && value(0)[61] == 'a');
}

static void test_limits(void)
{
    /* a value longer than the limit is cut, not dropped */
    char json[400] = "{\"long\":\"";
    memset(json + strlen(json), 'x', 200);
    strcat(json, "\",\"after\":1}");
    static const char *const PATHS[] = { "long", "after" };
    read_in_pieces(PATHS, 2, json, 16);
    CHECK(strlen(value(0)) == JSON_PICK_VALUE_MAX);
    CHECK_STR(value(1), "1");

    /* a key longer than the limit matches nothing, and what follows is still read */
    char long_key[200] = "{\"";
    memset(long_key + 2, 'k', 40);
    strcat(long_key, "\":1,\"b\":2}");
    static const char *const AFTER[] = { "kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk", "b" };
    read_in_pieces(AFTER, 2, long_key, 3);
    CHECK(json_pick_value(&pick, 0) == NULL);
    CHECK_STR(value(1), "2");

    /* deeper than the limit: nothing from down there, and the way back up is found */
    static const char *const DEEP[] = { "a[0][0][0][0][0][0][0].k", "z" };
    read_in_pieces(DEEP, 2, "{\"a\":[[[[[[[[{\"k\":\"v\"},\"s\"]]]]]]]],\"z\":9}", 2);
    CHECK(json_pick_value(&pick, 0) == NULL);
    CHECK_STR(value(1), "9");

    /* the first of two equal keys is the one */
    static const char *const TWICE[] = { "a" };
    read_in_pieces(TWICE, 1, "{\"a\":1,\"a\":2}", 3);
    CHECK_STR(value(0), "1");
}

static void test_broken_documents(void)
{
    static const char *const PATHS[] = { "a", "b" };
    json_pick_init(&pick, PATHS, 2);
    CHECK(json_pick_feed(&pick, "{\"a\":1}", 7));
    CHECK(!json_pick_feed(&pick, "}", 1)); /* closes what was never opened */
    CHECK(!json_pick_feed(&pick, "{\"b\":2}", 7));
    CHECK_STR(value(0), "1"); /* what was read before stays */
    CHECK(json_pick_value(&pick, 1) == NULL);

    /* cut off in the middle of a string: that string is not a value */
    read_in_pieces(PATHS, 2, "{\"a\":\"unfinis", 4);
    CHECK(json_pick_value(&pick, 0) == NULL);

    json_pick_init(&pick, PATHS, 2);
    CHECK(!json_pick_feed(&pick, "{\"a\":\"\\uZZZZ\"}", 14));

    /* an HTML error page instead of JSON finds nothing and breaks nothing */
    read_in_pieces(PATHS, 2, "<html><body>502 Bad Gateway</body></html>", 6);
    CHECK(json_pick_value(&pick, 0) == NULL && json_pick_value(&pick, 1) == NULL);
}

int main(void)
{
    test_picks_by_path();
    test_what_has_no_value();
    test_documents_that_are_not_objects();
    test_strings();
    test_a_value_is_cut_between_characters();
    test_limits();
    test_broken_documents();
    REPORT();
}
