/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "kit.h"
#include "wm.h"

#include <stdint.h>
#include <string.h>

#include "tinytest.h"

#define SCREEN_W 64
#define SCREEN_H 64

static uint8_t bits[SCREEN_W / 8 * SCREEN_H];
static gfx_t screen;

/* A scene that records what happens to it and answers a button with
 * whatever the test told it to. */
typedef struct {
    scene_t          scene;
    int              entered;
    int              left;
    int              presses;
    int              last_timer;
    int              surface_w;
    int              surface_h;
    scene_reaction_t next_reaction;
} probe_t;

static void probe_enter(scene_t *scene)
{
    ((probe_t *)scene)->entered++;
}

static void probe_leave(scene_t *scene)
{
    ((probe_t *)scene)->left++;
}

static scene_reaction_t probe_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)button;
    (void)press;
    probe_t *probe = (probe_t *)scene;
    probe->presses++;
    return probe->next_reaction;
}

/* Paints all it is given, and far beyond: the clip has to stop it. */
static void probe_draw(scene_t *scene, gfx_t *surface)
{
    probe_t *probe = (probe_t *)scene;
    probe->surface_w = surface->w;
    probe->surface_h = surface->h;
    gfx_fill(surface, -1000, -1000, 5000, 5000, true);
}

static scene_reaction_t probe_timer(scene_t *scene, int timer_id)
{
    probe_t *probe = (probe_t *)scene;
    probe->last_timer = timer_id;
    return probe->next_reaction;
}

static const scene_ops_t PROBE_OPS = {
    .enter = probe_enter,
    .leave = probe_leave,
    .button = probe_button,
    .timer = probe_timer,
    .draw = probe_draw,
};

static void draw_nothing(scene_t *scene, gfx_t *surface)
{
    (void)scene;
    (void)surface;
}

/* Also a scene without enter and leave, which are optional. */
static const scene_ops_t BLANK_OPS = { .button = probe_button, .draw = draw_nothing };

static probe_t menu, page, app;
static int header_draws;

static int header_surface_h;

static void draw_header(gfx_t *surface, const scene_t *scene, void *context)
{
    (void)scene;
    header_surface_h = surface->h;
    header_draws += *(int *)context;
}

static int header_step = 1;
static wm_t wm;

static void start(scene_t *first)
{
    memset(&menu, 0, sizeof menu);
    memset(&page, 0, sizeof page);
    memset(&app, 0, sizeof app);
    menu.scene.ops = page.scene.ops = app.scene.ops = &PROBE_OPS;
    app.scene.fullscreen = true;
    header_draws = 0;
    gfx_init(&screen, bits, SCREEN_W, SCREEN_H);
    wm_init(&wm, &menu.scene, first, draw_header, &header_step);
}

static void press(scene_button_t button, scene_press_t how)
{
    wm_button(&wm, button, how);
}

static void test_first_scene(void)
{
    start(&app.scene);
    CHECK(wm.current == &app.scene);
    CHECK(app.entered == 1 && menu.entered == 0);
    /* the first frame is a switch: nothing was on the screen before */
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_SWITCH);
    wm_shown(&wm);
    CHECK(wm_pending_change(&wm) == WM_CLEAN);
}

static void test_buttons_reach_the_scene(void)
{
    start(&menu.scene);
    wm_shown(&wm);

    press(SCENE_BUTTON_UP, SCENE_PRESS_SHORT);
    CHECK(menu.presses == 1);
    CHECK(wm_pending_change(&wm) == WM_CLEAN); /* the scene ignored it */

    menu.next_reaction.kind = SCENE_REDRAW;
    press(SCENE_BUTTON_DOWN, SCENE_PRESS_SHORT);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_INPUT);

    /* a long press on anything but BACK is the scene's too */
    press(SCENE_BUTTON_OK, SCENE_PRESS_LONG);
    CHECK(menu.presses == 3);
}

static void test_open_and_back(void)
{
    start(&menu.scene);
    wm_shown(&wm);

    menu.next_reaction = (scene_reaction_t){ SCENE_OPEN, &page.scene };
    press(SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &page.scene);
    CHECK(menu.left == 1 && page.entered == 1);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_SWITCH);
    wm_shown(&wm);

    /* back from a page is the menu */
    page.next_reaction.kind = SCENE_BACK;
    press(SCENE_BUTTON_BACK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &menu.scene);
    CHECK(page.left == 1 && menu.entered == 2);

    /* the page ended itself, so back from the menu does not reopen it */
    wm_shown(&wm);
    menu.next_reaction.kind = SCENE_BACK;
    press(SCENE_BUTTON_BACK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &menu.scene && page.entered == 1);
    CHECK(wm_pending_change(&wm) == WM_CLEAN);

    /* a scene that "opens" itself stays where it is */
    menu.next_reaction = (scene_reaction_t){ SCENE_OPEN, &menu.scene };
    press(SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &menu.scene && menu.entered == 2);
    CHECK(wm_pending_change(&wm) == WM_CLEAN);
}

