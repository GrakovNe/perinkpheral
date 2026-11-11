/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "screens.h"
#include "tinytest.h"

/* The menu and the splash under the buttons. The device page needs the
 * chip and is left to the device. */

static scene_t first_target, second_target, third_target;
static const menu_entry_t ENTRIES[] = {
    { "First", NULL, &first_target },
    { "Second", NULL, &second_target },
    { "Third", NULL, &third_target },
};
#define ENTRY_COUNT ((int)(sizeof ENTRIES / sizeof ENTRIES[0]))

static scene_reaction_t press(scene_t *scene, scene_button_t button)
{
    return scene->ops->button(scene, button, SCENE_PRESS_SHORT);
}

static scene_t *opened_by_ok(scene_t *menu)
{
    scene_reaction_t reaction = press(menu, SCENE_BUTTON_OK);
    return reaction.kind == SCENE_OPEN ? reaction.open : NULL;
}

static void test_menu_moves_and_wraps(void)
{
    scene_t *menu = scene_menu(ENTRIES, ENTRY_COUNT);
    CHECK(!menu->fullscreen);
    CHECK(opened_by_ok(menu) == &first_target);

    CHECK(press(menu, SCENE_BUTTON_DOWN).kind == SCENE_REDRAW);
    CHECK(opened_by_ok(menu) == &second_target);

    press(menu, SCENE_BUTTON_DOWN);
    press(menu, SCENE_BUTTON_DOWN); /* past the last: around to the first */
    CHECK(opened_by_ok(menu) == &first_target);

    press(menu, SCENE_BUTTON_UP); /* before the first: around to the last */
    CHECK(opened_by_ok(menu) == &third_target);
}

static void test_menu_back_and_long(void)
{
    scene_t *menu = scene_menu(ENTRIES, ENTRY_COUNT);
    CHECK(press(menu, SCENE_BUTTON_BACK).kind == SCENE_BACK);

    /* a long press means nothing in the menu and moves nothing */
    CHECK(menu->ops->button(menu, SCENE_BUTTON_DOWN, SCENE_PRESS_LONG).kind == SCENE_IGNORED);
    CHECK(opened_by_ok(menu) == &first_target);
}

static void test_empty_menu(void)
{
    scene_t *menu = scene_menu(NULL, 0);
    CHECK(press(menu, SCENE_BUTTON_DOWN).kind == SCENE_IGNORED);
    CHECK(press(menu, SCENE_BUTTON_OK).kind == SCENE_IGNORED);
    /* but the user can still leave it */
    CHECK(press(menu, SCENE_BUTTON_BACK).kind == SCENE_BACK);
}

static void test_menu_window(void)
{
    const int visible = 5;
    /* everything fits: the window never moves */
    CHECK(screens_menu_first_row(3, 0, visible) == 0);
    CHECK(screens_menu_first_row(3, 2, visible) == 0);
    CHECK(screens_menu_first_row(5, 4, visible) == 0);

    /* longer than the surface: the selection stays in the middle... */
    CHECK(screens_menu_first_row(20, 0, visible) == 0);
    CHECK(screens_menu_first_row(20, 2, visible) == 0);
    CHECK(screens_menu_first_row(20, 3, visible) == 1);
    CHECK(screens_menu_first_row(20, 10, visible) == 8);
    /* ...until the last rows, where the window stops at the end */
    CHECK(screens_menu_first_row(20, 17, visible) == 15);
    CHECK(screens_menu_first_row(20, 19, visible) == 15);

    CHECK(screens_menu_first_row(0, 0, visible) == 0);
}

/* What the splash shows: a scene that counts what is done to it. */
static int picture_enters, picture_leaves, picture_draws;
static bool picture_shown;

static void picture_enter(scene_t *scene) { (void)scene; picture_enters++; }
static void picture_leave(scene_t *scene) { (void)scene; picture_leaves++; }
static void picture_draw(scene_t *scene, gfx_t *surface) { (void)scene; (void)surface; picture_draws++; }
static bool picture_is_shown(scene_t *scene) { (void)scene; return picture_shown; }

static const scene_ops_t PICTURE_OPS = { .enter = picture_enter, .leave = picture_leave,
                                         .draw = picture_draw };
static scene_t picture = { .ops = &PICTURE_OPS, .fullscreen = true };

static int timer_set_id, timer_set_ms;

static void record_timer(int id, int32_t milliseconds)
{
    timer_set_id = id;
    timer_set_ms = milliseconds;
}

static void test_boot_splash(void)
{
    scene_t *boot = scene_boot_splash(&first_target, record_timer, NULL, NULL);
    CHECK(boot->fullscreen);

    /* it asks to be woken, and then moves on by itself */
    timer_set_ms = 0;
    boot->ops->enter(boot);
    CHECK(timer_set_ms > 0);
    scene_reaction_t on_timer = boot->ops->timer(boot, timer_set_id);
    CHECK(on_timer.kind == SCENE_OPEN && on_timer.open == &first_target);

    /* a press does not wait for the timer */
    scene_reaction_t on_press = press(boot, SCENE_BUTTON_OK);
    CHECK(on_press.kind == SCENE_OPEN && on_press.open == &first_target);

    /* leaving cancels the timer, or it would fire into the next scene */
    boot->ops->leave(boot);
    CHECK(timer_set_ms == 0);

    /* a long press is somebody else's */
    CHECK(boot->ops->button(boot, SCENE_BUTTON_OK, SCENE_PRESS_LONG).kind == SCENE_IGNORED);

    /* with a picture: it is held while the splash has the screen, and drawn
     * only while it can be */
    static uint8_t frame[400 * 300 / 8];
    gfx_t surface;
    gfx_init(&surface, frame, 400, 300);
    boot = scene_boot_splash(&first_target, record_timer, &picture, picture_is_shown);
    boot->ops->enter(boot);
    CHECK(picture_enters == 1 && timer_set_ms > 0);
    picture_shown = true;
    boot->ops->draw(boot, &surface);
    CHECK(picture_draws == 1);
    picture_shown = false;
    boot->ops->draw(boot, &surface);
    CHECK(picture_draws == 1);
    boot->ops->leave(boot);
    CHECK(picture_leaves == 1 && timer_set_ms == 0);
}

int main(void)
{
    test_boot_splash();
    test_menu_moves_and_wraps();
    test_menu_back_and_long();
    test_empty_menu();
    test_menu_window();
    REPORT();
}
