/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef d_m3ApplicationArea
#include "m3_area.h"
#endif
#include "gfx_icons.h"
#include "kit.h"
#include "list.h"
#include "notice.h"
#include "host_api.h"
#include "qrcodegen.h"
#include "wasm3.h"

/* The interpreter's own stack of values, apart from the application's
 * memory. */
#define INTERPRETER_STACK_BYTES 4096

#define HOST_MODULE "host"

/* How long one handler may run. Drawing a full screen takes an
 * interpreted application tens of milliseconds; one that is still at it
 * after this long is stuck. */
#define HANDLER_BUDGET_MS 3000

/* The clock is looked at once in this many yields of the interpreter,
 * which yields on every call and every turn of a loop. */
#define YIELDS_PER_CLOCK_CHECK 1024

static const char STOPPED_TOO_LONG[] = "it ran too long";

struct app {
    IM3Environment        environment;
    IM3Runtime            runtime;
    const app_services_t *services;
    const char           *name;

    IM3Function on_focus;
    IM3Function on_blur;
    IM3Function on_terminate;
    IM3Function on_back;
    IM3Function on_button;
    IM3Function on_timer;
    IM3Function on_draw;
    IM3Function on_data;
    IM3Function on_values;
    IM3Function on_settings;
    IM3Function on_intent;
    IM3Function on_alarm;
    /* Set only while app_on_values() runs: what data_value() reads. */
    const char *const *values;
    int                value_count;
    /* Set only while a handler of an answer runs: what data_response() says. */
    bool                   is_in_answer;
    const host_response_t *response;
    int                    response_status;
    uint32_t    data_buffer_offset; /* where in its memory the application takes data */
    bool        has_data_buffer;

    gfx_t      *surface; /* set only while app_on_draw() runs */
    bool        redraw_requested;
    int         redraw_flags;  /* HOST_REDRAW_*, of every request since the last take */
    bool        exit_requested;
    bool        focus_requested;
    /* What it put into the host's header; empty leaves a side to the host. */
    char        header[HOST_HEADER_RIGHT + 1][HOST_HEADER_TEXT_MAX + 1];
    const char *stop_reason; /* NULL while the application is alive */
};

/* The application whose handler is running, for m3_Yield() below: the
 * interpreter calls it without saying for whom. There is one application
 * and one host task, so one is enough. */
static struct {
    const app_services_t *services;
    int64_t               deadline_ms;
    uint32_t              yields;
} s_running;

/* The interpreter's hook: called on every call and every turn of a loop
 * of the application; a result that is not m3Err_none traps it. */
M3Result m3_Yield(void)
{
    if (s_running.services == NULL || ++s_running.yields % YIELDS_PER_CLOCK_CHECK != 0) {
        return m3Err_none;
    }
    return s_running.services->uptime_ms() > s_running.deadline_ms ? STOPPED_TOO_LONG
                                                                   : m3Err_none;
}

static void start_budget(const app_services_t *services)
{
    s_running.services = services;
    s_running.deadline_ms = services->uptime_ms() + HANDLER_BUDGET_MS;
    s_running.yields = 0;
}

static void stop_budget(void)
{
    s_running.services = NULL;
}

/* ---- the host API as the application sees it ---- */

_Static_assert(HOST_INK == GFX_SHADE_SOLID, "an application's ink is the solid shade");
_Static_assert(HOST_FONT_TEXT == GFX_FONT_TEXT && HOST_FONT_MEDIUM == GFX_FONT_MEDIUM &&
                   HOST_FONT_LARGE == GFX_FONT_LARGE && HOST_FONT_DISPLAY == GFX_FONT_DISPLAY &&
                   HOST_FONT_HUGE == GFX_FONT_HUGE && HOST_FONT_SMALL == GFX_FONT_SMALL,
               "applications name the fonts by the numbers gfx gives them");

/* The font an application names, HOST_FONT_TABULAR or not. */
static const gfx_font_t *font_by_id(int32_t font)
{
    return gfx_font((gfx_font_id_t)(font & ~HOST_FONT_TABULAR));
}

static bool is_tabular(int32_t font)
{
    return (font & HOST_FONT_TABULAR) != 0;
}

/* An offset and a length from the application, which is not trusted, as
 * a place in its memory; NULL when any of it lies outside. */
static const uint8_t *in_app_memory(IM3Runtime runtime, void *memory, uint32_t offset,
                                    int32_t length)
{
    uint32_t memory_size = m3_GetMemorySize(runtime);
    bool inside = length >= 0 && offset <= memory_size &&
                  (uint32_t)length <= memory_size - offset;
    return inside ? (const uint8_t *)memory + offset : NULL;
}

/* The same for memory the host is about to fill for the application. */
static uint8_t *in_app_memory_to_fill(IM3Runtime runtime, void *memory, uint32_t offset,
                                      int32_t capacity)
{
    return in_app_memory(runtime, memory, offset, capacity) != NULL ? (uint8_t *)memory + offset
                                                                    : NULL;
}

/* The application's bytes as a terminated string the drawing code takes.
 * False when they are not the application's to give. */
static bool copy_text(IM3Runtime runtime, void *memory, uint32_t offset, int32_t length,
                      char terminated[HOST_TEXT_MAX + 1])
{
    const uint8_t *text = in_app_memory(runtime, memory, offset, length);
    if (text == NULL) {
        return false;
    }
    if (length > HOST_TEXT_MAX) {
        length = HOST_TEXT_MAX;
    }
    memcpy(terminated, text, (size_t)length);
    terminated[length] = '\0';
    return true;
}

/* Far beyond any surface, and small enough that sums of two of them
 * cannot overflow: a coordinate or a size outside it is refused. */
#define COORDINATE_LIMIT 4096

static bool is_drawable(int32_t coordinate)
{
    return coordinate >= -COORDINATE_LIMIT && coordinate <= COORDINATE_LIMIT;
}

#define APP_OF(runtime) ((app_t *)m3_GetUserData(runtime))

/* An import the way wasm3 calls it, for the m3Api* macros to work on. Not
 * every import needs every one of the four arguments. */
#define MAYBE_UNUSED __attribute__((unused))
#define HOST_FUNCTION(name)                                                              \
    static const void *name(IM3Runtime runtime MAYBE_UNUSED, IM3ImportContext _ctx MAYBE_UNUSED, \
                            uint64_t *_sp MAYBE_UNUSED, void *_mem MAYBE_UNUSED)

HOST_FUNCTION(host_gfx_width)
{
    m3ApiReturnType(int32_t);
    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->surface != NULL ? app->surface->w : HOST_ERR_NOT_DRAWING);
}

HOST_FUNCTION(host_gfx_height)
{
    m3ApiReturnType(int32_t);
    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->surface != NULL ? app->surface->h : HOST_ERR_NOT_DRAWING);
}

HOST_FUNCTION(host_gfx_text)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, font_id);
    m3ApiGetArg(uint32_t, text);
    m3ApiGetArg(int32_t, length);

    app_t *app = APP_OF(runtime);
    const gfx_font_t *font = font_by_id(font_id);
    char terminated[HOST_TEXT_MAX + 1];
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    if (font == NULL || !is_drawable(x) || !is_drawable(y) ||
        !copy_text(runtime, _mem, text, length, terminated)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int baseline = y + gfx_font_ascent(font);
    m3ApiReturn(is_tabular(font_id)
                    ? gfx_text_tabular(app->surface, x, baseline, font, terminated, true)
                    : gfx_text_font(app->surface, x, baseline, font, terminated, true));
}

HOST_FUNCTION(host_gfx_text_width)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    m3ApiGetArg(uint32_t, text);
    m3ApiGetArg(int32_t, length);

    const gfx_font_t *font = font_by_id(font_id);
    char terminated[HOST_TEXT_MAX + 1];
    if (font == NULL || !copy_text(runtime, _mem, text, length, terminated)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(is_tabular(font_id) ? gfx_text_tabular_width(font, terminated)
                                    : gfx_text_font_width(font, terminated));
}