static void test_long_back_is_always_the_menu(void)
{
    start(&app.scene);
    wm_shown(&wm);

    press(SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    CHECK(wm.current == &menu.scene);
    CHECK(app.presses == 0); /* the scene is not even asked */
    CHECK(app.left == 1 && menu.entered == 1);

    /* already there: nothing happens, nothing is redrawn */
    wm_shown(&wm);
    press(SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    CHECK(wm.current == &menu.scene && menu.entered == 1);
    CHECK(wm_pending_change(&wm) == WM_CLEAN);
}

static void test_back_from_the_menu(void)
{
    /* the long press is the beginning again: back in the menu leads nowhere */
    start(&app.scene);
    press(SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    menu.next_reaction.kind = SCENE_BACK;
    press(SCENE_BUTTON_BACK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &menu.scene && app.entered == 1 && wm.depth == 0);

    /* the menu was the first thing on the screen: there is nowhere to go */
    start(&menu.scene);
    wm_shown(&wm);
    menu.next_reaction.kind = SCENE_BACK;
    press(SCENE_BUTTON_BACK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &menu.scene);
    CHECK(wm_pending_change(&wm) == WM_CLEAN);
}

static void test_stronger_change_wins(void)
{
    start(&menu.scene);
    /* the switch is still waiting for the panel when a key arrives */
    menu.next_reaction.kind = SCENE_REDRAW;
    press(SCENE_BUTTON_DOWN, SCENE_PRESS_SHORT);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_SWITCH);
    wm_shown(&wm);

    wm_invalidate(&wm, WM_CHANGED_BY_DATA);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_DATA);
    press(SCENE_BUTTON_DOWN, SCENE_PRESS_SHORT);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_INPUT);
    wm_invalidate(&wm, WM_CHANGED_BY_DATA);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_INPUT);
}

static void test_how_a_scene_wants_to_be_shown(void)
{
    start(&app.scene);
    wm_shown(&wm);
    /* from a timer, nobody looking - but the scene says it matters now */
    app.next_reaction.kind = SCENE_REDRAW_SOON;
    wm_timer(&wm, 0);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_URGENTLY);
    /* a plain redraw after it does not take the urgency away */
    app.next_reaction.kind = SCENE_REDRAW;
    wm_timer(&wm, 0);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_URGENTLY);
    wm_shown(&wm);

    /* under a finger it is shown at once anyway */
    app.next_reaction.kind = SCENE_REDRAW_SOON;
    press(SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_INPUT);
    wm_shown(&wm);

    /* a clean picture is asked for from anywhere, and a switch is still more */
    app.next_reaction.kind = SCENE_REDRAW_CLEAN;
    wm_timer(&wm, 0);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_CLEANLY);
    press(SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_SWITCH);
}

static void test_timer(void)
{
    start(&app.scene);
    wm_shown(&wm);
    app.next_reaction.kind = SCENE_REDRAW;
    wm_timer(&wm, 3);
    CHECK(app.last_timer == 3);
    /* nobody is looking: a timer's redraw is data, not input */
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_DATA);

    /* a scene without a timer handler is left alone */
    press(SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    menu.scene.ops = &BLANK_OPS;
    wm_shown(&wm);
    wm_timer(&wm, 1);
    CHECK(wm_pending_change(&wm) == WM_CLEAN);
}

static void test_suspend_and_resume(void)
{
    start(&app.scene);
    wm_shown(&wm);

    /* the scene lets go, and nothing reaches it meanwhile */
    wm_suspend(&wm);
    CHECK(app.left == 1 && wm.current == &app.scene);
    press(SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    wm_timer(&wm, 0);
    CHECK(app.presses == 0 && app.last_timer == 0);
    wm_suspend(&wm); /* twice is once */
    CHECK(app.left == 1);

    /* it comes back as from a fresh start, and is drawn again */
    wm_resume(&wm);
    CHECK(app.entered == 2 && app.left == 1);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_DATA);
    wm_resume(&wm);
    CHECK(app.entered == 2);

    /* suspended, the screen goes to another scene: the old one is not
     * asked to leave a second time */
    wm_shown(&wm);
    wm_suspend(&wm);
    wm_open(&wm, &page.scene);
    CHECK(wm.current == &page.scene && app.left == 2 && page.entered == 1);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_SWITCH);
    press(SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    CHECK(page.presses == 1);

    /* suspended, and the screen is given back to the very same scene */
    wm_shown(&wm);
    wm_suspend(&wm);
    wm_open(&wm, &page.scene);
    CHECK(page.entered == 2 && page.left == 1);
}

