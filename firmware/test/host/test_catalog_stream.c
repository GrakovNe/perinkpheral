/* SPDX-License-Identifier: MIT */
#include "catalog_stream.h"
#include "tinytest.h"

#include <stdlib.h>

#define HASH "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define MODULE "{\"name\":\"app.wasm\",\"size\":123,\"sha256\":\"" HASH "\"}"
#define MANIFEST "{\"name\":\"manifest.json\",\"size\":456,\"sha256\":\"" HASH "\"}"
#define FILE_LIST "\"files\":[" MODULE "," MANIFEST "]"
#define APP_ENTRY "{\"name\":\"clock\",\"version\":\"1.2.3\",\"abi\":3," FILE_LIST "}"
#define INDEX "{\"catalog\":1,\"apps\":[" APP_ENTRY "]}"

static const char *const wanted[] = {"clock", "weather", "absent"};
static catalog_stream_entry_t entries[3];
static catalog_stream_t parser;

static bool parse_chunks(const char *json, size_t length, size_t chunk)
{
    if (!catalog_stream_init(&parser, wanted, 3, entries)) return false;
    for (size_t offset = 0; offset < length; offset += chunk) {
        size_t count = length - offset < chunk ? length - offset : chunk;
        if (!catalog_stream_feed(&parser, json + offset, count)) return false;
    }
    return catalog_stream_finish(&parser);
}

static bool parse(const char *json)
{
    return parse_chunks(json, strlen(json), 1);
}

static void test_chunks(void)
{
    for (size_t split = 0; split <= strlen(INDEX); ++split) {
        CHECK(catalog_stream_init(&parser, wanted, 3, entries));
        CHECK(catalog_stream_feed(&parser, INDEX, split));
        CHECK(catalog_stream_feed(&parser, &INDEX[split], strlen(INDEX) - split));
        CHECK(catalog_stream_finish(&parser));
        CHECK(entries[0].found && entries[0].valid && !entries[1].found && !entries[2].found);
        CHECK_STR(entries[0].name, "clock");
        CHECK_STR(entries[0].version, "1.2.3");
        CHECK(entries[0].abi == 3 && entries[0].file_count == 2 && entries[0].author[0] == 0);
        CHECK_STR(entries[0].files[0].name, "app.wasm");
        CHECK_STR(entries[0].files[1].name, "manifest.json");
        CHECK(entries[0].files[0].size == 123 && entries[0].files[1].size == 456);
        CHECK(entries[0].files[0].sha256[0] == 1 && entries[0].files[0].sha256[31] == 0xef);
    }
    const char *metadata = "{\"apps\":[{\"files\":[" MODULE "," MANIFEST "],"
        "\"trusts\":[\"root.pem\"],\"writes_files\":true,\"system\":false,\"author\":\"Ada \\u0416 \\ud83d\\ude00\","
        "\"firmware\":\"0.1.35\",\"abi\":3,\"version\":\"1.2.3\",\"name\":\"clock\","
        "\"translations\":{\"ru\":{\"title\":\"Часы\",\"description\":\"Time\\nnow\"}},"
        "\"unknown\":[1.25,-1.2e-3,0,2E+4,true,false,null,{}]}],\"catalog\":1}";
    CHECK(parse(metadata));
    CHECK(entries[0].valid && entries[0].has_trusts && entries[0].writes_files && !entries[0].system);
    CHECK_STR(entries[0].author, "Ada Ж 😀");
    CHECK_STR(entries[0].firmware, "0.1.35");
    CHECK(sizeof parser < 2048);
    CHECK(sizeof(catalog_stream_entry_t) * CATALOG_INSTALLED_MAX < 20 * 1024);
}

