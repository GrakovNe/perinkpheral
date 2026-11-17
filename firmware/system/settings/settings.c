/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Settings on the device itself, with four buttons and no keyboard: pages
 * of rows, and under some of them pages of their own. Nothing is typed. A
 * value is one of a few, stepped through with UP and DOWN; a date is its
 * parts, each stepped the same way, OK going to the next and BACK to the
 * one before. What needs text - a network's password, a name - is set from
 * the device's web page.
 *
 * A system application: everything it shows and changes belongs to the
 * host, and goes through the system API.
 */
#include <stdbool.h>
#include <stddef.h>

#include "app_state.h"
#include "app_text.h"
#include "app_ui.h"
#include "cities.h"
#include "host_api.h"
#include "strings.h" /* made of the `strings` folder by tools/perinkpheral */

/* ---- where the user is ---- */

typedef enum {
    PAGE_ROOT,
    PAGE_GENERAL,
    PAGE_DATE_TIME,
    PAGE_CLOCK,      /* the date and the time, part by part */
    PAGE_ZONE,
    PAGE_APPS,
    PAGE_REMOVE_APP, /* "are you sure" */
    PAGE_DISPLAY,
    PAGE_VALUE,      /* one of a few values of a display setting */
    PAGE_WIFI,
    PAGE_FORGET,     /* "are you sure" */
    PAGE_RESTART,    /* "are you sure" */
    PAGE_RESET,      /* "are you sure" */
    PAGE_DEFAULT_APP, /* None and the installed applications */
    PAGE_BUTTONS,    /* what holding a button does: nothing on the screen says it */
    PAGE_NOTICE,     /* what came of it: "Restarting." */
} page_t;

/* The pages from the first one down to the one on the screen, each with
 * the row the user was on. */
#define DEPTH_MAX 5
typedef struct {
    page_t  page;
    int32_t selected;
    int32_t first; /* the top row of its list, where it stood */
} step_t;
static step_t  path[DEPTH_MAX];
static int32_t depth;

#define HERE (path[depth])

/* How many rows the list on the screen showed: a long UP or DOWN moves that many. */
static int32_t shown_rows = 1;

/* ---- what the pages keep between presses ---- */

#define TEXT_MAX 47

static uint32_t app_to_remove;
static char    app_to_remove_title[TEXT_MAX + 1];

static const char *notice;
static bool        notice_is_final; /* the device is restarting: nowhere to go back to */

static app_date_t clock; /* on PAGE_CLOCK */
#define YEAR_FIRST 2026
#define YEAR_LAST  2099

/* A display setting and the values the buttons step through: any value can
 * be set from the web page, these are the ones worth having here. */
/* How a value is put into words. */
typedef enum {
    SAY_FONT,
    SAY_FULL_EVERY,
    SAY_SECONDS,
    SAY_HOURS,
    SAY_ON_OFF,
    SAY_POWER_SAVING,
    SAY_MINUTES,
} saying_t;

typedef struct {
    int32_t        label; /* a STR_* */
    const char    *key;   /* a SYS_KEY_* */
    saying_t       saying;
    const int32_t *choices;
    int32_t        choice_count;
} display_setting_t;

static const int32_t FULL_EVERY_CHOICES[] = { 0, 1, 2, 3, 5, 10, 20, 50, SYS_REFRESH_FULL_NEVER };
static const int32_t MIN_SECONDS_CHOICES[] = { 10, 20, 30, 60, 120, 180, 300, 600, 900, 1800, 3600 };
static const int32_t FORCE_HOURS_CHOICES[] = { 1, 2, 3, 6, 12, 24, 48, 72, 168 };
static const int32_t OFF_ON_CHOICES[] = { 0, 1 };
#define COUNT_OF(array) ((int32_t)(sizeof(array) / sizeof(array)[0]))

/* The typeface is one of them: Departure Mono or Jersey. */
static const int32_t FONT_CHOICES[] = { 0, 1 };
static const char *const FONT_NAMES[] = { "Departure Mono", "Jersey" };

/* By the setting's value: never, always, and whenever no computer is on the cable. */
static const int32_t POWER_SAVING_CHOICES[] = { 0, 1, 2 };
static const int32_t IDLE_MINUTES_CHOICES[] = { 1, 5, 15, 30, 60 };

