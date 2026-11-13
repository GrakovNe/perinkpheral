/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "catalog.h"

#include <string.h>

#include "tinytest.h"

static void test_sources(void)
{
    const char *two = "https://raw.example.com/me/apps/HEAD/catalog/\n\nhttps://apps.example.org:8443\n";
    CHECK(catalog_sources_are_valid(two) && catalog_source_count(two) == 2);
    CHECK(catalog_sources_are_valid("") && catalog_source_count("") == 0);
    CHECK(catalog_sources_are_valid(CATALOG_OFFICIAL_SOURCE));
    CHECK(catalog_source_count(CATALOG_OFFICIAL_SOURCE) == 1);

    char source[CATALOG_SOURCE_URL_MAX + 1];
    CHECK(catalog_source_at(two, 0, source, sizeof source));
    CHECK_STR(source, "https://raw.example.com/me/apps/HEAD/catalog"); /* no slash at the end */
    CHECK(catalog_source_at(two, 1, source, sizeof source));
    CHECK_STR(source, "https://apps.example.org:8443");
    CHECK(!catalog_source_at(two, 2, source, sizeof source));
    CHECK(!catalog_source_at(two, -1, source, sizeof source));
    char small[8];
    CHECK(!catalog_source_at(two, 0, small, sizeof small));

    CHECK(!catalog_sources_are_valid("ftp://example.com/x"));
    CHECK(!catalog_sources_are_valid("example.com/catalog"));
    CHECK(!catalog_sources_are_valid("https://"));
    CHECK(!catalog_sources_are_valid("https://exa mple.com/x"));
    CHECK(!catalog_sources_are_valid("https://example.com/\r\nx"));
    CHECK(!catalog_sources_are_valid("https://a\nhttps://b\nhttps://c\nhttps://d\nhttps://e"));
    /* In the clear, the checksums of an index vouch for nothing. */
    CHECK(!catalog_sources_are_valid("http://192.168.1.5:8000"));
    char long_one[CATALOG_SOURCE_URL_MAX + 2] = "https://";
    memset(long_one + 8, 'a', sizeof long_one - 9);
    long_one[sizeof long_one - 1] = '\0';
    CHECK(!catalog_sources_are_valid(long_one));
    long_one[sizeof long_one - 2] = '\0';
    CHECK(catalog_sources_are_valid(long_one));
}

static void test_urls(void)
{
    char url[64];
    CHECK(catalog_url("https://a.example/c", NULL, CATALOG_INDEX_FILE, url, sizeof url));
    CHECK_STR(url, "https://a.example/c/catalog.json");
    CHECK(catalog_url("https://a.example/c", "weather", "app.wasm", url, sizeof url));
    CHECK_STR(url, "https://a.example/c/weather/app.wasm");
    char tight[36];
    CHECK(!catalog_url("https://a.example/c", "weather", "app.wasm", tight, sizeof tight));
    char enough[37];
    CHECK(catalog_url("https://a.example/c", "weather", "app.wasm", enough, sizeof enough));
}

int main(void)
{
    test_sources();
    test_urls();
    REPORT();
}
