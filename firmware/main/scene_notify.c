/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "notice.h"

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
    const notify_scene_t *notify = (const notify_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press == SCENE_PRESS_SHORT && (button == SCENE_BUTTON_OK || button == SCENE_BUTTON_BACK)) {
        notify->seen();
    }
    return reaction;
}

static void notify_draw(scene_t *scene, gfx_t *surface)
{
    const notify_scene_t *notify = (const notify_scene_t *)scene;
    const notice_t notice = {
        .first = notify->first,
        .second = notify->second,
        .detail = notify->detail,
        .icon = notify->icon,
        .ok = "Dismiss",
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
    static notify_scene_t notify = {
        .scene = { .ops = &NOTIFY_OPS, .asks_once = true },
    };
    notify.first = first;
    notify.second = second;
    notify.detail = detail;
    notify.icon = icon;
    notify.seen = seen;
    return &notify.scene;
}