static void test_syntax(void)
{
    const char *bad[] = {
        "", "{}", "[]", "{\"catalog\":1}", "{\"apps\":[]}", "{\"catalog\":2,\"apps\":[]}",
        "{\"catalog\":1.0,\"apps\":[]}", "{\"catalog\":true,\"apps\":[]}",
        "{\"catalog\":1,\"apps\":{}}", "{\"catalog\":1,\"apps\":[1]}",
        "{\"catalog\":1,\"catalog\":1,\"apps\":[]}", "{\"catalog\":1,\"apps\":[],\"apps\":[]}",
        "{\"catalog\":1,\"apps\":[],}", "{\"catalog\":1,\"apps\":[{},]}",
        "{\"catalog\":1 \"apps\":[]}", "{\"catalog\":1,\"apps\":[]}[]",
        "{\"catalog\":01,\"apps\":[]}", "{\"catalog\":1,\"apps\":[],\"extra\":.1}",
        "{\"catalog\":1,\"apps\":[],\"extra\":1.}", "{\"catalog\":1,\"apps\":[],\"extra\":1e}",
        "{\"catalog\":1,\"apps\":[],\"extra\":1e+}", "{\"catalog\":1,\"apps\":[],\"extra\":-}",
        "{\"catalog\":1,\"apps\":[],\"extra\":tru}", "{\"catalog\":1,\"apps\":[],\"extra\":falsee}",
        "{\"catalog\":1,\"apps\":[],\"extra\":\"\\x\"}", "{\"catalog\":1,\"apps\":[],\"extra\":\"\n\"}",
        "{\"catalog\":1,\"apps\":[],\"extra\":\"\\ud800\"}", "{\"catalog\":1,\"apps\":[],\"extra\":\"\\udc00\"}",
        "{\"catalog\":1,\"apps\":[],\"extra\":\"\\ud800\\u1234\"}",
        "{\"catalog\":1,\"apps\":[],\"extra\":\"\\ud800\\n\"}",
        "{\"catalog\":1,\"apps\":[],\"extra\":\"\xc0\x80\"}",
        "{\"catalog\":1,\"apps\":[],\"extra\":\"\xed\xa0\x80\"}",
        "{\"catalog\":1,\"apps\":[],\"extra\":\"\xf4\x90\x80\x80\"}",
        "{\"catalog\":1,\"apps\":[],\"extra\":\"\xe0\x80\x80\"}",
        "{\"catalog\":1,\"apps\":[],\"extra\":\"\xe2\x80\"}",
    };
    for (size_t index = 0; index < sizeof bad / sizeof bad[0]; ++index) CHECK(!parse(bad[index]));
    for (size_t length = 0; length < strlen(INDEX); ++length) CHECK(!parse_chunks(INDEX, length, 7));
    CHECK(parse(" \t\n{\"catalog\":1,\"apps\":[],\"extra\":\"\\u0000\"}\r "));
    CHECK(!entries[0].found);
    CHECK(!parse("{\"catalog\\u0000x\":1,\"apps\":[]}"));
}

static void test_entry_validation(void)
{
    const char *bad_fields[] = {
        "\"version\":null,\"abi\":3", "\"version\":\"\",\"abi\":3",
        "\"version\":\"1234567890123456\",\"abi\":3", "\"version\":\"1\"",
        "\"version\":\"1\",\"abi\":0", "\"version\":\"1\",\"abi\":-1",
        "\"version\":\"1\",\"abi\":4294967296", "\"version\":\"1\",\"abi\":3.0",
        "\"version\":\"1\",\"abi\":3,\"author\":\"\"", "\"version\":\"1\",\"abi\":3,\"author\":null",
        "\"version\":\"1\",\"abi\":3,\"author\":\" Ada\"", "\"version\":\"1\",\"abi\":3,\"author\":\"Ada\\n\"",
        "\"version\":\"1\",\"abi\":3,\"author\":\"Ada\\u0000\"",
        "\"version\":\"1\",\"abi\":3,\"system\":1", "\"version\":\"1\",\"abi\":3,\"trusts\":[{}]",
        "\"version\":\"1\",\"abi\":3,\"version\":\"2\"", "\"version\":\"1\",\"abi\":3,\"files\":null",
    };
    char json[4096];
    for (size_t index = 0; index < sizeof bad_fields / sizeof bad_fields[0]; ++index) {
        snprintf(json, sizeof json, "{\"catalog\":1,\"apps\":[{\"name\":\"clock\",%s," FILE_LIST "}]}", bad_fields[index]);
        CHECK(parse(json));
        CHECK(entries[0].found && !entries[0].valid);
    }
    CHECK(parse("{\"catalog\":1,\"apps\":[{\"name\":\"clock\",\"name\":\"weather\",\"version\":\"1\",\"abi\":3," FILE_LIST "}]}"));
    CHECK(entries[0].found && !entries[0].valid && entries[1].found && !entries[1].valid);
    CHECK(parse("{\"catalog\":1,\"apps\":[" APP_ENTRY "," APP_ENTRY "]}"));
    CHECK(entries[0].found && !entries[0].valid);
    CHECK(parse("{\"catalog\":1,\"apps\":[{\"name\":\"unknown\"}," APP_ENTRY "]}"));
    CHECK(entries[0].found && entries[0].valid);
    CHECK(parse("{\"catalog\":1,\"apps\":[{\"name\":\"clock\\u0000evil\",\"version\":\"1\",\"abi\":3," FILE_LIST "}]}"));
    CHECK(!entries[0].found);
    const char *bad_files[] = {
        "[]", "[" MODULE "]", "[" MANIFEST "," MODULE "]", "[" MODULE "," MODULE "," MANIFEST "]",
        "[{}," MANIFEST "]", "[1," MANIFEST "]", "[" MODULE ",null]",
        "[{\"name\":\"app.wasm\",\"size\":0,\"sha256\":\"" HASH "\"}," MANIFEST "]",
        "[{\"name\":\"../x\",\"size\":1,\"sha256\":\"" HASH "\"}," MANIFEST "]",
        "[{\"name\":\"app.wasm\",\"size\":1,\"sha256\":\"short\"}," MANIFEST "]",
        "[{\"name\":\"app.wasm\",\"size\":1,\"size\":1,\"sha256\":\"" HASH "\"}," MANIFEST "]",
        "[" MODULE "," MODULE "," MODULE "," MODULE "," MODULE "," MODULE "," MODULE "," MODULE "," MANIFEST "]",
    };
    for (size_t index = 0; index < sizeof bad_files / sizeof bad_files[0]; ++index) {
        snprintf(json, sizeof json, "{\"catalog\":1,\"apps\":[{\"name\":\"clock\",\"version\":\"1\",\"abi\":3,\"files\":%s}]}", bad_files[index]);
        CHECK(parse(json));
        CHECK(entries[0].found && !entries[0].valid);
    }
}

