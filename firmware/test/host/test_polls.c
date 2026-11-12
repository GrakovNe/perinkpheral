/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <string.h>

#include "app_manifest.h"
#include "poll_schedule.h"
#include "text_template.h"
#include "tinytest.h"

/* ---- polls in the manifest ---- */

#define MANIFEST_START "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"polls\":"

static void test_polls_in_the_manifest(void)
{
    static const char json[] =
        MANIFEST_START "[{\"url\":\"http://10.0.0.2:32400/status\",\"every\":15,"
                       "\"headers\":{\"Accept\":\"application/json\",\"X-Token\":\"{{token}}\"}},"
                       "{\"url\":\"https://example.org/x\",\"every\":3600}]}";
    app_manifest_t manifest;
    CHECK(app_manifest_parse(json, &manifest) == APP_MANIFEST_OK);
    CHECK(manifest.poll_count == 2);
    CHECK(manifest.poll_every_s[0] == 15 && manifest.poll_every_s[1] == 3600);

    app_poll_t poll;
    CHECK(app_manifest_parse_poll(json, 0, &poll) == APP_MANIFEST_OK);
    CHECK_STR(poll.url, "http://10.0.0.2:32400/status");
    CHECK(poll.every_s == 15 && poll.header_count == 2);
    CHECK_STR(poll.headers[0].name, "Accept");
    CHECK_STR(poll.headers[1].value, "{{token}}");

    CHECK(app_manifest_parse_poll(json, 1, &poll) == APP_MANIFEST_OK);
    CHECK_STR(poll.url, "https://example.org/x");
    CHECK(poll.header_count == 0);

    /* a number that is not there */
    CHECK(app_manifest_parse_poll(json, 2, &poll) == APP_MANIFEST_BAD_POLL);
    CHECK(app_manifest_parse_poll(json, -1, &poll) == APP_MANIFEST_BAD_POLL);
}

static app_manifest_result_t parse_polls(const char *polls)
{
    char json[1024];
    strcpy(json, MANIFEST_START);
    strcat(json, polls);
    strcat(json, "}");
    app_manifest_t manifest;
    return app_manifest_parse(json, &manifest);
}

static void test_bad_polls(void)
{
    CHECK(parse_polls("[]") == APP_MANIFEST_OK);
    CHECK(parse_polls("{}") == APP_MANIFEST_BAD_POLL);
    CHECK(parse_polls("[5]") == APP_MANIFEST_BAD_POLL);
    /* the url: there, http or https */
    CHECK(parse_polls("[{\"every\":15}]") == APP_MANIFEST_BAD_POLL);
    CHECK(parse_polls("[{\"url\":\"ftp://x/y\",\"every\":15}]") == APP_MANIFEST_BAD_POLL);
    CHECK(parse_polls("[{\"url\":\"example.org\",\"every\":15}]") == APP_MANIFEST_BAD_POLL);
    /* every: there, a number, ten seconds to a day */
    CHECK(parse_polls("[{\"url\":\"http://x/\"}]") == APP_MANIFEST_BAD_POLL);
    CHECK(parse_polls("[{\"url\":\"http://x/\",\"every\":\"15\"}]") == APP_MANIFEST_BAD_POLL);
    CHECK(parse_polls("[{\"url\":\"http://x/\",\"every\":9}]") == APP_MANIFEST_BAD_POLL);
    CHECK(parse_polls("[{\"url\":\"http://x/\",\"every\":10}]") == APP_MANIFEST_OK);
    CHECK(parse_polls("[{\"url\":\"http://x/\",\"every\":86400}]") == APP_MANIFEST_OK);
    CHECK(parse_polls("[{\"url\":\"http://x/\",\"every\":86401}]") == APP_MANIFEST_BAD_POLL);
    /* headers: an object of strings, four at most */
    CHECK(parse_polls("[{\"url\":\"http://x/\",\"every\":15,\"headers\":[]}]") ==
          APP_MANIFEST_BAD_POLL);
    CHECK(parse_polls("[{\"url\":\"http://x/\",\"every\":15,\"headers\":{\"A\":1}}]") ==
          APP_MANIFEST_BAD_POLL);
    CHECK(parse_polls("[{\"url\":\"http://x/\",\"every\":15,"
                      "\"headers\":{\"A\":\"1\",\"B\":\"2\",\"C\":\"3\",\"D\":\"4\"}}]") ==
          APP_MANIFEST_OK);
    CHECK(parse_polls("[{\"url\":\"http://x/\",\"every\":15,"
                      "\"headers\":{\"A\":\"1\",\"B\":\"2\",\"C\":\"3\",\"D\":\"4\",\"E\":\"5\"}}]") ==
          APP_MANIFEST_BAD_POLL);
    /* five polls */
    CHECK(parse_polls("[{\"url\":\"http://x/\",\"every\":15},{\"url\":\"http://x/\",\"every\":15},"
                      "{\"url\":\"http://x/\",\"every\":15},{\"url\":\"http://x/\",\"every\":15},"
                      "{\"url\":\"http://x/\",\"every\":15}]") == APP_MANIFEST_BAD_POLL);
}

