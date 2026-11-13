/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_manifest.h"

#include <string.h>

#include "host_api.h"
#include "tinytest.h"

static app_manifest_t manifest;

static app_manifest_result_t parse(const char *json)
{
    return app_manifest_parse(json, &manifest);
}

static void test_complete(void)
{
    CHECK(parse("{\"name\":\"hello\",\"title\":\"Hello\",\"version\":\"0.1.0\","
                "\"abi\":2,\"fullscreen\":true}") == APP_MANIFEST_OK);
    CHECK_STR(manifest.name, "hello");
    CHECK_STR(manifest.title, "Hello");
    CHECK_STR(manifest.version, "0.1.0");
    CHECK(manifest.fullscreen);
}

static void test_defaults_and_unknown_fields(void)
{
    /* no version, no fullscreen, and fields of a later host */
    CHECK(parse("{\"name\":\"plex\",\"title\":\"Now Playing\",\"abi\":2,"
                "\"hosts\":[\"192.168.1.10\"],"
                "\"settings\":[{\"key\":\"token\",\"type\":\"secret\",\"title\":\"Token\"}]}") == APP_MANIFEST_OK);
    CHECK_STR(manifest.version, "");
    CHECK(!manifest.fullscreen);
    CHECK(manifest.poll_count == 0);
}

static void test_not_json(void)
{
    CHECK(parse("") == APP_MANIFEST_NOT_JSON);
    CHECK(parse("{\"name\":") == APP_MANIFEST_NOT_JSON);
    CHECK(parse("[1,2]") == APP_MANIFEST_NOT_JSON);
    CHECK(parse("\"hello\"") == APP_MANIFEST_NOT_JSON);
}

static void test_name(void)
{
    CHECK(parse("{\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"\",\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":5,\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_BAD_NAME);
    /* it becomes a folder and a settings namespace: no capitals, no paths */
    CHECK(parse("{\"name\":\"Hello\",\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"../x\",\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"a b\",\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_BAD_NAME);

    CHECK(parse("{\"name\":\"a234567890123\",\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_OK);
    CHECK(parse("{\"name\":\"a2345678901234\",\"title\":\"T\",\"abi\":2}") ==
          APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"my-app_2\",\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_OK);
}

static void test_title(void)
{
    CHECK(parse("{\"name\":\"a\",\"abi\":2}") == APP_MANIFEST_BAD_TITLE);
    CHECK(parse("{\"name\":\"a\",\"title\":\"\",\"abi\":2}") == APP_MANIFEST_BAD_TITLE);
    CHECK(parse("{\"name\":\"a\",\"title\":\"12345678901234567890123\",\"abi\":2}") ==
          APP_MANIFEST_OK);
    CHECK(parse("{\"name\":\"a\",\"title\":\"123456789012345678901234\",\"abi\":2}") ==
          APP_MANIFEST_BAD_TITLE);
}

static void test_other_fields(void)
{
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"fullscreen\":\"yes\"}") ==
          APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"version\":7}") ==
          APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"version\":\"123456789012345\"}") ==
          APP_MANIFEST_OK);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"version\":\"1234567890123456\"}") ==
          APP_MANIFEST_BAD_FIELD);
    /* a zero inside a string would cut it short unnoticed */
    CHECK(parse("{\"name\":\"good\\u0000x\",\"title\":\"T\",\"abi\":2}") != APP_MANIFEST_OK);
}

static void test_abi(void)
{
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\"}") == APP_MANIFEST_OTHER_ABI);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":1}") == APP_MANIFEST_OTHER_ABI);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":\"1\"}") == APP_MANIFEST_OTHER_ABI);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2.5}") == APP_MANIFEST_OTHER_ABI);
}