static void test_bounds(void)
{
    char *json = malloc(CATALOG_STREAM_MAX_BYTES + 2);
    CHECK(json != NULL);
    if (!json) return;
    const char *prefix = "{\"catalog\":1,\"apps\":[{\"name\":\"clock\",\"version\":\"1\",\"abi\":3," FILE_LIST ",\"translations\":\"";
    size_t prefix_size = strlen(prefix);
    memcpy(json, prefix, prefix_size);
    memset(json + prefix_size, 'a', CATALOG_STREAM_MAX_BYTES - prefix_size - 4);
    memcpy(json + CATALOG_STREAM_MAX_BYTES - 4, "\"}]}", 4);
    json[CATALOG_STREAM_MAX_BYTES] = '\0';
    CHECK(parse_chunks(json, CATALOG_STREAM_MAX_BYTES, 307));
    CHECK(entries[0].valid && entries[0].found);
    CHECK(!catalog_stream_feed(&parser, " ", 1));
    CHECK(!catalog_stream_finish(&parser));
    free(json);
    char nested[512] = "{\"catalog\":1,\"apps\":[],\"extra\":";
    size_t offset = strlen(nested);
    memset(nested + offset, '[', CATALOG_STREAM_DEPTH_MAX - 1);
    memset(nested + offset + CATALOG_STREAM_DEPTH_MAX - 1, ']', CATALOG_STREAM_DEPTH_MAX - 1);
    strcpy(nested + offset + 2 * (CATALOG_STREAM_DEPTH_MAX - 1), "}");
    CHECK(parse(nested));
    memset(nested + offset, '[', CATALOG_STREAM_DEPTH_MAX);
    memset(nested + offset + CATALOG_STREAM_DEPTH_MAX, ']', CATALOG_STREAM_DEPTH_MAX);
    strcpy(nested + offset + 2 * CATALOG_STREAM_DEPTH_MAX, "}");
    CHECK(!parse(nested));
    const char *duplicate[] = {"clock", "clock"};
    const char *invalid[] = {"../clock"};
    CHECK(!catalog_stream_init(&parser, duplicate, 2, entries));
    CHECK(!catalog_stream_init(&parser, invalid, 1, entries));
    CHECK(!catalog_stream_init(&parser, wanted, CATALOG_INSTALLED_MAX + 1, entries));
    CHECK(!catalog_stream_init(&parser, NULL, 1, entries));
    CHECK(catalog_stream_init(&parser, NULL, 0, NULL));
    CHECK(catalog_stream_feed(&parser, INDEX, strlen(INDEX)) && catalog_stream_finish(&parser));
}

int main(void)
{
    test_chunks();
    test_syntax();
    test_entry_validation();
    test_bounds();
    REPORT();
}