/* ---- {{key}} ---- */

static bool settings_value(void *context, const char *key, char *value, size_t value_size)
{
    (void)context;
    const char *found = strcmp(key, "token") == 0    ? "s3cret"
                        : strcmp(key, "server") == 0 ? "10.0.0.2"
                        : strcmp(key, "empty") == 0  ? ""
                        : strcmp(key, "base") == 0   ? "http://10.0.0.2:8123"
                        : strcmp(key, "city") == 0   ? "New York&x=1/~a"
                                                     : NULL;
    if (found == NULL || strlen(found) >= value_size) {
        return false;
    }
    strcpy(value, found);
    return true;
}

static void test_template_for_a_url(void)
{
    char filled[64];
    /* a value stays one value: what would cut the URL goes in encoded */
    CHECK(text_template_fill_url("http://a/q?c={{city}}&t={{token}}", settings_value, NULL, filled,
                                 sizeof filled));
    CHECK_STR(filled, "http://a/q?c=New%20York%26x%3D1%2F~a&t=s3cret");
    /* the template's own text is the author's and is left alone */
    CHECK(text_template_fill_url("http://a/b c?x={{empty}}", settings_value, NULL, filled,
                                 sizeof filled));
    CHECK_STR(filled, "http://a/b c?x=");
    /* the longer text has to fit too */
    char tight[27];
    CHECK(!text_template_fill_url("http://a/?c={{city}}", settings_value, NULL, tight,
                                  sizeof tight));
    CHECK_STR(tight, "");
    char just_enough[36];
    CHECK(text_template_fill_url("http://a/?c={{city}}", settings_value, NULL, just_enough,
                                 sizeof just_enough));
    /* a setting the URL begins with is the server itself: it goes in as it is */
    char whole[64];
    CHECK(text_template_fill_url("{{base}}/api?c={{base}}", settings_value, NULL, whole,
                                 sizeof whole));
    CHECK_STR(whole, "http://10.0.0.2:8123/api?c=http%3A%2F%2F10.0.0.2%3A8123");
    /* a header is not a URL: there the value goes in as it is */
    CHECK(text_template_fill("{{city}}", settings_value, NULL, filled, sizeof filled));
    CHECK_STR(filled, "New York&x=1/~a");
}

static void test_template(void)
{
    char filled[48];
    CHECK(text_template_fill("plain text", settings_value, NULL, filled, sizeof filled));
    CHECK_STR(filled, "plain text");
    CHECK(text_template_fill("", settings_value, NULL, filled, sizeof filled));
    CHECK_STR(filled, "");

    CHECK(text_template_fill("http://{{server}}/x?t={{token}}", settings_value, NULL, filled,
                             sizeof filled));
    CHECK_STR(filled, "http://10.0.0.2/x?t=s3cret");
    CHECK(text_template_fill("{{token}}", settings_value, NULL, filled, sizeof filled));
    CHECK_STR(filled, "s3cret");
    CHECK(text_template_fill("a{{empty}}b", settings_value, NULL, filled, sizeof filled));
    CHECK_STR(filled, "ab");

    /* a key nobody set, braces left open, nothing between them */
    CHECK(!text_template_fill("x{{nope}}", settings_value, NULL, filled, sizeof filled));
    CHECK_STR(filled, "");
    CHECK(!text_template_fill("x{{token", settings_value, NULL, filled, sizeof filled));
    CHECK(!text_template_fill("x{{}}", settings_value, NULL, filled, sizeof filled));
    CHECK(!text_template_fill("{{a_key_far_too_long}}", settings_value, NULL, filled,
                              sizeof filled));

    /* the result has to fit, with its terminator */
    char exact[7];
    CHECK(text_template_fill("{{token}}", settings_value, NULL, exact, sizeof exact));
    CHECK_STR(exact, "s3cret");
    char short_by_one[6];
    CHECK(!text_template_fill("{{token}}", settings_value, NULL, short_by_one,
                              sizeof short_by_one));
    CHECK(!text_template_fill("1234567", settings_value, NULL, exact, sizeof exact));
    CHECK(text_template_fill("123456", settings_value, NULL, exact, sizeof exact));
}