HOST_FUNCTION(host_gfx_text_fit)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    m3ApiGetArg(uint32_t, text);
    m3ApiGetArg(int32_t, length);
    m3ApiGetArg(int32_t, max_width);
    m3ApiGetArg(uint32_t, fitted_offset);
    m3ApiGetArg(int32_t, capacity);

    const gfx_font_t *font = font_by_id(font_id);
    char terminated[HOST_TEXT_MAX + 1];
    char *fitted = (char *)in_app_memory_to_fill(runtime, _mem, fitted_offset, capacity);
    if (font == NULL || fitted == NULL || !is_drawable(max_width) ||
        !copy_text(runtime, _mem, text, length, terminated)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    char cut[HOST_TEXT_MAX + 1];
    gfx_text_fit(font, terminated, max_width, cut, sizeof cut);
    int32_t cut_length = (int32_t)strlen(cut);
    if (cut_length > capacity) {
        m3ApiReturn(HOST_ERR_NO_SPACE);
    }
    memcpy(fitted, cut, (size_t)cut_length);
    m3ApiReturn(cut_length);
}

HOST_FUNCTION(host_gfx_font_height)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    const gfx_font_t *font = font_by_id(font_id);
    m3ApiReturn(font != NULL ? gfx_font_height(font) : HOST_ERR_ARGUMENT);
}

HOST_FUNCTION(host_gfx_font_ascent)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    const gfx_font_t *font = font_by_id(font_id);
    m3ApiReturn(font != NULL ? gfx_font_ascent(font) : HOST_ERR_ARGUMENT);
}

HOST_FUNCTION(host_gfx_font_cap_height)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    const gfx_font_t *font = font_by_id(font_id);
    m3ApiReturn(font != NULL ? gfx_font_cap_height(font) : HOST_ERR_ARGUMENT);
}

HOST_FUNCTION(host_gfx_rect)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, height);
    m3ApiGetArg(int32_t, fill);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    if (!is_drawable(x) || !is_drawable(y) || !is_drawable(width) || !is_drawable(height)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (fill == HOST_RECT_FILLED) {
        gfx_fill(app->surface, x, y, width, height, true);
    } else {
        gfx_frame(app->surface, x, y, width, height, 1, true);
    }
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_gfx_line)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x0);
    m3ApiGetArg(int32_t, y0);
    m3ApiGetArg(int32_t, x1);
    m3ApiGetArg(int32_t, y1);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    if (!is_drawable(x0) || !is_drawable(y0) || !is_drawable(x1) || !is_drawable(y1)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    gfx_line(app->surface, x0, y0, x1, y1, true);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_gfx_bitmap)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, height);
    m3ApiGetArg(uint32_t, bits_offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    /* Bounded by the surface first, so that the size below cannot overflow. */
    if (!is_drawable(x) || !is_drawable(y) || width <= 0 || height <= 0 ||
        width > app->surface->w || height > app->surface->h) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int32_t bytes_per_row = (width + 7) / 8;
    const uint8_t *bits = in_app_memory(runtime, _mem, bits_offset, bytes_per_row * height);
    if (bits == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    for (int32_t row = 0; row < height; row++) {
        for (int32_t column = 0; column < width; column++) {
            bool is_set = (bits[row * bytes_per_row + column / 8] & (0x80 >> (column % 8))) != 0;
            if (is_set) {
                gfx_pixel(app->surface, x + column, y + row, true);
            }
        }
    }
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_gfx_icon)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, icon);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, cell);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    if (!is_drawable(x) || !is_drawable(y) || cell < 1 || cell > HOST_ICON_CELL_MAX ||
        !gfx_icon(app->surface, (gfx_icon_id_t)icon, x, y, cell, true)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(HOST_OK);
}

static bool is_an_icon(int32_t icon)
{
    return icon >= 0 && icon < GFX_ICON_COUNT;
}

/* A list of more rows than this is somebody's mistake, not a list. */
#define LIST_ROWS_MAX 10000

/* The list as the application describes it, checked: false when it is not
 * the application's to give, or not a list. */
static bool read_list(IM3Runtime runtime, void *memory, uint32_t offset, list_t *list)
{
    const uint8_t *given = in_app_memory(runtime, memory, offset, sizeof(host_list_t));
    if (given == NULL) {
        return false;
    }
    host_list_t asked;
    memcpy(&asked, given, sizeof asked);
    bool is_a_list = asked.count >= 0 && asked.count <= LIST_ROWS_MAX && asked.selected >= -1 &&
                     asked.selected <= LIST_ROWS_MAX && asked.first >= -LIST_ROWS_MAX &&
                     asked.first <= LIST_ROWS_MAX && is_drawable(asked.top) && asked.top >= 0 &&
                     is_drawable(asked.height) && asked.height >= 0;
    *list = (list_t){ .count = asked.count, .selected = asked.selected, .top = asked.top,
                      .height = asked.height, .first = asked.first };
    return is_a_list;
}

HOST_FUNCTION(host_ui_list_visible)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, list_offset);
    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    list_t list;
    if (!read_list(runtime, _mem, list_offset, &list)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(list_visible_rows(app->surface, &list));
}

HOST_FUNCTION(host_ui_list_first)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, list_offset);
    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    list_t list;
    if (!read_list(runtime, _mem, list_offset, &list)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(list_first_row(app->surface, &list));
}

HOST_FUNCTION(host_ui_list_row)
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
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    list_t list;
    char label[HOST_TEXT_MAX + 1];
    char value[HOST_TEXT_MAX + 1];
    if (!read_list(runtime, _mem, list_offset, &list) ||
        (icon != HOST_ICON_NONE && !is_an_icon(icon)) ||
        !copy_text(runtime, _mem, label_offset, label_length, label) ||
        !copy_text(runtime, _mem, value_offset, value_length, value)) {
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
    text[size - 1] = '\0';
    return text[0] != '\0' ? text : NULL;
}

HOST_FUNCTION(host_ui_notice)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, notice_offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    const uint8_t *given = in_app_memory(runtime, _mem, notice_offset, sizeof(host_notice_t));
    if (given == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    host_notice_t asked;
    memcpy(&asked, given, sizeof asked);
    if ((asked.icon != HOST_ICON_NONE && !is_an_icon(asked.icon)) || asked.first[0] == '\0') {
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

HOST_FUNCTION(host_ui_text)
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
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    char text[HOST_TEXT_MAX + 1];
    if (!is_drawable(x) || !is_drawable(y) || style < HOST_UI_TEXT || style > HOST_UI_VALUE ||
        max_width < 0 || !is_drawable(max_width) ||
        !copy_text(runtime, _mem, text_offset, length, text)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(kit_text(app->surface, x, y, (kit_style_t)style, text, max_width));
}

HOST_FUNCTION(host_ui_text_width)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, style);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);

    char text[HOST_TEXT_MAX + 1];
    if (style < HOST_UI_TEXT || style > HOST_UI_VALUE ||
        !copy_text(runtime, _mem, text_offset, length, text)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(gfx_text_font_width(kit_font((kit_style_t)style), text));
}

HOST_FUNCTION(host_ui_text_height)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, style);
    if (style < HOST_UI_TEXT || style > HOST_UI_VALUE) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(gfx_font_height(kit_font((kit_style_t)style)));
}

HOST_FUNCTION(host_ui_title)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    char text[HOST_TEXT_MAX + 1];
    if (!copy_text(runtime, _mem, text_offset, length, text)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(kit_title(app->surface, text));
}

HOST_FUNCTION(host_ui_icon)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, icon);
    m3ApiGetArg(int32_t, size);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    if (!is_drawable(x) || !is_drawable(y) || !is_an_icon(icon) || size < HOST_UI_ICON_SMALL ||
        size > HOST_UI_ICON_LARGE) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    kit_icon(app->surface, x, y, icon, (kit_icon_size_t)size);
    m3ApiReturn(HOST_OK);
}

/* The name of a button is a word: "BACK" is the longest there is. */
#define BUTTON_NAME_MAX 8

