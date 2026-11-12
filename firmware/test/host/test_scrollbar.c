/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scrollbar.h"

#include "tinytest.h"

#define TRACK 200

static void test_a_list_that_fits_fills_the_track(void)
{
    scrollbar_thumb_t thumb = scrollbar_thumb(TRACK, 4, 6, 0);
    CHECK(thumb.top == 0 && thumb.height == TRACK);
}

static void test_the_thumb_is_the_share_shown(void)
{
    /* five of ten rows: half the track, and at its ends to the pixel */
    scrollbar_thumb_t thumb = scrollbar_thumb(TRACK, 10, 5, 0);
    CHECK(thumb.top == 0 && thumb.height == 100);
    thumb = scrollbar_thumb(TRACK, 10, 5, 5);
    CHECK(thumb.top + thumb.height == TRACK);
    thumb = scrollbar_thumb(TRACK, 10, 5, 2);
    CHECK(thumb.top == 40 && thumb.height == 100);
}

static void test_a_long_list_keeps_a_thumb_that_can_be_seen(void)
{
    scrollbar_thumb_t thumb = scrollbar_thumb(TRACK, 500, 6, 0);
    CHECK(thumb.height == 12 && thumb.top == 0);
    thumb = scrollbar_thumb(TRACK, 500, 6, 494);
    CHECK(thumb.top + thumb.height == TRACK);
    /* a first row past the last one that can be first is the last */
    thumb = scrollbar_thumb(TRACK, 500, 6, 9999);
    CHECK(thumb.top + thumb.height == TRACK);
    thumb = scrollbar_thumb(TRACK, 500, 6, -3);
    CHECK(thumb.top == 0);
}

int main(void)
{
    test_a_list_that_fits_fills_the_track();
    test_the_thumb_is_the_share_shown();
    test_a_long_list_keeps_a_thumb_that_can_be_seen();
    REPORT();
}
