/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "frame_box.h"

#include <string.h>

#include "tinytest.h"

#define COLUMNS 6
#define ROWS    5

static void test_no_change_is_empty(void)
{
    uint8_t frame[COLUMNS * ROWS] = { 0 }, other[COLUMNS * ROWS] = { 0 };
    frame_box_t box = frame_box_of_change(frame, other, COLUMNS, ROWS);
    CHECK(frame_box_is_empty(&box));
}

static void test_one_bit(void)
{
    uint8_t frame[COLUMNS * ROWS] = { 0 }, other[COLUMNS * ROWS] = { 0 };
    other[2 * COLUMNS + 3] = 0x10;
    frame_box_t box = frame_box_of_change(frame, other, COLUMNS, ROWS);
    CHECK(box.column == 3 && box.row == 2 && box.columns == 1 && box.rows == 1);
}

static void test_box_holds_every_change(void)
{
    uint8_t frame[COLUMNS * ROWS] = { 0 }, other[COLUMNS * ROWS] = { 0 };
    other[1 * COLUMNS + 4] = 1;
    other[3 * COLUMNS + 1] = 1;
    other[4 * COLUMNS + 2] = 1;
    frame_box_t box = frame_box_of_change(frame, other, COLUMNS, ROWS);
    CHECK(box.column == 1 && box.row == 1 && box.columns == 4 && box.rows == 4);
}

static void test_corners(void)
{
    uint8_t frame[COLUMNS * ROWS] = { 0 }, other[COLUMNS * ROWS] = { 0 };
    other[0] = 1;
    other[COLUMNS * ROWS - 1] = 1;
    frame_box_t box = frame_box_of_change(frame, other, COLUMNS, ROWS);
    CHECK(box.column == 0 && box.row == 0 && box.columns == COLUMNS && box.rows == ROWS);
}

static void test_join(void)
{
    frame_box_t empty = { 0, 0, 0, 0 }, a = { 1, 1, 2, 2 }, b = { 4, 0, 1, 2 };
    frame_box_t joined = frame_box_join(&a, &b);
    CHECK(joined.column == 1 && joined.row == 0 && joined.columns == 4 && joined.rows == 3);
    joined = frame_box_join(&empty, &a);
    CHECK(memcmp(&joined, &a, sizeof a) == 0);
    joined = frame_box_join(&a, &empty);
    CHECK(memcmp(&joined, &a, sizeof a) == 0);
    joined = frame_box_join(&empty, &empty);
    CHECK(frame_box_is_empty(&joined));
}

int main(void)
{
    test_no_change_is_empty();
    test_one_bit();
    test_box_holds_every_change();
    test_corners();
    test_join();
    REPORT();
}
