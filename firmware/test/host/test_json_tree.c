/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "json_tree.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tinytest.h"

static bool prints_as(const char *text, const char *expected)
{
    json_t *tree = json_parse(text);
    char   *printed = json_print(tree);
    bool    is_same = printed != NULL && strcmp(printed, expected) == 0;
    if (!is_same) {
        printf("  %s -> %s, expected %s\n", text, printed != NULL ? printed : "(none)", expected);
    }
    free(printed);
    json_free(tree);
    return is_same;
}

static bool is_refused(const char *text)
{
    json_t *tree = json_parse(text);
    json_free(tree);
    return tree == NULL;
}

static void test_values(void)
{
    CHECK(prints_as(" { \"a\" : 1 , \"b\" : [ true , false , null ] , \"c\" : \"x\" } ",
                    "{\"a\":1,\"b\":[true,false,null],\"c\":\"x\"}"));
    CHECK(prints_as("[]", "[]") && prints_as("{}", "{}") && prints_as("\"\"", "\"\""));
    CHECK(prints_as("-0", "0") && prints_as("-12", "-12") &&
          prints_as("9223372036854775807", "9223372036854775807"));
    CHECK(prints_as("15.75", "15") && prints_as("-3.0", "-3"));
    /* escapes in, the plain text out; what must be escaped is, on the way back */
    CHECK(prints_as("\"a\\n\\t\\\"\\\\\\/b\"", "\"a\\u000a\\u0009\\\"\\\\/b\""));
    CHECK(prints_as("\"\\u0416\\u00e9\"", "\"\xD0\x96\xC3\xA9\""));
    CHECK(prints_as("\"\\ud83d\\ude00\"", "\"\xF0\x9F\x98\x80\""));
    CHECK(prints_as("\"\xD0\x96\"", "\"\xD0\x96\""));
}

static void test_refusals(void)
{
    static const char *const BAD[] = {
        "", " ", "{", "}", "[", "]", "[1,]", "[,1]", "{\"a\"}", "{\"a\":}", "{a:1}", "{\"a\":1,}",
        "{\"a\":1 \"b\":2}", "1 2", "[1] x", "tru", "nul", "01", "-", "1.", ".5", "1e3", "1E-2",
        "\"abc", "\"a\\x\"", "\"a\\u12\"", "\"a\\ud83d\"", "\"a\\ude00\"", "\"a\\u0000\"",
        "\"line\nbreak\"", "99999999999999999999", "'a'", "[1 2]", "{\"a\":1}}", "\"\\",
    };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        if (!is_refused(BAD[i])) {
            printf("  taken: %s\n", BAD[i]);
        }
        CHECK(is_refused(BAD[i]));
    }
    CHECK(is_refused(NULL));

    /* nesting to the limit and one deeper */
    char deep[2 * (JSON_DEPTH_MAX + 1) + 1];
    for (int depth = JSON_DEPTH_MAX; depth <= JSON_DEPTH_MAX + 1; depth++) {
        memset(deep, '[', (size_t)depth);
        memset(deep + depth, ']', (size_t)depth);
        deep[2 * depth] = '\0';
        CHECK(is_refused(deep) == (depth > JSON_DEPTH_MAX));
    }
}

static void test_reading(void)
{
    json_t *tree = json_parse("{\"name\":\"clock\",\"abi\":2,\"half\":2.5,\"on\":true,"
                              "\"rows\":[\"a\",\"b\",\"c\"],\"in\":{\"k\":\"v\"}}");
    CHECK(tree != NULL && json_is_object(tree) && json_count(tree) == 6);
    CHECK(json_is_string(json_get(tree, "name")) && strcmp(json_get(tree, "name")->text, "clock") == 0);
    CHECK(json_is_number(json_get(tree, "abi")) && json_get(tree, "abi")->number == 2 &&
          json_get(tree, "abi")->is_whole);
    CHECK(json_get(tree, "half")->number == 2 && !json_get(tree, "half")->is_whole);
    CHECK(json_is_bool(json_get(tree, "on")) && json_is_true(json_get(tree, "on")));
    CHECK(json_get(tree, "none") == NULL && json_get(NULL, "name") == NULL &&
          json_get(json_get(tree, "rows"), "name") == NULL);
    const json_t *rows = json_get(tree, "rows");
    CHECK(json_is_array(rows) && json_count(rows) == 3 && strcmp(json_at(rows, 2)->text, "c") == 0 &&
          json_at(rows, 3) == NULL && json_at(rows, -1) == NULL);
    int           seen = 0;
    const json_t *row;
    json_for_each(row, rows)
    {
        seen++;
    }
    json_for_each(row, (const json_t *)NULL)
    {
        seen += 100;
    }
    CHECK(seen == 3);
    CHECK(strcmp(json_get(json_get(tree, "in"), "k")->name, "k") == 0);
    CHECK(json_count(json_get(tree, "name")) == 0 && json_count(NULL) == 0);
    CHECK(!json_is_string(NULL) && !json_is_number(NULL) && !json_is_array(NULL) &&
          !json_is_object(NULL) && !json_is_bool(NULL) && !json_is_true(NULL));
    json_free(tree);
    json_free(NULL);
}

