/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "kit.h"
#include "scenes.h"

#include <stddef.h>

#include "list.h"
#include "menu_order.h"
#include "screens.h"
#include "tinytest.h"

/* The menu and the splash under the buttons. The device page needs the
 * chip and is left to the device. */

static scene_t first_target, second_target, third_target;
static menu_entry_t ENTRIES[] = {
    { "First", NULL, &first_target, "first" },
    { "Second", NULL, &second_target, "second" },
    { "Third", NULL, &third_target, "third" },
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

    CHECK(press(menu, SCENE_BUTTON_DOWN).kind == SCENE_REDRAW_NO_BLINK);
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

    /* with a default application BACK is the way to it */
    scene_menu_set_back_target(&second_target);
    scene_reaction_t back = press(menu, SCENE_BUTTON_BACK);
    CHECK(back.kind == SCENE_OPEN && back.open == &second_target);
    scene_menu_set_back_target(NULL);
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

/* A surface on which a list has room for five rows. */
#define FIVE_ROWS_HEIGHT 220

static int first_row(int count, int selected)
{
    static uint8_t bits[400 / 8 * FIVE_ROWS_HEIGHT];
    gfx_t g;
    gfx_init(&g, bits, 400, FIVE_ROWS_HEIGHT);
    const list_t list = { .count = count, .selected = selected };
    return list_first_row(&g, &list);
}

static void test_menu_window(void)
{
    /* everything fits: the window never moves */
    CHECK(first_row(3, 0) == 0);
    CHECK(first_row(3, 2) == 0);
    CHECK(first_row(5, 4) == 0);

    /* longer than the surface: the selection stays in the middle... */
    CHECK(first_row(20, 0) == 0);
    CHECK(first_row(20, 2) == 0);
    CHECK(first_row(20, 3) == 1);
    CHECK(first_row(20, 10) == 8);
    /* ...until the last rows, where the window stops at the end */
    CHECK(first_row(20, 17) == 15);
    CHECK(first_row(20, 19) == 15);

    CHECK(first_row(0, 0) == 0);

    /* a list that has only a part of the surface: under a title, over a
     * row of buttons */
    static uint8_t bits[400 / 8 * 260];
    gfx_t g;
    gfx_init(&g, bits, 400, 260);
    list_t part = { .count = 20, .selected = 19, .top = 60, .height = 150 };
    CHECK(list_visible_rows(&g, &part) == 3);
    CHECK(list_first_row(&g, &part) == 17);
    part.height = 0; /* down to the bottom */
    CHECK(list_visible_rows(&g, &part) == 4);
    /* its rows are drawn inside it and nowhere else */
    const list_row_t row = { .label = "Row", .icon = LIST_NO_ICON };
    gfx_clear(&g, false);
    part.selected = 0;
    list_draw_row(&g, &part, 0, &row, false);
    bool ink_above = false;
    for (int y = 0; y < part.top && !ink_above; y++) {
        for (int x = 0; x < 400 && !ink_above; x++) {
            ink_above = gfx_get(&g, x, y);
        }
    }
    CHECK(!ink_above && gfx_get(&g, 20, part.top + 20));

    /* facts nobody chooses from: the list is where the user scrolled it,
     * and no further than its end */
    list_t facts = { .count = 10, .selected = LIST_NONE_SELECTED, .first = 3 };
    CHECK(list_first_row(&g, &facts) == 3);
    facts.first = 99;
    CHECK(list_first_row(&g, &facts) == 10 - list_visible_rows(&g, &facts));
    facts.first = -5;
    CHECK(list_first_row(&g, &facts) == 0);
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
    scene_t *boot = scene_boot_splash(&first_target, record_timer, &picture, picture_is_shown);
    CHECK(boot->fullscreen);

    /* it holds the picture, asks to be woken, and then moves on by itself */
    picture_shown = true;
    timer_set_ms = 0;
    boot->ops->enter(boot);
    CHECK(picture_enters == 1 && timer_set_ms > 1000);
    static uint8_t frame[400 * 300 / 8];
    gfx_t surface;
    gfx_init(&surface, frame, 400, 300);
    boot->ops->draw(boot, &surface);
    CHECK(picture_draws == 1);
    scene_reaction_t on_timer = boot->ops->timer(boot, timer_set_id);
    CHECK(on_timer.kind == SCENE_REPLACE && on_timer.open == &first_target);

    /* a press does not wait for the timer; a long one is somebody else's */
    scene_reaction_t on_press = press(boot, SCENE_BUTTON_OK);
    CHECK(on_press.kind == SCENE_REPLACE && on_press.open == &first_target);
    CHECK(boot->ops->button(boot, SCENE_BUTTON_OK, SCENE_PRESS_LONG).kind == SCENE_IGNORED);

    /* leaving cancels the timer, or it would fire into the next scene */
    boot->ops->leave(boot);
    CHECK(picture_leaves == 1 && timer_set_ms == 0);

    /* set aside and back - an application took an answer off the screen:
     * it has lingered, and moves on without waiting six seconds anew */
    boot->ops->enter(boot);
    CHECK(timer_set_ms > 0 && timer_set_ms < 100);
    boot->ops->leave(boot);

    /* a picture that cannot be shown: nothing is drawn, and it does not wait */
    picture_shown = false;
    boot->ops->enter(boot);
    CHECK(timer_set_ms > 0 && timer_set_ms < 100);
    boot->ops->draw(boot, &surface);
    CHECK(picture_draws == 1);
    boot->ops->leave(boot);
}

/* Text over several lines: broken at spaces, no more lines than asked for. */
static void test_text_lines(void)
{
    static uint8_t frame[400 * 300 / 8];
    gfx_t g;
    gfx_init(&g, frame, 400, 300);
    int word = gfx_text_font_width(gfx_font_system(), "word");
    CHECK(kit_text_lines(&g, 0, 0, KIT_TEXT, "word", 400, 4) == 1);
    CHECK(kit_text_lines(&g, 0, 0, KIT_TEXT, "word word word", word + 4, 4) == 3);
    CHECK(kit_text_lines(&g, 0, 0, KIT_TEXT, "word word word", word * 2 + 40, 4) == 2);
    CHECK(kit_text_lines(&g, 0, 0, KIT_TEXT, "word word word", word + 4, 2) == 2);
    CHECK(kit_text_lines(&g, 0, 0, KIT_TEXT, "", 400, 4) == 0);
    CHECK(kit_text_lines(&g, 0, 0, KIT_TEXT, "   ", 400, 4) == 0);
    /* a word wider than the line is a line of its own, cut */
    CHECK(kit_text_lines(&g, 0, 0, KIT_TEXT, "wordwordwordword word", word + 4, 4) == 2);
}

static int answers, confirmations;

static void record_answer(bool is_confirmed)
{
    answers++;
    confirmations += is_confirmed ? 1 : 0;
}

/* The host's notices take the whole screen. */
static void draw_whole_screen(scene_t *scene)
{
    static uint8_t pixels[400 / 8 * 300];
    gfx_t surface;
    gfx_init(&surface, pixels, 400, 300);
    scene->ops->draw(scene, &surface);
}

static void test_confirm(void)
{
    /* OK is the answer "yes", BACK is "no" */
    scene_t *confirm = scene_confirm("Update", "firmware?", 0, "Update", record_answer);
    answers = confirmations = 0;
    CHECK(press(confirm, SCENE_BUTTON_OK).kind == SCENE_IGNORED);
    CHECK(answers == 1 && confirmations == 1);
    CHECK(press(confirm, SCENE_BUTTON_BACK).kind == SCENE_IGNORED);
    CHECK(answers == 2 && confirmations == 1);
    /* the other two buttons answer nothing, and so does a press that is not a short one */
    press(confirm, SCENE_BUTTON_UP);
    press(confirm, SCENE_BUTTON_DOWN);
    CHECK(confirm->ops->button(confirm, SCENE_BUTTON_OK, SCENE_PRESS_DOWN).kind == SCENE_IGNORED);
    CHECK(answers == 2);
    /* losing the screen is "no" */
    confirm->ops->leave(confirm);
    CHECK(answers == 3 && confirmations == 1);
    draw_whole_screen(confirm);
}

static void test_working(void)
{
    scene_t *working = scene_working("Updating", "in progress");
    for (int button = SCENE_BUTTON_UP; button <= SCENE_BUTTON_BACK; button++) {
        CHECK(press(working, (scene_button_t)button).kind == SCENE_IGNORED);
    }
    draw_whole_screen(working);
}

static int formats;

static void record_format(void)
{
    formats++;
}

static void test_storage_error(void)
{
    static scene_t next_scene;
    scene_t *storage = scene_storage_error(&next_scene, record_format);
    formats = 0;
    /* BACK leads on with nothing erased */
    scene_reaction_t reaction = press(storage, SCENE_BUTTON_BACK);
    CHECK(reaction.kind == SCENE_REPLACE && reaction.open == &next_scene && formats == 0);
    /* the buttons that are not named do nothing */
    CHECK(press(storage, SCENE_BUTTON_DOWN).kind == SCENE_IGNORED && formats == 0);
    press(storage, SCENE_BUTTON_OK);
    CHECK(formats == 1);
    draw_whole_screen(storage);
}

/* ---- moving a row of the menu ---- */

static menu_entry_t kept_order[8];
static int          kept_count, kept_times;

static void keep(const menu_entry_t *entries, int count)
{
    for (int i = 0; i < count; i++) {
        kept_order[i] = entries[i];
    }
    kept_count = count;
    kept_times++;
}

static scene_reaction_t long_press(scene_t *scene, scene_button_t button)
{
    return scene->ops->button(scene, button, SCENE_PRESS_LONG);
}

static void test_menu_order(void)
{
    menu_entry_t entries[] = {
        { "A", NULL, &first_target, "a" }, { "B", NULL, &second_target, "b" },
        { "C", NULL, &third_target, "c" }, { "S", NULL, NULL, "sys.settings" },
    };
    /* Moving the second down two: A C S B. OK keeps it and says so. */
    scene_menu_set_on_order(keep);
    kept_times = 0;
    scene_t *menu = scene_menu(entries, 4);
    press(menu, SCENE_BUTTON_DOWN);
    CHECK(long_press(menu, SCENE_BUTTON_OK).kind == SCENE_REDRAW_NO_BLINK);
    CHECK(press(menu, SCENE_BUTTON_DOWN).kind == SCENE_REDRAW_NO_BLINK);
    press(menu, SCENE_BUTTON_DOWN);
    press(menu, SCENE_BUTTON_DOWN); /* at the end it stays: no way round */
    CHECK(entries[3].scene == &second_target && entries[1].scene == &third_target);
    CHECK(kept_times == 0);
    CHECK(press(menu, SCENE_BUTTON_OK).kind == SCENE_REDRAW_NO_BLINK); /* kept, not opened */
    CHECK(kept_times == 1 && kept_count == 4 && kept_order[3].scene == &second_target);
    CHECK(opened_by_ok(menu) == &second_target); /* the row it was moved with stays chosen */

    /* BACK puts a moved row back where it was, and nothing is kept. */
    press(menu, SCENE_BUTTON_UP); /* on S, the third */
    long_press(menu, SCENE_BUTTON_OK);
    press(menu, SCENE_BUTTON_UP);
    press(menu, SCENE_BUTTON_UP);
    press(menu, SCENE_BUTTON_UP); /* at the top it stays */
    CHECK(entries[0].name[0] == 's');
    CHECK(press(menu, SCENE_BUTTON_BACK).kind == SCENE_REDRAW_NO_BLINK); /* not out of the menu */
    CHECK(entries[0].scene == &first_target && entries[2].name[0] == 's' && kept_times == 1);
    CHECK(opened_by_ok(menu) == NULL); /* the system one has no scene here: put back on it */

    /* The menu left while a row is moved: put back, as BACK does. */
    long_press(menu, SCENE_BUTTON_OK);
    press(menu, SCENE_BUTTON_UP);
    menu->ops->leave(menu);
    CHECK(entries[2].name[0] == 's' && kept_times == 1);
    /* A long BACK - the beginning again - puts it back too. */
    long_press(menu, SCENE_BUTTON_OK);
    press(menu, SCENE_BUTTON_UP);
    CHECK(long_press(menu, SCENE_BUTTON_BACK).kind == SCENE_REDRAW_NO_BLINK);
    CHECK(entries[2].name[0] == 's' && kept_times == 1);
    CHECK(long_press(menu, SCENE_BUTTON_BACK).kind == SCENE_IGNORED); /* nothing to put back */
    /* OK without a move keeps nothing. */
    long_press(menu, SCENE_BUTTON_OK);
    press(menu, SCENE_BUTTON_OK);
    CHECK(kept_times == 1);
    /* One entry has nowhere to go. */
    scene_t *alone = scene_menu(entries, 1);
    CHECK(long_press(alone, SCENE_BUTTON_OK).kind == SCENE_IGNORED);
    scene_menu_set_on_order(NULL);

    /* The order as it is kept and applied: a name a line; what it does not
     * name comes first, as it came; what it names that is no entry is passed over. */
    menu_entry_t kept[] = { { "A", NULL, NULL, "a" }, { "C", NULL, NULL, "c" }, { "S", NULL, NULL, "sys.settings" } };
    char order[MENU_ORDER_MAX];
    CHECK(menu_order_write(kept, 3, order, sizeof order));
    CHECK_STR(order, "a\nc\nsys.settings\n");
    char tiny[4];
    CHECK(!menu_order_write(kept, 3, tiny, sizeof tiny) && tiny[0] == '\0');
    menu_entry_t now[] = { { "S", NULL, NULL, "sys.settings" }, { "N", NULL, NULL, "new" },
                           { "C", NULL, NULL, "c" }, { "M", NULL, NULL, "newer" }, { "A", NULL, NULL, "a" } };
    menu_order_apply(now, 5, "a\ngone\nc\nsys.settings\n");
    CHECK_STR(now[0].name, "new");
    CHECK_STR(now[1].name, "newer");
    CHECK_STR(now[2].name, "a");
    CHECK_STR(now[3].name, "c");
    CHECK_STR(now[4].name, "sys.settings");
    /* A removed application forgets its place: back again, it comes first. */
    char forgetting[] = "a\nweather\nsys.settings\n";
    CHECK(menu_order_forget(forgetting, "weather"));
    CHECK_STR(forgetting, "a\nsys.settings\n");
    CHECK(!menu_order_forget(forgetting, "weath"));
    CHECK(menu_order_forget(forgetting, "sys.settings"));
    CHECK_STR(forgetting, "a\n");
    char unended[] = "a\nb";
    CHECK(menu_order_forget(unended, "b"));
    CHECK_STR(unended, "a\n");
    menu_entry_t same[] = { { "B", NULL, NULL, "b" }, { "A", NULL, NULL, "a" } };
    menu_order_apply(same, 2, "");
    CHECK_STR(same[0].name, "b"); /* no order set: as they came */
}

int main(void)
{
    test_menu_order();
    test_confirm();
    test_working();
    test_storage_error();
    test_boot_splash();
    test_text_lines();
    test_menu_moves_and_wraps();
    test_menu_back_and_long();
    test_empty_menu();
    test_menu_window();
    REPORT();
}
