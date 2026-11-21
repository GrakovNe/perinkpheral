/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "notice.h"
#include "words.h"

/* Something an application has to tell wherever the user has gone: the
 * time is up, a meeting begins. The host shows it, under its header, over
 * whatever had the screen, and keeps it there until somebody says they
 * saw it - the application is not loaded for any of that. */
typedef struct {
    scene_t          scene;
    const char      *first;
    const char      *second;
    const char      *detail;
    int              icon;
    scenes_action_fn seen;
} notify_scene_t;

static scene_reaction_t notify_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    const notify_scene_t *notification = (const notify_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press == SCENE_PRESS_SHORT && (button == SCENE_BUTTON_OK || button == SCENE_BUTTON_BACK)) {
        notification->seen();
    }
    return reaction;
}

static void notify_draw(scene_t *scene, gfx_t *surface)
{
    const notify_scene_t *notification = (const notify_scene_t *)scene;
    const notice_t notice = {
        .first = notification->first,
        .second = notification->second,
        .detail = notification->detail,
        .icon = notification->icon,
        .ok = word(WORD_DISMISS),
    };
    notice_draw(surface, &notice);
}

static const scene_ops_t NOTIFY_OPS = {
    .button = notify_button,
    .draw = notify_draw,
};

scene_t *scene_notify(const char *first, const char *second, const char *detail, int icon,
                      scenes_action_fn seen)
{
    static notify_scene_t notification = {
        .scene = { .ops = &NOTIFY_OPS, .asks_once = true },
    };
    notification.first = first;
    notification.second = second;
    notification.detail = detail;
    notification.icon = icon;
    notification.seen = seen;
    /* The next notice is the same scene: its own number keeps a press made for the one before
     * from dismissing it. */
    uint32_t question = notification.scene.question;
    notification.scene.question = question == UINT32_MAX ? 1 : question + 1;
    return &notification.scene;
}