/* Every value chosen from a few on a page of its own: the display's, then two of General's. */
static const display_setting_t VALUE_SETTINGS[] = {
    { STR_FONT, SYS_KEY_FONT, SAY_FONT, FONT_CHOICES, COUNT_OF(FONT_CHOICES) },
    { STR_FULL_REFRESH, SYS_KEY_REFRESH_FULL_EVERY, SAY_FULL_EVERY, FULL_EVERY_CHOICES,
      COUNT_OF(FULL_EVERY_CHOICES) },
    { STR_REFRESH_INTERVAL, SYS_KEY_REFRESH_MIN_SECONDS, SAY_SECONDS, MIN_SECONDS_CHOICES,
      COUNT_OF(MIN_SECONDS_CHOICES) },
    { STR_FORCED_REFRESH, SYS_KEY_REFRESH_FORCE_HOURS, SAY_HOURS, FORCE_HOURS_CHOICES,
      COUNT_OF(FORCE_HOURS_CHOICES) },
    { STR_FAST_REFRESH, SYS_KEY_REFRESH_FAST, SAY_ON_OFF, OFF_ON_CHOICES, COUNT_OF(OFF_ON_CHOICES) },
    { STR_CLOCK_IN_HEADER, SYS_KEY_HEADER_CLOCK, SAY_ON_OFF, OFF_ON_CHOICES,
      COUNT_OF(OFF_ON_CHOICES) },
    { STR_POWER_SAVING, SYS_KEY_POWER_SAVING, SAY_POWER_SAVING, POWER_SAVING_CHOICES,
      COUNT_OF(POWER_SAVING_CHOICES) },
    { STR_IDLE_TIMEOUT, SYS_KEY_MENU_IDLE_MINUTES, SAY_MINUTES, IDLE_MINUTES_CHOICES,
      COUNT_OF(IDLE_MINUTES_CHOICES) },
};
#define DISPLAY_ROWS       6 /* the display's, at the top of the table */
#define VALUE_POWER_SAVING 6
#define VALUE_IDLE_MINUTES 7

#define ROOT_ROWS 7

/* By the setting's value: never, always, and whenever no computer is on the cable. */
#define POWER_SAVINGS 3
static const int32_t POWER_SAVING_NAMES[POWER_SAVINGS] = { STR_DISABLED, STR_ALWAYS, STR_AUTO };


static const display_setting_t *edited; /* on PAGE_VALUE */
static app_picker_t             edited_choice; /* which of its choices is in the frame */

/* A page of settings is redrawn at every press, and a full refresh in the
 * middle of stepping through a list is a second of a black screen: none
 * of the host's making while this application is on the screen. */
#define REDRAW HOST_REDRAW_NO_BLINK

/* ---- small things ---- */

static bool is_same(const char *text, const char *other)
{
    int32_t at = 0;
    while (text[at] != '\0' && text[at] == other[at]) {
        at++;
    }
    return text[at] == other[at];
}

static void copy_text(char *to, int32_t to_size, const char *from)
{
    app_text_t text;
    app_text_start(&text, to, to_size);
    app_text_add(&text, from);
}

/* What the host knows under `what`; empty when it is not known. */
static void read_info(int32_t what, char value[TEXT_MAX + 1])
{
    int32_t length = sys_info(what, value, TEXT_MAX);
    value[length > 0 ? length : 0] = '\0';
}

/* The host's settings are a store by key, the device page's as much as
 * this application's: it reads and writes them as text. */
static void read_setting(const char *key, char value[TEXT_MAX + 1])
{
    int32_t length = sys_setting_get(key, app_text_length(key), value, TEXT_MAX);
    value[length > 0 ? length : 0] = '\0';
}

static int32_t read_number(const char *key)
{
    sys_number_t number = { 0 };
    sys_setting_range(key, app_text_length(key), &number);
    return number.value;
}

static int32_t write_setting(const char *key, const char *value)
{
    return sys_setting_set(key, app_text_length(key), value, app_text_length(value));
}

static int32_t write_number(const char *key, int32_t value)
{
    char       text[TEXT_MAX + 1];
    app_text_t out;
    app_text_start(&out, text, sizeof text);
    app_text_add_number(&out, value);
    return write_setting(key, text);
}

static int32_t wrapped(int32_t value, int32_t first, int32_t last)
{
    return value > last ? first : value < first ? last : value;
}

/* ---- the time zone ---- */

/* The cities by the names the user reads: in Russian when the device speaks it, in English
 * otherwise. The list is in the order of its English names; city_order[] is the order of the
 * names shown, made once the language is known. */
static bool    is_russian;
static uint8_t city_order[CITY_COUNT];

static const char *city_name(int32_t city)
{
    return is_russian ? CITIES[city].ru : CITIES[city].name;
}

/* Before, by their bytes: UTF-8 keeps the order of the Cyrillic alphabet, but for Ё. */
static bool is_before(const char *text, const char *other)
{
    int32_t at = 0;
    while (text[at] != '\0' && text[at] == other[at]) {
        at++;
    }
    return (uint8_t)text[at] < (uint8_t)other[at];
}

