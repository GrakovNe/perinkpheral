/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "notice.h"
#include "words.h"

/* Somebody on the network asked for something that cannot be taken back:
 * the device says what, and whoever stands at it answers - OK does it,
 * BACK does not, and so do the menu and silence. */
typedef struct {
    scene_t          scene;
    const char      *first;
    const char      *second;
    int              icon;
    const char      *action;
    scenes_answer_fn answer;
} confirm_scene_t;

/* Whoever asked takes the first answer and makes nothing of the rest: a
 * press is followed by the scene leaving the screen, which says "no". */
static scene_reaction_t confirm_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    const confirm_scene_t *confirm = (const confirm_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press != SCENE_PRESS_SHORT) {
        return reaction;
    }
    /* Answered: whoever asked gives the screen back to what had it. */
    if (button == SCENE_BUTTON_OK) {
        confirm->answer(true);
    } else if (button == SCENE_BUTTON_BACK) {
        confirm->answer(false);
    }
    return reaction;
}

/* The screen went to somebody else with the question open: not confirmed. */
static void confirm_leave(scene_t *scene)
{
    ((const confirm_scene_t *)scene)->answer(false);
}

static void confirm_draw(scene_t *scene, gfx_t *surface)
{
    const confirm_scene_t *confirm = (const confirm_scene_t *)scene;
    const notice_t notice = {
        .first = confirm->first,
        .second = confirm->second,
        .icon = confirm->icon,
        .ok = confirm->action,
        .back = word(WORD_CANCEL),
    };
    notice_draw(surface, &notice);
}

static const scene_ops_t CONFIRM_OPS = {
    .leave = confirm_leave,
    .button = confirm_button,
    .draw = confirm_draw,
};

scene_t *scene_confirm(const char *first, const char *second, int icon, const char *action,
                       scenes_answer_fn answer)
{
    static confirm_scene_t confirm = {
        .scene = { .ops = &CONFIRM_OPS, .fullscreen = true, .asks_once = true },
    };
    confirm.first = first;
    confirm.second = second;
    confirm.icon = icon;
    confirm.action = action;
    confirm.answer = answer;
    return &confirm.scene;
}
