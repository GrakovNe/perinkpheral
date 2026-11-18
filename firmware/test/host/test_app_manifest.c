/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_manifest.h"

#include <string.h>
#include <stdio.h>

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
                "\"abi\":3,\"fullscreen\":true}") == APP_MANIFEST_OK);
    CHECK_STR(manifest.name, "hello");
    CHECK_STR(manifest.title, "Hello");
    CHECK_STR(manifest.version, "0.1.0");
    CHECK(manifest.fullscreen);
}

static void test_defaults_and_unknown_fields(void)
{
    /* no version, no fullscreen, and fields of a later host */
    CHECK(parse("{\"name\":\"plex\",\"title\":\"Now Playing\",\"abi\":3,"
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
    CHECK(parse("{\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":5,\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_BAD_NAME);
    /* it becomes a folder and a settings namespace: no capitals, no paths */
    CHECK(parse("{\"name\":\"Hello\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"../x\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"a b\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_BAD_NAME);

    CHECK(parse("{\"name\":\"a234567890123\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_OK);
    CHECK(parse("{\"name\":\"a2345678901234\",\"title\":\"T\",\"abi\":3}") ==
          APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"my-app_2\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_OK);
}

static void test_title(void)
{
    CHECK(parse("{\"name\":\"a\",\"abi\":3}") == APP_MANIFEST_BAD_TITLE);
    CHECK(parse("{\"name\":\"a\",\"title\":\"\",\"abi\":3}") == APP_MANIFEST_BAD_TITLE);
    CHECK(parse("{\"name\":\"a\",\"title\":\"12345678901234567890123\",\"abi\":3}") ==
          APP_MANIFEST_OK);
    CHECK(parse("{\"name\":\"a\",\"title\":\"123456789012345678901234\",\"abi\":3}") ==
          APP_MANIFEST_BAD_TITLE);
}

static void test_other_fields(void)
{
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"fullscreen\":\"yes\"}") ==
          APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"version\":7}") ==
          APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"version\":\"123456789012345\"}") ==
          APP_MANIFEST_OK);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"version\":\"1234567890123456\"}") ==
          APP_MANIFEST_BAD_FIELD);
    /* a zero inside a string would cut it short unnoticed */
    CHECK(parse("{\"name\":\"good\\u0000x\",\"title\":\"T\",\"abi\":3}") != APP_MANIFEST_OK);
}

static void test_author(void)
{
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_OK);
    CHECK_STR(manifest.author, "");
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"author\":\"Иван Иванов\"}") == APP_MANIFEST_OK);
    CHECK_STR(manifest.author, "Иван Иванов");
    const char *invalid[] = { "null", "42", "\"\"", "\" Ada\"", "\"Ada \"", "\"Ada\\nLovelace\"", "\"Ada\\u007f\"" };
    char text[256];
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        snprintf(text, sizeof text, "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"author\":%s}", invalid[i]);
        CHECK(parse(text) == APP_MANIFEST_BAD_FIELD);
    }
    char author[APP_AUTHOR_MAX + 2];
    memset(author, 'a', sizeof author);
    author[APP_AUTHOR_MAX] = '\0';
    snprintf(text, sizeof text, "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"author\":\"%s\"}", author);
    CHECK(parse(text) == APP_MANIFEST_OK);
    author[APP_AUTHOR_MAX] = 'a';
    author[APP_AUTHOR_MAX + 1] = '\0';
    snprintf(text, sizeof text, "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"author\":\"%s\"}", author);
    CHECK(parse(text) == APP_MANIFEST_BAD_FIELD);
    /* The limit counts UTF-8 bytes, not displayed characters. */
    for (size_t i = 0; i < 32; i++) {
        author[i * 2] = (char)0xd0;
        author[i * 2 + 1] = (char)0x90;
    }
    snprintf(text, sizeof text, "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"author\":\"%s\"}", author);
    CHECK(parse(text) == APP_MANIFEST_BAD_FIELD);
}

