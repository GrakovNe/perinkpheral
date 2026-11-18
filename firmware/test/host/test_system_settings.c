/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 * The real Settings app with host services replaced; no device actions occur. */
#define HOST_IMPORT(name, ...) __VA_ARGS__
#define APP_EXPORT(name, ...) __VA_ARGS__
#include "host/base.h"
#undef HOST_SYSTEM_IMPORT
#define HOST_SYSTEM_IMPORT(name, ...) __VA_ARGS__
#include "host_api.h"
#include "tinytest.h"
#include "../../../sdk/lib/app_text.c"
#include "../../../sdk/lib/app_fixed.c"
#include "../../../sdk/lib/app_values.c"
#include "../../../sdk/lib/app_state.c"
#include "../../../sdk/lib/app_days.c"
#define wrapped sdk_wrapped
#include "../../../sdk/lib/app_ui.c"
#undef wrapped
#include "../../system/settings/settings.c"

static int32_t redraw_flags, action_count, last_action, installed_count = 3;
static uint32_t chosen_default;
static char changed_key[64], changed_value[64];
void screen_redraw(int32_t flags) { redraw_flags = flags; }
void screen_leave(void) {}
int32_t header_set_text(int32_t side, const char *text, int32_t length)
{ (void)side; (void)text; return length; }
int32_t sys_run(int32_t action) { action_count++; last_action = action; return HOST_OK; }
int32_t sys_info(int32_t what, char *value, int32_t capacity)
{ (void)what; (void)value; (void)capacity; return 0; }
int32_t sys_setting_get(const char *key, int32_t length, char *value, int32_t capacity)
{ (void)key; (void)length; (void)value; (void)capacity; return 0; }
int32_t sys_setting_range(const char *key, int32_t length, sys_number_t *number)
{ (void)key; (void)length; *number = (sys_number_t){0}; return HOST_OK; }
int32_t sys_setting_set(const char *key, int32_t length, const char *value, int32_t count)
{
    snprintf(changed_key, sizeof changed_key, "%.*s", length, key);
    snprintf(changed_value, sizeof changed_value, "%.*s", count, value);
    return HOST_OK;
}
int32_t sys_app_list(uint32_t *ids, int32_t capacity)
{ for (int32_t i = 0; i < capacity && i < installed_count; i++) ids[i] = i + 1; return installed_count; }
int32_t sys_app_title(uint32_t id, char *text, int32_t capacity)
{ return snprintf(text, capacity, "App %u", id); }
uint32_t sys_app_get_default(void) { return chosen_default; }
int32_t sys_app_set_default(uint32_t id) { chosen_default = id; return HOST_OK; }
int32_t sys_app_remove(uint32_t id) { (void)id; return HOST_OK; }
int32_t sys_time_set(int64_t moment) { (void)moment; return HOST_OK; }
int32_t session_get(void *data, int32_t length) { (void)data; (void)length; return 0; }
int32_t session_set(const void *data, int32_t length) { (void)data; return length; }
int32_t time_local(host_local_time_t *now) { *now = (host_local_time_t){0}; return HOST_OK; }
int32_t time_from_local(const host_local_time_t *parts, int64_t *moment)
{ (void)parts; *moment = 0; return HOST_OK; }
int32_t time_text(int64_t time, int32_t style, char *text, int32_t capacity)
{ (void)time; (void)style; (void)text; (void)capacity; return 0; }
int64_t time_now(void) { return 1794832230; }

int32_t kv_get(const char *key, int32_t length, void *value, int32_t capacity)
{ (void)key; (void)length; (void)value; (void)capacity; return 0; }
int32_t kv_set(const char *key, int32_t length, const void *value, int32_t count)
{ (void)key; (void)length; (void)value; (void)count; return HOST_OK; }
int32_t gfx_font_ascent(int32_t font) { (void)font; return 10; }
int32_t gfx_draw_text(int32_t x, int32_t y, int32_t font, const char *text, int32_t length)
{ (void)x; (void)y; (void)font; (void)text; return length; }
int32_t ui_draw_date(const host_date_t *date) { (void)date; return HOST_OK; }
int32_t ui_draw_notice(const host_notice_t *notice) { (void)notice; return HOST_OK; }
int32_t ui_draw_picker(const host_picker_t *picker) { (void)picker; return HOST_OK; }
int32_t ui_list_first(const host_list_t *list) { return list->first; }
int32_t ui_list_visible(const host_list_t *list) { (void)list; return 5; }
int32_t ui_draw_list_row(const host_list_t *list, int32_t row, const char *label,
                        int32_t label_length, const char *value, int32_t value_length, int32_t icon)
{ (void)list; (void)row; (void)label; (void)label_length; (void)value; (void)value_length; (void)icon; return HOST_OK; }

int32_t answer_count(int32_t index) { (void)index; return 0; }
int32_t answer_value(int32_t index, char *text, int32_t capacity)
{ (void)index; (void)text; (void)capacity; return 0; }
int32_t answer_element(int32_t index, int32_t element, char *text, int32_t capacity)
{ (void)index; (void)element; (void)text; (void)capacity; return 0; }

