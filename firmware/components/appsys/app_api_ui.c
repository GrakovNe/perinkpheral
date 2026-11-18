/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The host's look: the ui_* calls, and what an application asks of the screen - a redraw, the header, a notice, the focus, leaving.
 */
#include "app_internal.h"

_Static_assert(HOST_ICON_COUNT == GFX_ICON_COUNT, "the contract's icons are the host's");
_Static_assert(HOST_ICON_SUNSET == GFX_ICON_SUNSET, "in the same order");

static bool is_valid_icon(int32_t icon)
{
    return icon >= 0 && icon < GFX_ICON_COUNT;
}

/* A list of more rows than this is somebody's mistake, not a list. */
#define LIST_ROWS_MAX 10000

/* The list as the application describes it, checked: false when it is not
 * the application's to give, or not a list. */
static bool read_list(IM3Runtime runtime, void *memory, uint32_t offset, list_t *list)
{
    const uint8_t *given = app_memory_at(runtime, memory, offset, sizeof(host_list_t));
    if (given == NULL) {
        return false;
    }
    host_list_t asked;
    memcpy(&asked, given, sizeof asked);
    bool is_valid_list = asked.count >= 0 && asked.count <= LIST_ROWS_MAX && asked.selected >= -1 &&
                     asked.selected <= LIST_ROWS_MAX && asked.first >= -LIST_ROWS_MAX &&
                     asked.first <= LIST_ROWS_MAX && is_drawable(asked.top) && asked.top >= 0 &&
                     is_drawable(asked.height) && asked.height >= 0;
    *list = (list_t){ .count = asked.count, .selected = asked.selected, .top = asked.top,
                      .height = asked.height, .first = asked.first };
    return is_valid_list;
}

HOST_FUNCTION(ui_list_visible)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, list_offset);
    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    list_t list;
    if (!read_list(runtime, _mem, list_offset, &list)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(list_visible_rows(app->surface, &list));
}

HOST_FUNCTION(ui_list_first)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, list_offset);
    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    list_t list;
    if (!read_list(runtime, _mem, list_offset, &list)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(list_first_row(app->surface, &list));
}

HOST_FUNCTION(ui_draw_list_row)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, list_offset);
    m3ApiGetArg(int32_t, index);
    m3ApiGetArg(uint32_t, label_offset);
    m3ApiGetArg(int32_t, label_length);
    m3ApiGetArg(uint32_t, value_offset);
    m3ApiGetArg(int32_t, value_length);
    m3ApiGetArg(int32_t, icon);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    list_t list;
    char label[HOST_TEXT_MAX + 1];
    char value[HOST_TEXT_MAX + 1];
    if (!read_list(runtime, _mem, list_offset, &list) ||
        (icon != HOST_ICON_NONE && !is_valid_icon(icon)) ||
        !app_text_copy(runtime, _mem, label_offset, label_length, label) ||
        !app_text_copy(runtime, _mem, value_offset, value_length, value)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    list_row_t row = { .label = label, .value = value,
                       .icon = icon == HOST_ICON_NONE ? LIST_NO_ICON : icon };
    list_draw_row(app->surface, &list, index, &row, false);
    list_draw_scrollbar(app->surface, &list);
    m3ApiReturn(HOST_OK);
}

/* A text of the notice, or NULL for an empty one: terminated here whatever
 * the application left in its last byte. */
static const char *notice_text(char *text, size_t size)
{
    /* A text that fills its field to the last byte may end inside a
     * character: it ends before it. */
    if (memchr(text, '\0', size) == NULL) {
        size_t length = size - 1;
        while (length > 0 && gfx_is_utf8_tail(text[length])) {
            length--;
        }
        text[length] = '\0';
    }
    return text[0] != '\0' ? text : NULL;
}

