/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Drawing: the gfx_* calls.
 */
#include "app_internal.h"

HOST_FUNCTION(gfx_width)
{
    m3ApiReturnType(int32_t);
    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->surface != NULL ? app->surface->w : HOST_ERR_NOT_DRAWING);
}

HOST_FUNCTION(gfx_height)
{
    m3ApiReturnType(int32_t);
    app_t *app = APP_OF(runtime);
    m3ApiReturn(app->surface != NULL ? app->surface->h : HOST_ERR_NOT_DRAWING);
}

HOST_FUNCTION(gfx_text)
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
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    if (font == NULL || !is_drawable(x) || !is_drawable(y) ||
        !app_text_copy(runtime, _mem, text, length, terminated)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int baseline = y + gfx_font_ascent(font);
    m3ApiReturn(is_tabular(font_id)
                    ? gfx_text_tabular(app->surface, x, baseline, font, terminated, true)
                    : gfx_text_font(app->surface, x, baseline, font, terminated, true));
}

HOST_FUNCTION(gfx_text_width)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    m3ApiGetArg(uint32_t, text);
    m3ApiGetArg(int32_t, length);

    const gfx_font_t *font = font_by_id(font_id);
    char terminated[HOST_TEXT_MAX + 1];
    if (font == NULL || !app_text_copy(runtime, _mem, text, length, terminated)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(is_tabular(font_id) ? gfx_text_tabular_width(font, terminated)
                                    : gfx_text_font_width(font, terminated));
}

HOST_FUNCTION(gfx_text_ink)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    m3ApiGetArg(uint32_t, text);
    m3ApiGetArg(int32_t, length);
    m3ApiGetArg(uint32_t, ink_offset);

    const gfx_font_t *font = font_by_id(font_id);
    char terminated[HOST_TEXT_MAX + 1];
    uint8_t *out = app_memory_to_fill(runtime, _mem, ink_offset, (int32_t)sizeof(host_text_ink_t));
    if (font == NULL || out == NULL || !app_text_copy(runtime, _mem, text, length, terminated)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int first, end;
    if (is_tabular(font_id)) {
        gfx_text_tabular_ink(font, terminated, &first, &end);
    } else {
        gfx_text_font_ink(font, terminated, &first, &end);
    }
    /* the application's memory keeps no alignment the chip would need */
    host_text_ink_t answer = { first, end };
    memcpy(out, &answer, sizeof answer);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(gfx_text_fit)
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
    char *fitted = (char *)app_memory_to_fill(runtime, _mem, fitted_offset, capacity);
    if (font == NULL || fitted == NULL || !is_drawable(max_width) ||
        !app_text_copy(runtime, _mem, text, length, terminated)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    char cut[HOST_TEXT_MAX + 1];
    gfx_text_fit(font, terminated, max_width, cut, sizeof cut);
    /* Digits of equal width take more room than the font's own: the text
     * is fitted again, narrower by what it runs over, until it fits as it
     * will be drawn. */
    if ((font_id & HOST_FONT_TABULAR) != 0) {
        int32_t room = max_width;
        enum { FITTING_TURNS = 8 }; /* each turn drops a character or more: enough for any text */
        for (int turn = 0; turn < FITTING_TURNS && cut[0] != '\0'; turn++) {
            int32_t over = gfx_text_tabular_width(font, cut) - max_width;
            if (over <= 0) {
                break;
            }
            room = room > over ? room - over : 0;
            gfx_text_fit(font, terminated, room, cut, sizeof cut);
        }
    }
    int32_t cut_length = (int32_t)strlen(cut);
    if (cut_length > capacity) {
        m3ApiReturn(HOST_ERR_NO_SPACE);
    }
    memcpy(fitted, cut, (size_t)cut_length);
    m3ApiReturn(cut_length);
}

HOST_FUNCTION(gfx_font_height)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    const gfx_font_t *font = font_by_id(font_id);
    m3ApiReturn(font != NULL ? gfx_font_height(font) : HOST_ERR_ARGUMENT);
}

HOST_FUNCTION(gfx_font_ascent)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    const gfx_font_t *font = font_by_id(font_id);
    m3ApiReturn(font != NULL ? gfx_font_ascent(font) : HOST_ERR_ARGUMENT);
}

HOST_FUNCTION(gfx_font_cap_height)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, font_id);
    const gfx_font_t *font = font_by_id(font_id);
    m3ApiReturn(font != NULL ? gfx_font_cap_height(font) : HOST_ERR_ARGUMENT);
}

