/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_update_policy.h"

#include <stdio.h>
#include "tinytest.h"

static const char INSTALLED[] =
    "{\"name\":\"clock\",\"title\":\"Clock\",\"version\":\"0.1.9\",\"abi\":3}";

static bool allows(const char *old_fields, const char *new_fields)
{
    char installed[4096], candidate[4096];
    snprintf(installed, sizeof installed,
             "{\"name\":\"clock\",\"title\":\"Clock\",\"version\":\"0.1.9\",\"abi\":3%s}", old_fields);
    snprintf(candidate, sizeof candidate,
             "{\"name\":\"clock\",\"title\":\"New title\",\"version\":\"0.1.10\",\"abi\":3%s}", new_fields);
    return app_update_policy_allows(installed, candidate, "0.1.10", "0.1.45-review");
}

static void test_versions(void)
{
    CHECK(app_update_version_is_newer("0.1.9", "0.1.10"));
    CHECK(app_update_version_is_newer("0.999.999", "1.0.0"));
    CHECK(app_update_version_is_newer("0.0.0", "999.999.999"));
    const char *refused[] = {"0.1.9", "0.1.8", "0.0.999", "0.1.10-beta", "0.1.10+build",
                            "00.1.10", "0.01.10", "0.1.010", "0.1", "0.1.10.0", "",
                            "v0.1.10", "0.1.1000", "0.1.-1", "0.1.10 ", NULL};
    for (size_t index = 0; index < sizeof refused / sizeof refused[0]; index++) {
        CHECK(!app_update_version_is_newer("0.1.9", refused[index]));
    }
    CHECK(!app_update_version_is_newer("0.1.9-beta", "0.1.10"));
    CHECK(!app_update_version_is_newer(NULL, "0.1.10"));
}

static void test_identity_and_compatibility(void)
{
    CHECK(allows("", ""));
    CHECK(allows("", ",\"author\":\"Perinkpheral\""));
    CHECK(allows("", ",\"firmware\":\"0.1.45\""));
    CHECK(!allows("", ",\"firmware\":\"0.1.46\""));
    const char *candidate[] = {
        "{\"name\":\"other\",\"title\":\"Clock\",\"version\":\"0.1.10\",\"abi\":3}",
        "{\"name\":\"clock\",\"title\":\"Clock\",\"version\":\"0.1.10\",\"abi\":4}",
        "{\"name\":\"clock\",\"title\":\"Clock\",\"version\":\"0.1.11\",\"abi\":3}",
        "{}", "not json", NULL,
    };
    for (size_t index = 0; index < sizeof candidate / sizeof candidate[0]; index++) {
        CHECK(!app_update_policy_allows(INSTALLED, candidate[index], "0.1.10", "0.1.45"));
    }
    CHECK(!app_update_policy_allows(NULL, INSTALLED, "0.1.9", "0.1.45"));
    CHECK(!app_update_policy_allows(INSTALLED, INSTALLED, NULL, "0.1.45"));
    CHECK(!app_update_policy_allows(INSTALLED, INSTALLED, "0.1.9", NULL));
    const char *next = "{\"name\":\"clock\",\"title\":\"Clock\",\"version\":\"0.1.10\",\"abi\":3}";
    CHECK(!app_update_policy_allows(INSTALLED, next, "0.1.10", "unknown"));
    CHECK(!app_update_policy_allows("{}", next, "0.1.10", "0.1.45"));
}

static void test_permissions(void)
{
    CHECK(!allows("", ",\"system\":true"));
    CHECK(!allows("", ",\"writes_files\":true"));
    CHECK(allows(",\"system\":true,\"writes_files\":true", ",\"system\":true,\"writes_files\":true"));
    CHECK(allows(",\"system\":true,\"writes_files\":true", ""));
    CHECK(!allows("", ",\"certificates\":[\"root.pem\"]"));
    CHECK(allows(",\"certificates\":[\"root.pem\",\"other.pem\"]", ",\"certificates\":[\"root.pem\"]"));
    CHECK(!allows(",\"certificates\":[\"root.pem\"]", ",\"certificates\":[\"other.pem\"]"));
    CHECK(allows(",\"certificates\":[\"root.pem\"]", ""));
}

static void test_destinations(void)
{
    CHECK(!allows("", ",\"hosts\":[\"example.com\"]"));
    CHECK(allows(",\"hosts\":[\"example.com\",\"old.example.com\"]", ",\"hosts\":[\"example.com\"]"));
    CHECK(!allows(",\"hosts\":[\"example.com\"]", ",\"hosts\":[\"new.example.com\"]"));
    CHECK(!allows("", ",\"companion\":\"https://example.com/\""));
    CHECK(allows(",\"companion\":\"https://example.com/\"", ",\"companion\":\"https://example.com/\""));
    CHECK(!allows(",\"companion\":\"https://example.com/\"", ",\"companion\":\"https://example.com/new\""));
    CHECK(allows(",\"companion\":\"https://example.com/\"", ""));
    const char *poll = ",\"polls\":[{\"url\":\"https://example.com/data\",\"every\":900}]";
    CHECK(allows(poll, poll));
    CHECK(!allows("", poll));
    CHECK(allows(poll, ",\"polls\":[{\"url\":\"https://example.com/data\",\"every\":600}]"));
    CHECK(!allows(poll, ",\"polls\":[{\"url\":\"https://new.example.com/data\",\"every\":900}]"));
    CHECK(allows(poll, ""));
}

static void test_settings(void)
{
    const char *server = ",\"hosts\":[\"{{server}}\"],\"settings\":[{\"key\":\"server\",\"type\":\"url\",\"title\":\"Server\",\"default\":\"https://example.com\"}]";
    CHECK(allows(server, server));
    CHECK(!allows(server, ",\"hosts\":[\"{{server}}\"],\"settings\":[{\"key\":\"server\",\"type\":\"url\",\"title\":\"Server\",\"default\":\"https://new.example.com\"}]"));
    CHECK(!allows(server, ",\"settings\":[{\"key\":\"server\",\"type\":\"text\",\"title\":\"Server\",\"default\":\"https://example.com\"}]"));
    CHECK(!allows(server, ""));
    CHECK(allows("", ",\"settings\":[{\"key\":\"style\",\"type\":\"text\",\"title\":\"Style\",\"default\":\"round\"}]"));
    const char *poll = ",\"polls\":[{\"url\":\"{{server}}/data\",\"every\":900}],\"settings\":[{\"key\":\"server\",\"type\":\"url\",\"title\":\"Server\",\"default\":\"https://example.com\"}]";
    CHECK(allows(poll, poll));
    CHECK(!allows(poll, ",\"polls\":[{\"url\":\"{{server}}/data\",\"every\":900}],\"settings\":[{\"key\":\"server\",\"type\":\"url\",\"title\":\"Server\",\"default\":\"https://other.example.com\"}]"));
    CHECK(allows(server, ",\"settings\":[{\"title\":\"Translated title\",\"default\":\"https://example.com\",\"type\":\"url\",\"key\":\"server\"}],\"hosts\":[\"{{server}}\"]"));
}

int main(void)
{
    test_versions();
    test_identity_and_compatibility();
    test_permissions();
    test_destinations();
    test_settings();
    REPORT();
}