static int black_pixels(int x0, int y0, int x1, int y1)
{
    int count = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            count += gfx_get(&screen, x, y);
        }
    }
    return count;
}

static void test_frame_with_header(void)
{
    start(&menu.scene);
    wm_draw(&wm, &screen);
    CHECK(header_draws == 1 && header_surface_h == WM_HEADER_HEIGHT);
    /* the scene gets the area under the header and cannot leave it */
    CHECK(menu.surface_w == SCREEN_W && menu.surface_h == SCREEN_H - WM_HEADER_HEIGHT - WM_BOTTOM_EDGE);
    CHECK(black_pixels(0, 0, SCREEN_W, WM_HEADER_HEIGHT) == 0);
    CHECK(black_pixels(0, WM_HEADER_HEIGHT, SCREEN_W, SCREEN_H - WM_BOTTOM_EDGE) ==
          SCREEN_W * (SCREEN_H - WM_HEADER_HEIGHT - WM_BOTTOM_EDGE));
    CHECK(black_pixels(0, SCREEN_H - WM_BOTTOM_EDGE, SCREEN_W, SCREEN_H) == 0);
    /* drawing alone does not mean the panel got it */
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_SWITCH);
}

static void test_fullscreen_frame(void)
{
    start(&app.scene);
    wm_draw(&wm, &screen);
    CHECK(header_draws == 0);
    CHECK(app.surface_w == SCREEN_W && app.surface_h == SCREEN_H);
    CHECK(black_pixels(0, 0, SCREEN_W, SCREEN_H) == SCREEN_W * SCREEN_H);

    /* the next frame starts clean: nothing of the application is left */
    press(SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    menu.scene.ops = &BLANK_OPS;
    wm_draw(&wm, &screen);
    CHECK(black_pixels(0, 0, SCREEN_W, SCREEN_H) == 0);
}

static void test_header_share(void)
{
    int left, right;
    /* both fit: both are whole */
    wm_header_share(100, 140, 320, &left, &right);
    CHECK(left == 100 && right == 140);
    /* the left is long: the right keeps its own, the left has the rest */
    wm_header_share(400, 140, 320, &left, &right);
    CHECK(left == 180 && right == 140);
    /* the right is long */
    wm_header_share(100, 400, 320, &left, &right);
    CHECK(left == 100 && right == 220);
    /* both are: half each */
    wm_header_share(400, 400, 320, &left, &right);
    CHECK(left == 160 && right == 160);
    /* one alone may have it all, and no more */
    wm_header_share(0, 400, 320, &left, &right);
    CHECK(left == 0 && right == 320);
}

static void test_drop(void)
{
    /* a scene that is going away: the home takes the screen from it, and
     * BACK from the home does not lead to it */
    start(&menu.scene);
    wm_open(&wm, &app.scene);
    wm_button(&wm, SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    CHECK(wm.current == &menu.scene && !wm_has(&wm, &app.scene));
    wm_close(&wm, &app.scene);
    CHECK(wm.current == &menu.scene && !wm_has(&wm, &app.scene));

    wm_open(&wm, &app.scene);
    int left = app.left;
    wm_close(&wm, &app.scene);
    CHECK(wm.current == &menu.scene && app.left == left + 1);

    /* any other scene is nothing to it */
    wm_open(&wm, &page.scene);
    wm_close(&wm, &app.scene);
    CHECK(wm.current == &page.scene);
}

static void test_stack(void)
{
    static probe_t question, alarm;
    start(&menu.scene);
    memset(&question, 0, sizeof question);
    memset(&alarm, 0, sizeof alarm);
    question.scene.ops = alarm.scene.ops = &PROBE_OPS;

    /* the menu opens the application: it is all there is */
    menu.next_reaction = (scene_reaction_t){ SCENE_OPEN, &app.scene };
    press(SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &app.scene && wm.depth == 1);

    /* something over it: the application leaves and waits */
    wm_open(&wm, &question.scene);
    CHECK(wm.current == &question.scene && app.left == 1 && wm_has(&wm, &app.scene));
    /* and is there again, entered anew, when that is closed */
    wm_close(&wm, &question.scene);
    CHECK(wm.current == &app.scene && app.entered == 2 && question.left == 1);

    /* a scene opens another over itself; BACK there is one step down */
    app.next_reaction = (scene_reaction_t){ SCENE_OPEN, &page.scene };
    press(SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &page.scene && wm.depth == 2);
    page.next_reaction.kind = SCENE_BACK;
    press(SCENE_BUTTON_BACK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &app.scene && wm.depth == 1);

    /* the long press empties the stack; a question over the menu, closed,
     * leaves the menu, and BACK in the menu leads nowhere */
    press(SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    CHECK(wm.current == &menu.scene && wm.depth == 0);
    wm_open(&wm, &question.scene);
    CHECK(wm.current == &question.scene && wm.depth == 1);
    wm_close(&wm, &question.scene);
    CHECK(wm.current == &menu.scene);
    menu.next_reaction.kind = SCENE_BACK;
    press(SCENE_BUTTON_BACK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &menu.scene && wm.depth == 0);
    wm_open(&wm, &app.scene);

    /* a scene closed while it waits under another is simply not returned to */
    wm_open(&wm, &alarm.scene);
    wm_close(&wm, &app.scene);
    CHECK(wm.current == &alarm.scene && wm.depth == 1);
    wm_back(&wm);
    CHECK(wm.current == &menu.scene && wm.depth == 0);
    /* and BACK with nothing under is the menu, still */
    wm_back(&wm);
    CHECK(wm.current == &menu.scene);

    /* opened again - its alarm came - a scene moves to the top, and BACK
     * returns to what the user was looking at */
    wm_open(&wm, &app.scene);
    wm_open(&wm, &page.scene);
    wm_open(&wm, &alarm.scene);
    wm_open(&wm, &app.scene);
    CHECK(wm.current == &app.scene && wm.depth == 3);
    wm_back(&wm);
    CHECK(wm.current == &alarm.scene && wm.depth == 2);
    wm_back(&wm);
    CHECK(wm.current == &page.scene && wm.depth == 1);
    wm_open(&wm, &app.scene);
    wm_close(&wm, &page.scene);
    CHECK(wm.current == &app.scene && wm.depth == 1);

    /* the long press over a question takes that away too */
    wm_open(&wm, &question.scene);
    press(SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    CHECK(wm.current == &menu.scene && wm.depth == 0 && !wm_has(&wm, &question.scene));

    /* opening from the menu starts anew, whatever the menu was over */
    menu.next_reaction = (scene_reaction_t){ SCENE_OPEN, &page.scene };
    press(SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &page.scene && wm.depth == 1 && !wm_has(&wm, &app.scene));

    /* a scene that gives its place away is not returned to */
    page.next_reaction = (scene_reaction_t){ SCENE_REPLACE, &app.scene };
    press(SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &app.scene && wm.depth == 1);
    page.next_reaction.kind = SCENE_IGNORED;

    /* more than the stack holds: the oldest is forgotten, the top stays */
    static probe_t many[WM_STACK_DEPTH + 1];
    for (int i = 0; i <= WM_STACK_DEPTH; i++) {
        memset(&many[i], 0, sizeof many[i]);
        many[i].scene.ops = &PROBE_OPS;
        wm_open(&wm, &many[i].scene);
    }
    CHECK(wm.depth == WM_STACK_DEPTH && wm.current == &many[WM_STACK_DEPTH].scene &&
          !wm_has(&wm, &app.scene));
}


/* ---- the chart of the kit ---- */

static int ink_in(const gfx_t *g, int x, int y, int w, int h)
{
    int count = 0;
    for (int row = y; row < y + h; row++) {
        for (int column = x; column < x + w; column++) {
            count += gfx_get(g, column, row);
        }
    }
    return count;
}

static void test_chart(void)
{
    static uint8_t bits[200 / 8 * 100];
    gfx_t g;
    gfx_init(&g, bits, 200, 100);

    /* bars: the greatest value reaches the top, the least stays at the bottom */
    const int32_t two[] = { 0, 10 };
    kit_chart_t chart = { .left = 0, .top = 0, .width = 200, .height = 100,
                          .values = two, .count = 2, .kind = KIT_CHART_BARS };
    gfx_clear(&g, false);
    kit_chart(&g, &chart);
    CHECK(gfx_get(&g, 150, 0) && gfx_get(&g, 150, 99));
    CHECK(!gfx_get(&g, 50, 0) && !gfx_get(&g, 50, 50) && gfx_get(&g, 50, 99));
    CHECK(!gfx_get(&g, 199, 50)); /* the gap after a bar */

    /* a rising line: ink goes from the bottom left to the top right */
    const int32_t rising[] = { 1, 2, 3, 4, 5 };
    chart.values = rising;
    chart.count = 5;
    chart.kind = KIT_CHART_LINE;
    gfx_clear(&g, false);
    kit_chart(&g, &chart);
    CHECK(ink_in(&g, 0, 90, 10, 10) > 0 && ink_in(&g, 190, 0, 10, 10) > 0);
    CHECK(ink_in(&g, 0, 0, 60, 40) == 0 && ink_in(&g, 140, 60, 60, 40) == 0);

    /* an area has more ink than its line, and nothing over the line */
    int line_ink = ink_in(&g, 0, 0, 200, 100);
    chart.kind = KIT_CHART_AREA;
    chart.fill_shade = 4;
    gfx_clear(&g, false);
    kit_chart(&g, &chart);
    CHECK(ink_in(&g, 0, 0, 200, 100) > line_ink && ink_in(&g, 0, 0, 60, 40) == 0);
    CHECK(g.shade == GFX_SHADE_SOLID); /* the surface's shade is given back */

    /* a gap: nothing is drawn across a missing value */
    const int32_t broken[] = { 5, 5, KIT_CHART_NO_VALUE, 5, 5 };
    chart.values = broken;
    chart.kind = KIT_CHART_LINE;
    gfx_clear(&g, false);
    kit_chart(&g, &chart);
    CHECK(ink_in(&g, 60, 0, 30, 100) == 0 && ink_in(&g, 110, 0, 30, 100) == 0);
    CHECK(ink_in(&g, 0, 0, 50, 100) > 0 && ink_in(&g, 150, 0, 50, 100) > 0);

    /* steps: the value holds, so the middle of a span is at the old level */
    const int32_t stepped[] = { 0, 10 };
    chart.values = stepped;
    chart.count = 2;
    chart.kind = KIT_CHART_STEPS;
    gfx_clear(&g, false);
    kit_chart(&g, &chart);
    CHECK(gfx_get(&g, 100, 98) && !gfx_get(&g, 100, 50) && gfx_get(&g, 198, 50));

    /* the captions take room at the left and the plot moves right */
    chart.kind = KIT_CHART_BARS;
    chart.flags = KIT_CHART_RANGE;
    gfx_clear(&g, false);
    kit_chart(&g, &chart);
    CHECK(ink_in(&g, 0, 0, 16, 12) > 0 && ink_in(&g, 0, 88, 16, 12) > 0);

    /* nothing to scale by, extremes, one value, no values: drawn or left alone, never a fault */
    const int32_t flat[] = { 7, 7, 7 };
    const int32_t extremes[] = { INT32_MAX, INT32_MIN + 1, 0 };
    const int32_t none[] = { KIT_CHART_NO_VALUE, KIT_CHART_NO_VALUE };
    for (int kind = KIT_CHART_LINE; kind <= KIT_CHART_STEPS; kind++) {
        for (int flags = 0; flags < 16; flags++) {
            kit_chart_t odd = { .left = -5, .top = -5, .width = 300, .height = 150,
                                .kind = (kit_chart_kind_t)kind, .flags = flags, .decimals = 9 };
            odd.values = flat; odd.count = 3; kit_chart(&g, &odd);
            odd.values = extremes; odd.count = 3; kit_chart(&g, &odd);
            odd.values = flat; odd.count = 1; kit_chart(&g, &odd);
            odd.values = none; odd.count = 2; kit_chart(&g, &odd);
            odd.width = 1; odd.height = 3; odd.values = extremes; odd.count = 3; kit_chart(&g, &odd);
        }
    }
    gfx_clear(&g, false);
    kit_chart_t middle = { .left = 0, .top = 0, .width = 200, .height = 100,
                           .values = flat, .count = 3, .kind = KIT_CHART_LINE };
    kit_chart(&g, &middle);
    CHECK(ink_in(&g, 0, 40, 200, 20) > 0 && ink_in(&g, 0, 0, 200, 30) == 0 &&
          ink_in(&g, 0, 70, 200, 30) == 0);
}

int main(void)
{
    test_chart();
    test_stack();
    test_drop();
    test_header_share();
    test_first_scene();
    test_buttons_reach_the_scene();
    test_open_and_back();
    test_long_back_is_always_the_menu();
    test_back_from_the_menu();
    test_stronger_change_wins();
    test_how_a_scene_wants_to_be_shown();
    test_timer();
    test_suspend_and_resume();
    test_frame_with_header();
    test_fullscreen_frame();
    REPORT();
}
