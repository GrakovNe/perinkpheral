/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "notice_queue.h"

#include <string.h>

#include "tinytest.h"

static notice_queue_t queue;

static void test_in_turn(void)
{
    notice_queue_init(&queue);
    CHECK(notice_queue_next(&queue) == NULL);
    CHECK(notice_queue_add(&queue, "Time is up", NULL, "Tea", 7));
    CHECK(notice_queue_add(&queue, "Meeting", "10:00", NULL, 8));

    /* the oldest first, and one at a time */
    const notice_told_t *told = notice_queue_next(&queue);
    CHECK(told != NULL && strcmp(told->first, "Time is up") == 0 && told->second[0] == '\0' &&
          strcmp(told->detail, "Tea") == 0 && told->icon == 7);
    CHECK(notice_queue_is_shown(&queue));
    CHECK(notice_queue_next(&queue) == NULL);

    /* nothing was dismissed: nothing leaves */
    CHECK(!notice_queue_take_dismissed(&queue));
    /* dismissed in the handler, it leaves after it, and the next is shown */
    notice_queue_seen(&queue);
    CHECK(notice_queue_take_dismissed(&queue));
    CHECK(!notice_queue_is_shown(&queue) && !notice_queue_take_dismissed(&queue));
    told = notice_queue_next(&queue);
    CHECK(told != NULL && strcmp(told->first, "Meeting") == 0 && strcmp(told->second, "10:00") == 0);

    /* taken off the screen another way: out of the queue as well */
    notice_queue_gone(&queue);
    CHECK(!notice_queue_is_shown(&queue) && notice_queue_next(&queue) == NULL);
    notice_queue_gone(&queue); /* nothing shown: nothing goes */
    CHECK(queue.count == 0);

    /* seen with nothing shown is not an answer to the next one */
    notice_queue_seen(&queue);
    CHECK(!notice_queue_take_dismissed(&queue));
    CHECK(notice_queue_add(&queue, "Again", NULL, NULL, 0));
    CHECK(notice_queue_next(&queue) != NULL && !notice_queue_take_dismissed(&queue));
}

static void test_room(void)
{
    notice_queue_init(&queue);
    for (int i = 0; i < NOTICE_QUEUE_MAX; i++) {
        CHECK(notice_queue_add(&queue, "One", NULL, NULL, 0));
    }
    CHECK(!notice_queue_add(&queue, "Too many", NULL, NULL, 0));
    notice_queue_next(&queue);
    notice_queue_gone(&queue);
    CHECK(notice_queue_add(&queue, "Room again", NULL, NULL, 0));
}

static void test_long_text_is_cut_between_characters(void)
{
    notice_queue_init(&queue);
    /* 62 bytes of ASCII and a letter of two: 64 bytes, one over */
    char text[80];
    memset(text, 'a', 62);
    strcpy(text + 62, "\xD0\xB6");
    CHECK(notice_queue_add(&queue, text, NULL, NULL, 0));
    const notice_told_t *told = notice_queue_next(&queue);
    CHECK(strlen(told->first) == 62);
    /* one byte shorter, it fits whole */
    text[61] = '\0';
    strcat(text, "\xD0\xB6");
    notice_queue_init(&queue);
    notice_queue_add(&queue, text, NULL, NULL, 0);
    CHECK(strlen(notice_queue_next(&queue)->first) == 63);
}

int main(void)
{
    test_in_turn();
    test_room();
    test_long_text_is_cut_between_characters();
    REPORT();
}
