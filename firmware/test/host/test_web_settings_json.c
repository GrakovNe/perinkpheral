/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>

#include "tinytest.h"
#include "web_settings_json.h"

static int allocation_count;
static int fail_at;
static int live_allocations;

static void *test_malloc(size_t size)
{
    if (++allocation_count == fail_at) {
        return NULL;
    }
    void *memory = malloc(size);
    if (memory != NULL) {
        live_allocations++;
    }
    return memory;
}

static void *test_calloc(size_t count, size_t size)
{
    if (++allocation_count == fail_at) {
        return NULL;
    }
    void *memory = calloc(count, size);
    if (memory != NULL) {
        live_allocations++;
    }
    return memory;
}

static void test_free(void *memory)
{
    if (memory != NULL) {
        live_allocations--;
    }
    free(memory);
}

#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#include "../../components/appstore/json_tree.c"
#include "../../main/web_settings_json.c"
#undef malloc
#undef calloc
#undef free

static void check_complete(const json_t *json, const appconf_t *settings)
{
    int count;
    const appconf_field_t *fields = appconf_fields(&count);
    for (int i = 0; i < count; i++) {
        const appconf_field_t *field = &fields[i];
        const json_t *member = json_get(json, field->key);
        if ((field->flags & APPCONF_SECRET) != 0) {
            CHECK(member == NULL);
        } else if (field->kind == APPCONF_TEXT) {
            CHECK(json_is_string(member) && strcmp(member->text, appconf_text(settings, field)) == 0);
        } else if (field->kind == APPCONF_FLAG) {
            CHECK(json_is_bool(member) && json_is_true(member) == (appconf_number(settings, field) != 0));
        } else {
            CHECK(json_is_number(member) && member->number == appconf_number(settings, field));
        }
    }
    CHECK(strcmp(json_get(json, "hostname")->text, settings->hostname) == 0);
    const appconf_language_t *known = appconf_languages(&count);
    const json_t *languages = json_get(json, "languages");
    CHECK(json_count(languages) == count);
    for (int i = 0; i < count; i++) {
        const json_t *language = json_at(languages, i);
        CHECK(strcmp(json_get(language, "code")->text, known[i].code) == 0);
        CHECK(strcmp(json_get(language, "name")->text, known[i].name) == 0);
    }
    const json_t *order = json_get(json, "menu_order");
    CHECK(json_count(order) == 2);
    CHECK(strcmp(json_at(order, 0)->text, "clock") == 0);
    CHECK(strcmp(json_at(order, 1)->text, "weather") == 0);
}

int main(void)
{
    appconf_t settings;
    appconf_defaults(&settings, "1234");
    strcpy(settings.default_app, "clock");
    char order[] = "clock\nweather";
    json_t *json = web_settings_json(&settings, order);
    CHECK(json != NULL);
    check_complete(json, &settings);
    const int total_allocations = allocation_count;
    json_free(json);
    CHECK(live_allocations == 0);

    /* Fail once, then let memory recover: no incomplete object may escape. */
    for (fail_at = 1; fail_at <= total_allocations; fail_at++) {
        allocation_count = 0;
        strcpy(order, "clock\nweather");
        json = web_settings_json(&settings, order);
        CHECK(json == NULL);
        json_free(json);
        CHECK(live_allocations == 0);
    }
    fail_at = 0;
    order[0] = '\0';
    json = web_settings_json(&settings, order);
    CHECK(json != NULL && json_count(json_get(json, "menu_order")) == 0);
    json_free(json);
    CHECK(live_allocations == 0);
    REPORT();
}