HOST_FUNCTION(ui_draw_notice)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, notice_offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    const uint8_t *given = app_memory_at(runtime, _mem, notice_offset, sizeof(host_notice_t));
    if (given == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    host_notice_t asked;
    memcpy(&asked, given, sizeof asked);
    if ((asked.icon != HOST_ICON_NONE && !is_valid_icon(asked.icon)) || asked.first[0] == '\0') {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    notice_t notice = {
        .first = notice_text(asked.first, sizeof asked.first),
        .second = notice_text(asked.second, sizeof asked.second),
        .detail = notice_text(asked.detail, sizeof asked.detail),
        .icon = asked.icon == HOST_ICON_NONE ? NOTICE_NO_ICON : asked.icon,
        .ok = notice_text(asked.ok, sizeof asked.ok),
        .back = notice_text(asked.back, sizeof asked.back),
        .is_working = asked.is_working != 0,
    };
    notice_draw(app->surface, &notice);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(ui_draw_text_lines)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, style);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, max_lines);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    char text[HOST_TEXT_MAX + 1];
    if (!is_drawable(x) || !is_drawable(y) || style < HOST_UI_TEXT || style > HOST_UI_VALUE ||
        width <= 0 || !is_drawable(width) || max_lines < 1 || max_lines > HOST_UI_LINES_MAX ||
        !app_text_copy(runtime, _mem, text_offset, length, text)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(kit_text_lines(app->surface, x, y, (kit_style_t)style, text, width, max_lines));
}

HOST_FUNCTION(ui_draw_text)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, style);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    m3ApiGetArg(int32_t, max_width);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    char text[HOST_TEXT_MAX + 1];
    if (!is_drawable(x) || !is_drawable(y) || style < HOST_UI_TEXT || style > HOST_UI_VALUE ||
        max_width < 0 || !is_drawable(max_width) ||
        !app_text_copy(runtime, _mem, text_offset, length, text)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(kit_text(app->surface, x, y, (kit_style_t)style, text, max_width));
}

HOST_FUNCTION(ui_text_width)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, style);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);

    char text[HOST_TEXT_MAX + 1];
    if (style < HOST_UI_TEXT || style > HOST_UI_VALUE ||
        !app_text_copy(runtime, _mem, text_offset, length, text)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(gfx_text_font_width(kit_font((kit_style_t)style), text));
}

HOST_FUNCTION(ui_text_height)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, style);
    if (style < HOST_UI_TEXT || style > HOST_UI_VALUE) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(gfx_font_height(kit_font((kit_style_t)style)));
}

HOST_FUNCTION(ui_draw_title)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    char text[HOST_TEXT_MAX + 1];
    if (!app_text_copy(runtime, _mem, text_offset, length, text)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(kit_title(app->surface, text));
}

HOST_FUNCTION(ui_draw_icon)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, icon);
    m3ApiGetArg(int32_t, size);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    if (!is_drawable(x) || !is_drawable(y) || !is_valid_icon(icon) || size < HOST_UI_ICON_SMALL ||
        size > HOST_UI_ICON_LARGE) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    kit_icon(app->surface, x, y, icon, (kit_icon_size_t)size);
    m3ApiReturn(HOST_OK);
}

/* The name of a button is a word: "BACK" is the longest there is. */
#define BUTTON_NAME_MAX 8

HOST_FUNCTION(ui_draw_button)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(uint32_t, name_offset);
    m3ApiGetArg(int32_t, name_length);
    m3ApiGetArg(uint32_t, action_offset);
    m3ApiGetArg(int32_t, action_length);
    m3ApiGetArg(int32_t, mirrored);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    char name[HOST_TEXT_MAX + 1];
    char action[HOST_TEXT_MAX + 1];
    if (!is_drawable(x) || !is_drawable(y) || name_length < 1 || name_length > BUTTON_NAME_MAX ||
        !app_text_copy(runtime, _mem, name_offset, name_length, name) ||
        !app_text_copy(runtime, _mem, action_offset, action_length, action)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int baseline = y + gfx_font_ascent(kit_font(KIT_TEXT));
    m3ApiReturn(kit_button(app->surface, x, baseline, name, action, mirrored != 0));
}