static void order_cities(void)
{
    char language[TEXT_MAX + 1];
    read_setting(SYS_KEY_LANGUAGE, language);
    is_russian = is_same(language, "ru");
    for (int32_t at = 0; at < CITY_COUNT; at++) {
        int32_t city = at;
        /* by insertion: seventy-odd names, once */
        while (city > 0 && is_before(city_name(at), city_name(city_order[city - 1]))) {
            city_order[city] = city_order[city - 1];
            city--;
        }
        city_order[city] = (uint8_t)at;
    }
}

/* Where a city stands in city_order[]. */
static int32_t position_of(int32_t city)
{
    for (int32_t at = 0; at < CITY_COUNT; at++) {
        if (city_order[at] == city) {
            return at;
        }
    }
    return 0;
}

/* The city the zone was chosen by - on the device or on its page; a zone several cities share
 * is shown by the one chosen, and one set otherwise by the first that has it. -1 when no city
 * here has the zone. */
static int32_t current_city(void)
{
    char zone[TEXT_MAX + 1];
    char picked[TEXT_MAX + 1];
    read_setting(SYS_KEY_TIME_ZONE, zone);
    read_setting(SYS_KEY_TIME_ZONE_CITY, picked);
    int32_t first_with_zone = -1;
    for (int32_t city = 0; city < CITY_COUNT; city++) {
        if (!is_same(CITIES[city].zone, zone)) {
            continue;
        }
        if (is_same(CITIES[city].name, picked)) {
            return city;
        }
        if (first_with_zone < 0) {
            first_with_zone = city;
        }
    }
    return first_with_zone;
}

/* ---- the clock ---- */

#define SECONDS_PER_MINUTE 60

static bool clock_save(void)
{
    /* The host turns the local time into the moment, its zone and summer time with it: the
     * clock is set once, to what was meant. */
    host_local_time_t parts = { .year = clock.year, .month = clock.month, .day = clock.day,
                                .hour = clock.hour, .minute = clock.minute };
    int64_t moment;
    return time_from_local(&parts, &moment) == HOST_OK && moment > 0 &&
           sys_time_set(moment) == HOST_OK;
}

/* ---- the rows of a page ---- */

/* The installed applications, as the host lists them now. */
#define APPS_MAX SYS_APP_LIST_MAX
static uint32_t app_ids[APPS_MAX];

static int32_t app_count(void)
{
    int32_t count = sys_app_list(app_ids, APPS_MAX);
    return count < 0 ? 0 : count > APPS_MAX ? APPS_MAX : count;
}

/* The title of the application on the row, terminated; false past the last. */
static bool app_title_at(int32_t row, char title[TEXT_MAX + 1])
{
    int32_t length = row < app_count() ? sys_app_title(app_ids[row], title, TEXT_MAX) : -1;
    title[length > 0 ? length : 0] = '\0';
    return length >= 0;
}

/* The row of the default application among the installed ones; -1 for none. */
static int32_t default_app_row(void)
{
    uint32_t chosen = sys_app_default();
    int32_t  count = app_count();
    for (int32_t row = 0; chosen != 0 && row < count; row++) {
        if (app_ids[row] == chosen) {
            return row;
        }
    }
    return -1;
}

static void format_choice(const display_setting_t *setting, int32_t value, char text[TEXT_MAX + 1])
{
    app_text_t out;
    app_text_start(&out, text, TEXT_MAX + 1);
    switch (setting->saying) {
    case SAY_FULL_EVERY:
        if (value == 0) {
            app_text_add(&out, app_string(STR_ALWAYS));
        } else if (value == SYS_REFRESH_FULL_NEVER) {
            app_text_add(&out, app_string(STR_NEVER));
        } else {
            app_text_add(&out, app_string(STR_EVERY));
            app_text_add_number(&out, value);
        }
        break;
    case SAY_ON_OFF:
        app_text_add(&out, app_string(value != 0 ? STR_ON : STR_OFF));
        break;
    case SAY_FONT:
        app_text_add(&out, FONT_NAMES[value % 2]);
        break;
    case SAY_SECONDS:
        if (value >= SECONDS_PER_MINUTE && value % SECONDS_PER_MINUTE == 0) {
            app_text_add_number(&out, value / SECONDS_PER_MINUTE);
            app_text_add(&out, app_string(STR_UNIT_MIN));
        } else {
            app_text_add_number(&out, value);
            app_text_add(&out, app_string(STR_UNIT_S));
        }
        break;
    case SAY_HOURS:
        app_text_add_number(&out, value);
        app_text_add(&out, app_string(STR_UNIT_H));
        break;
    case SAY_POWER_SAVING:
        app_text_add(&out, app_string(POWER_SAVING_NAMES[(uint32_t)value % POWER_SAVINGS]));
        break;
    case SAY_MINUTES:
        app_text_add_number(&out, value);
        app_text_add(&out, app_string(STR_UNIT_MIN));
        break;
    }
}

