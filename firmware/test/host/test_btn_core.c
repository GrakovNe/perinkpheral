/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "btn_core.h"

#include "tinytest.h"

#define STEP_MS 5

static const btn_core_cfg_t CFG = { .debounce = 4, .long_ms = 600 };

typedef struct {
    btn_core_t b;
    uint32_t   now;
    int        shorts;
    int        longs;
    int        downs;
    int        ups;
    btn_event_t last; /* the event reported last */
} rig_t;

static void rig_init(rig_t *r)
{
    btn_core_init(&r->b);
    r->now = 1000;
    r->shorts = 0;
    r->longs = 0;
    r->downs = 0;
    r->ups = 0;
    r->last = BTN_NONE;
}

/* Feeds a string of samples, one per STEP_MS: '0' pressed, '1' idle. */
static void feed(rig_t *r, const char *levels)
{
    for (const char *p = levels; *p != '\0'; p++) {
        btn_event_t events[BTN_EVENTS_PER_SAMPLE];
        int count = btn_core_feed(&r->b, *p == '1', r->now, &CFG, events);
        for (int i = 0; i < count; i++) {
            r->shorts += events[i] == BTN_SHORT;
            r->longs += events[i] == BTN_LONG;
            r->downs += events[i] == BTN_DOWN;
            r->ups += events[i] == BTN_UP;
            r->last = events[i];
        }
        r->now += STEP_MS;
    }
}

static void hold(rig_t *r, int ms)
{
    for (int t = 0; t < ms; t += STEP_MS) {
        feed(r, "0");
    }
}

static void test_short_press(void)
{
    rig_t r;
    rig_init(&r);
    feed(&r, "1111");
    feed(&r, "000");
    CHECK(r.shorts == 0 && !r.b.down);      /* not yet stable */
    feed(&r, "0");
    CHECK(r.b.down && r.shorts == 0);       /* pressed, nothing reported yet */
    hold(&r, 100);
    CHECK(r.shorts == 0 && r.longs == 0);
    feed(&r, "1");
    CHECK(r.shorts == 1 && r.longs == 0);   /* reported on release */
    feed(&r, "1111");
    CHECK(r.shorts == 1);
}

static void test_long_press(void)
{
    rig_t r;
    rig_init(&r);
    hold(&r, 595);
    CHECK(r.longs == 0);
    hold(&r, 20);
    CHECK(r.longs == 1 && r.shorts == 0);   /* while still held */
    hold(&r, 2000);
    CHECK(r.longs == 1);                    /* once */
    feed(&r, "1");
    CHECK(r.shorts == 0 && r.longs == 1);   /* release after long says nothing */
    feed(&r, "0000");
    feed(&r, "1");
    CHECK(r.shorts == 1);                   /* and the next press works */
}

static void test_bounce(void)
{
    rig_t r;
    rig_init(&r);
    /* chatter on the way down, then a steady press, then a release */
    feed(&r, "1010010000000");
    feed(&r, "1");
    CHECK(r.shorts == 1 && r.longs == 0);
    /* chatter on the way up: nothing more */
    feed(&r, "011011111");
    CHECK(r.shorts == 1);
    /* a glitch shorter than the debounce: nothing */
    feed(&r, "000111");
    CHECK(r.shorts == 1 && r.longs == 0);
}

static void test_counter_saturates(void)
{
    rig_t r;
    rig_init(&r);
    hold(&r, 10000);
    CHECK(r.longs == 1 && r.b.lows == 255);
    feed(&r, "1");
    CHECK(r.b.lows == 0 && !r.b.down && !r.b.long_sent);
}

static void test_clock_wrap(void)
{
    rig_t r;
    rig_init(&r);
    r.now = UINT32_MAX - 300;
    hold(&r, 700);                          /* now wraps around during the hold */
    CHECK(r.longs == 1);
}

static void test_down_and_up(void)
{
    rig_t r;
    rig_init(&r);

    /* a short press: down once it is stable, then up and short together,
     * in that order */
    feed(&r, "000");
    CHECK(r.downs == 0);
    feed(&r, "0");
    CHECK(r.downs == 1 && r.last == BTN_DOWN);
    hold(&r, 100);
    CHECK(r.downs == 1 && r.ups == 0);
    feed(&r, "1");
    CHECK(r.ups == 1 && r.shorts == 1 && r.last == BTN_SHORT);

    /* a long press: down, long while held, and the release is only an up */
    hold(&r, 700);
    CHECK(r.downs == 2 && r.longs == 1 && r.ups == 1);
    feed(&r, "1");
    CHECK(r.ups == 2 && r.shorts == 1 && r.last == BTN_UP);

    /* a touch too brief to be a press is neither */
    feed(&r, "0011");
    CHECK(r.downs == 2 && r.ups == 2);
}

int main(void)
{
    test_down_and_up();
    test_short_press();
    test_long_press();
    test_bounce();
    test_counter_saturates();
    test_clock_wrap();
    REPORT();
}