HOST_FUNCTION(ui_draw_actions)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, ok_offset);
    m3ApiGetArg(int32_t, ok_length);
    m3ApiGetArg(uint32_t, back_offset);
    m3ApiGetArg(int32_t, back_length);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    char ok[HOST_TEXT_MAX + 1];
    char back[HOST_TEXT_MAX + 1];
    if (!app_text_copy(runtime, _mem, ok_offset, ok_length, ok) ||
        !app_text_copy(runtime, _mem, back_offset, back_length, back)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    kit_actions(app->surface, ok[0] != '\0' ? ok : NULL, back[0] != '\0' ? back : NULL);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(ui_draw_bar)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, value);
    m3ApiGetArg(int32_t, maximum);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    if (!is_drawable(x) || !is_drawable(y) || width < 1 || !is_drawable(width)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    kit_bar(app->surface, x, y, width, value, maximum);
    m3ApiReturn(HOST_OK);
}

/* ui_draw_value() and ui_value_width(): the same arguments, drawn or measured. */
static int32_t value_with_unit(IM3Runtime runtime, void *memory, gfx_t *surface, int32_t x,
                               int32_t y, int32_t font_id, uint32_t value_offset,
                               int32_t value_length, uint32_t unit_offset, int32_t unit_length)
{
    const gfx_font_t *font = font_by_id(font_id);
    char value[HOST_TEXT_MAX + 1], unit[HOST_TEXT_MAX + 1];
    if (font == NULL || !is_drawable(x) || !is_drawable(y) ||
        !app_text_copy(runtime, memory, value_offset, value_length, value) ||
        !app_text_copy(runtime, memory, unit_offset, unit_length, unit)) {
        return HOST_ERR_ARGUMENT;
    }
    return kit_value(surface, x, y, font, is_tabular(font_id), value, unit);
}

HOST_FUNCTION(ui_draw_value)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, font_id);
    m3ApiGetArg(uint32_t, value);
    m3ApiGetArg(int32_t, value_length);
    m3ApiGetArg(uint32_t, unit);
    m3ApiGetArg(int32_t, unit_length);
    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    m3ApiReturn(value_with_unit(runtime, _mem, app->surface, x, y, font_id, value, value_length,
                                unit, unit_length));
}

HOST_FUNCTION(ui_value_width)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    m3ApiGetArg(uint32_t, value);
    m3ApiGetArg(int32_t, value_length);
    m3ApiGetArg(uint32_t, unit);
    m3ApiGetArg(int32_t, unit_length);
    m3ApiReturn(value_with_unit(runtime, _mem, NULL, 0, 0, font_id, value, value_length, unit,
                                unit_length));
}

/* Why an application has nothing to show, said the same way by all of
 * them: what the last answer from the network came to. */