static bool is_automatic(void)
{
    return read_number(SYS_KEY_TIME_AUTOMATIC) != 0;
}

static bool has_network(void)
{
    char network[TEXT_MAX + 1];
    read_info(SYS_INFO_NETWORK, network);
    return network[0] != '\0';
}

static int32_t row_count(page_t page)
{
    switch (page) {
    case PAGE_ROOT:
        return ROOT_ROWS;
    case PAGE_GENERAL:
        return 5;
    case PAGE_DATE_TIME:
        return is_automatic() ? 1 : 2; /* by hand only when not from the network */
    case PAGE_ZONE:
        return CITY_COUNT;
    case PAGE_APPS: {
        int32_t apps = app_count();
        return apps > 0 ? apps : 1; /* or a row that says there are none */
    }
    case PAGE_DISPLAY:
        return DISPLAY_ROWS;
    case PAGE_DEFAULT_APP:
        return 1 + app_count();
    case PAGE_BUTTONS:
        return 4;
    case PAGE_WIFI:
        return has_network() ? 4 : 3; /* nothing to forget without a network */
    default:
        return 0;
    }
}

/* The label of a row and the value shown against the right edge; either
 * may be empty. */
static void describe_row(page_t page, int32_t row, char label[TEXT_MAX + 1],
                         char value[TEXT_MAX + 1])
{
    static const int32_t ROOT[ROOT_ROWS] = { STR_GENERAL, STR_APPS,    STR_DISPLAY, STR_WIFI,
                                             STR_BUTTONS, STR_RESTART, STR_RESET };
    /* The press, and what it does. */
    static const int32_t BUTTONS[][2] = { { STR_HOLD_BACK, STR_MENU },
                                          { STR_HOLD_OK, STR_MOVE },
                                          { STR_HOLD_UP_DOWN, STR_PAGE },
                                          { STR_UP_BACK, STR_RESTART } };
    static const int32_t GENERAL[] = { STR_DATE_TIME, STR_TIME_ZONE, STR_POWER_SAVING,
                                       STR_DEFAULT_APP, STR_IDLE_TIMEOUT };
    static const int32_t WIFI[] = { STR_NETWORK, STR_IP_ADDRESS, STR_STATUS, STR_FORGET_NETWORK };
    label[0] = '\0';
    value[0] = '\0';
    switch (page) {
    case PAGE_ROOT:
        copy_text(label, TEXT_MAX + 1, app_string(ROOT[row]));
        break;
    case PAGE_GENERAL:
        copy_text(label, TEXT_MAX + 1, app_string(GENERAL[row]));
        if (row == 1) {
            int32_t city = current_city();
            copy_text(value, TEXT_MAX + 1, city >= 0 ? city_name(city) : app_string(STR_CUSTOM));
        } else if (row == 2) {
            copy_text(value, TEXT_MAX + 1,
                      app_string(POWER_SAVING_NAMES[read_number(SYS_KEY_POWER_SAVING) % POWER_SAVINGS]));
        } else if (row == 3) {
            int32_t chosen = default_app_row();
            if (chosen < 0 || !app_title_at(chosen, value)) {
                copy_text(value, TEXT_MAX + 1, app_string(STR_NONE));
            }
        } else if (row == 4) {
            app_text_t out;
            app_text_start(&out, value, TEXT_MAX + 1);
            app_text_add_number(&out, read_number(SYS_KEY_MENU_IDLE_MINUTES));
            app_text_add(&out, app_string(STR_UNIT_MIN));
        }
        break;
    case PAGE_DATE_TIME:
        if (row == 0) {
            copy_text(label, TEXT_MAX + 1, app_string(STR_AUTOMATIC));
            copy_text(value, TEXT_MAX + 1, app_string(is_automatic() ? STR_ON : STR_OFF));
        } else {
            copy_text(label, TEXT_MAX + 1, app_string(STR_SET_DATE_TIME));
        }
        break;
    case PAGE_ZONE:
        copy_text(label, TEXT_MAX + 1, city_name(city_order[row]));
        break;
    case PAGE_BUTTONS:
        copy_text(label, TEXT_MAX + 1, app_string(BUTTONS[row][0]));
        copy_text(value, TEXT_MAX + 1, app_string(BUTTONS[row][1]));
        break;
    case PAGE_DEFAULT_APP:
        if (row == 0 || !app_title_at(row - 1, label)) {
            copy_text(label, TEXT_MAX + 1, app_string(STR_NONE));
        }
        break;
    case PAGE_APPS: {
        if (!app_title_at(row, label)) {
            copy_text(label, TEXT_MAX + 1, app_string(STR_NO_APPS));
        }
        break;
    }
    case PAGE_DISPLAY:
        copy_text(label, TEXT_MAX + 1, app_string(VALUE_SETTINGS[row].label));
        format_choice(&VALUE_SETTINGS[row], read_number(VALUE_SETTINGS[row].key), value);
        break;
    case PAGE_WIFI:
        copy_text(label, TEXT_MAX + 1, app_string(WIFI[row]));
        if (row == 0) {
            read_info(SYS_INFO_NETWORK, value);
            if (value[0] == '\0') {
                copy_text(value, TEXT_MAX + 1, app_string(STR_NOT_SET));
            }
        } else if (row == 1) {
            read_info(SYS_INFO_ADDRESS, value);
        } else if (row == 2) {
            read_info(SYS_INFO_WIFI, value);
            copy_text(value, TEXT_MAX + 1,
                      app_string(is_same(value, SYS_WIFI_ONLINE)       ? STR_ONLINE
                                 : is_same(value, SYS_WIFI_CONNECTING) ? STR_CONNECTING
                                                                       : STR_SETUP_MODE));
        }
        break;
    default:
        break;
    }
}