static void test_settings(void)
{
    const char *good = "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"token\",\"type\":\"secret\",\"title\":\"Token\"},{\"key\":\"units\",\"type\":\"choice\",\"title\":\"Units\",\"options\":[\"celsius\",\"fahrenheit\"],\"help\":\"What the numbers are in.\"},{\"key\":\"lat\",\"type\":\"number\",\"title\":\"Latitude\"},{\"key\":\"big\",\"type\":\"toggle\",\"title\":\"Large digits\"},{\"key\":\"city\",\"type\":\"text\",\"title\":\"City\"}]}";
    CHECK(parse(good) == APP_MANIFEST_OK);
    /* no settings at all is fine too */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_OK);

    static const char *const BAD[] = {
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":\"nope\"}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"type\":\"text\",\"title\":\"No key\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"k\",\"title\":\"No type\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"k\",\"type\":\"text\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"k\",\"type\":\"colour\",\"title\":\"Unknown type\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"has space\",\"type\":\"text\",\"title\":\"T\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"0123456789012345\",\"type\":\"text\",\"title\":\"T\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"k\",\"type\":\"choice\",\"title\":\"T\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"k\",\"type\":\"choice\",\"title\":\"T\",\"options\":[]}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"k\",\"type\":\"choice\",\"title\":\"T\",\"options\":[1]}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"T\",\"options\":[\"a\"]}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"T\",\"help\":7}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"A\"},{\"key\":\"k\",\"type\":\"number\",\"title\":\"B\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"k0\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k1\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k2\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k3\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k4\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k5\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k6\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k7\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k8\",\"type\":\"text\",\"title\":\"T\"}]}",
    };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        CHECK(parse(BAD[i]) == APP_MANIFEST_BAD_SETTING);
    }

    /* what the page may store, by the type the manifest gives the key */
    bool is_secret = false;
    CHECK(app_manifest_accepts_setting(good, "token", "s3cr3t", &is_secret) && is_secret);
    CHECK(app_manifest_accepts_setting(good, "city", "Berlin", &is_secret) && !is_secret);
    CHECK(app_manifest_accepts_setting(good, "units", "fahrenheit", NULL));
    CHECK(!app_manifest_accepts_setting(good, "units", "kelvin", NULL));
    CHECK(app_manifest_accepts_setting(good, "lat", "-55.75", NULL));
    CHECK(app_manifest_accepts_setting(good, "lat", "7", NULL));
    CHECK(!app_manifest_accepts_setting(good, "lat", "north", NULL));
    CHECK(!app_manifest_accepts_setting(good, "lat", "1.2.3", NULL));
    CHECK(!app_manifest_accepts_setting(good, "lat", "-", NULL));
    CHECK(app_manifest_accepts_setting(good, "big", "1", NULL));
    CHECK(!app_manifest_accepts_setting(good, "big", "yes", NULL));
    /* empty takes a setting back, whatever its type */
    CHECK(app_manifest_accepts_setting(good, "lat", "", NULL));
    CHECK(app_manifest_accepts_setting(good, "units", "", NULL));
    /* a key the manifest does not have is nobody's setting */
    CHECK(!app_manifest_accepts_setting(good, "forecast", "x", NULL));
    CHECK(!app_manifest_accepts_setting("{\"name\":\"a\"}", "token", "x", NULL));
    /* a line break would be a header of the user's own in a request */
    CHECK(!app_manifest_accepts_setting(good, "city", "x\r\nHost: evil", NULL));
    CHECK(!app_manifest_accepts_setting(good, "token", "tab\there", NULL));
    char too_long[APP_SETTING_VALUE_MAX + 2];
    memset(too_long, 'x', sizeof too_long - 1);
    too_long[sizeof too_long - 1] = '\0';
    CHECK(!app_manifest_accepts_setting(good, "city", too_long, NULL));

    /* a place is a name, and names the two numbers the page fills for it */
    const char *with_place = "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"city\",\"type\":\"place\",\"title\":\"City\",\"latitude\":\"lat\",\"longitude\":\"lon\"},{\"key\":\"lat\",\"type\":\"number\",\"title\":\"N\"},{\"key\":\"lon\",\"type\":\"number\",\"title\":\"N\"}]}";
    CHECK(parse(with_place) == APP_MANIFEST_OK);
    CHECK(app_manifest_accepts_setting(with_place, "city", "Berlin, Germany", NULL));
    CHECK(app_manifest_accepts_setting(with_place, "lat", "52.52", NULL));
    CHECK(!app_manifest_accepts_setting(with_place, "lon", "east", NULL));
    static const char *const BAD_PLACES[] = {
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"city\",\"type\":\"place\",\"title\":\"City\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"city\",\"type\":\"place\",\"title\":\"City\",\"latitude\":\"lat\",\"longitude\":\"lon\"},{\"key\":\"lat\",\"type\":\"number\",\"title\":\"N\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"city\",\"type\":\"place\",\"title\":\"City\",\"latitude\":\"lat\",\"longitude\":\"lat\"},{\"key\":\"lat\",\"type\":\"number\",\"title\":\"N\"},{\"key\":\"lon\",\"type\":\"number\",\"title\":\"N\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"city\",\"type\":\"place\",\"title\":\"City\",\"latitude\":\"lat\",\"longitude\":\"lon\"},{\"key\":\"lat\",\"type\":\"number\",\"title\":\"N\"},{\"key\":\"lon\",\"type\":\"text\",\"title\":\"N\"}]}",
    };
    for (size_t i = 0; i < sizeof BAD_PLACES / sizeof BAD_PLACES[0]; i++) {
        CHECK(parse(BAD_PLACES[i]) == APP_MANIFEST_BAD_SETTING);
    }

    /* the news an application asks for, by name; an unknown kind is a mistake */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_OK && manifest.intents == 0);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"intents\":[\"time_changed\",\"network_up\",\"started\"]}") == APP_MANIFEST_OK);
    CHECK(manifest.intents == ((1u << HOST_INTENT_TIME_CHANGED) | (1u << HOST_INTENT_NETWORK_UP) |
                               (1u << HOST_INTENT_STARTED)));
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"intents\":[\"moon_phase\"]}") == APP_MANIFEST_BAD_INTENT);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"intents\":\"time_changed\"}") == APP_MANIFEST_BAD_INTENT);

    /* the system API is asked for in so many words */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_OK && !manifest.system);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"system\":true}") == APP_MANIFEST_OK && manifest.system);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"system\":\"yes\"}") == APP_MANIFEST_BAD_FIELD);

    /* a default is a value the setting itself would take */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"on\",\"type\":\"toggle\",\"title\":\"T\",\"default\":\"1\"},{\"key\":\"n\",\"type\":\"number\",\"title\":\"T\",\"default\":\"15\"},{\"key\":\"u\",\"type\":\"choice\",\"title\":\"T\",\"default\":\"b\",\"options\":[\"a\",\"b\"]}]}") == APP_MANIFEST_OK);
    static const char *const BAD_DEFAULTS[] = {
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"on\",\"type\":\"toggle\",\"title\":\"T\",\"default\":\"yes\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"on\",\"type\":\"toggle\",\"title\":\"T\",\"default\":1}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"n\",\"type\":\"number\",\"title\":\"T\",\"default\":\"many\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"u\",\"type\":\"choice\",\"title\":\"T\",\"default\":\"c\",\"options\":[\"a\",\"b\"]}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"settings\":[{\"key\":\"s\",\"type\":\"secret\",\"title\":\"T\",\"default\":\"x\"}]}",
    };
    for (size_t i = 0; i < sizeof BAD_DEFAULTS / sizeof BAD_DEFAULTS[0]; i++) {
        CHECK(parse(BAD_DEFAULTS[i]) == APP_MANIFEST_BAD_SETTING);
    }
}

