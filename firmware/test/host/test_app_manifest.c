/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_manifest.h"

#include <string.h>

#include "tinytest.h"

static app_manifest_t manifest;

static app_manifest_result_t parse(const char *json)
{
    return app_manifest_parse(json, &manifest);
}

static void test_complete(void)
{
    CHECK(parse("{\"name\":\"hello\",\"title\":\"Hello\",\"version\":\"0.1.0\","
                "\"abi\":1,\"fullscreen\":true}") == APP_MANIFEST_OK);
    CHECK_STR(manifest.name, "hello");
    CHECK_STR(manifest.title, "Hello");
    CHECK_STR(manifest.version, "0.1.0");
    CHECK(manifest.fullscreen);
}

static void test_defaults_and_unknown_fields(void)
{
    /* no version, no fullscreen, and fields of a later host */
    CHECK(parse("{\"name\":\"plex\",\"title\":\"Now Playing\",\"abi\":1,"
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
    CHECK(parse("{\"title\":\"T\",\"abi\":1}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"\",\"title\":\"T\",\"abi\":1}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":5,\"title\":\"T\",\"abi\":1}") == APP_MANIFEST_BAD_NAME);
    /* it becomes a folder and a settings namespace: no capitals, no paths */
    CHECK(parse("{\"name\":\"Hello\",\"title\":\"T\",\"abi\":1}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"../x\",\"title\":\"T\",\"abi\":1}") == APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"a b\",\"title\":\"T\",\"abi\":1}") == APP_MANIFEST_BAD_NAME);

    CHECK(parse("{\"name\":\"a234567890123\",\"title\":\"T\",\"abi\":1}") == APP_MANIFEST_OK);
    CHECK(parse("{\"name\":\"a2345678901234\",\"title\":\"T\",\"abi\":1}") ==
          APP_MANIFEST_BAD_NAME);
    CHECK(parse("{\"name\":\"my-app_2\",\"title\":\"T\",\"abi\":1}") == APP_MANIFEST_OK);
}

static void test_title(void)
{
    CHECK(parse("{\"name\":\"a\",\"abi\":1}") == APP_MANIFEST_BAD_TITLE);
    CHECK(parse("{\"name\":\"a\",\"title\":\"\",\"abi\":1}") == APP_MANIFEST_BAD_TITLE);
    CHECK(parse("{\"name\":\"a\",\"title\":\"12345678901234567890123\",\"abi\":1}") ==
          APP_MANIFEST_OK);
    CHECK(parse("{\"name\":\"a\",\"title\":\"123456789012345678901234\",\"abi\":1}") ==
          APP_MANIFEST_BAD_TITLE);
}

static void test_other_fields(void)
{
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"fullscreen\":\"yes\"}") ==
          APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"version\":7}") ==
          APP_MANIFEST_BAD_FIELD);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"version\":\"123456789012345\"}") ==
          APP_MANIFEST_OK);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"version\":\"1234567890123456\"}") ==
          APP_MANIFEST_BAD_FIELD);
    /* a zero inside a string would cut it short unnoticed */
    CHECK(parse("{\"name\":\"good\\u0000x\",\"title\":\"T\",\"abi\":1}") != APP_MANIFEST_OK);
}

static void test_abi(void)
{
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\"}") == APP_MANIFEST_OTHER_ABI);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":2}") == APP_MANIFEST_OTHER_ABI);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":\"1\"}") == APP_MANIFEST_OTHER_ABI);
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":1.5}") == APP_MANIFEST_OTHER_ABI);
}

static void test_settings(void)
{
    const char *good = "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"token\",\"type\":\"secret\",\"title\":\"Token\"},{\"key\":\"units\",\"type\":\"choice\",\"title\":\"Units\",\"options\":[\"celsius\",\"fahrenheit\"],\"help\":\"What the numbers are in.\"},{\"key\":\"lat\",\"type\":\"number\",\"title\":\"Latitude\"},{\"key\":\"big\",\"type\":\"toggle\",\"title\":\"Large digits\"},{\"key\":\"city\",\"type\":\"text\",\"title\":\"City\"}]}";
    CHECK(parse(good) == APP_MANIFEST_OK);
    /* no settings at all is fine too */
    CHECK(parse("{\"name\":\"a\",\"title\":\"T\",\"abi\":1}") == APP_MANIFEST_OK);

    static const char *const BAD[] = {
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":\"nope\"}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"type\":\"text\",\"title\":\"No key\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"k\",\"title\":\"No type\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"k\",\"type\":\"text\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"k\",\"type\":\"colour\",\"title\":\"Unknown type\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"has space\",\"type\":\"text\",\"title\":\"T\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"0123456789012345\",\"type\":\"text\",\"title\":\"T\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"k\",\"type\":\"choice\",\"title\":\"T\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"k\",\"type\":\"choice\",\"title\":\"T\",\"options\":[]}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"k\",\"type\":\"choice\",\"title\":\"T\",\"options\":[1]}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"T\",\"options\":[\"a\"]}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"T\",\"help\":7}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"k\",\"type\":\"text\",\"title\":\"A\"},{\"key\":\"k\",\"type\":\"number\",\"title\":\"B\"}]}",
        "{\"name\":\"a\",\"title\":\"T\",\"abi\":1,\"settings\":[{\"key\":\"k0\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k1\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k2\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k3\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k4\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k5\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k6\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k7\",\"type\":\"text\",\"title\":\"T\"},{\"key\":\"k8\",\"type\":\"text\",\"title\":\"T\"}]}",
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
    REPORT();
}