static void test_abi(void)
{
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_OTHER_ABI);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_OK);
    CHECK(manifest.abi == 3);
    CHECK_STR(app_manifest_result_text(APP_MANIFEST_OTHER_ABI), "Update the app.");
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\"}") == APP_MANIFEST_OTHER_ABI);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":1}") == APP_MANIFEST_OTHER_ABI);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":\"1\"}") == APP_MANIFEST_OTHER_ABI);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3.5}") == APP_MANIFEST_OTHER_ABI);
}

/* The least firmware an application runs on, and versions as numbers. */
static void test_firmware(void)
{
    uint32_t number = 0;
    CHECK(app_version_number("0.1.8", &number) && number == 1008);
    CHECK(app_version_number("0.1.8-0adbfb4", &number) && number == 1008); /* a build's */
    CHECK(app_version_number("1.0.0", &number) && number == 1000000);
    CHECK(app_version_number("0.10.0", &number) && number > 1008);   /* not compared as text */
    static const char *const NOT_ONE[] = { "", "1", "1.2", "1.2.", "a.b.c", "1.2.3.4", "1.2.3x", "1000.0.0", "-1.0.0" };
    for (size_t i = 0; i < sizeof NOT_ONE / sizeof NOT_ONE[0]; i++) {
        CHECK(!app_version_number(NOT_ONE[i], &number));
    }

    app_manifest_t parsed;
    CHECK(app_manifest_parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3}", &parsed) == APP_MANIFEST_OK);
    CHECK(parsed.firmware == 0 && app_manifest_runs_on(&parsed, "0.0.1"));
    CHECK(app_manifest_parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"firmware\":\"0.1.8\"}", &parsed) ==
          APP_MANIFEST_OK);
    CHECK(parsed.firmware == 1008);
    CHECK(app_manifest_runs_on(&parsed, "0.1.8") && app_manifest_runs_on(&parsed, "0.1.8-0adbfb4"));
    CHECK(app_manifest_runs_on(&parsed, "0.2.0") && !app_manifest_runs_on(&parsed, "0.1.7"));
    CHECK(app_manifest_runs_on(&parsed, NULL) && app_manifest_runs_on(&parsed, "unknown"));
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"firmware\":\"new\"}") == APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"firmware\":8}") == APP_MANIFEST_BAD_FIELD);
}