HOST_FUNCTION(host_ui_button)
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
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    char name[HOST_TEXT_MAX + 1];
    char action[HOST_TEXT_MAX + 1];
    if (!is_drawable(x) || !is_drawable(y) || name_length < 1 || name_length > BUTTON_NAME_MAX ||
        !copy_text(runtime, _mem, name_offset, name_length, name) ||
        !copy_text(runtime, _mem, action_offset, action_length, action)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int baseline = y + gfx_font_ascent(kit_font(KIT_TEXT));
    m3ApiReturn(kit_button(app->surface, x, baseline, name, action, mirrored != 0));
}

HOST_FUNCTION(host_ui_actions)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, ok_offset);
    m3ApiGetArg(int32_t, ok_length);
    m3ApiGetArg(uint32_t, back_offset);
    m3ApiGetArg(int32_t, back_length);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    char ok[HOST_TEXT_MAX + 1];
    char back[HOST_TEXT_MAX + 1];
    if (!copy_text(runtime, _mem, ok_offset, ok_length, ok) ||
        !copy_text(runtime, _mem, back_offset, back_length, back)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    kit_actions(app->surface, ok[0] != '\0' ? ok : NULL, back[0] != '\0' ? back : NULL);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_ui_bar)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, value);
    m3ApiGetArg(int32_t, maximum);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    if (!is_drawable(x) || !is_drawable(y) || width < 1 || !is_drawable(width)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    kit_bar(app->surface, x, y, width, value, maximum);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_ui_frame)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, height);
    m3ApiGetArg(int32_t, arrows);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    if (!is_drawable(x) || !is_drawable(y) || width < 1 || !is_drawable(width) || height < 1 ||
        !is_drawable(height)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    kit_frame(app->surface, x, y, width, height, (arrows & HOST_UI_ARROW_UP) != 0,
              (arrows & HOST_UI_ARROW_DOWN) != 0);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_ui_picker)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, picker_offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    const uint8_t *given = in_app_memory(runtime, _mem, picker_offset, sizeof(host_picker_t));
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
    kit_picker(app->surface, &picker);
    m3ApiReturn(HOST_OK);
}

#define DATE_YEAR_MAX 9999

HOST_FUNCTION(host_ui_date)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, date_offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    const uint8_t *given = in_app_memory(runtime, _mem, date_offset, sizeof(host_date_t));
    if (given == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    host_date_t asked;
    memcpy(&asked, given, sizeof asked);
    bool is_a_date = asked.year >= 0 && asked.year <= DATE_YEAR_MAX && asked.month >= 1 &&
                     asked.month <= 12 && asked.day >= 1 && asked.day <= 31 && asked.hour >= 0 &&
                     asked.hour <= 23 && asked.minute >= 0 && asked.minute <= 59;
    bool has_any = (asked.has & (HOST_DATE_HAS_TIME | HOST_DATE_HAS_DATE)) != 0;
    if (!is_a_date || !has_any || asked.selected < HOST_DATE_HOUR ||
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
    kit_date(app->surface, &date);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_gfx_color)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, color);
    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    if (color < HOST_PAPER || color > HOST_INK) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    bool is_grey = color > HOST_PAPER && color < HOST_INK;
    if (is_grey && !app->services->dithers()) {
        color = color >= HOST_INK / 2 ? HOST_INK : HOST_PAPER;
    }
    app->surface->shade = (uint8_t)color;
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_data_value)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, capacity);

    app_t *app = APP_OF(runtime);
    uint8_t *text = in_app_memory_to_fill(runtime, _mem, text_offset, capacity);
    if (text == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    bool has_value = index >= 0 && index < app->value_count && app->values[index] != NULL;
    if (!has_value) {
        m3ApiReturn(HOST_ERR_NOT_FOUND);
    }
    size_t length = strlen(app->values[index]);
    if (length > (size_t)capacity) {
        m3ApiReturn(HOST_ERR_NO_SPACE);
    }
    memcpy(text, app->values[index], length);
    m3ApiReturn((int32_t)length);
}

HOST_FUNCTION(host_data_response)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, response_offset);

    app_t *app = APP_OF(runtime);
    host_response_t *response = (host_response_t *)in_app_memory_to_fill(
        runtime, _mem, response_offset, (int32_t)sizeof *response);
    if (response == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (!app->is_in_answer) {
        m3ApiReturn(HOST_ERR_NOT_FOUND);
    }
    if (app->response != NULL) {
        memcpy(response, app->response, sizeof *response);
    } else {
        memset(response, 0, sizeof *response);
        response->length = -1;
    }
    response->status = app->response_status;
    m3ApiReturn(HOST_OK);
}

/* The largest code an application gets: 57 modules a side. Both buffers
 * of the generator are on the host's stack while it works. */
#define QR_VERSION_MAX 10
#define QR_CELL_MAX    16

HOST_FUNCTION(host_gfx_qr)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, cell);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    const uint8_t *text = in_app_memory(runtime, _mem, text_offset, length);
    app_t *app = APP_OF(runtime);
    if (text == NULL || length > HOST_QR_TEXT_MAX || cell < 0 || cell > QR_CELL_MAX ||
        !is_drawable(x) || !is_drawable(y)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (cell > 0 && app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    char terminated[HOST_QR_TEXT_MAX + 1];
    memcpy(terminated, text, (size_t)length);
    terminated[length] = '\0';
    uint8_t modules[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];
    uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];
    if (!qrcodegen_encodeText(terminated, scratch, modules, qrcodegen_Ecc_MEDIUM,
                              qrcodegen_VERSION_MIN, QR_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
        m3ApiReturn(HOST_ERR_NO_SPACE);
    }
    int size = qrcodegen_getSize(modules);
    for (int row = 0; cell > 0 && row < size; row++) {
        /* A run of dark modules in a row is one rectangle. */
        for (int column = 0; column < size;) {
            if (!qrcodegen_getModule(modules, column, row)) {
                column++;
                continue;
            }
            int run_start = column;
            while (column < size && qrcodegen_getModule(modules, column, row)) {
                column++;
            }
            gfx_fill(app->surface, x + run_start * cell, y + row * cell,
                     (column - run_start) * cell, cell, true);
        }
    }
    m3ApiReturn(size);
}

HOST_FUNCTION(host_request_redraw)
{
    m3ApiGetArg(int32_t, flags);
    app_t *app = APP_OF(runtime);
    app->redraw_requested = true;
    /* Bits the host does not know are an application from the future. */
    app->redraw_flags |= flags & (HOST_REDRAW_SOON | HOST_REDRAW_CLEAN | HOST_REDRAW_NO_BLINK);
    m3ApiSuccess();
}

HOST_FUNCTION(host_header_text)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, side);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    const uint8_t *text = in_app_memory(runtime, _mem, text_offset, length);
    if ((side != HOST_HEADER_LEFT && side != HOST_HEADER_RIGHT) || text == NULL ||
        length > HOST_HEADER_TEXT_MAX) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    app_t *app = APP_OF(runtime);
    memcpy(app->header[side], text, (size_t)length);
    app->header[side][length] = '\0';
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_header_status)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, side);
    m3ApiGetArg(int32_t, icon);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    app_t *app = APP_OF(runtime);
    char text[HOST_TEXT_MAX + 1];
    bool is_an_icon = icon == HOST_ICON_NONE || (icon >= 0 && icon < GFX_ICON_COUNT);
    if ((side != HOST_HEADER_LEFT && side != HOST_HEADER_RIGHT) || !is_an_icon ||
        length > HOST_STATUS_TEXT_MAX ||
        !copy_text(runtime, _mem, text_offset, length, text)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (app->services->header_status != NULL) {
        app->services->header_status(app->name, side, icon, text);
    }
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_request_focus)
{
    APP_OF(runtime)->focus_requested = true;
    m3ApiSuccess();
}

HOST_FUNCTION(host_request_exit)
{
    APP_OF(runtime)->exit_requested = true;
    m3ApiSuccess();
}

