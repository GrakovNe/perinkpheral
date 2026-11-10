/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_manifest.h"

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
                "\"settings\":[{\"key\":\"token\",\"type\":\"secret\"}]}") == APP_MANIFEST_OK);
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

int main(void)
{
    test_complete();
    test_defaults_and_unknown_fields();
    test_not_json();
    test_name();
    test_title();
    test_other_fields();
    test_abi();
    REPORT();
}