HOST_FUNCTION(ui_draw_reason)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, status);
    static const struct {
        int32_t status;
        int     icon;
        int     first, detail; /* a word_t each; -1: none */
    } REASONS[] = {
        { 0, GFX_ICON_HOURGLASS, WORD_LOADING, -1 },
        { HOST_FAILED_NOT_READY, GFX_ICON_GEAR, WORD_NOT_SET_UP, WORD_OPEN_THE_PAGE },
        { HOST_FAILED_NO_NETWORK, GFX_ICON_NO_SIGNAL, WORD_NO_NETWORK, -1 },
        { HOST_FAILED_NO_ANSWER, GFX_ICON_WARNING, WORD_NO_ANSWER, -1 },
        { HOST_FAILED_TIMEOUT, GFX_ICON_WARNING, WORD_NO_ANSWER, -1 },
        { HOST_FAILED_NOT_ALLOWED, GFX_ICON_WARNING, WORD_NOT_ALLOWED, -1 },
        { HOST_FAILED_NOT_TRUSTED, GFX_ICON_WARNING, WORD_NOT_TRUSTED, WORD_CHECK_THE_CERTIFICATE },
        { HOST_FAILED_TOO_LARGE, GFX_ICON_WARNING, WORD_TOO_LARGE, -1 },
        { HOST_FAILED_NO_MEMORY, GFX_ICON_WARNING, WORD_NO_MEMORY, -1 },
        { 401, GFX_ICON_WARNING, WORD_ACCESS_DENIED, WORD_CHECK_THE_SETTINGS },
        { 403, GFX_ICON_WARNING, WORD_ACCESS_DENIED, WORD_CHECK_THE_SETTINGS },
        { 404, GFX_ICON_WARNING, WORD_NOT_FOUND, WORD_CHECK_THE_ADDRESS },
    };
    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    if (status >= 200 && status < 300) {
        m3ApiReturn(0); /* an answer came: what is wrong, if anything, is the application's to say */
    }
    char     code[sizeof(((host_notice_t *)0)->detail)]; /* shown as the detail of a notice */
    notice_t notice = { .first = word(WORD_NO_ANSWER), .detail = code, .icon = GFX_ICON_WARNING };
    snprintf(code, sizeof code, "%s %d", word(WORD_CODE), (int)status);
    for (size_t i = 0; i < sizeof REASONS / sizeof REASONS[0]; i++) {
        if (REASONS[i].status == status) {
            notice.first = word((word_t)REASONS[i].first);
            notice.detail = REASONS[i].detail >= 0 ? word((word_t)REASONS[i].detail) : NULL;
            notice.icon = REASONS[i].icon;
            break;
        }
    }
    notice_draw(app->surface, &notice);
    m3ApiReturn(1);
}

HOST_FUNCTION(ui_draw_frame)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, height);
    m3ApiGetArg(int32_t, arrows);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    if (!is_drawable(x) || !is_drawable(y) || width < 1 || !is_drawable(width) || height < 1 ||
        !is_drawable(height)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    kit_frame(app->surface, x, y, width, height, (arrows & HOST_UI_ARROW_UP) != 0,
              (arrows & HOST_UI_ARROW_DOWN) != 0);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(ui_draw_picker)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, picker_offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    const uint8_t *given = app_memory_at(runtime, _mem, picker_offset, sizeof(host_picker_t));
    if (given == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    host_picker_t asked;
    memcpy(&asked, given, sizeof asked);
    kit_picker_t picker = {
        .above = notice_text(asked.above, sizeof asked.above),
        .chosen = notice_text(asked.chosen, sizeof asked.chosen),
        .below = notice_text(asked.below, sizeof asked.below),
        .ok = notice_text(asked.ok, sizeof asked.ok),
        .back = notice_text(asked.back, sizeof asked.back),
    };
    if (picker.chosen == NULL) {
        picker.chosen = "";
    }
    picker.ok = picker.ok != NULL ? picker.ok : word(WORD_SAVE);
    picker.back = picker.back != NULL ? picker.back : word(WORD_CANCEL);
    kit_picker(app->surface, &picker);
    m3ApiReturn(HOST_OK);
}

/* What the application asked for as the kit takes it; false when it is
 * not a cell. `asked` keeps the texts the cell points into. */
static bool cell_of(IM3Runtime runtime, void *memory, uint32_t cell_offset, uint32_t picture_offset,
                    int32_t icon, host_cell_t *asked, kit_cell_t *cell)
{
    if (icon != HOST_ICON_NONE && !is_valid_icon(icon)) {
        return false;
    }
    const uint8_t *given = app_memory_at(runtime, memory, cell_offset, sizeof *asked);
    if (given == NULL) {
        return false;
    }
    memcpy(asked, given, sizeof *asked);
    const uint8_t *picture = NULL;
    if (picture_offset != 0) {
        picture = app_picture_at(runtime, memory, picture_offset);
        if (picture == NULL) {
            return false;
        }
    }
    if (!is_drawable(asked->x) || !is_drawable(asked->y) || !is_drawable(asked->width) ||
        !is_drawable(asked->height) || asked->width <= 0 || asked->height <= 0) {
        return false;
    }
    const char *value = notice_text(asked->value, sizeof asked->value);
    *cell = (kit_cell_t){
        .left = asked->x, .top = asked->y, .width = asked->width, .height = asked->height,
        .name = notice_text(asked->name, sizeof asked->name),
        .value = value != NULL ? value : "",
        .picture = picture,
        .icon = icon,
        .size = asked->size,
    };
    return true;
}

HOST_FUNCTION(ui_cell_fit)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, cell_offset);
    m3ApiGetArg(uint32_t, picture_offset);
    m3ApiGetArg(int32_t, icon);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    host_cell_t asked;
    kit_cell_t  cell;
    if (!cell_of(runtime, _mem, cell_offset, picture_offset, icon, &asked, &cell)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(kit_cell_fit(app->surface, &cell));
}