HOST_FUNCTION(host_timer_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int32_t, milliseconds);
    if (id < 0 || id >= HOST_TIMER_COUNT || milliseconds < 0) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    APP_OF(runtime)->services->timer_set(id, milliseconds);
    m3ApiReturn(HOST_OK);
}

/* The key as a terminated string; false when it is not a key at all. */
static bool copy_key(IM3Runtime runtime, void *memory, uint32_t offset, int32_t length,
                     char key[HOST_KV_KEY_MAX + 1])
{
    const uint8_t *bytes = in_app_memory(runtime, memory, offset, length);
    if (bytes == NULL || length < 1 || length > HOST_KV_KEY_MAX) {
        return false;
    }
    for (int32_t i = 0; i < length; i++) {
        bool allowed = bytes[i] >= '!' && bytes[i] <= '~';
        if (!allowed) {
            return false;
        }
        key[i] = (char)bytes[i];
    }
    key[length] = '\0';
    return true;
}

HOST_FUNCTION(host_kv_get)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, key_offset);
    m3ApiGetArg(int32_t, key_length);
    m3ApiGetArg(uint32_t, value_offset);
    m3ApiGetArg(int32_t, capacity);

    app_t *app = APP_OF(runtime);
    char key[HOST_KV_KEY_MAX + 1];
    uint8_t *value = in_app_memory_to_fill(runtime, _mem, value_offset, capacity);
    if (value == NULL || !copy_key(runtime, _mem, key_offset, key_length, key)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->kv_get(app->name, key, value, capacity));
}

HOST_FUNCTION(host_kv_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, key_offset);
    m3ApiGetArg(int32_t, key_length);
    m3ApiGetArg(uint32_t, value_offset);
    m3ApiGetArg(int32_t, length);

    app_t *app = APP_OF(runtime);
    char key[HOST_KV_KEY_MAX + 1];
    const uint8_t *value = in_app_memory(runtime, _mem, value_offset, length);
    if (value == NULL || length > HOST_KV_VALUE_MAX ||
        !copy_key(runtime, _mem, key_offset, key_length, key)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->kv_set(app->name, key, value, length));
}

/* A file's name from the application, terminated; false when it is not
 * the application's memory or not a name a file can have. */
static bool copy_file_name(IM3Runtime runtime, void *memory, uint32_t offset, int32_t length,
                           char name[HOST_FILE_NAME_MAX + 1])
{
    const uint8_t *text = length >= 1 && length <= HOST_FILE_NAME_MAX
                              ? in_app_memory(runtime, memory, offset, length)
                              : NULL;
    if (text == NULL) {
        return false;
    }
    for (int32_t i = 0; i < length; i++) {
        char character = (char)text[i];
        bool allowed = (character >= 'a' && character <= 'z') ||
                       (character >= '0' && character <= '9') || character == '-' ||
                       character == '_' || (character == '.' && i > 0);
        if (!allowed) {
            return false;
        }
        name[i] = character;
    }
    name[length] = '\0';
    return true;
}

HOST_FUNCTION(host_file_size)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, name_offset);
    m3ApiGetArg(int32_t, name_length);
    app_t *app = APP_OF(runtime);
    char name[HOST_FILE_NAME_MAX + 1];
    if (!copy_file_name(runtime, _mem, name_offset, name_length, name)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->file_size(app->name, name));
}

HOST_FUNCTION(host_file_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, name_offset);
    m3ApiGetArg(int32_t, name_length);
    m3ApiGetArg(int32_t, offset);
    m3ApiGetArg(uint32_t, bytes_offset);
    m3ApiGetArg(int32_t, capacity);
    app_t *app = APP_OF(runtime);
    char name[HOST_FILE_NAME_MAX + 1];
    uint8_t *bytes = in_app_memory_to_fill(runtime, _mem, bytes_offset, capacity);
    if (bytes == NULL || offset < 0 ||
        !copy_file_name(runtime, _mem, name_offset, name_length, name)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->file_read(app->name, name, offset, bytes, capacity));
}

/* A picture is read from its file a few rows at a time: whole, it would
 * not fit the application's hands, and the host has no use for a copy. */
#define PICTURE_CHUNK_BYTES 512

HOST_FUNCTION(host_gfx_bitmap_file)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, height);
    m3ApiGetArg(uint32_t, name_offset);
    m3ApiGetArg(int32_t, name_length);
    m3ApiGetArg(int32_t, offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    char name[HOST_FILE_NAME_MAX + 1];
    if (!is_drawable(x) || !is_drawable(y) || width <= 0 || height <= 0 ||
        width > app->surface->w || height > app->surface->h || offset < 0 ||
        !copy_file_name(runtime, _mem, name_offset, name_length, name)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int32_t bytes_per_row = (width + 7) / 8;
    int32_t rows_per_chunk = PICTURE_CHUNK_BYTES / bytes_per_row;
    uint8_t rows[PICTURE_CHUNK_BYTES];
    for (int32_t row = 0; row < height; row += rows_per_chunk) {
        int32_t row_count = height - row < rows_per_chunk ? height - row : rows_per_chunk;
        int32_t wanted = row_count * bytes_per_row;
        if (app->services->file_read(app->name, name, offset + row * bytes_per_row, rows,
                                     wanted) != wanted) {
            /* No such file, or it ends before the picture does. */
            m3ApiReturn(HOST_ERR_NOT_FOUND);
        }
        for (int32_t chunk_row = 0; chunk_row < row_count; chunk_row++) {
            const uint8_t *bits = rows + chunk_row * bytes_per_row;
            for (int32_t column = 0; column < width; column++) {
                if ((bits[column / 8] & (0x80 >> (column % 8))) != 0) {
                    gfx_pixel(app->surface, x + column, y + row + chunk_row, true);
                }
            }
        }
    }
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_poll_now)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, poll);
    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->services->poll_now(app->name, poll) ? HOST_OK : HOST_ERR_ARGUMENT);
}

/* "Name: value" lines into headers; false when a line is not that or
 * there are too many. */
static bool parse_headers(const char *lines, int32_t length, app_request_t *request)
{
    request->header_count = 0;
    int32_t at = 0;
    while (at < length) {
        int32_t line_end = at;
        while (line_end < length && lines[line_end] != '\n') {
            line_end++;
        }
        int32_t colon = at;
        while (colon < line_end && lines[colon] != ':') {
            colon++;
        }
        int32_t value_start = colon + 1;
        while (value_start < line_end && lines[value_start] == ' ') {
            value_start++;
        }
        int32_t name_length = colon - at, value_length = line_end - value_start;
        if (line_end > at) { /* an empty line is skipped */
            if (colon == line_end || name_length < 1 || name_length > APP_HEADER_NAME_MAX ||
                value_length > APP_HEADER_VALUE_MAX || request->header_count == APP_HEADERS_MAX) {
                return false;
            }
            app_header_t *header = &request->headers[request->header_count++];
            memcpy(header->name, lines + at, (size_t)name_length);
            header->name[name_length] = '\0';
            memcpy(header->value, lines + value_start, (size_t)value_length);
            header->value[value_length] = '\0';
        }
        at = line_end + 1;
    }
    return true;
}

/* Text an application gives for a request: no control characters, which
 * in a URL or a header would be the start of something else. */
static bool is_request_text(const uint8_t *text, int32_t length, bool allow_line_breaks)
{
    for (int32_t i = 0; i < length; i++) {
        bool is_line_break = text[i] == '\n';
        if ((text[i] < ' ' || text[i] > '~') && !(allow_line_breaks && is_line_break)) {
            return false;
        }
    }
    return true;
}