static void test_sections(void)
{
    app_on_load();
    app_on_button(BTN_DOWN_PRESSED);
    app_on_button(BTN_DOWN_PRESSED);
    app_on_button(BTN_OK_PRESSED);
    CHECK(HERE.page == PAGE_APPEARANCE);
    CHECK(row_count(HERE.page) == 4);
    const char *keys[] = { SYS_KEY_FONT, SYS_KEY_HEADER_CLOCK, NULL, SYS_KEY_MENU_IDLE_MINUTES };
    for (int32_t row = 0; row < 4; row++) {
        HERE.selected = row;
        app_on_button(BTN_OK_PRESSED);
        if (keys[row]) {
            CHECK(HERE.page == PAGE_VALUE);
            CHECK_STR(edited->key, keys[row]);
            app_on_button(BTN_DOWN_PRESSED);
            app_on_button(BTN_OK_PRESSED);
            CHECK_STR(changed_key, keys[row]);
        } else {
            CHECK(HERE.page == PAGE_DEFAULT_APP);
            app_on_button(BTN_DOWN_PRESSED);
            app_on_button(BTN_OK_PRESSED);
            CHECK(chosen_default == 1);
        }
        CHECK(HERE.page == PAGE_APPEARANCE);
    }
    CHECK(row_count(PAGE_GENERAL) == 3);
    CHECK(row_count(PAGE_DISPLAY) == 4);
    for (int32_t row = 0; row < row_count(PAGE_DISPLAY); row++) {
        activate(PAGE_DISPLAY, row);
        CHECK(edited != &VALUE_SETTINGS[VALUE_FONT]);
        CHECK(edited != &VALUE_SETTINGS[VALUE_HEADER_CLOCK]);
        app_on_back();
    }
}

static void test_all_lists_wrap(void)
{
    const page_t lists[] = { PAGE_ROOT, PAGE_GENERAL, PAGE_DATE_TIME, PAGE_ZONE, PAGE_APPS,
                            PAGE_APPEARANCE, PAGE_DISPLAY, PAGE_WIFI, PAGE_DEFAULT_APP };
    for (size_t i = 0; i < sizeof lists / sizeof lists[0]; i++) {
        app_on_load();
        open(lists[i], 0);
        int32_t last = row_count(lists[i]) - 1;
        app_on_button(BTN_UP_PRESSED);
        CHECK(HERE.selected == last);
        app_on_button(BTN_DOWN_PRESSED);
        CHECK(HERE.selected == 0);
        shown_rows = 5;
        app_on_button(BTN_UP_LONG_PRESSED);
        CHECK(HERE.selected == last);
        app_on_button(BTN_DOWN_LONG_PRESSED);
        CHECK(HERE.selected == 0);
    }
    for (size_t i = 0; i < COUNT_OF(VALUE_SETTINGS); i++) {
        app_on_load();
        edit_value(&VALUE_SETTINGS[i]);
        edited_choice.chosen = 0;
        app_on_button(BTN_UP_PRESSED);
        CHECK(edited_choice.chosen == edited_choice.count - 1);
        app_on_button(BTN_DOWN_PRESSED);
        CHECK(edited_choice.chosen == 0);
        app_on_button(BTN_UP_LONG_PRESSED);
        CHECK(edited_choice.chosen == edited_choice.count - 1);
        app_on_button(BTN_DOWN_LONG_PRESSED);
        CHECK(edited_choice.chosen == 0);
    }
    installed_count = 0;
    app_on_load();
    open(PAGE_APPS, 0);
    app_on_button(BTN_UP_PRESSED);
    CHECK(HERE.selected == 0);
    app_on_button(BTN_DOWN_LONG_PRESSED);
    CHECK(HERE.selected == 0);
}

static void test_destructive_actions_require_confirmation(void)
{
    const int32_t actions[] = { SYS_DO_RESTART, SYS_DO_FACTORY_RESET };
    const page_t questions[] = { PAGE_RESTART, PAGE_RESET };
    for (int32_t i = 0; i < 2; i++) {
        app_on_load();
        action_count = 0;
        for (int32_t row = 0; row < 5 + i; row++) app_on_button(BTN_DOWN_PRESSED);
        app_on_button(BTN_OK_PRESSED);
        CHECK(HERE.page == questions[i]);
        CHECK(action_count == 0);
        CHECK((redraw_flags & HOST_REDRAW_QUESTION) != 0);
        app_on_back();
        CHECK(HERE.page == PAGE_ROOT && action_count == 0);
        app_on_button(BTN_OK_PRESSED);
        app_on_button(BTN_OK_PRESSED); /* a fresh confirmation, after the question is shown */
        CHECK(action_count == 1 && last_action == actions[i]);
        CHECK(HERE.page == PAGE_NOTICE && notice_is_final);
        app_on_button(BTN_OK_PRESSED);
        CHECK(action_count == 1);
    }
}

int main(void)
{
    test_sections();
    test_all_lists_wrap();
    test_destructive_actions_require_confirmation();
    REPORT();
}
