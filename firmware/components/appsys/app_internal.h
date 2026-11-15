/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Inside the application runtime: what app.c (loading, the handlers, the
 * import table) and the calls of the contract by area - app_api_draw.c,
 * app_api_ui.c, app_api_data.c, app_api_time.c, app_api_sys.c - share. Not
 * for anybody outside components/appsys.
 */
#pragma once

#include "words.h"
#include "app.h"

#include <stdlib.h>
#include <stdio.h>
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
#include "manifest_limits.h"
#include "qrcodegen.h"
#include "wasm3.h"
#include "units.h"

/* What an application may export, as the contract declares it: the host
 * looks every one up when the module is loaded, and refuses a module that
 * exports one otherwise than declared. */
typedef enum {
#define APP_EXPORT_ROW(name, arguments, has_result) EXPORT_##name,
#include "host_exports.inc"
#undef APP_EXPORT_ROW
    EXPORT_COUNT
} export_t;

#define HANDLER(app, name) ((app)->exports[EXPORT_##name])

struct app {
    IM3Environment        environment;
    IM3Runtime            runtime;
    const app_services_t *services;
    const char           *name;

    IM3Function exports[EXPORT_COUNT]; /* NULL: not exported */
    /* Set only while app_on_values() runs: what data_value() reads. */
    json_pick_t *picked;
    /* Set only while a handler of an answer runs: what data_response() and
     * data_item() say. */
    bool                   is_in_answer;
    const host_response_t *response;
    int                    response_status;
    int                    answer_item;
    uint32_t    data_buffer_offset; /* where in its memory the application takes data */
    bool        has_data_buffer;

    gfx_t      *surface; /* set only while app_on_draw() runs */
    bool        redraw_requested;
    bool        not_drawing_logged; /* once an instance: a mistake every beginner makes */
    bool        slow_logged;        /* once an instance: a handler that took too long */
    bool        has_wrong_export;   /* a handler declared otherwise than the contract has it */
    int         redraw_flags;  /* HOST_REDRAW_*, of every request since the last take */
    bool        exit_requested;
    bool        focus_requested;
    /* What it put into the host's header; empty leaves a side to the host. */
    char        header[HOST_HEADER_RIGHT + 1][HOST_HEADER_TEXT_MAX + 1];
    const char *stop_reason; /* NULL while the application is alive */
};

/* ---- the host API as the application sees it ---- */

_Static_assert(HOST_INK == GFX_SHADE_SOLID, "an application's ink is the solid shade");
_Static_assert(HOST_FONT_TEXT == GFX_FONT_TEXT && HOST_FONT_MEDIUM == GFX_FONT_MEDIUM &&
                   HOST_FONT_LARGE == GFX_FONT_LARGE && HOST_FONT_DISPLAY == GFX_FONT_DISPLAY &&
                   HOST_FONT_HUGE == GFX_FONT_HUGE && HOST_FONT_SMALL == GFX_FONT_SMALL,
               "applications name the fonts by the numbers gfx gives them");

/* The font an application names, HOST_FONT_TABULAR or not. */
static inline const gfx_font_t *font_by_id(int32_t font)
{
    return gfx_font((gfx_font_id_t)(font & ~HOST_FONT_TABULAR));
}

static inline bool is_tabular(int32_t font)
{
    return (font & HOST_FONT_TABULAR) != 0;
}

/* An offset and a length from the application, which is not trusted, as
 * a place in its memory; NULL when any of it lies outside. */
const uint8_t *app_memory_at(IM3Runtime runtime, void *memory, uint32_t offset,
                                    int32_t length);

/* The same for memory the host is about to fill for the application. */
uint8_t *app_memory_to_fill(IM3Runtime runtime, void *memory, uint32_t offset,
                                      int32_t capacity);

/* The application's bytes as a terminated string the drawing code takes.
 * False when they are not the application's to give. */
bool app_text_copy(IM3Runtime runtime, void *memory, uint32_t offset, int32_t length,
                      char terminated[HOST_TEXT_MAX + 1]);

/* Far beyond any surface, and small enough that sums of two of them
 * cannot overflow: a coordinate or a size outside it is refused. */
#define COORDINATE_LIMIT 4096

static inline bool is_drawable(int32_t coordinate)
{
    return coordinate >= -COORDINATE_LIMIT && coordinate <= COORDINATE_LIMIT;
}

#define APP_OF(runtime) ((app_t *)m3_GetUserData(runtime))

/* An import the way wasm3 calls it, for the m3Api* macros to work on. Not
 * every import needs every one of the four arguments. */
#define MAYBE_UNUSED __attribute__((unused))
#define HOST_FUNCTION(name)                                                                      \
    const void *app_api_##name(IM3Runtime runtime MAYBE_UNUSED, IM3ImportContext _ctx MAYBE_UNUSED, \
                               uint64_t *_sp MAYBE_UNUSED, void *_mem MAYBE_UNUSED)

/* Every call of the contract, answered in app_api_<area>.c. */
#define HOST_IMPORT_ROW(name, signature) HOST_FUNCTION(name);
#include "host_imports.inc"
#include "system_imports.inc"
#undef HOST_IMPORT_ROW

/* A drawing call from a handler draws nothing, and an application that
 * ignores what it returns shows an empty screen with no word of why: the
 * log says it, once an instance. */
int32_t app_not_drawing(app_t *app);

/* A file's name from the application, terminated; false when it is not
 * the application's memory or not a name a file can have. */
bool app_file_name_copy(IM3Runtime runtime, void *memory, uint32_t offset, int32_t length,
                           char name[HOST_FILE_NAME_MAX + 1]);

/* A picture in the application's memory, whole: its two bytes of size and
 * every row they promise. */
const uint8_t *app_picture_at(IM3Runtime runtime, void *memory, uint32_t offset);