static void test_settings(void)
{
    const char *good = "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"token\",\"type\":\"secret\",\"title\":\"Token\"},{\"key\":\"units\",\"type\":\"choice\",\"title\":\"Units\",\"options\":[\"celsius\",\"fahrenheit\"],\"help\":\"What the numbers are in.\"},{\"key\":\"lat\",\"type\":\"number\",\"title\":\"Latitude\"},{\"key\":\"big\",\"type\":\"toggle\",\"title\":\"Large digits\"},{\"key\":\"city\",\"type\":\"text\",\"title\":\"City\"}]}";
    CHECK(parse(good) == APP_MANIFEST_OK);
    /* no settings at all is fine too */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_OK);

    static const char *const BAD[] = {
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":\"nope\"}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"type\":\"text\",\"title\":\"No key\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k\",\"title\":\"No type\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k\",\"type\":\"text\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k\",\"type\":\"colour\",\"title\":\"Unknown type\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"has space\",\"type\":\"text\",\"title\":\"T\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"0123456789012345\",\"type\":\"text\",\"title\":\"T\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k\",\"type\":\"choice\",\"title\":\"T\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k\",\"type\":\"choice\",\"title\":\"T\",\"options\":[]}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k\",\"type\":\"choice\",\"title\":\"T\",\"options\":[1]}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"T\",\"options\":[\"a\"]}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"T\",\"help\":7}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"A\"},{\"key\":\"k\",\"type\":\"number\",\"title\":\"B\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k0\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k1\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k2\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k3\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k4\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k5\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k6\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k7\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k8\",\"type\":\"text\",\"title\":\"T\"}]}",
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
    const char *with_place = "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"city\",\"type\":\"place\",\"title\":\"City\",\"latitude\":\"lat\",\"longitude\":\"lon\"},{\"key\":\"lat\",\"type\":\"number\",\"title\":\"N\"},{\"key\":\"lon\",\"type\":\"number\",\"title\":\"N\"}]}";
    CHECK(parse(with_place) == APP_MANIFEST_OK);
    CHECK(app_manifest_accepts_setting(with_place, "city", "Berlin, Germany", NULL));
    CHECK(app_manifest_accepts_setting(with_place, "lat", "52.52", NULL));
    CHECK(!app_manifest_accepts_setting(with_place, "lon", "east", NULL));
    static const char *const BAD_PLACES[] = {
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"city\",\"type\":\"place\",\"title\":\"City\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"city\",\"type\":\"place\",\"title\":\"City\",\"latitude\":\"lat\",\"longitude\":\"lon\"},{\"key\":\"lat\",\"type\":\"number\",\"title\":\"N\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"city\",\"type\":\"place\",\"title\":\"City\",\"latitude\":\"lat\",\"longitude\":\"lat\"},{\"key\":\"lat\",\"type\":\"number\",\"title\":\"N\"},{\"key\":\"lon\",\"type\":\"number\",\"title\":\"N\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"city\",\"type\":\"place\",\"title\":\"City\",\"latitude\":\"lat\",\"longitude\":\"lon\"},{\"key\":\"lat\",\"type\":\"number\",\"title\":\"N\"},{\"key\":\"lon\",\"type\":\"text\",\"title\":\"N\"}]}",
    };
    for (size_t i = 0; i < sizeof BAD_PLACES / sizeof BAD_PLACES[0]; i++) {
        CHECK(parse(BAD_PLACES[i]) == APP_MANIFEST_BAD_SETTING);
    }

    /* the news an application asks for, by name; an unknown kind is a mistake */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_OK && manifest.intents == 0);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"intents\":[\"time_changed\",\"network_up\",\"started\"]}") == APP_MANIFEST_OK);
    CHECK(manifest.intents == ((1u << HOST_INTENT_TIME_CHANGED) | (1u << HOST_INTENT_NETWORK_UP) |
                               (1u << HOST_INTENT_STARTED)));
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"intents\":[\"moon_phase\"]}") == APP_MANIFEST_BAD_INTENT);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"intents\":\"time_changed\"}") == APP_MANIFEST_BAD_INTENT);

    /* the system API is asked for in so many words */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_OK && !manifest.system);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"system\":true}") == APP_MANIFEST_OK && manifest.system);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"system\":\"yes\"}") == APP_MANIFEST_BAD_FIELD);

    /* certificate authorities of its own: files of the package, two at most */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3}") == APP_MANIFEST_OK && manifest.certificate_count == 0);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"certificates\":[\"root.pem\"]}") == APP_MANIFEST_OK &&
          manifest.certificate_count == 1);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"certificates\":[\"a.pem\",\"b.der\"]}") == APP_MANIFEST_OK &&
          manifest.certificate_count == 2);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"certificates\":[\"a\",\"b\",\"c\"]}") == APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"certificates\":\"root.pem\"}") == APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"certificates\":[\"../x\"]}") == APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"certificates\":[\".hidden\"]}") == APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"certificates\":[\"app.wasm\"]}") == APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"certificates\":[1]}") == APP_MANIFEST_BAD_FIELD);
    {
        char files[MANIFEST_CERTIFICATES_MAX][MANIFEST_FILE_NAME_MAX + 1];
        CHECK(app_manifest_certificates("{\"certificates\":[\"a.pem\",\"b.der\"]}", files, 2) == 2 &&
              strcmp(files[0], "a.pem") == 0 && strcmp(files[1], "b.der") == 0);
        CHECK(app_manifest_certificates("{\"certificates\":[\"a.pem\",\"b.der\"]}", files, 1) == 1);
        CHECK(app_manifest_certificates("{}", files, 2) == 0);
    }

    /* a default is a value the setting itself would take */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"on\",\"type\":\"toggle\",\"title\":\"T\",\"default\":\"1\"},{\"key\":\"n\",\"type\":\"number\",\"title\":\"T\",\"default\":\"15\"},{\"key\":\"u\",\"type\":\"choice\",\"title\":\"T\",\"default\":\"b\",\"options\":[\"a\",\"b\"]}]}") == APP_MANIFEST_OK);
    static const char *const BAD_DEFAULTS[] = {
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"on\",\"type\":\"toggle\",\"title\":\"T\",\"default\":\"yes\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"on\",\"type\":\"toggle\",\"title\":\"T\",\"default\":1}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"n\",\"type\":\"number\",\"title\":\"T\",\"default\":\"many\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"u\",\"type\":\"choice\",\"title\":\"T\",\"default\":\"c\",\"options\":[\"a\",\"b\"]}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"s\",\"type\":\"secret\",\"title\":\"T\",\"default\":\"x\"}]}",
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
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,"
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
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,"
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
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,"
                "\"settings\":[{\"key\":\"feed\",\"type\":\"link\",\"title\":\"C\"}],"
                "\"polls\":[{\"url\":\"{{feed}}/more\",\"every\":60}]}") == APP_MANIFEST_BAD_POLL);

    /* a title is counted in characters: twenty-three of any European alphabet */
    CHECK(parse("{\"name\":\"a\",\"abi\":3,\"title\":\"\xD0\x9A\xD0\xB0\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB4"
                "\xD0\xB0\xD1\x80\xD1\x8C \xD0\xB8 \xD0\xBD\xD0\xB0\xD0\xBF\xD0\xBE\xD0\xBC\xD0\xB8\xD0\xBD\xD0\xB0"
                "\xD0\xBD\xD0\xB8\xD1\x8F\"}") == APP_MANIFEST_OK); /* "Календарь и напоминания": 23 */
    CHECK(parse("{\"name\":\"a\",\"abi\":3,\"title\":\"\xD0\x9A\xD0\xB0\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB4"
                "\xD0\xB0\xD1\x80\xD1\x8C \xD0\xB8 \xD0\xBD\xD0\xB0\xD0\xBF\xD0\xBE\xD0\xBC\xD0\xB8\xD0\xBD\xD0\xB0"
                "\xD0\xBD\xD0\xB8\xD1\x8F!\"}") != APP_MANIFEST_OK); /* 24 */
    CHECK(parse("{\"name\":\"a\",\"abi\":3,\"title\":\"123456789012345678901234\"}") != APP_MANIFEST_OK);
    /* and not in characters of three bytes each: those would not fit */
    CHECK(parse("{\"name\":\"a\",\"abi\":3,\"title\":\"\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD"
                "\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\"}") != APP_MANIFEST_OK);

    /* a poll's pace may be the user's choice in a setting */
    CHECK(app_manifest_interval_seconds("15 min") == 900 && app_manifest_interval_seconds("1 h") == 3600);
    CHECK(app_manifest_interval_seconds("3 hours") == 10800 && app_manifest_interval_seconds("30 s") == 30);
    CHECK(app_manifest_interval_seconds("900") == 900 && app_manifest_interval_seconds("Off") == 0);
    CHECK(app_manifest_interval_seconds("5 parsecs") == 0 && app_manifest_interval_seconds("") == 0);
    {
        static const char PACED[] =
            "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,"
            "\"settings\":[{\"key\":\"every\",\"type\":\"choice\",\"title\":\"Refresh\","
            "\"options\":[\"5 min\",\"1 h\"],\"default\":\"1 h\"}],"
            "\"polls\":[{\"url\":\"https://example.org/x\",\"every\":\"{{every}}\"}]}";
        app_manifest_t paced;
        CHECK(app_manifest_parse(PACED, &paced) == APP_MANIFEST_OK);
        CHECK(paced.poll_every_s[0] == 3600 && strcmp(paced.poll_every_key[0], "every") == 0);
        CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,"
                    "\"polls\":[{\"url\":\"https://example.org/x\",\"every\":\"{{nothing}}\"}]}") ==
              APP_MANIFEST_BAD_POLL);
    }

    /* a poll or a host may begin with a setting only when it is a server's */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,"
                "\"settings\":[{\"key\":\"server\",\"type\":\"text\",\"title\":\"S\"}],"
                "\"polls\":[{\"url\":\"{{server}}/x\",\"every\":60}]}") == APP_MANIFEST_BAD_POLL);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,"
                "\"polls\":[{\"url\":\"{{nobody}}/x\",\"every\":60}]}") == APP_MANIFEST_BAD_POLL);
}