HOST_FUNCTION(gfx_rect)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, height);
    m3ApiGetArg(int32_t, fill);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
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

HOST_FUNCTION(gfx_line)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x0);
    m3ApiGetArg(int32_t, y0);
    m3ApiGetArg(int32_t, x1);
    m3ApiGetArg(int32_t, y1);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    if (!is_drawable(x0) || !is_drawable(y0) || !is_drawable(x1) || !is_drawable(y1)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    gfx_line(app->surface, x0, y0, x1, y1, true);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(gfx_bitmap)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, width);
    m3ApiGetArg(int32_t, height);
    m3ApiGetArg(uint32_t, bits_offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    /* Bounded by the surface first, so that the size below cannot overflow. */
    if (!is_drawable(x) || !is_drawable(y) || width <= 0 || height <= 0 ||
        width > app->surface->w || height > app->surface->h) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int32_t bytes_per_row = gfx_row_bytes(width);
    const uint8_t *bits = app_memory_at(runtime, _mem, bits_offset, bytes_per_row * height);
    if (bits == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    for (int32_t row = 0; row < height; row++) {
        for (int32_t column = 0; column < width; column++) {
            bool is_set = (bits[row * bytes_per_row + column / 8] & gfx_bit_of(column)) != 0;
            if (is_set) {
                gfx_pixel(app->surface, x + column, y + row, true);
            }
        }
    }
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(gfx_icon)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, icon);
    m3ApiGetArg(int32_t, cell);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    if (!is_drawable(x) || !is_drawable(y) || cell < 1 || cell > HOST_ICON_CELL_MAX ||
        !gfx_icon(app->surface, (gfx_icon_id_t)icon, x, y, cell, true)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(gfx_icon_ink)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, icon);
    m3ApiGetArg(int32_t, cell);
    m3ApiGetArg(uint32_t, ink_offset);

    uint8_t *out = app_memory_to_fill(runtime, _mem, ink_offset, (int32_t)sizeof(host_icon_ink_t));
    gfx_icon_ink_t ink;
    if (out == NULL || cell < 1 || cell > HOST_ICON_CELL_MAX ||
        !gfx_icon_ink((gfx_icon_id_t)icon, cell, &ink)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    /* the application's memory keeps no alignment the chip would need */
    host_icon_ink_t answer = { ink.first_x, ink.last_x, ink.first_y, ink.last_y };
    memcpy(out, &answer, sizeof answer);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(gfx_picture)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(uint32_t, picture_offset);
    m3ApiGetArg(int32_t, cell);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    const uint8_t *picture = app_picture_at(runtime, _mem, picture_offset);
    if (picture == NULL || !is_drawable(x) || !is_drawable(y) || cell < 1 ||
        cell > HOST_ICON_CELL_MAX) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    gfx_picture(app->surface, x, y, picture, cell, true);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(gfx_color)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, color);
    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
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

/* The largest code an application gets: 57 modules a side. Both buffers
 * of the generator are on the host's stack while it works. */
#define QR_VERSION_MAX 10
#define QR_CELL_MAX    16

HOST_FUNCTION(gfx_qr)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, cell);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    const uint8_t *text = app_memory_at(runtime, _mem, text_offset, length);
    app_t *app = APP_OF(runtime);
    if (text == NULL || length > HOST_QR_TEXT_MAX || cell < 0 || cell > QR_CELL_MAX ||
        !is_drawable(x) || !is_drawable(y)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    if (cell > 0 && app->surface == NULL) {
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
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

/* A picture is read from its file a few rows at a time: whole, it would
 * not fit the application's hands, and the host has no use for a copy. */
#define PICTURE_CHUNK_BYTES 512

HOST_FUNCTION(gfx_bitmap_file)
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
        m3ApiReturn(app_not_drawing(APP_OF(runtime)));
    }
    char name[HOST_FILE_NAME_MAX + 1];
    if (!is_drawable(x) || !is_drawable(y) || width <= 0 || height <= 0 ||
        width > app->surface->w || height > app->surface->h || offset < 0 ||
        offset > MANIFEST_FILES_MAX_BYTES ||
        !app_file_name_copy(runtime, _mem, name_offset, name_length, name)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int32_t bytes_per_row = gfx_row_bytes(width);
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
                if ((bits[column / 8] & gfx_bit_of(column)) != 0) {
                    gfx_pixel(app->surface, x + column, y + row + chunk_row, true);
                }
            }
        }
    }
    m3ApiReturn(HOST_OK);
}