HOST_FUNCTION(host_http_request)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, tag);
    m3ApiGetArg(int32_t, method);
    m3ApiGetArg(uint32_t, url_offset);
    m3ApiGetArg(int32_t, url_length);
    m3ApiGetArg(uint32_t, headers_offset);
    m3ApiGetArg(int32_t, headers_length);
    m3ApiGetArg(uint32_t, body_offset);
    m3ApiGetArg(int32_t, body_length);

    app_t *app = APP_OF(runtime);
    const uint8_t *url = in_app_memory(runtime, _mem, url_offset, url_length);
    const uint8_t *headers = in_app_memory(runtime, _mem, headers_offset, headers_length);
    const uint8_t *body = in_app_memory(runtime, _mem, body_offset, body_length);
    bool is_sound = url != NULL && headers != NULL && body != NULL && url_length >= 1 &&
                    url_length <= APP_URL_MAX && headers_length <= HOST_HTTP_HEADERS_MAX &&
                    body_length <= HOST_HTTP_BODY_MAX && method >= HOST_HTTP_GET &&
                    method <= HOST_HTTP_METHOD_LAST && tag >= HOST_REQUEST_FIRST &&
                    tag <= HOST_REQUEST_LAST && is_request_text(url, url_length, false) &&
                    is_request_text(headers, headers_length, true);
    /* Too large for the stack an application runs on. */
    app_request_t *request = is_sound ? malloc(sizeof *request) : NULL;
    if (request == NULL) {
        m3ApiReturn(is_sound ? HOST_ERR_NO_SPACE : HOST_ERR_ARGUMENT);
    }
    int32_t result = HOST_ERR_ARGUMENT;
    if (parse_headers((const char *)headers, headers_length, request)) {
        request->tag = tag;
        request->method = method;
        memcpy(request->url, url, (size_t)url_length);
        request->url[url_length] = '\0';
        request->body = body;
        request->body_length = body_length;
        result = app->services->http_request(app->name, request);
    }
    free(request);
    m3ApiReturn(result);
}

HOST_FUNCTION(host_poll_set_interval)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, poll);
    m3ApiGetArg(int32_t, seconds);
    app_t *app = APP_OF(runtime);
    bool changed = seconds > 0 && app->services->poll_set_interval(app->name, poll,
                                                                  (uint32_t)seconds);
    m3ApiReturn(changed ? HOST_OK : HOST_ERR_ARGUMENT);
}

HOST_FUNCTION(host_alarm_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int32_t, seconds);
    app_t *app = APP_OF(runtime);
    if (id < 0 || id >= HOST_ALARM_COUNT || seconds < 0) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->alarm_set(app->name, id, seconds, false) ? HOST_OK
                                                                         : HOST_ERR_NO_SPACE);
}

HOST_FUNCTION(host_alarm_set_every)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int32_t, seconds);
    m3ApiGetArg(int32_t, first_in);
    app_t *app = APP_OF(runtime);
    if (id < 0 || id >= HOST_ALARM_COUNT || seconds < HOST_ALARM_EVERY_MIN ||
        seconds > HOST_ALARM_EVERY_MAX || first_in < 0 || first_in > HOST_ALARM_EVERY_MAX ||
        app->services->alarm_set_every == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (first_in == 0) {
        first_in = seconds;
    }
    m3ApiReturn(app->services->alarm_set_every(app->name, id, seconds, first_in)
                    ? HOST_OK
                    : HOST_ERR_NO_SPACE);
}

HOST_FUNCTION(host_alarm_set_at)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int64_t, unix_time);
    app_t *app = APP_OF(runtime);
    if (id < 0 || id >= HOST_ALARM_COUNT || unix_time <= 0) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->alarm_set(app->name, id, unix_time, true) ? HOST_OK
                                                                           : HOST_ERR_NO_SPACE);
}

HOST_FUNCTION(host_file_write)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, name_offset);
    m3ApiGetArg(int32_t, name_length);
    m3ApiGetArg(int32_t, offset);
    m3ApiGetArg(uint32_t, bytes_offset);
    m3ApiGetArg(int32_t, length);
    app_t *app = APP_OF(runtime);
    char name[HOST_FILE_NAME_MAX + 1];
    const uint8_t *bytes = in_app_memory(runtime, _mem, bytes_offset, length);
    if (bytes == NULL || offset < 0 ||
        !copy_file_name(runtime, _mem, name_offset, name_length, name)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->file_write(app->name, name, offset, bytes, length));
}

HOST_FUNCTION(host_time_now)
{
    m3ApiReturnType(int64_t);
    m3ApiReturn(APP_OF(runtime)->services->unix_time());
}

HOST_FUNCTION(host_time_zone_offset)
{
    m3ApiReturnType(int32_t);
    m3ApiReturn(APP_OF(runtime)->services->time_zone_offset());
}

#define SECONDS_PER_DAY 86400

/* Days from 1970-01-01 to a date of the calendar; any year, any month
 * number - the thirteenth month is next January. */
static int64_t days_from_civil(int64_t year, int64_t month, int64_t day)
{
    year += (month - 1) / 12;
    month = (month - 1) % 12 + 1;
    if (month < 1) {
        month += 12;
        year -= 1;
    }
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    int64_t year_of_era = year - era * 400;
    int64_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

/* The moment as the user's clock and calendar show it. */
static void take_apart(const app_t *app, int64_t utc_seconds, host_local_time_t *parts_out)
{
    /* Local time is UTC moved by the offset, so the calendar of UTC reads it. */
    time_t local_seconds = (time_t)(utc_seconds + app->services->time_zone_offset_at(utc_seconds));
    struct tm parts;
    gmtime_r(&local_seconds, &parts);
    *parts_out = (host_local_time_t){
        .year = parts.tm_year + 1900,
        .month = parts.tm_mon + 1,
        .day = parts.tm_mday,
        .weekday = parts.tm_wday,
        .hour = parts.tm_hour,
        .minute = parts.tm_min,
        .second = parts.tm_sec,
    };
}

static int32_t local_time_at(IM3Runtime runtime, void *memory, uint32_t parts_offset,
                             int64_t utc_seconds)
{
    host_local_time_t *parts = (host_local_time_t *)in_app_memory_to_fill(
        runtime, memory, parts_offset, (int32_t)sizeof(host_local_time_t));
    if (parts == NULL) {
        return HOST_ERR_ARGUMENT;
    }
    /* The application's memory is not aligned for anybody: copy, do not assign. */
    host_local_time_t taken_apart;
    take_apart(APP_OF(runtime), utc_seconds, &taken_apart);
    memcpy(parts, &taken_apart, sizeof taken_apart);
    return HOST_OK;
}

HOST_FUNCTION(host_time_local)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, now_offset);
    int64_t utc_seconds = APP_OF(runtime)->services->unix_time();
    if (utc_seconds == 0) {
        m3ApiReturn(in_app_memory_to_fill(runtime, _mem, now_offset,
                                          (int32_t)sizeof(host_local_time_t)) != NULL
                        ? HOST_ERR_NOT_FOUND
                        : HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(local_time_at(runtime, _mem, now_offset, utc_seconds));
}

HOST_FUNCTION(host_time_local_at)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int64_t, unix_time);
    m3ApiGetArg(uint32_t, parts_offset);
    m3ApiReturn(local_time_at(runtime, _mem, parts_offset, unix_time));
}

HOST_FUNCTION(host_time_from_local)
{
    m3ApiReturnType(int64_t);
    m3ApiGetArg(uint32_t, parts_offset);
    const uint8_t *given = in_app_memory(runtime, _mem, parts_offset,
                                         (int32_t)sizeof(host_local_time_t));
    if (given == NULL) {
        m3ApiReturn(0);
    }
    host_local_time_t parts;
    memcpy(&parts, given, sizeof parts);
    app_t *app = APP_OF(runtime);
    /* The date and time as if they were UTC; the moment is that less the
     * zone's offset - the offset at the moment itself, which a second look
     * finds when the clocks changed in between. */
    int64_t as_utc = days_from_civil(parts.year, parts.month, parts.day) * SECONDS_PER_DAY +
                     (int64_t)parts.hour * 3600 + (int64_t)parts.minute * 60 + parts.second;
    int64_t moment = as_utc - app->services->time_zone_offset_at(as_utc);
    moment = as_utc - app->services->time_zone_offset_at(moment);
    m3ApiReturn(moment);
}

HOST_FUNCTION(host_time_uptime_ms)
{
    m3ApiReturnType(int64_t);
    m3ApiReturn(APP_OF(runtime)->services->uptime_ms());
}