static const char *page_title(page_t page)
{
    switch (page) {
    case PAGE_GENERAL:
        return app_string(STR_GENERAL);
    case PAGE_DATE_TIME:
    case PAGE_CLOCK:
        return app_string(STR_DATE_TIME);
    case PAGE_ZONE:
        return app_string(STR_TIME_ZONE);
    case PAGE_DEFAULT_APP:
        return app_string(STR_DEFAULT_APP);
    case PAGE_BUTTONS:
        return app_string(STR_BUTTONS);
    case PAGE_APPS:
    case PAGE_REMOVE_APP:
        return app_string(STR_APPS);
    case PAGE_DISPLAY:
        return app_string(STR_DISPLAY);
    case PAGE_VALUE:
        return app_string(edited->label);
    case PAGE_WIFI:
    case PAGE_FORGET:
        return app_string(STR_WIFI);
    case PAGE_RESTART:
        return app_string(STR_RESTART);
    case PAGE_RESET:
        return app_string(STR_RESET);
    default:
        return app_string(STR_SETTINGS);
    }
}

/* ---- moving between pages ---- */

/* The page on the screen changed, or what it shows did. */
static bool is_question(page_t page);

static void show(void)
{
    const char *title = page_title(HERE.page);
    header_text(HOST_HEADER_LEFT, title, app_text_length(title));
    /* A question is answered only by an OK pressed once it is on the panel: the host holds
     * back one meant for the row that led here. */
    request_redraw(REDRAW | (is_question(HERE.page) ? HOST_REDRAW_QUESTION : 0));
}

static void open(page_t page, int32_t selected)
{
    if (depth + 1 < DEPTH_MAX) {
        depth++;
    }
    HERE.page = page;
    HERE.selected = selected;
    HERE.first = selected; /* the chosen row at the top when the page opens; moved up when drawn */
    show();
}

static void go_back(void)
{
    if (depth == 0) {
        request_exit();
        return;
    }
    depth--;
    /* A page may have fewer rows than when it was left. */
    int32_t rows = row_count(HERE.page);
    if (rows > 0 && HERE.selected >= rows) {
        HERE.selected = rows - 1;
    }
    show();
}

/* What an action came to. A final notice stays until the device restarts. */
static void tell(const char *text, bool is_final)
{
    notice = text;
    notice_is_final = is_final;
    depth = 0;
    HERE.page = PAGE_ROOT;
    HERE.selected = 0;
    open(PAGE_NOTICE, 0);
}

static void tell_result(int32_t result)
{
    if (result == HOST_OK) {
        tell(app_string(STR_RESTARTING), true);
    } else {
        tell(app_string(result == HOST_ERR_FAILED ? STR_STORAGE_ERROR : STR_FAILED), false);
    }
}

/* ---- OK on a row ---- */

/* Opens the page where one of a few values is chosen. */
static void edit_value(const display_setting_t *setting)
{
    edited = setting;
    /* The value may be one the web page set: start from the nearest choice
     * that is not below it. */
    int32_t value = read_number(edited->key);
    edited_choice = (app_picker_t){ .count = edited->choice_count,
                                    .chosen = edited->choice_count - 1 };
    for (int32_t choice = 0; choice < edited->choice_count; choice++) {
        if (edited->choices[choice] >= value) {
            edited_choice.chosen = choice;
            break;
        }
    }
    open(PAGE_VALUE, 0);
}

