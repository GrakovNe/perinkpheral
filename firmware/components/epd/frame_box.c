/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "frame_box.h"

#include <string.h>

bool frame_box_is_empty(const frame_box_t *box)
{
    return box->rows <= 0 || box->columns <= 0;
}

frame_box_t frame_box_of_change(const uint8_t *frame, const uint8_t *other, int columns, int rows)
{
    int first_row = rows, last_row = -1, first_column = columns, last_column = -1;
    for (int row = 0; row < rows; row++) {
        const uint8_t *line = frame + row * columns, *other_line = other + row * columns;
        if (memcmp(line, other_line, (size_t)columns) == 0) {
            continue;
        }
        if (first_row == rows) {
            first_row = row;
        }
        last_row = row;
        int left = 0, right = columns - 1;
        while (line[left] == other_line[left]) {
            left++;
        }
        while (line[right] == other_line[right]) {
            right--;
        }
        if (left < first_column) {
            first_column = left;
        }
        if (right > last_column) {
            last_column = right;
        }
    }
    frame_box_t box = { 0, 0, 0, 0 };
    if (last_row >= 0) {
        box.column = first_column;
        box.row = first_row;
        box.columns = last_column - first_column + 1;
        box.rows = last_row - first_row + 1;
    }
    return box;
}

frame_box_t frame_box_join(const frame_box_t *box, const frame_box_t *other)
{
    if (frame_box_is_empty(box)) {
        return *other;
    }
    if (frame_box_is_empty(other)) {
        return *box;
    }
    int column = box->column < other->column ? box->column : other->column;
    int row = box->row < other->row ? box->row : other->row;
    int column_end = box->column + box->columns > other->column + other->columns
                         ? box->column + box->columns
                         : other->column + other->columns;
    int row_end = box->row + box->rows > other->row + other->rows ? box->row + box->rows
                                                                  : other->row + other->rows;
    frame_box_t joined = { column, row, column_end - column, row_end - row };
    return joined;
}