/* ---- the schedule ---- */

static void test_schedule(void)
{
    poll_schedule_t schedule;
    poll_schedule_init(&schedule);
    CHECK(poll_schedule_next_wanted(&schedule) == NULL);
    CHECK(poll_schedule_pace_ms(&schedule, 0, 0) == 0);

    /* the first runs are spread out */
    uint32_t first_in_ms;
    CHECK(poll_schedule_add(&schedule, 0xA, 0, 60, &first_in_ms) && first_in_ms == 0);
    CHECK(poll_schedule_add(&schedule, 0xB, 0, 15, &first_in_ms) && first_in_ms == 2000);
    CHECK(poll_schedule_add(&schedule, 0xB, 1, 900, &first_in_ms) && first_in_ms == 4000);
    CHECK(poll_schedule_pace_ms(&schedule, 0xA, 0) == 60000);
    CHECK(poll_schedule_pace_ms(&schedule, 0xB, 0) == 15000);

    /* nothing is wanted until its time comes; then the one waiting longest first */
    CHECK(poll_schedule_next_wanted(&schedule) == NULL);
    CHECK(poll_schedule_want(&schedule, 0xB, 0));
    CHECK(poll_schedule_want(&schedule, 0xA, 0));
    CHECK(poll_schedule_want(&schedule, 0xB, 0)); /* wanted already: keeps its place */
    const poll_entry_t *next = poll_schedule_next_wanted(&schedule);
    CHECK(next != NULL && next->app == 0xB && next->poll == 0);
    /* not answered yet - no network, say: it stays the next */
    CHECK(poll_schedule_next_wanted(&schedule) == next);
    poll_schedule_answered(&schedule, 0xB, 0);
    next = poll_schedule_next_wanted(&schedule);
    CHECK(next != NULL && next->app == 0xA);
    poll_schedule_answered(&schedule, 0xA, 0);
    CHECK(poll_schedule_next_wanted(&schedule) == NULL);
    CHECK(!poll_schedule_want(&schedule, 0xC, 0));
    CHECK(!poll_schedule_want(&schedule, 0xA, 1));
}

static void test_schedule_hurry(void)
{
    poll_schedule_t schedule;
    uint32_t first_in_ms;
    poll_schedule_init(&schedule);
    poll_schedule_add(&schedule, 0xA, 0, 600, &first_in_ms);
    poll_schedule_add(&schedule, 0xB, 0, 600, &first_in_ms);
    poll_schedule_add(&schedule, 0xB, 1, 900, &first_in_ms);
    /* the settings of one application changed: its polls, and only its */
    poll_schedule_want_of(&schedule, 0xB);
    int asked = 0;
    const poll_entry_t *next;
    while ((next = poll_schedule_next_wanted(&schedule)) != NULL) {
        CHECK(next->app == 0xB && next->poll == asked);
        poll_schedule_answered(&schedule, next->app, next->poll);
        asked++;
    }
    CHECK(asked == 2);
    /* the network is back: everything */
    poll_schedule_want_all(&schedule);
    asked = 0;
    while ((next = poll_schedule_next_wanted(&schedule)) != NULL) {
        poll_schedule_answered(&schedule, next->app, next->poll);
        asked++;
    }
    CHECK(asked == 3);
}

static void test_pace(void)
{
    poll_schedule_t schedule;
    uint32_t first_in_ms;
    bool is_another = false;
    poll_schedule_init(&schedule);
    poll_schedule_add(&schedule, 0xA, 0, 600, &first_in_ms);
    /* the server said when to come again */
    CHECK(poll_schedule_set_interval(&schedule, 0xA, 0, 3600, &is_another) && is_another);
    CHECK(poll_schedule_pace_ms(&schedule, 0xA, 0) == 3600000);
    /* saying it again moves nothing */
    CHECK(poll_schedule_set_interval(&schedule, 0xA, 0, 3600, &is_another) && !is_another);
    /* the limits of a manifest hold */
    CHECK(poll_schedule_set_interval(&schedule, 0xA, 0, 1, &is_another));
    CHECK(poll_schedule_pace_ms(&schedule, 0xA, 0) == 10000);
    CHECK(poll_schedule_set_interval(&schedule, 0xA, 0, 999999999, &is_another));
    CHECK(poll_schedule_pace_ms(&schedule, 0xA, 0) == 86400000);
    CHECK(!poll_schedule_set_interval(&schedule, 0xA, 1, 60, &is_another));
    CHECK(!poll_schedule_set_interval(&schedule, 0xC, 0, 60, &is_another));
}

