/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "screens.h"

/* Somebody on the network asked for something that cannot be taken back:
 * the device says what, and whoever stands at it answers. Cancel is where
 * the selection starts; BACK, the menu and silence all mean the same. */
typedef struct {
    scene_t           scene;
    const char       *question;
    const char       *detail;
    const char       *action;
    scenes_answer_fn  answer;
    bool              action_is_selected;
} confirm_scene_t;

/* Whoever asked takes the first answer and makes nothing of the rest: a
 * press is followed by the scene leaving the screen, which says "no". */
static void give_answer(confirm_scene_t *confirm, bool is_confirmed)
{
    confirm->answer(is_confirmed);
}

static scene_reaction_t confirm_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    confirm_scene_t *confirm = (confirm_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press != SCENE_PRESS_SHORT) {
        return reaction;
    }
    switch (button) {
    case SCENE_BUTTON_UP:
    case SCENE_BUTTON_DOWN:
        confirm->action_is_selected = !confirm->action_is_selected;
        reaction.kind = SCENE_REDRAW_NO_BLINK;
        break;
    case SCENE_BUTTON_OK:
        give_answer(confirm, confirm->action_is_selected);
        reaction.kind = SCENE_BACK;
        break;
    case SCENE_BUTTON_BACK:
        give_answer(confirm, false);
        reaction.kind = SCENE_BACK;
        break;
    }
    return reaction;
}

/* The screen went to somebody else with the question open: not confirmed. */
static void confirm_leave(scene_t *scene)
{
    give_answer((confirm_scene_t *)scene, false);
}

static void confirm_draw(scene_t *scene, gfx_t *surface)
{
    const confirm_scene_t *confirm = (const confirm_scene_t *)scene;
    const screens_question_t question = {
        .title = confirm->question,
        .question = confirm->detail,
        .cancel = "Cancel",
        .action = confirm->action,
        .action_is_selected = confirm->action_is_selected,
    };
    screens_question(surface, &question);
}

static const scene_ops_t CONFIRM_OPS = {
    .leave = confirm_leave,
    .button = confirm_button,
    .draw = confirm_draw,
};

scene_t *scene_confirm(const char *question, const char *detail, const char *action,
                       scenes_answer_fn answer)
{
    static confirm_scene_t confirm = { .scene = { .ops = &CONFIRM_OPS } };
    confirm.question = question;
    confirm.detail = detail;
    confirm.action = action;
    confirm.answer = answer;
    confirm.action_is_selected = false;
    return &confirm.scene;
}