static void activate(page_t page, int32_t row)
{
    switch (page) {
    case PAGE_ROOT: {
        static const page_t SECTIONS[ROOT_ROWS] = { PAGE_GENERAL, PAGE_APPS,    PAGE_DISPLAY,
                                                    PAGE_WIFI,    PAGE_BUTTONS, PAGE_RESTART,
                                                    PAGE_RESET };
        open(SECTIONS[row], 0);
        break;
    }
    case PAGE_GENERAL:
        if (row == 0) {
            open(PAGE_DATE_TIME, 0);
        } else if (row == 1) {
            int32_t city = current_city();
            open(PAGE_ZONE, city >= 0 ? position_of(city) : 0);
        } else if (row == 2) {
            edit_value(&VALUE_SETTINGS[VALUE_POWER_SAVING]);
        } else if (row == 3) {
            open(PAGE_DEFAULT_APP, default_app_row() + 1); /* None is the first row */
        } else {
            edit_value(&VALUE_SETTINGS[VALUE_IDLE_MINUTES]);
        }
        break;
    case PAGE_DEFAULT_APP:
        /* The applications as the host lists them now: an OK that came before the page was
         * drawn again must not choose from a list never read. */
        if (row > app_count()) {
            break;
        }
        if (sys_app_set_default(row == 0 ? 0 : app_ids[row - 1]) == HOST_OK) {
            go_back();
        } else {
            tell(app_string(STR_FAILED), false);
        }
        break;
    case PAGE_DATE_TIME:
        if (row == 0) {
            write_number(SYS_KEY_TIME_AUTOMATIC, is_automatic() ? 0 : 1);
            show();
        } else if (is_automatic()) {
            /* Turned on from the web page while this row was on the screen. */
            HERE.selected = 0;
            show();
        } else {
            app_date_start(&clock, HOST_DATE_HAS_TIME | HOST_DATE_HAS_DATE, YEAR_FIRST,
                           YEAR_LAST);
            open(PAGE_CLOCK, 0);
        }
        break;
    case PAGE_ZONE: {
        const city_t *city = &CITIES[city_order[row]];
        if (write_setting(SYS_KEY_TIME_ZONE, city->zone) == HOST_OK &&
            write_setting(SYS_KEY_TIME_ZONE_CITY, city->name) == HOST_OK) {
            go_back();
        } else {
            tell(app_string(STR_FAILED), false);
        }
        break;
    }
    case PAGE_APPS: {
        if (app_title_at(row, app_to_remove_title)) {
            app_to_remove = app_ids[row];
            open(PAGE_REMOVE_APP, 0);
        }
        break;
    }
    case PAGE_DISPLAY:
        edit_value(&VALUE_SETTINGS[row]);
        break;
    case PAGE_WIFI:
        if (row == 3) {
            open(PAGE_FORGET, 0);
        }
        break;
    default:
        break;
    }
}

/* ---- the buttons ---- */

static bool is_list(page_t page)
{
    return row_count(page) > 0;
}

static bool is_question(page_t page)
{
    return page == PAGE_REMOVE_APP || page == PAGE_FORGET || page == PAGE_RESTART ||
           page == PAGE_RESET;
}

/* OK on a question does it; BACK, as everywhere, goes back. */
static void answer_question(page_t page)
{
    switch (page) {
    case PAGE_REMOVE_APP:
        if (sys_app_remove(app_to_remove) == HOST_OK) {
            go_back(); /* to the list, which the host reads anew */
        } else {
            tell(app_string(STR_STORAGE_ERROR), false);
        }
        break;
    case PAGE_FORGET:
        tell_result(sys_do(SYS_DO_FORGET_NETWORK));
        break;
    case PAGE_RESTART:
        tell_result(sys_do(SYS_DO_RESTART));
        break;
    default:
        tell_result(sys_do(SYS_DO_FACTORY_RESET));
        break;
    }
}

static void press_up_or_down(int32_t by)
{
    page_t page = HERE.page;
    if (page == PAGE_VALUE) {
        /* The ends are ends: no wrapping from "Never" to "Always". */
        if (app_picker_button(&edited_choice, by < 0 ? BTN_UP_PRESSED : BTN_DOWN_PRESSED) !=
            APP_UI_CHANGED) {
            return;
        }
    } else if (page == PAGE_ZONE) {
        HERE.selected = wrapped(HERE.selected + by, 0, CITY_COUNT - 1);
    } else if (is_list(page)) {
        HERE.selected = wrapped(HERE.selected + by, 0, row_count(page) - 1);
    } else {
        return;
    }
    request_redraw(REDRAW);
}