static void test_building(void)
{
    json_t *root = json_new_object();
    json_add_string(root, "name", "a \"quoted\" one");
    json_add_number(root, "free", 123456);
    json_add_number(root, "below", -7);
    json_add_bool(root, "ok", true);
    json_t *list = json_new_array();
    json_array_add(list, json_new_string("x"));
    json_array_add(list, json_new_string("y"));
    json_add_item(root, "list", list);
    char *text = json_print(root);
    CHECK(text != NULL && strcmp(text, "{\"name\":\"a \\\"quoted\\\" one\",\"free\":123456,"
                                       "\"below\":-7,\"ok\":true,\"list\":[\"x\",\"y\"]}") == 0);
    free(text);

    /* a member taken out belongs to whoever took it, and may go elsewhere */
    json_t *moved = json_detach(root, "list");
    CHECK(moved == list && json_get(root, "list") == NULL && json_detach(root, "list") == NULL);
    json_t *other = json_new_object();
    json_add_item(other, "rows", moved);
    json_remove(root, "free");
    json_remove(root, "none");
    text = json_print(root);
    CHECK(text != NULL && strcmp(text, "{\"name\":\"a \\\"quoted\\\" one\",\"below\":-7,\"ok\":true}") == 0);
    free(text);
    text = json_print(other);
    CHECK(text != NULL && strcmp(text, "{\"rows\":[\"x\",\"y\"]}") == 0);
    free(text);

    /* arguments that are not there take nothing and leak nothing */
    CHECK(json_add_string(NULL, "a", "b") == NULL && json_add_string(root, NULL, "b") == NULL &&
          json_add_string(root, "a", NULL) == NULL && json_add_item(root, "a", NULL) == NULL);
    json_array_add(root, json_new_string("not an array"));
    json_array_add(NULL, json_new_string("nowhere"));
    CHECK(json_print(NULL) == NULL && json_new_string(NULL) == NULL);
    json_free(other);
    json_free(root);
}

/* Whatever the text, the parser answers and what it took prints and parses again. */
static void test_any_text(void)
{
    static const char ALPHABET[] = "{}[]\",:\\ -0123456789.eEtrufalsn\n\t\x01\xC3\xA9u00dD8";
    uint32_t          seed = 12345;
    int               taken = 0;
    for (int round = 0; round < 60000; round++) {
        char text[40];
        seed = seed * 1664525u + 1013904223u;
        int length = (int)((seed >> 16) % (sizeof text - 1));
        for (int i = 0; i < length; i++) {
            seed = seed * 1664525u + 1013904223u;
            text[i] = ALPHABET[(seed >> 16) % (sizeof ALPHABET - 1)];
        }
        text[length] = '\0';
        json_t *tree = json_parse(text);
        if (tree != NULL) {
            taken++;
            char   *printed = json_print(tree);
            json_t *again = json_parse(printed);
            char   *reprinted = json_print(again);
            CHECK(printed != NULL && again != NULL && reprinted != NULL &&
                  strcmp(printed, reprinted) == 0);
            free(reprinted);
            json_free(again);
            free(printed);
        }
        json_free(tree);
    }
    CHECK(taken > 100);
}

int main(void)
{
    test_values();
    test_refusals();
    test_reading();
    test_building();
    test_any_text();
    REPORT();
}