HOST_FUNCTION(host_time_boot_id)
{
    m3ApiReturnType(uint32_t);
    const app_services_t *services = APP_OF(runtime)->services;
    m3ApiReturn(services->boot_id != NULL ? services->boot_id() : 0);
}

HOST_FUNCTION(host_log)
{
    m3ApiGetArg(int32_t, level);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    const uint8_t *text = in_app_memory(runtime, _mem, text_offset, length);
    if (text != NULL) {
        APP_OF(runtime)->services->log(level, (const char *)text, length);
    }
    m3ApiSuccess();
}

/* ---- the system API ---- */

#define LOGO_WORD "perINKpheraL"

HOST_FUNCTION(host_sys_info)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, what);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, capacity);
    uint8_t *text = in_app_memory_to_fill(runtime, _mem, text_offset, capacity);
    app_t *app = APP_OF(runtime);
    if (text == NULL || app->services->system_info == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_info(what, (char *)text, capacity));
}



HOST_FUNCTION(host_sys_number_get)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, what);
    m3ApiGetArg(uint32_t, number_offset);
    uint8_t *number = in_app_memory_to_fill(runtime, _mem, number_offset, sizeof(sys_number_t));
    app_t *app = APP_OF(runtime);
    if (number == NULL || app->services->system_number_get == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    sys_number_t read = { 0 };
    int32_t result = app->services->system_number_get(what, &read.value, &read.least, &read.most);
    if (result == HOST_OK) {
        memcpy(number, &read, sizeof read);
    }
    m3ApiReturn(result);
}

HOST_FUNCTION(host_sys_number_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, what);
    m3ApiGetArg(int32_t, value);
    app_t *app = APP_OF(runtime);
    if (app->services->system_number_set == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_number_set(what, value));
}

HOST_FUNCTION(host_sys_text_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, what);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    const uint8_t *text = in_app_memory(runtime, _mem, text_offset, length);
    app_t *app = APP_OF(runtime);
    if (text == NULL || length > SYS_TEXT_MAX || app->services->system_text_set == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    char terminated[SYS_TEXT_MAX + 1];
    memcpy(terminated, text, (size_t)length);
    terminated[length] = '\0';
    m3ApiReturn(app->services->system_text_set(what, terminated));
}

HOST_FUNCTION(host_sys_time_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int64_t, unix_seconds);
    app_t *app = APP_OF(runtime);
    if (app->services->system_time_set == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_time_set(unix_seconds));
}

HOST_FUNCTION(host_sys_app_list)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, ids_offset);
    m3ApiGetArg(int32_t, capacity);
    app_t *app = APP_OF(runtime);
    /* Checked before it is multiplied: a capacity cannot wrap round. */
    if (capacity < 0 || capacity > SYS_APP_LIST_MAX ||
        app->services->system_app_list == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    uint8_t *ids = in_app_memory_to_fill(runtime, _mem, ids_offset,
                                         capacity * (int32_t)sizeof(uint32_t));
    if (ids == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    uint32_t listed[SYS_APP_LIST_MAX];
    int32_t  count = app->services->system_app_list(listed, capacity);
    memcpy(ids, listed, (size_t)(count < capacity ? count : capacity) * sizeof(uint32_t));
    m3ApiReturn(count);
}

HOST_FUNCTION(host_sys_app_title)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, index);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, capacity);
    uint8_t *text = in_app_memory_to_fill(runtime, _mem, text_offset, capacity);
    app_t *app = APP_OF(runtime);
    if (text == NULL || app->services->system_app_title == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_app_title(index, (char *)text, capacity));
}

HOST_FUNCTION(host_sys_app_remove)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, index);
    app_t *app = APP_OF(runtime);
    if (app->services->system_app_remove == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_app_remove(index));
}

HOST_FUNCTION(host_sys_app_default)
{
    m3ApiReturnType(uint32_t);
    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->services->system_app_default != NULL ? app->services->system_app_default()
                                                          : 0);
}

HOST_FUNCTION(host_sys_app_set_default)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, id);
    app_t *app = APP_OF(runtime);
    if (app->services->system_app_set_default == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_app_set_default(id));
}

HOST_FUNCTION(host_sys_do)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, action);
    app_t *app = APP_OF(runtime);
    if (app->services->system_do == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(app->services->system_do(action));
}

HOST_FUNCTION(host_sys_logo)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, scale);
    if (scale < 1 || scale > SYS_LOGO_SCALE_MAX || !is_drawable(x) || !is_drawable(y)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(HOST_ERR_NOT_DRAWING);
    }
    /* One stroke between the letters, as on the splash. */
    gfx_text_sq_ex(app->surface, x, y, LOGO_WORD, scale, true, scale);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(host_sys_logo_width)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, scale);
    if (scale < 1 || scale > SYS_LOGO_SCALE_MAX) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(gfx_text_sq_width_ex(LOGO_WORD, scale, scale));
}

HOST_FUNCTION(host_sys_logo_height)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, scale);
    if (scale < 1 || scale > SYS_LOGO_SCALE_MAX) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(gfx_text_sq_height(scale));
}

/* wasm3 signatures: the result, then the arguments. v nothing, i i32,
 * I i64, * a pointer into the application's memory. */
static const struct {
    const char *name;
    const char *signature;
    M3RawCall   function;
} HOST_API[] = {
    { "gfx_width", "i()", host_gfx_width },
    { "gfx_height", "i()", host_gfx_height },
    { "gfx_text", "i(iii*i)", host_gfx_text },
    { "gfx_text_width", "i(i*i)", host_gfx_text_width },
    { "gfx_font_height", "i(i)", host_gfx_font_height },
    { "gfx_font_ascent", "i(i)", host_gfx_font_ascent },
    { "gfx_font_cap_height", "i(i)", host_gfx_font_cap_height },
    { "gfx_rect", "i(iiiii)", host_gfx_rect },
    { "gfx_line", "i(iiii)", host_gfx_line },
    { "gfx_bitmap", "i(iiii*)", host_gfx_bitmap },
    { "gfx_color", "i(i)", host_gfx_color },
    { "data_value", "i(iii)", host_data_value },
    { "data_response", "i(i)", host_data_response },
    { "gfx_qr", "i(iii*i)", host_gfx_qr },
    { "gfx_icon", "i(iiii)", host_gfx_icon },
    { "ui_list_visible", "i(*)", host_ui_list_visible },
    { "ui_list_first", "i(*)", host_ui_list_first },
    { "ui_list_row", "i(*i*i*ii)", host_ui_list_row },
    { "ui_notice", "i(*)", host_ui_notice },
    { "ui_text", "i(iii*ii)", host_ui_text },
    { "ui_text_width", "i(i*i)", host_ui_text_width },
    { "ui_text_height", "i(i)", host_ui_text_height },
    { "ui_title", "i(*i)", host_ui_title },
    { "ui_icon", "i(iiii)", host_ui_icon },
    { "ui_button", "i(ii*i*ii)", host_ui_button },
    { "ui_actions", "i(*i*i)", host_ui_actions },
    { "ui_bar", "i(iiiii)", host_ui_bar },
    { "ui_frame", "i(iiiii)", host_ui_frame },
    { "ui_picker", "i(*)", host_ui_picker },
    { "ui_date", "i(*)", host_ui_date },
    { "gfx_text_fit", "i(i*ii*i)", host_gfx_text_fit },
    { "gfx_bitmap_file", "i(iiii*ii)", host_gfx_bitmap_file },
    { "request_redraw", "v(i)", host_request_redraw },
    { "request_focus", "v()", host_request_focus },
    { "request_exit", "v()", host_request_exit },
    { "header_text", "i(i*i)", host_header_text },
    { "timer_set", "i(ii)", host_timer_set },
    { "kv_get", "i(*i*i)", host_kv_get },
    { "kv_set", "i(*i*i)", host_kv_set },
    { "file_size", "i(ii)", host_file_size },
    { "file_read", "i(iiiii)", host_file_read },
    { "poll_now", "i(i)", host_poll_now },
    { "http_request", "i(iiiiiiii)", host_http_request },
    { "poll_set_interval", "i(ii)", host_poll_set_interval },
    { "alarm_set", "i(ii)", host_alarm_set },
    { "alarm_set_at", "i(iI)", host_alarm_set_at },
    { "alarm_set_every", "i(iii)", host_alarm_set_every },
    { "header_status", "i(ii*i)", host_header_status },
    { "file_write", "i(iiiii)", host_file_write },
    { "time_now", "I()", host_time_now },
    { "time_zone_offset", "i()", host_time_zone_offset },
    { "time_local", "i(i)", host_time_local },
    { "time_local_at", "i(Ii)", host_time_local_at },
    { "time_from_local", "I(i)", host_time_from_local },
    { "time_uptime_ms", "I()", host_time_uptime_ms },
    { "time_boot_id", "i()", host_time_boot_id },
    { "log", "v(i*i)", host_log },
};
#define HOST_API_COUNT (sizeof HOST_API / sizeof HOST_API[0])