/* The user's own server, as the settings of the test hold it. */
static const char *server_setting;

static bool setting_value(void *context, const char *key, char *value, size_t value_size)
{
    (void)context;
    if ((strcmp(key, "server") != 0 && strcmp(key, "feed") != 0) || server_setting == NULL) {
        return false;
    }
    snprintf(value, value_size, "%s", server_setting);
    return true;
}

static void test_a_server_of_the_users_own(void)
{
    const char *manifest =
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,"
        "\"settings\":[{\"key\":\"server\",\"type\":\"url\",\"title\":\"Server\"},"
        "{\"key\":\"token\",\"type\":\"secret\",\"title\":\"Token\"}],"
        "\"polls\":[{\"url\":\"{{server}}/api/states\",\"every\":60}],"
        "\"hosts\":[\"{{server}}\"]}";
    CHECK(parse(manifest) == APP_MANIFEST_OK);
    /* where the requests go is the user's to write, not the application's */
    CHECK(app_manifest_is_server_setting(manifest, "server"));
    CHECK(!app_manifest_is_server_setting(manifest, "token"));
    CHECK(!app_manifest_is_server_setting(manifest, "nobody"));
    /* the setting takes a server and nothing more */
    CHECK(app_manifest_accepts_setting(manifest, "server", "https://ha.example.org", NULL));
    CHECK(app_manifest_accepts_setting(manifest, "server", "http://192.168.1.10:8123", NULL));
    CHECK(app_manifest_accepts_setting(manifest, "server", "", NULL)); /* not set */
    CHECK(!app_manifest_accepts_setting(manifest, "server", "ha.example.org", NULL));
    CHECK(!app_manifest_accepts_setting(manifest, "server", "https://ha.example.org/api", NULL));
    CHECK(!app_manifest_accepts_setting(manifest, "server", "https://me:pw@ha.example.org", NULL));
    CHECK(!app_manifest_accepts_setting(manifest, "server", "ftp://ha.example.org", NULL));
    /* requests of its own go where the user said the server is, and nowhere else */
    server_setting = "http://192.168.1.10:8123";
    CHECK(app_manifest_allows_url(manifest, "http://192.168.1.10:8123/api/services/light/on",
                                  setting_value, NULL));
    CHECK(!app_manifest_allows_url(manifest, "http://192.168.1.10:9000/api", setting_value, NULL));
    CHECK(!app_manifest_allows_url(manifest, "http://192.168.1.11:8123/api", setting_value, NULL));
    server_setting = NULL; /* nothing set: nowhere to go */
    CHECK(!app_manifest_allows_url(manifest, "http://192.168.1.10:8123/api", setting_value, NULL));
    CHECK(app_manifest_setting_type(manifest, "server") == APP_SETTING_URL);
    CHECK(app_manifest_setting_type(manifest, "nothing") == -1);
    char secrets[APP_SETTINGS_MAX][APP_SETTING_KEY_MAX + 1];
    CHECK(app_manifest_secret_keys(manifest, secrets, APP_SETTINGS_MAX) == 1);
    CHECK_STR(secrets[0], "token");

    /* a link is a whole address the user was given, and the whole of a poll's */
    static const char CALENDAR[] =
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,"
        "\"settings\":[{\"key\":\"feed\",\"type\":\"link\",\"title\":\"Calendar\"}],"
        "\"hosts\":[\"{{feed}}\"],\"polls\":[{\"url\":\"{{feed}}\",\"every\":900}]}";
    CHECK(parse(CALENDAR) == APP_MANIFEST_OK);
    CHECK(app_manifest_accepts_setting(CALENDAR, "feed",
                                       "https://calendar.example.org/ical/me/private-1a2b/basic.ics?x=1",
                                       NULL));
    CHECK(!app_manifest_accepts_setting(CALENDAR, "feed", "webcal://calendar.example.org/a.ics", NULL));
    CHECK(!app_manifest_accepts_setting(CALENDAR, "feed", "https://me:pw@example.org/a.ics", NULL));
    CHECK(!app_manifest_accepts_setting(CALENDAR, "feed", "https://example.org/a b.ics", NULL));
    CHECK(app_manifest_is_server_setting(CALENDAR, "feed")); /* the user's to write */
    server_setting = "https://calendar.example.org/ical/me/basic.ics";
    CHECK(app_manifest_allows_url(CALENDAR, "https://calendar.example.org/ical/me/basic.ics",
                                  setting_value, NULL));
    CHECK(!app_manifest_allows_url(CALENDAR, "https://evil.example/ical", setting_value, NULL));
    server_setting = NULL;
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,"
                "\"settings\":[{\"key\":\"feed\",\"type\":\"link\",\"title\":\"C\"}],"
                "\"polls\":[{\"url\":\"{{feed}}/more\",\"every\":60}]}") == APP_MANIFEST_BAD_POLL);

    /* a title is counted in characters: twenty-three of any European alphabet */
    CHECK(parse("{\"name\":\"a\",\"abi\":2,\"title\":\"\xD0\x9A\xD0\xB0\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB4"
                "\xD0\xB0\xD1\x80\xD1\x8C \xD0\xB8 \xD0\xBD\xD0\xB0\xD0\xBF\xD0\xBE\xD0\xBC\xD0\xB8\xD0\xBD\xD0\xB0"
                "\xD0\xBD\xD0\xB8\xD1\x8F\"}") == APP_MANIFEST_OK); /* "Календарь и напоминания": 23 */
    CHECK(parse("{\"name\":\"a\",\"abi\":2,\"title\":\"\xD0\x9A\xD0\xB0\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB4"
                "\xD0\xB0\xD1\x80\xD1\x8C \xD0\xB8 \xD0\xBD\xD0\xB0\xD0\xBF\xD0\xBE\xD0\xBC\xD0\xB8\xD0\xBD\xD0\xB0"
                "\xD0\xBD\xD0\xB8\xD1\x8F!\"}") != APP_MANIFEST_OK); /* 24 */
    CHECK(parse("{\"name\":\"a\",\"abi\":2,\"title\":\"123456789012345678901234\"}") != APP_MANIFEST_OK);
    /* and not in characters of three bytes each: those would not fit */
    CHECK(parse("{\"name\":\"a\",\"abi\":2,\"title\":\"\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD"
                "\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\"}") != APP_MANIFEST_OK);

    /* a poll's pace may be the user's choice in a setting */
    CHECK(app_manifest_pace_seconds("15 min") == 900 && app_manifest_pace_seconds("1 h") == 3600);
    CHECK(app_manifest_pace_seconds("3 hours") == 10800 && app_manifest_pace_seconds("30 s") == 30);
    CHECK(app_manifest_pace_seconds("900") == 900 && app_manifest_pace_seconds("Off") == 0);
    CHECK(app_manifest_pace_seconds("5 parsecs") == 0 && app_manifest_pace_seconds("") == 0);
    {
        static const char PACED[] =
            "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,"
            "\"settings\":[{\"key\":\"every\",\"type\":\"choice\",\"title\":\"Refresh\","
            "\"options\":[\"5 min\",\"1 h\"],\"default\":\"1 h\"}],"
            "\"polls\":[{\"url\":\"https://example.org/x\",\"every\":\"{{every}}\"}]}";
        app_manifest_t paced;
        CHECK(app_manifest_parse(PACED, &paced) == APP_MANIFEST_OK);
        CHECK(paced.poll_every_s[0] == 3600 && strcmp(paced.poll_every_key[0], "every") == 0);
        CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,"
                    "\"polls\":[{\"url\":\"https://example.org/x\",\"every\":\"{{nothing}}\"}]}") ==
              APP_MANIFEST_BAD_POLL);
    }

    /* a poll or a host may begin with a setting only when it is a server's */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,"
                "\"settings\":[{\"key\":\"server\",\"type\":\"text\",\"title\":\"S\"}],"
                "\"polls\":[{\"url\":\"{{server}}/x\",\"every\":60}]}") == APP_MANIFEST_BAD_POLL);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,"
                "\"polls\":[{\"url\":\"{{nobody}}/x\",\"every\":60}]}") == APP_MANIFEST_BAD_POLL);
}