HOST_FUNCTION(ui_draw_cell)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, cell_offset);
    m3ApiGetArg(uint32_t, picture_offset);
    m3ApiGetArg(int32_t, icon);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    host_cell_t asked;
    kit_cell_t  cell;
    if (!cell_of(runtime, _mem, cell_offset, picture_offset, icon, &asked, &cell)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    kit_cell(app->surface, &cell);
    m3ApiReturn(HOST_OK);
}

_Static_assert(HOST_CHART_LINE == KIT_CHART_LINE && HOST_CHART_AREA == KIT_CHART_AREA &&
               HOST_CHART_BARS == KIT_CHART_BARS && HOST_CHART_STEPS == KIT_CHART_STEPS &&
               HOST_CHART_AXIS == KIT_CHART_AXIS && HOST_CHART_FROM_ZERO == KIT_CHART_FROM_ZERO &&
               HOST_CHART_RANGE == KIT_CHART_RANGE && HOST_CHART_MARK_LAST == KIT_CHART_MARK_LAST &&
               HOST_CHART_RANGE_RIGHT == KIT_CHART_RANGE_RIGHT &&
               HOST_CHART_NO_VALUE == KIT_CHART_NO_VALUE,
               "the chart of the contract is the chart of the kit");

#define CHART_FILL_SHADE 4 /* the grey of the area under a line */