/* The system API: for an application whose manifest asked for it. */
static const struct {
    const char *name;
    const char *signature;
    M3RawCall   function;
} SYSTEM_API[] = {
    { "sys_info", "i(i*i)", host_sys_info },
    { "sys_number_get", "i(i*)", host_sys_number_get },
    { "sys_number_set", "i(ii)", host_sys_number_set },
    { "sys_text_set", "i(i*i)", host_sys_text_set },
    { "sys_time_set", "i(I)", host_sys_time_set },
    { "sys_app_list", "i(*i)", host_sys_app_list },
    { "sys_app_title", "i(i*i)", host_sys_app_title },
    { "sys_app_remove", "i(i)", host_sys_app_remove },
    { "sys_app_default", "i()", host_sys_app_default },
    { "sys_app_set_default", "i(i)", host_sys_app_set_default },
    { "sys_do", "i(i)", host_sys_do },
    { "sys_logo", "i(iii)", host_sys_logo },
    { "sys_logo_width", "i(i)", host_sys_logo_width },
    { "sys_logo_height", "i(i)", host_sys_logo_height },
};
#define SYSTEM_API_COUNT (sizeof SYSTEM_API / sizeof SYSTEM_API[0])

static bool is_offered(const uint8_t *module_name, uint32_t module_length,
                       const uint8_t *field_name, uint32_t field_length, bool with_system)
{
    if (module_length != strlen(HOST_MODULE) ||
        memcmp(module_name, HOST_MODULE, module_length) != 0) {
        return false;
    }
    for (size_t i = 0; i < HOST_API_COUNT; i++) {
        if (field_length == strlen(HOST_API[i].name) &&
            memcmp(field_name, HOST_API[i].name, field_length) == 0) {
            return true;
        }
    }
    for (size_t i = 0; with_system && i < SYSTEM_API_COUNT; i++) {
        if (field_length == strlen(SYSTEM_API[i].name) &&
            memcmp(field_name, SYSTEM_API[i].name, field_length) == 0) {
            return true;
        }
    }
    return false;
}

/* A reader over the module's bytes that cannot run off their end: past
 * it everything reads as zero and `failed` is set. */
typedef struct {
    const uint8_t *next;
    const uint8_t *end;
    bool           failed;
} wasm_reader_t;

static uint8_t read_byte(wasm_reader_t *reader)
{
    if (reader->next >= reader->end) {
        reader->failed = true;
        return 0;
    }
    return *reader->next++;
}

#define LEB_PAYLOAD_BITS 7
#define LEB_MORE_FLAG    0x80
#define LEB_U32_MAX_BYTES 5

static uint32_t read_leb_u32(wasm_reader_t *reader)
{
    uint32_t value = 0;
    for (int index = 0; index < LEB_U32_MAX_BYTES; index++) {
        uint8_t byte = read_byte(reader);
        value |= (uint32_t)(byte & ~LEB_MORE_FLAG) << (index * LEB_PAYLOAD_BITS);
        if ((byte & LEB_MORE_FLAG) == 0) {
            return value;
        }
    }
    reader->failed = true;
    return 0;
}

/* A length-prefixed name; NULL when it does not fit in what is left. */
static const uint8_t *read_name(wasm_reader_t *reader, uint32_t *length)
{
    *length = read_leb_u32(reader);
    if (reader->failed || *length > (size_t)(reader->end - reader->next)) {
        reader->failed = true;
        return NULL;
    }
    const uint8_t *name = reader->next;
    reader->next += *length;
    return name;
}

#define WASM_HEADER_BYTES    8
#define WASM_SECTION_IMPORT  2
#define WASM_IMPORT_FUNCTION 0

/* Whether everything the module imports is a function of the host API.
 * The interpreter would load a module that imports something else and
 * fail only when it is called. APP_OK, or why not: APP_ERR_OTHER_API for a
 * call of the host's own that this host does not have - a module made
 * for another version of it - and APP_ERR_BAD_MODULE for anything else. */
static app_load_result_t check_imports(const uint8_t *wasm, size_t wasm_size, bool with_system)
{
    if (wasm_size < WASM_HEADER_BYTES) {
        return APP_ERR_BAD_MODULE;
    }
    wasm_reader_t reader = { wasm + WASM_HEADER_BYTES, wasm + wasm_size, false };
    while (reader.next < reader.end) {
        uint8_t section = read_byte(&reader);
        uint32_t section_size = read_leb_u32(&reader);
        if (reader.failed || section_size > (size_t)(reader.end - reader.next)) {
            return APP_ERR_BAD_MODULE;
        }
        if (section != WASM_SECTION_IMPORT) {
            reader.next += section_size;
            continue;
        }
        uint32_t import_count = read_leb_u32(&reader);
        for (uint32_t i = 0; i < import_count && !reader.failed; i++) {
            uint32_t module_length, field_length;
            const uint8_t *module_name = read_name(&reader, &module_length);
            const uint8_t *field_name = read_name(&reader, &field_length);
            uint8_t kind = read_byte(&reader);
            read_leb_u32(&reader); /* the function's type */
            if (reader.failed || kind != WASM_IMPORT_FUNCTION) {
                return APP_ERR_BAD_MODULE;
            }
            if (is_offered(module_name, module_length, field_name, field_length, with_system)) {
                continue;
            }
            /* The system API without having asked for it is not a matter
             * of versions; neither is another module's function. */
            bool is_the_hosts = module_length == sizeof HOST_MODULE - 1 &&
                                memcmp(module_name, HOST_MODULE, module_length) == 0;
            bool is_system = is_offered(module_name, module_length, field_name, field_length,
                                        true);
            return is_the_hosts && !is_system ? APP_ERR_OTHER_API : APP_ERR_BAD_MODULE;
        }
        return reader.failed ? APP_ERR_BAD_MODULE : APP_OK;
    }
    return APP_OK;
}

/* False when the module declares one of these with another signature. A
 * function the module does not import is simply not linked. */
static bool link_host_api(IM3Module module)
{
    for (size_t i = 0; i < HOST_API_COUNT; i++) {
        M3Result result = m3_LinkRawFunction(module, HOST_MODULE, HOST_API[i].name,
                                             HOST_API[i].signature, HOST_API[i].function);
        if (result != m3Err_none && result != m3Err_functionLookupFailed) {
            return false;
        }
    }
    /* Who may import these was settled before the module was parsed. */
    for (size_t i = 0; i < SYSTEM_API_COUNT; i++) {
        M3Result result = m3_LinkRawFunction(module, HOST_MODULE, SYSTEM_API[i].name,
                                             SYSTEM_API[i].signature, SYSTEM_API[i].function);
        if (result != m3Err_none && result != m3Err_functionLookupFailed) {
            return false;
        }
    }
    return true;
}

/* ---- lifecycle ---- */

static IM3Function find_export(app_t *app, const char *name)
{
    IM3Function function = NULL;
    return m3_FindFunction(&function, app->runtime, name) == m3Err_none ? function : NULL;
}