static void test_companion(void)
{
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2,"
                "\"companion\":\"https://example.org/a/setup.html\"}") == APP_MANIFEST_OK);
    /* a browser is to take a secret from it: https, and an address, not a script */
    static const char *const BAD[] = { "\"http://example.org/a\"", "\"https://\"",
                                       "\"javascript:alert(1)\"", "\"https://a b\"", "1" };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        char manifest[160];
        snprintf(manifest, sizeof manifest,
                 "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"companion\":%s}", BAD[i]);
        CHECK(parse(manifest) == APP_MANIFEST_BAD_FIELD);
    }
}

static void test_hosts(void)
{
    const char *manifest = "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"hosts\":[\"api.example.com\",\"192.168.1.10:32400\"]}";
    CHECK(parse(manifest) == APP_MANIFEST_OK);
    CHECK(app_manifest_allows_url(manifest, "https://api.example.com/v1/x?y=1", NULL, NULL));
    CHECK(app_manifest_allows_url(manifest, "http://API.Example.com", NULL, NULL));
    CHECK(app_manifest_allows_url(manifest, "http://192.168.1.10:32400/status", NULL, NULL));
    /* another port is another host; so is a longer or a shorter name */
    CHECK(!app_manifest_allows_url(manifest, "http://192.168.1.10/status", NULL, NULL));
    CHECK(!app_manifest_allows_url(manifest, "https://api.example.com.evil.net/", NULL, NULL));
    CHECK(app_manifest_is_same_host("http://Example.com/a", "https://example.COM/b?c"));
    CHECK(!app_manifest_is_same_host("http://example.com/a", "http://example.com:8080/a"));
    CHECK(!app_manifest_is_same_host("http://example.com/a", "http://example.com.evil.net/a"));
    CHECK(!app_manifest_is_same_host("http://example.com/a", "http://example.com@evil.net/a"));
    CHECK(!app_manifest_is_same_host("http://example.com/a", "ftp://example.com/a"));
    /* a port that is the scheme's own is no other host, written or not */
    CHECK(app_manifest_is_same_host("https://example.com/a", "https://example.com:443/b"));
    CHECK(app_manifest_is_same_host("http://example.com/a", "http://example.com:80/b"));
    CHECK(app_manifest_is_same_host("http://example.com/a", "https://example.com:443/a"));
    CHECK(app_manifest_is_same_host("http://example.com:8080/a", "http://example.com:8080/b"));
    CHECK(app_manifest_allows_url(manifest, "https://api.example.com:443/v1", NULL, NULL));
    CHECK(app_manifest_allows_url(manifest, "http://api.example.com:80/v1", NULL, NULL));
    CHECK(!app_manifest_allows_url(manifest, "http://api.example.com:8080/v1", NULL, NULL));
    CHECK(!app_manifest_allows_url(manifest, "http://192.168.1.10:80/status", NULL, NULL));
    CHECK(!app_manifest_allows_url(manifest, "https://example.com/", NULL, NULL));
    CHECK(!app_manifest_allows_url(manifest, "https://evil.net/api.example.com", NULL, NULL));
    /* what is before an @ is not the host */
    CHECK(!app_manifest_allows_url(manifest, "https://api.example.com@evil.net/", NULL, NULL));
    CHECK(!app_manifest_allows_url(manifest, "ftp://api.example.com/", NULL, NULL));
    CHECK(!app_manifest_allows_url(manifest, "https:///x", NULL, NULL) && !app_manifest_allows_url(manifest, "", NULL, NULL));
    /* no list: nowhere */
    CHECK(!app_manifest_allows_url("{\"name\":\"a\",\"title\":\"T\",\"abi\":2}", "https://api.example.com/",
                                   NULL, NULL));
    static const char *const BAD[] = {
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"hosts\":\"api.example.com\"}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"hosts\":[1]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"hosts\":[\"\"]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":2,\"hosts\":[\"a\",\"b\",\"c\",\"d\",\"e\"]}",
    };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        CHECK(parse(BAD[i]) == APP_MANIFEST_BAD_HOSTS);
    }
}

int main(void)
{
    test_complete();
    test_defaults_and_unknown_fields();
    test_not_json();
    test_name();
    test_title();
    test_other_fields();
    test_abi();
    test_settings();
    test_hosts();
    test_companion();
    test_a_server_of_the_users_own();
    REPORT();
}
