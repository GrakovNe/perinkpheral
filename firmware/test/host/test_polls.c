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
    uint32_t wait_ms;
    CHECK(poll_schedule_due(&schedule, 0, &wait_ms) == NULL && wait_ms == UINT32_MAX);

    /* two applications; the first runs are spread out, in the order added */
    CHECK(poll_schedule_add(&schedule, 0, 0, 60, 1000));
    CHECK(poll_schedule_add(&schedule, 1, 0, 15, 1000));
    const poll_entry_t *due = poll_schedule_due(&schedule, 1000, &wait_ms);
    CHECK(due != NULL && due->app == 0 && wait_ms == 0);
    poll_schedule_done(&schedule, due, 1500);

    CHECK(poll_schedule_due(&schedule, 1500, &wait_ms) == NULL && wait_ms == 1500);
    due = poll_schedule_due(&schedule, 3000, &wait_ms);
    CHECK(due != NULL && due->app == 1);
    poll_schedule_done(&schedule, due, 3200);

    /* then each on its own period, counted from when it was fetched */
    CHECK(poll_schedule_due(&schedule, 3200, &wait_ms) == NULL && wait_ms == 15000);
    due = poll_schedule_due(&schedule, 18200, &wait_ms);
    CHECK(due != NULL && due->app == 1);
    poll_schedule_done(&schedule, due, 18200);
    CHECK(poll_schedule_due(&schedule, 18200, &wait_ms) == NULL && wait_ms == 15000);

    /* after a long sleep both are due: the one waiting longest first, and
     * no burst to catch up with the periods slept through */
    int64_t much_later = 10 * 60 * 1000;
    due = poll_schedule_due(&schedule, much_later, &wait_ms);
    CHECK(due != NULL && due->app == 1);
    poll_schedule_done(&schedule, due, much_later);
    due = poll_schedule_due(&schedule, much_later, &wait_ms);
    CHECK(due != NULL && due->app == 0);
    poll_schedule_done(&schedule, due, much_later);
    CHECK(poll_schedule_due(&schedule, much_later, &wait_ms) == NULL && wait_ms == 15000);
}

static void test_schedule_hurry(void)
{
    poll_schedule_t schedule;
    poll_schedule_init(&schedule);
    CHECK(poll_schedule_add(&schedule, 0, 0, 600, 0));
    CHECK(poll_schedule_add(&schedule, 1, 0, 600, 0));
    CHECK(poll_schedule_add(&schedule, 1, 1, 900, 0));
    uint32_t wait_ms;
    const poll_entry_t *due;
    /* run everything once, so that all three wait for their periods */
    while ((due = poll_schedule_due(&schedule, 100000, &wait_ms)) != NULL) {
        poll_schedule_done(&schedule, due, 100000);
    }
    CHECK(wait_ms == 600000);

    /* the settings of application 1 changed: both of its polls, and only they */
    poll_schedule_hurry(&schedule, 1, 100500);
    int hurried = 0;
    while ((due = poll_schedule_due(&schedule, 100500, &wait_ms)) != NULL) {
        CHECK(due->app == 1);
        poll_schedule_done(&schedule, due, 100500);
        hurried++;
    }
    CHECK(hurried == 2);
    CHECK(wait_ms == 599500); /* application 0 kept its time */
}

