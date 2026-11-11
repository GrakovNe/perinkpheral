/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "log_ring.h"

#include <string.h>

#include "tinytest.h"

#define CAPACITY 32

static char storage[CAPACITY];
static log_ring_t ring;
static char text[CAPACITY + 1];

static void write(const char *line)
{
    log_ring_write(&ring, line, strlen(line));
}

static const char *read_all(void)
{
    log_ring_read(&ring, text, sizeof text);
    return text;
}

static void test_keeps_what_fits(void)
{
    log_ring_init(&ring, storage, CAPACITY);
    CHECK_STR(read_all(), "");
    write("one\n");
    write("two\n");
    CHECK_STR(read_all(), "one\ntwo\n");
    CHECK(log_ring_read(&ring, text, sizeof text) == 8);
}

static void test_oldest_goes_first(void)
{
    log_ring_init(&ring, storage, CAPACITY);
    write("first line\n");
    write("second line\n");
    /* this one pushes the first out, and cuts into the second */
    write("third line, a longer one\n");
    /* the line cut in the middle is dropped whole */
    CHECK_STR(read_all(), "third line, a longer one\n");

    write("4\n");
    CHECK_STR(read_all(), "third line, a longer one\n4\n");
}

static void test_wraps_many_times(void)
{
    log_ring_init(&ring, storage, CAPACITY);
    for (int i = 0; i < 100; i++) {
        write("0123456\n");
    }
    write("last\n");
    const char *kept = read_all();
    CHECK(strlen(kept) <= CAPACITY);
    CHECK(strncmp(kept, "0123456\n", 8) == 0); /* starts on a whole line */
    CHECK(strcmp(kept + strlen(kept) - 5, "last\n") == 0);
}

static void test_longer_than_the_ring(void)
{
    log_ring_init(&ring, storage, CAPACITY);
    write("a line that is far longer than the whole ring can keep\nend\n");
    CHECK_STR(read_all(), "end\n");
}

static void test_small_reader(void)
{
    log_ring_init(&ring, storage, CAPACITY);
    write("one\ntwo\nthree\n");
    char small[10];
    /* nine bytes of room: the newest whole lines that fit */
    CHECK(log_ring_read(&ring, small, sizeof small) == 6);
    CHECK_STR(small, "three\n");

    char none[1];
    CHECK(log_ring_read(&ring, none, sizeof none) == 0 && none[0] == '\0');
    CHECK(log_ring_read(&ring, none, 0) == 0);
}

int main(void)
{
    test_keeps_what_fits();
    test_oldest_goes_first();
    test_wraps_many_times();
    test_longer_than_the_ring();
    test_small_reader();
    REPORT();
}