static void test_companion(void)
{
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,"
                "\"companion\":\"https://example.org/a/setup.html\"}") == APP_MANIFEST_OK);
    /* a browser is to take a secret from it: https, and an address, not a script */
    static const char *const BAD[] = { "\"http://example.org/a\"", "\"https://\"",
                                       "\"javascript:alert(1)\"", "\"https://a b\"", "1" };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        char manifest[160];
        snprintf(manifest, sizeof manifest,
                 "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"companion\":%s}", BAD[i]);
        CHECK(parse(manifest) == APP_MANIFEST_BAD_FIELD);
    }
}

static void test_hosts(void)
{
    const char *manifest = "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"hosts\":[\"api.example.com\",\"192.168.1.10:32400\"]}";
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
    CHECK(!app_manifest_allows_url("{\"name\":\"a\",\"title\":\"T\",\"abi\":3}", "https://api.example.com/",
                                   NULL, NULL));
    static const char *const BAD[] = {
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"hosts\":\"api.example.com\"}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"hosts\":[1]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"hosts\":[\"\"]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"hosts\":[\"a\",\"b\",\"c\",\"d\",\"e\"]}",
    };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        CHECK(parse(BAD[i]) == APP_MANIFEST_BAD_HOSTS);
    }
}

static void test_list_setting(void)
{
    const char *json = "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"t\","
                       "\"type\":\"list\",\"title\":\"Tickers\",\"default\":[\"AAPL\",\"MSFT\"]}]}";
    CHECK(parse(json) == APP_MANIFEST_OK);
    CHECK(app_manifest_setting_type(json, "t") == APP_SETTING_LIST);
    CHECK(app_manifest_accepts_setting(json, "t", "AAPL", NULL));
    CHECK(app_manifest_accepts_setting(json, "t", "AAPL\nBRK.B\nSAP.DE", NULL));
    CHECK(app_manifest_accepts_setting(json, "t", "", NULL)); /* not set */
    CHECK(!app_manifest_accepts_setting(json, "t", "AAPL\n\nMSFT", NULL)); /* an empty item */
    CHECK(!app_manifest_accepts_setting(json, "t", "AAPL\n", NULL));
    CHECK(!app_manifest_accepts_setting(json, "t", "AA\tPL", NULL));
    /* as many items as the value holds */
    CHECK(app_manifest_accepts_setting(json, "t", "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL\nM\nN\nO\nP\nQ\nR",
                                       NULL));
    /* a line break is a list's alone */
    const char *text = "{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"k\","
                       "\"type\":\"text\",\"title\":\"K\"}]}";
    CHECK(!app_manifest_accepts_setting(text, "k", "a\nb", NULL));
    /* a list's default is a list */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"t\",\"type\":"
                "\"list\",\"title\":\"T\",\"default\":\"AAPL\"}]}") == APP_MANIFEST_BAD_SETTING);
    /* a default as it would be stored: what a setting with nothing stored reads as */
    char preset[16];
    CHECK(app_manifest_setting_default(json, "t", preset, sizeof preset) == 9);
    CHECK_STR(preset, "AAPL\nMSFT");
    CHECK(app_manifest_setting_default(json, "t", preset, 9) == -2);
    CHECK(app_manifest_setting_default(json, "none", preset, sizeof preset) == -1);
    CHECK(app_manifest_setting_default(text, "k", preset, sizeof preset) == -1); /* no default */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"t\",\"type\":"
                "\"list\",\"title\":\"T\",\"default\":[\"\"]}]}") == APP_MANIFEST_BAD_SETTING);
}