/* A long UP or DOWN on a list: a page of rows, to the ends and no further. */
static void page_up_or_down(int32_t by)
{
    page_t page = HERE.page;
    if (page == PAGE_VALUE || !is_list(page)) {
        return;
    }
    int32_t last = row_count(page) - 1;
    int32_t to = HERE.selected + by * shown_rows;
    to = to < 0 ? 0 : to > last ? last : to;
    if (to != HERE.selected) {
        HERE.first += to - HERE.selected; /* the page moves with it */
        HERE.selected = to;
        request_redraw(REDRAW);
    }
}

static void press_ok(void)
{
    page_t page = HERE.page;
    if (page == PAGE_VALUE) {
        if (write_number(edited->key, edited->choices[edited_choice.chosen]) == HOST_OK) {
            go_back();
        } else {
            tell(app_string(STR_FAILED), false);
        }
    } else if (is_question(page)) {
        answer_question(page);
    } else if (page == PAGE_NOTICE) {
        if (!notice_is_final) {
            go_back();
        }
    } else {
        activate(page, HERE.selected);
    }
}

static void press_back(void)
{
    if (HERE.page == PAGE_NOTICE && notice_is_final) {
        return;
    } else {
        go_back();
    }
}

/* ---- where the user was, while another scene had the screen ---- */

/* Settings is unloaded whenever another application takes an answer off the screen: what it
 * kept of where the user was brings them back to the same page, row and half-set value. */
typedef struct {
    int32_t      depth;
    step_t       path[DEPTH_MAX];
    app_date_t   clock;
    int32_t      edited; /* in VALUE_SETTINGS; -1 for none */
    app_picker_t edited_choice;
    uint32_t     app_to_remove;
} place_t;
APP_SESSION_FITS(place_t);

void app_on_blur(void)
{
    place_t place = { .depth = depth, .clock = clock, .edited = -1, .edited_choice = edited_choice,
                      .app_to_remove = app_to_remove };
    for (int32_t i = 0; i < DEPTH_MAX; i++) {
        place.path[i] = path[i];
    }
    if (edited != NULL) {
        place.edited = (int32_t)(edited - VALUE_SETTINGS);
    }
    /* What came of something is said once: back to where it was done from. */
    if (HERE.page == PAGE_NOTICE && place.depth > 0) {
        place.depth--;
    }
    app_session_save(&place, sizeof place);
}

void app_on_focus(void)
{
    place_t place;
    if (!app_session_load(&place, sizeof place) || place.depth < 0 || place.depth >= DEPTH_MAX ||
        place.edited >= (int32_t)COUNT_OF(VALUE_SETTINGS)) {
        return;
    }
    depth = place.depth;
    for (int32_t i = 0; i < DEPTH_MAX; i++) {
        path[i] = place.path[i];
    }
    clock = place.clock;
    edited = place.edited >= 0 ? &VALUE_SETTINGS[place.edited] : NULL;
    edited_choice = place.edited_choice;
    app_to_remove = place.app_to_remove;
    /* The title is the application's as it is called now; one removed meanwhile is no question. */
    if (HERE.page == PAGE_REMOVE_APP) {
        int32_t length = sys_app_title(app_to_remove, app_to_remove_title, TEXT_MAX);
        app_to_remove_title[length > 0 ? length : 0] = '\0';
        if (length <= 0 && depth > 0) {
            depth--;
        }
    }
    if (HERE.page == PAGE_VALUE && edited == NULL && depth > 0) {
        depth--;
    }
    show();
}

void app_init(void)
{
    order_cities();
    depth = 0;
    HERE.page = PAGE_ROOT;
    HERE.selected = 0;
    show();
}

/* The date and the time: the library knows what the buttons do there. */
static void press_on_clock(int32_t event)
{
    switch (app_date_button(&clock, event)) {
    case APP_UI_CHANGED:
        request_redraw(REDRAW);
        break;
    case APP_UI_DONE:
        if (clock_save()) {
            go_back();
        } else {
            tell(app_string(STR_FAILED), false);
        }
        break;
    case APP_UI_CANCELLED:
        go_back();
        break;
    case APP_UI_IGNORED:
        break;
    }
}

void app_on_button(int32_t event)
{
    if (HERE.page == PAGE_CLOCK) {
        press_on_clock(event);
        return;
    }
    switch (event) {
    case BTN_UP_PRESSED:
        press_up_or_down(-1);
        break;
    case BTN_DOWN_PRESSED:
        press_up_or_down(1);
        break;
    case BTN_UP_LONG_PRESSED:
    case BTN_DOWN_LONG_PRESSED:
        page_up_or_down(event == BTN_UP_LONG_PRESSED ? -1 : 1);
        break;
    case BTN_OK_PRESSED:
        press_ok();
        break;
    }
}

/* It has pages of its own: BACK is a page back, and out from the first. */
void app_on_back(void)
{
    if (HERE.page == PAGE_CLOCK) {
        press_on_clock(BTN_BACK_PRESSED);
    } else {
        press_back();
    }
}