HOST_FUNCTION(ui_draw_chart)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, chart_offset);
    m3ApiGetArg(uint32_t, values_offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    host_chart_t   asked;
    const uint8_t *given = app_memory_at(runtime, _mem, chart_offset, sizeof asked);
    if (given == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    memcpy(&asked, given, sizeof asked);
    if (asked.count < 1 || asked.count > HOST_CHART_VALUES_MAX || asked.kind < HOST_CHART_LINE ||
        asked.kind > HOST_CHART_STEPS || asked.decimals < 0 || asked.decimals > KIT_CHART_DECIMALS_MAX ||
        !is_drawable(asked.x) || !is_drawable(asked.y) || !is_drawable(asked.width) ||
        !is_drawable(asked.height) || asked.width <= 0 || asked.height <= 0) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    /* A copy: the application's memory need not be aligned for numbers. */
    int32_t        values[HOST_CHART_VALUES_MAX];
    size_t         values_size = (size_t)asked.count * sizeof values[0];
    const uint8_t *numbers = app_memory_at(runtime, _mem, values_offset, values_size);
    if (numbers == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    memcpy(values, numbers, values_size);
    const kit_chart_t chart = {
        .left = asked.x, .top = asked.y, .width = asked.width, .height = asked.height,
        .values = values, .count = asked.count,
        .kind = (kit_chart_kind_t)asked.kind, .flags = asked.flags, .decimals = asked.decimals,
        .fill_shade = app->services->dithers() ? CHART_FILL_SHADE : 0,
    };
    kit_chart(app->surface, &chart);
    m3ApiReturn(HOST_OK);
}

#define DATE_YEAR_MAX 9999

HOST_FUNCTION(ui_draw_date)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, date_offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    const uint8_t *given = app_memory_at(runtime, _mem, date_offset, sizeof(host_date_t));
    if (given == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    host_date_t asked;
    memcpy(&asked, given, sizeof asked);
    bool is_valid_date = asked.year >= 0 && asked.year <= DATE_YEAR_MAX && asked.month >= 1 &&
                     asked.month <= 12 && asked.day >= 1 && asked.day <= 31 && asked.hour >= 0 &&
                     asked.hour <= 23 && asked.minute >= 0 && asked.minute <= 59;
    bool has_any = (asked.has & (HOST_DATE_HAS_TIME | HOST_DATE_HAS_DATE)) != 0;
    if (!is_valid_date || !has_any || asked.selected < HOST_DATE_HOUR ||
        asked.selected > HOST_DATE_NONE) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    kit_date_t date = {
        .year = asked.year, .month = asked.month, .day = asked.day,
        .hour = asked.hour, .minute = asked.minute,
        .has_time = (asked.has & HOST_DATE_HAS_TIME) != 0,
        .has_date = (asked.has & HOST_DATE_HAS_DATE) != 0,
        .selected = (kit_date_part_t)asked.selected,
        .ok = notice_text(asked.ok, sizeof asked.ok),
        .back = notice_text(asked.back, sizeof asked.back),
    };
    /* What the buttons do, when the application left it to the host. */
    if (date.selected != KIT_DATE_NONE) {
        kit_date_part_t first = date.has_time ? KIT_DATE_HOUR : KIT_DATE_DAY;
        kit_date_part_t last = date.has_date ? KIT_DATE_YEAR : KIT_DATE_MINUTE;
        if (date.ok == NULL) {
            date.ok = word(date.selected == last ? WORD_SAVE : WORD_NEXT);
        }
        if (date.back == NULL) {
            date.back = word(date.selected == first ? WORD_CANCEL : WORD_BACK);
        }
    }
    kit_date(app->surface, &date);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(screen_redraw)
{
    m3ApiGetArg(int32_t, flags);
    app_t *app = APP_OF(runtime);
    app->redraw_requested = true;
    /* Bits the host does not know are an application from the future. */
    app->redraw_flags |= flags & (HOST_REDRAW_SOON | HOST_REDRAW_CLEAN | HOST_REDRAW_NO_BLINK |
                                  HOST_REDRAW_QUESTION);
    m3ApiSuccess();
}

HOST_FUNCTION(header_set_text)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, side);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    const uint8_t *text = app_memory_at(runtime, _mem, text_offset, length);
    if ((side != HOST_HEADER_LEFT && side != HOST_HEADER_RIGHT) || text == NULL ||
        length > HOST_HEADER_TEXT_MAX) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    app_t *app = APP_OF(runtime);
    memcpy(app->header[side], text, (size_t)length);
    app->header[side][length] = '\0';
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(header_set_status)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, side);
    m3ApiGetArg(int32_t, icon);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    app_t *app = APP_OF(runtime);
    char text[HOST_TEXT_MAX + 1];
    bool is_valid_icon = icon == HOST_ICON_NONE || (icon >= 0 && icon < GFX_ICON_COUNT);
    if ((side != HOST_HEADER_LEFT && side != HOST_HEADER_RIGHT) || !is_valid_icon ||
        length > HOST_STATUS_TEXT_MAX ||
        !app_text_copy(runtime, _mem, text_offset, length, text)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (app->services->header_set_status != NULL) {
        app->services->header_set_status(app->name, side, icon, text);
    }
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(notification_post)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, notice_offset);

    app_t *app = APP_OF(runtime);
    const uint8_t *given = app_memory_at(runtime, _mem, notice_offset, sizeof(host_notice_t));
    if (given == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    host_notice_t asked;
    memcpy(&asked, given, sizeof asked);
    bool is_valid_icon = asked.icon == HOST_ICON_NONE || (asked.icon >= 0 && asked.icon < GFX_ICON_COUNT);
    const char *first = notice_text(asked.first, sizeof asked.first);
    if (!is_valid_icon || first == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (app->services->notification_post == NULL ||
        !app->services->notification_post(first, notice_text(asked.second, sizeof asked.second),
                               notice_text(asked.detail, sizeof asked.detail),
                               asked.icon == HOST_ICON_NONE ? NOTICE_NO_ICON : asked.icon)) {
        m3ApiReturn(HOST_ERR_NO_SPACE);
    }
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(screen_request)
{
    APP_OF(runtime)->focus_requested = true;
    m3ApiSuccess();
}

_Static_assert(APP_SESSION_BYTES == HOST_SESSION_MAX, "the session's room is the contract's");

HOST_FUNCTION(session_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, offset);
    m3ApiGetArg(int32_t, length);
    app_t         *app = APP_OF(runtime);
    const uint8_t *data = app_memory_at(runtime, _mem, offset, length);
    if (data == NULL || length < 0 || length > HOST_SESSION_MAX || app->session == NULL ||
        !app->session_is_writable) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (!app->session->is_closed) { /* left the stack: what its last blur says is over */
        memcpy(app->session->bytes, data, (size_t)length);
        app->session->length = length;
    }
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(session_get)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, offset);
    m3ApiGetArg(int32_t, capacity);
    app_t   *app = APP_OF(runtime);
    uint8_t *buffer = (uint8_t *)app_memory_at(runtime, _mem, offset, capacity);
    if (buffer == NULL || capacity < 0) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int32_t length = app->session != NULL && !app->session->is_closed ? app->session->length : 0;
    if (length > capacity) {
        m3ApiReturn(HOST_ERR_NO_SPACE);
    }
    if (length > 0) { memcpy(buffer, app->session->bytes, (size_t)length); }
    m3ApiReturn(length);
}

HOST_FUNCTION(scene_state_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, offset);
    m3ApiGetArg(int32_t, length);
    app_t *app = APP_OF(runtime);
    const uint8_t *data = app_memory_at(runtime, _mem, offset, length);
    app_session_t *session = app->session;
    if (data == NULL || length < 0 || length > HOST_SCENE_STATE_MAX || session == NULL ||
        !app->session_is_writable) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (session->is_closed) {
        m3ApiReturn(HOST_OK);
    }
    if (length == 0) {
        free(session->scene_state);
        session->scene_state = NULL;
    } else {
        if (length > session->scene_state_length) {
            uint8_t *storage = realloc(session->scene_state, (size_t)length);
            if (storage == NULL) {
                m3ApiReturn(HOST_ERR_NO_SPACE);
            }
            session->scene_state = storage;
        }
        memcpy(session->scene_state, data, (size_t)length);
    }
    session->scene_state_length = length;
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(scene_state_get)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, offset);
    m3ApiGetArg(int32_t, capacity);
    app_t *app = APP_OF(runtime);
    uint8_t *buffer = app_memory_to_fill(runtime, _mem, offset, capacity);
    if (buffer == NULL || capacity < 0) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int32_t length = app->session != NULL && !app->session->is_closed ? app->session->scene_state_length : 0;
    if (length > capacity) {
        m3ApiReturn(HOST_ERR_NO_SPACE);
    }
    if (length > 0) {
        memcpy(buffer, app->session->scene_state, (size_t)length);
    }
    m3ApiReturn(length);
}

HOST_FUNCTION(screen_leave)
{
    APP_OF(runtime)->exit_requested = true;
    m3ApiSuccess();
}