/* The one place a handler's result is looked at: a trap stops the
 * application. */
static void note_result(app_t *app, M3Result result)
{
    if (result != m3Err_none) {
        app->stop_reason = result;
        app->redraw_requested = true;
    }
}

#define CALL_HANDLER(app, handler, ...)                                  \
    do {                                                                 \
        if ((app)->stop_reason == NULL && (handler) != NULL) {           \
            start_budget((app)->services);                               \
            note_result((app), m3_CallV((handler), ##__VA_ARGS__));      \
            stop_budget();                                               \
        }                                                                \
    } while (0)

/* Builds the runtime and loads the module into it; the application is
 * left for app_unload() whatever happens. */
static app_load_result_t load_module(app_t *app, const uint8_t *wasm, size_t wasm_size)
{
    app->environment = m3_NewEnvironment();
    app->runtime = app->environment != NULL
                       ? m3_NewRuntime(app->environment, INTERPRETER_STACK_BYTES, app)
                       : NULL;
    if (app->runtime == NULL) {
        return APP_ERR_NO_MEMORY;
    }

    IM3Module module = NULL;
    if (m3_ParseModule(app->environment, &module, wasm, (uint32_t)wasm_size) != m3Err_none) {
        return APP_ERR_BAD_MODULE;
    }
    M3Result load_result = m3_LoadModule(app->runtime, module);
    if (load_result != m3Err_none) {
        /* Until it is loaded the module is ours to free. */
        m3_FreeModule(module);
        return load_result == m3Err_mallocFailed ? APP_ERR_NO_MEMORY : APP_ERR_BAD_MODULE;
    }
    return link_host_api(module) ? APP_OK : APP_ERR_OTHER_API;
}

/* Asks the application where it takes data. Without a sound answer it
 * simply gets none. */
static void find_data_buffer(app_t *app)
{
    IM3Function data_buffer = find_export(app, "app_data_buffer");
    uint32_t offset = 0;
    if (data_buffer == NULL) {
        return;
    }
    /* It is the application's code like any handler: on the same budget. */
    start_budget(app->services);
    M3Result result = m3_CallV(data_buffer);
    stop_budget();
    if (result != m3Err_none) {
        note_result(app, result);
        return;
    }
    if (m3_GetResultsV(data_buffer, &offset) != m3Err_none) {
        return;
    }
    uint32_t memory_size = m3_GetMemorySize(app->runtime);
    app->has_data_buffer = offset <= memory_size && HOST_DATA_CHUNK_MAX <= memory_size - offset;
    app->data_buffer_offset = offset;
}

app_load_result_t app_load(const char *name, const uint8_t *wasm, size_t wasm_size,
                           bool with_system, const app_services_t *services, app_t **loaded)
{
    *loaded = NULL;
    app_load_result_t imports = check_imports(wasm, wasm_size, with_system);
    if (imports != APP_OK) {
        return imports;
    }
    app_t *app = calloc(1, sizeof *app);
    if (app == NULL) {
        return APP_ERR_NO_MEMORY;
    }
    app->services = services;
    app->name = name;
    app_load_result_t result = load_module(app, wasm, wasm_size);
    if (result != APP_OK) {
        app_unload(app);
        return result;
    }

    app->on_focus = find_export(app, "app_on_focus");
    app->on_blur = find_export(app, "app_on_blur");
    app->on_terminate = find_export(app, "app_on_terminate");
    app->on_back = find_export(app, "app_on_back");
    app->on_button = find_export(app, "app_on_button");
    app->on_timer = find_export(app, "app_on_timer");
    app->on_draw = find_export(app, "app_on_draw");
    app->on_data = find_export(app, "app_on_data");
    app->on_values = find_export(app, "app_on_values");
    app->on_settings = find_export(app, "app_on_settings");
    app->on_intent = find_export(app, "app_on_intent");
    app->on_alarm = find_export(app, "app_on_alarm");
    find_data_buffer(app);
    CALL_HANDLER(app, find_export(app, "app_init"));
    app->exit_requested = false;

    *loaded = app;
    return APP_OK;
}

void app_unload(app_t *app)
{
    if (app == NULL) {
        return;
    }
    if (app->runtime != NULL) {
        m3_FreeRuntime(app->runtime);
    }
    if (app->environment != NULL) {
        m3_FreeEnvironment(app->environment);
    }
    free(app);
#ifdef d_m3ApplicationArea
    /* The next application starts in an area nobody has been in. */
    m3_area_reset();
#endif
}

void app_focus(app_t *app)
{
    CALL_HANDLER(app, app->on_focus);
    app->exit_requested = false; /* only a button or a timer may ask to leave */
}

void app_blur(app_t *app)
{
    CALL_HANDLER(app, app->on_blur);
}

void app_terminate(app_t *app)
{
    CALL_HANDLER(app, app->on_terminate);
}

bool app_handles_back(const app_t *app)
{
    return app->on_back != NULL;
}

void app_back(app_t *app)
{
    CALL_HANDLER(app, app->on_back);
}

void app_button(app_t *app, int button, int press)
{
    CALL_HANDLER(app, app->on_button, (int32_t)BTN_EVENT(button, press));
}

void app_timer(app_t *app, int id)
{
    CALL_HANDLER(app, app->on_timer, (int32_t)id);
}

void app_draw(app_t *app, gfx_t *surface)
{
    app->surface = surface;
    surface->shade = GFX_SHADE_SOLID;
    CALL_HANDLER(app, app->on_draw);
    surface->shade = GFX_SHADE_SOLID;
    app->surface = NULL;
    app->exit_requested = false;
}

void app_answer_begins(app_t *app, const host_response_t *response, int http_status)
{
    app->is_in_answer = true;
    app->response = response;
    app->response_status = http_status;
}

void app_answer_ends(app_t *app)
{
    app->is_in_answer = false;
    app->response = NULL;
}

void app_data(app_t *app, int request, const uint8_t *data, size_t length, int flags,
              int http_status)
{
    if (app->on_data == NULL || !app->has_data_buffer || length > HOST_DATA_CHUNK_MAX) {
        return;
    }
    /* Looked up again every time: the interpreter may move the memory. */
    uint8_t *memory = m3_GetMemory(app->runtime, NULL, 0);
    if (memory == NULL || app->stop_reason != NULL) {
        return;
    }
    memcpy(memory + app->data_buffer_offset, data, length);
    CALL_HANDLER(app, app->on_data, (int32_t)request, (int32_t)app->data_buffer_offset,
                 (int32_t)length, (int32_t)flags, (int32_t)http_status);
}

void app_values(app_t *app, int request, int http_status, const char *const *values, int count)
{
    app->values = values;
    app->value_count = count;
    CALL_HANDLER(app, app->on_values, (int32_t)request, (int32_t)http_status);
    app->values = NULL;
    app->value_count = 0;
}

void app_alarm(app_t *app, int id)
{
    CALL_HANDLER(app, app->on_alarm, (int32_t)id);
}

void app_settings_changed(app_t *app)
{
    CALL_HANDLER(app, app->on_settings);
}

void app_intent(app_t *app, int intent)
{
    CALL_HANDLER(app, app->on_intent, (int32_t)intent);
}

const char *app_header_text(const app_t *app, int side)
{
    return app->header[side];
}

bool app_take_focus_request(app_t *app)
{
    bool requested = app->focus_requested;
    app->focus_requested = false;
    return requested;
}

bool app_take_redraw_request(app_t *app)
{
    bool requested = app->redraw_requested;
    app->redraw_requested = false;
    return requested;
}

int app_take_redraw_flags(app_t *app)
{
    int flags = app->redraw_flags;
    app->redraw_flags = 0;
    return flags;
}

bool app_take_exit_request(app_t *app)
{
    bool requested = app->exit_requested;
    app->exit_requested = false;
    return requested;
}

bool app_has_stopped(const app_t *app)
{
    return app->stop_reason != NULL;
}

const char *app_stop_reason(const app_t *app)
{
    return app->stop_reason != NULL ? app->stop_reason : "";
}