/* ---- the picture ---- */

#define MARGIN       16
#define LINE_HEIGHT  30

static void draw_text(int32_t left, int32_t baseline, int32_t font, const char *text)
{
    gfx_text(left, baseline - gfx_font_ascent(font), font, text, app_text_length(text));
}

/* Whether the row is the one that is set now: a check against it. */
static bool is_current_row(page_t page, int32_t row)
{
    return (page == PAGE_ZONE && city_order[row] == current_city()) ||
           (page == PAGE_DEFAULT_APP && row == default_app_row() + 1);
}

/* A list, drawn by the host as it draws its own. It stands where it stood until the chosen
 * row reaches an edge: the step keeps its top row. */
static void draw_list(step_t *step)
{
    page_t  page = step->page;
    int32_t rows = row_count(page);
    const host_list_t list = { .count = rows, .selected = step->selected, .first = step->first };
    int32_t first = ui_list_first(&list);
    int32_t visible = ui_list_visible(&list);
    step->first = first;
    shown_rows = visible > 0 ? visible : 1;
    for (int32_t row = first; row < first + visible && row < rows; row++) {
        char label[TEXT_MAX + 1];
        char value[TEXT_MAX + 1];
        describe_row(page, row, label, value);
        /* What OK does to an application here, said by a picture. */
        bool is_app_row = page == PAGE_APPS && app_title_at(row, value);
        if (is_app_row) {
            value[0] = '\0';
        }
        int32_t icon = is_app_row                   ? HOST_ICON_TRASH
                       : is_current_row(page, row) ? HOST_ICON_CHECK
                                                   : HOST_ICON_NONE;
        ui_list_row(&list, row, label, app_text_length(label), value, app_text_length(value), icon);
    }
}

/* A question, asked by the host's own notice. */
static void draw_question(const char *first, const char *second, int32_t icon, const char *action)
{
    host_notice_t question = { .icon = icon };
    copy_text(question.first, sizeof question.first, first);
    copy_text(question.second, sizeof question.second, second);
    copy_text(question.ok, sizeof question.ok, action);
    copy_text(question.back, sizeof question.back, app_string(STR_CANCEL));
    ui_notice(&question);
}

/* ---- setting a value: what UP and DOWN lead to is on the screen ---- */

static void draw_value(void)
{
    char above[TEXT_MAX + 1] = "";
    char chosen[TEXT_MAX + 1];
    char below[TEXT_MAX + 1] = "";
    if (app_picker_above(&edited_choice) >= 0) {
        format_choice(edited, edited->choices[app_picker_above(&edited_choice)], above);
    }
    format_choice(edited, edited->choices[edited_choice.chosen], chosen);
    if (app_picker_below(&edited_choice) >= 0) {
        format_choice(edited, edited->choices[app_picker_below(&edited_choice)], below);
    }
    app_picker_draw_texts(&edited_choice, above, chosen, below);
}

/* The cities go round: after the last comes the first. `at` is a place in city_order[]. */
void app_on_draw(void)
{
    page_t page = HERE.page;
    char line[TEXT_MAX + 1];
    app_text_t text;
    app_text_start(&text, line, sizeof line);
    switch (page) {
    case PAGE_CLOCK:
        app_date_draw(&clock);
        break;
    case PAGE_VALUE:
        draw_value();
        break;
    case PAGE_REMOVE_APP:
        app_text_add(&text, app_to_remove_title);
        app_text_add(&text, "?");
        draw_question(app_string(STR_REMOVE), line, HOST_ICON_TRASH, app_string(STR_REMOVE));
        break;
    case PAGE_FORGET:
        draw_question(app_string(STR_FORGET), app_string(STR_FORGET_WHAT), HOST_ICON_NO_SIGNAL,
                      app_string(STR_FORGET));
        break;
    case PAGE_RESET:
        draw_question(app_string(STR_FACTORY), app_string(STR_FACTORY_WHAT), HOST_ICON_WARNING,
                      app_string(STR_RESET_ACTION));
        break;
    case PAGE_RESTART:
        draw_question(app_string(STR_RESTART), app_string(STR_RESTART_WHAT), HOST_ICON_UPDATE,
                      app_string(STR_RESTART));
        break;
    case PAGE_NOTICE:
        draw_text(MARGIN, LINE_HEIGHT, HOST_FONT_TEXT, notice);
        break;
    default: {
        /* The list may have become shorter since the row was chosen. */
        int32_t rows = row_count(page);
        if (HERE.selected >= rows) {
            HERE.selected = rows - 1;
        }
        draw_list(&HERE);
        break;
    }
    }
}
