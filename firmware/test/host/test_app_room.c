/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_room.h"

#include "manifest_limits.h"
#include "tinytest.h"

#define TOTAL 20

static void test_first_fit(void)
{
    /* an empty partition: from its start, and nothing larger than it */
    CHECK(app_room_first_fit(NULL, 0, TOTAL, 3) == 0);
    CHECK(app_room_first_fit(NULL, 0, TOTAL, TOTAL) == 0);
    CHECK(app_room_first_fit(NULL, 0, TOTAL, TOTAL + 1) == -1);
    CHECK(app_room_first_fit(NULL, 0, TOTAL, 0) == -1);

    /* [0,2) taken, [2,4) free, [4,7) taken, [7,10) free, [10,20) taken */
    app_room_extent_t taken[] = { { 0, 2 }, { 4, 3 }, { 10, 10 } };
    CHECK(app_room_first_fit(taken, 3, TOTAL, 2) == 2);
    CHECK(app_room_first_fit(taken, 3, TOTAL, 3) == 7);
    /* five free sectors, but in two pieces */
    CHECK(app_room_free(taken, 3, TOTAL) == 5);
    CHECK(app_room_first_fit(taken, 3, TOTAL, 4) == -1);

    /* the free run at the end */
    app_room_extent_t low[] = { { 0, 5 } };
    CHECK(app_room_first_fit(low, 1, TOTAL, 15) == 5);
    CHECK(app_room_first_fit(low, 1, TOTAL, 16) == -1);
}

static void test_to_move(void)
{
    app_room_extent_t packed[] = { { 0, 2 }, { 2, 3 }, { 5, 1 } };
    CHECK(app_room_to_move(packed, 3) == -1);
    CHECK(app_room_to_move(NULL, 0) == -1);
    /* a gap before the second: it moves down to sector 2 */
    app_room_extent_t gap[] = { { 0, 2 }, { 4, 3 }, { 10, 1 } };
    CHECK(app_room_to_move(gap, 3) == 1);
    /* a gap at the start */
    app_room_extent_t first[] = { { 1, 2 } };
    CHECK(app_room_to_move(first, 1) == 0);

    /* moved one by one, they end packed: each move lands where the gap began */
    app_room_extent_t moving[] = { { 3, 2 }, { 7, 4 }, { 15, 1 } };
    int moves = 0;
    for (int i; (i = app_room_to_move(moving, 3)) >= 0 && moves < 10; moves++) {
        app_room_extent_t others[3];
        int count = 0;
        for (int j = 0; j < 3; j++) {
            if (j != i) {
                others[count++] = moving[j];
            }
        }
        moving[i].start = app_room_first_fit(others, count, TOTAL, moving[i].sectors);
    }
    CHECK(moves == 3);
    CHECK(moving[0].start == 0 && moving[1].start == 2 && moving[2].start == 6);
    CHECK(app_room_first_fit(moving, 3, TOTAL, 13) == 7);
}

static void test_promised(void)
{
    /* one that only reads keeps nothing free; one that writes, the rest of its share */
    app_room_files_t apps[] = {
        { false, 60 * 1024 },
        { true, 12 * 1024 },
        { true, 0 },
        { true, MANIFEST_FILES_WRITER_MAX_BYTES + 4096 }, /* a package larger than the share */
    };
    CHECK(app_room_promised(apps, 1) == 0);
    CHECK(app_room_promised(apps, 2) == MANIFEST_FILES_WRITER_MAX_BYTES - 12 * 1024);
    CHECK(app_room_promised(apps, 4) == 2 * MANIFEST_FILES_WRITER_MAX_BYTES - 12 * 1024);
}

int main(void)
{
    test_first_fit();
    test_to_move();
    test_promised();
    REPORT();
}