static void test_schedule_is_bounded(void)
{
    poll_schedule_t schedule;
    uint32_t first_in_ms;
    poll_schedule_init(&schedule);
    for (int i = 0; i < POLL_SCHEDULE_MAX; i++) {
        CHECK(poll_schedule_add(&schedule, i, 0, 60, &first_in_ms));
    }
    CHECK(!poll_schedule_add(&schedule, 99, 0, 60, &first_in_ms));
}

static void test_pick(void)
{
    static app_poll_t poll;
    const char *with_pick =
        "{\"name\":\"w\",\"title\":\"W\",\"abi\":2,\"polls\":[{\"url\":\"https://a.example/x\","
        "\"every\":600,\"pick\":[\"current.temperature_2m\",\"daily.weather_code[0]\"]},"
        "{\"url\":\"http://b.example/y\",\"every\":60}]}";
    app_manifest_t manifest;
    CHECK(app_manifest_parse(with_pick, &manifest) == APP_MANIFEST_OK);
    CHECK(app_manifest_parse_poll(with_pick, 0, &poll) == APP_MANIFEST_OK);
    CHECK(poll.pick_count == 2);
    CHECK_STR(poll.pick[0], "current.temperature_2m");
    CHECK_STR(poll.pick[1], "daily.weather_code[0]");
    /* no "pick": the answer goes to the application as it comes */
    CHECK(app_manifest_parse_poll(with_pick, 1, &poll) == APP_MANIFEST_OK && poll.pick_count == 0);

    const char *bad[] = {
        "{\"name\":\"w\",\"title\":\"W\",\"abi\":2,\"polls\":[{\"url\":\"http://a/x\",\"every\":60,\"pick\":\"a.b\"}]}",
        "{\"name\":\"w\",\"title\":\"W\",\"abi\":2,\"polls\":[{\"url\":\"http://a/x\",\"every\":60,\"pick\":[1]}]}",
        "{\"name\":\"w\",\"title\":\"W\",\"abi\":2,\"polls\":[{\"url\":\"http://a/x\",\"every\":60,\"pick\":[\"\"]}]}",
        ("{\"name\":\"w\",\"title\":\"W\",\"abi\":2,\"polls\":[{\"url\":\"http://a/x\",\"every\":60,\"pick\":["
         "\"0123456789012345678901234567890123456789012345678901234567890123\"]}]}"),
    };
    for (int i = 0; i < 4; i++) {
        CHECK(app_manifest_parse(bad[i], &manifest) == APP_MANIFEST_BAD_POLL);
    }
}

static void test_floor_and_removal(void)
{
    poll_schedule_t schedule;
    uint32_t first_in_ms;
    poll_schedule_init(&schedule);
    poll_schedule_add(&schedule, 0xA, 0, 60, &first_in_ms);
    poll_schedule_add(&schedule, 0xB, 0, 900, &first_in_ms);
    /* saving power: no poll closer to its last than the floor */
    poll_schedule_set_floor(&schedule, 300);
    CHECK(poll_schedule_pace_ms(&schedule, 0xA, 0) == 300000);
    CHECK(poll_schedule_pace_ms(&schedule, 0xB, 0) == 900000);
    /* and the pace it asked for is back with the floor gone */
    poll_schedule_set_floor(&schedule, 0);
    CHECK(poll_schedule_pace_ms(&schedule, 0xA, 0) == 60000);
    /* an application that was removed loses its polls, wanted or not */
    poll_schedule_want_all(&schedule);
    const int left[] = { 0xB };
    poll_schedule_keep_only(&schedule, left, 1);
    CHECK(!poll_schedule_has(&schedule, 0xA, 0) && poll_schedule_has(&schedule, 0xB, 0));
    const poll_entry_t *next = poll_schedule_next_wanted(&schedule);
    CHECK(next != NULL && next->app == 0xB);
}

int main(void)
{
    test_floor_and_removal();
    test_polls_in_the_manifest();
    test_bad_polls();
    test_template();
    test_schedule();
    test_schedule_is_bounded();
    test_pick();
    test_schedule_hurry();
    test_template_for_a_url();
    test_pace();
    REPORT();
}