static void test_pace_and_alarms(void)
{
    poll_schedule_t schedule;
    poll_schedule_init(&schedule);
    CHECK(poll_schedule_add(&schedule, 0, 0, 600, 0));
    uint32_t wait_ms;
    const poll_entry_t *due;
    while ((due = poll_schedule_due(&schedule, 50000, &wait_ms)) != NULL) {
        poll_schedule_finished(&schedule, due->app, due->poll, 50000);
    }
    CHECK(wait_ms == 600000);
    /* the server said: come again in an hour */
    CHECK(poll_schedule_set_interval(&schedule, 0, 0, 3600, 60000));
    CHECK(poll_schedule_due(&schedule, 60000, &wait_ms) == NULL && wait_ms == 3600000);
    /* said again a while later, it is the same pace: the hour is not started over */
    CHECK(poll_schedule_set_interval(&schedule, 0, 0, 3600, 1860000));
    CHECK(poll_schedule_due(&schedule, 1860000, &wait_ms) == NULL && wait_ms == 1800000);
    /* the limits of a manifest hold */
    CHECK(poll_schedule_set_interval(&schedule, 0, 0, 1, 60000));
    CHECK(poll_schedule_due(&schedule, 60000, &wait_ms) == NULL && wait_ms == 10000);
    CHECK(poll_schedule_set_interval(&schedule, 0, 0, 999999999, 60000));
    CHECK(poll_schedule_due(&schedule, 60000, &wait_ms) == NULL && wait_ms == 86400000);
    CHECK(!poll_schedule_set_interval(&schedule, 0, 1, 60, 60000));
    CHECK(!poll_schedule_set_interval(&schedule, 5, 0, 60, 60000));

    app_alarms_t alarms;
    int app = 0, alarm = 0;
    app_alarms_init(&alarms);
    CHECK(!app_alarms_take_due(&alarms, 1000, &app, &alarm));
    CHECK(app_alarms_wait_ms(&alarms, 1000) == UINT32_MAX);
    /* two applications, and two alarms of one of them */
    CHECK(app_alarms_set(&alarms, 0x7a7a, 0, 31000, false));
    CHECK(app_alarms_set(&alarms, 0x1111, 2, 11000, false));
    CHECK(app_alarms_set(&alarms, 0x1111, 3, 21000, true));
    CHECK(app_alarms_wait_ms(&alarms, 1000) == 10000);
    CHECK(!app_alarms_take_due(&alarms, 10999, &app, &alarm));
    CHECK(app_alarms_take_due(&alarms, 11000, &app, &alarm) && app == 0x1111 && alarm == 2);
    CHECK(!app_alarms_take_due(&alarms, 11000, &app, &alarm)); /* once */
    /* set anew: the same alarm, another time */
    CHECK(app_alarms_set(&alarms, 0x7a7a, 0, 15000, false));
    CHECK(app_alarms_take_due(&alarms, 15000, &app, &alarm) && app == 0x7a7a && alarm == 0);
    /* the wall clock is set five seconds ahead: the alarm for a time of day
     * comes five seconds sooner */
    app_alarms_clock_moved(&alarms, 5000);
    CHECK(!app_alarms_take_due(&alarms, 15999, &app, &alarm));
    CHECK(app_alarms_take_due(&alarms, 16000, &app, &alarm) && app == 0x1111 && alarm == 3);
    /* and one "in so long" does not move */
    CHECK(app_alarms_set(&alarms, 0x1111, 1, 40000, false));
    app_alarms_clock_moved(&alarms, -3600000);
    CHECK(app_alarms_wait_ms(&alarms, 30000) == 10000);
    /* taken back; an application that was removed loses its own */
    app_alarms_cancel(&alarms, 0x1111, 1);
    CHECK(app_alarms_wait_ms(&alarms, 30000) == UINT32_MAX);
    CHECK(app_alarms_set(&alarms, 0x1111, 0, 50000, false));
    CHECK(app_alarms_set(&alarms, 0x7a7a, 0, 60000, false));
    const int left[] = { 0x7a7a };
    app_alarms_keep_only(&alarms, left, 1);
    CHECK(app_alarms_take_due(&alarms, 99999, &app, &alarm) && app == 0x7a7a);
    CHECK(!app_alarms_take_due(&alarms, 99999, &app, &alarm));
    /* the table is of a size */
    for (int i = 0; i < APP_ALARMS_MAX; i++) {
        CHECK(app_alarms_set(&alarms, 1, i, 1, false));
    }
    CHECK(!app_alarms_set(&alarms, 2, 0, 1, false));
    CHECK(app_alarms_set(&alarms, 1, 0, 2, false)); /* one that is there can be set anew */
}

static void test_schedule_is_bounded(void)
{
    poll_schedule_t schedule;
    poll_schedule_init(&schedule);
    for (int i = 0; i < POLL_SCHEDULE_MAX; i++) {
        CHECK(poll_schedule_add(&schedule, i, 0, 60, 0));
    }
    CHECK(!poll_schedule_add(&schedule, 99, 0, 60, 0));
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
    uint32_t wait_ms;
    poll_schedule_init(&schedule);
    CHECK(poll_schedule_add(&schedule, 0xA, 0, 60, 0));
    CHECK(poll_schedule_add(&schedule, 0xB, 0, 900, 0));
    /* saving power: nothing is closer than five minutes, and the pace asked
     * for is kept for when the floor is gone */
    poll_schedule_set_floor(&schedule, 300);
    poll_schedule_finished(&schedule, 0xA, 0, 1000);
    poll_schedule_finished(&schedule, 0xB, 0, 1000);
    CHECK(poll_schedule_due(&schedule, 300999, &wait_ms) == NULL && wait_ms == 1);
    CHECK(poll_schedule_due(&schedule, 301000, &wait_ms)->app == 0xA);
    poll_schedule_set_floor(&schedule, 0);
    poll_schedule_finished(&schedule, 0xA, 0, 301000);
    CHECK(poll_schedule_due(&schedule, 361000, &wait_ms)->app == 0xA);
    /* one application is removed: its polls go, the other's stay */
    CHECK(poll_schedule_has(&schedule, 0xA, 0) && poll_schedule_has(&schedule, 0xB, 0));
    const int left[] = { 0xB };
    poll_schedule_keep_only(&schedule, left, 1);
    CHECK(!poll_schedule_has(&schedule, 0xA, 0) && poll_schedule_has(&schedule, 0xB, 0));
    CHECK(schedule.count == 1);
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
    test_pace_and_alarms();
    REPORT();
}