static bool default_setting(void *context, const char *key, char *value, size_t size)
{
    return app_manifest_setting_default(context, key, value, (int)size) >= 0;
}

static bool empty_setting(void *context, const char *key, char *value, size_t size)
{
    (void)context; (void)key;
    if (size == 0) { return false; }
    value[0] = '\0';
    return true;
}

static void test_four_default_links(void)
{
    static const char STATUS[] =
        "{\"name\":\"status\",\"title\":\"Service status\",\"abi\":3,\"settings\":["
        "{\"key\":\"page1\",\"type\":\"link\",\"title\":\"Page 1\",\"default\":\"https://www.githubstatus.com/api/v2/status.json\"},"
        "{\"key\":\"page2\",\"type\":\"link\",\"title\":\"Page 2\",\"default\":\"https://www.cloudflarestatus.com/api/v2/status.json\"},"
        "{\"key\":\"page3\",\"type\":\"link\",\"title\":\"Page 3\",\"default\":\"https://discordstatus.com/api/v2/status.json\"},"
        "{\"key\":\"page4\",\"type\":\"link\",\"title\":\"Page 4\",\"default\":\"https://status.openai.com/api/v2/status.json\"}],"
        "\"hosts\":[\"{{page1}}\",\"{{page2}}\",\"{{page3}}\",\"{{page4}}\"],\"polls\":["
        "{\"url\":\"{{page1}}\",\"every\":300},{\"url\":\"{{page2}}\",\"every\":300},"
        "{\"url\":\"{{page3}}\",\"every\":300},{\"url\":\"{{page4}}\",\"every\":300}]}";
    CHECK(parse(STATUS) == APP_MANIFEST_OK);
    CHECK(manifest.poll_count == 4);
    for (int index = 0; index < 4; index++) {
        char key[16], url[256], reference[20];
        snprintf(key, sizeof key, "page%d", index + 1);
        snprintf(reference, sizeof reference, "{{%s}}", key);
        CHECK(app_manifest_setting_default(STATUS, key, url, sizeof url) > 0);
        CHECK(app_manifest_accepts_setting(STATUS, key, url, NULL));
        CHECK(app_manifest_allows_url(STATUS, url, default_setting, (void *)STATUS));
        CHECK(!app_manifest_allows_url(STATUS, url, empty_setting, NULL));
        app_poll_t poll;
        CHECK(app_manifest_parse_poll(STATUS, index, &poll) == APP_MANIFEST_OK);
        CHECK_STR(poll.url, reference);
    }
    CHECK(!app_manifest_allows_url(STATUS, "https://evil.example/", default_setting, (void *)STATUS));
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":3,\"settings\":[{\"key\":\"page\","
                "\"type\":\"link\",\"title\":\"Page\",\"default\":\"https://user:pass@example.com/a\"}]}") == APP_MANIFEST_BAD_SETTING);
}

int main(void)
{
    test_four_default_links();
    test_list_setting();
    test_complete();
    test_defaults_and_unknown_fields();
    test_not_json();
    test_name();
    test_title();
    test_other_fields();
    test_author();
    test_abi();
    test_firmware();
    test_settings();
    test_hosts();
    test_companion();
    test_a_server_of_the_users_own();
    REPORT();
}
