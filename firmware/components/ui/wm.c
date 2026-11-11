/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wm.h"

#include <assert.h>
#include <stddef.h>

/* A stronger reason is never replaced by a weaker one: a switch that is
 * still waiting for the panel stays a switch after one more key press. */
static void mark_changed(wm_t *wm, wm_change_t change)
{
    if (change > wm->change) {
        wm->change = change;
    }
}

static void give_screen_to(wm_t *wm, scene_t *next)
{
    /* The scene that is there already stays - unless it was suspended:
     * then it has to enter again. */
    if (next == NULL || (next == wm->current && !wm->suspended)) {
        return;
    }
    /* A suspended scene has left already. */
    if (wm->current != NULL && !wm->suspended && wm->current->ops->leave != NULL) {
        wm->current->ops->leave(wm->current);
    }
    wm->suspended = false;
    wm->current = next;
    if (next->ops->enter != NULL) {
        next->ops->enter(next);
    }
    mark_changed(wm, WM_CHANGED_BY_SWITCH);
}

/* The user asked for the menu over a scene that was not done: BACK from
 * the menu returns to it. */
static void open_home_over_current(wm_t *wm)
{
    if (wm->current != wm->home) {
        wm->before_home = wm->current;
        give_screen_to(wm, wm->home);
    }
}

static void go_back(wm_t *wm)
{
    if (wm->current == wm->home) {
        give_screen_to(wm, wm->before_home);
    } else {
        /* The scene ended itself: there is nothing to return to. */
        wm->before_home = NULL;
        give_screen_to(wm, wm->home);
    }
}

void wm_init(wm_t *wm, scene_t *home, scene_t *first, wm_draw_header_fn draw_header,
             void *header_context)
{
    assert(home != NULL && first != NULL && draw_header != NULL);
    wm->home = home;
    wm->current = NULL;
    wm->before_home = NULL;
    wm->change = WM_CLEAN;
    wm->suspended = false;
    wm->draw_header = draw_header;
    wm->header_context = header_context;
    give_screen_to(wm, first);
}

/* cause: what a redraw asked for by this reaction is due to. */
static void react(wm_t *wm, scene_reaction_t reaction, wm_change_t cause)
{
    switch (reaction.kind) {
    case SCENE_IGNORED:
        break;
    case SCENE_REDRAW:
        mark_changed(wm, cause);
        break;
    case SCENE_REDRAW_SOON:
        mark_changed(wm, WM_CHANGED_URGENTLY);
        mark_changed(wm, cause);
        break;
    case SCENE_REDRAW_NO_BLINK:
        /* Under a finger too: a press must not be what makes the row blink. */
        mark_changed(wm, WM_CHANGED_IN_A_ROW);
        break;
    case SCENE_REDRAW_CLEAN:
        mark_changed(wm, WM_CHANGED_CLEANLY);
        break;
    case SCENE_OPEN:
        give_screen_to(wm, reaction.open);
        break;
    case SCENE_BACK:
        go_back(wm);
        break;
    }
}

void wm_button(wm_t *wm, scene_button_t button, scene_press_t press)
{
    if (wm->suspended) {
        return;
    }
    /* The one press no scene can take away from the user. */
    if (button == SCENE_BUTTON_BACK && press == SCENE_PRESS_LONG) {
        open_home_over_current(wm);
        return;
    }

    react(wm, wm->current->ops->button(wm->current, button, press), WM_CHANGED_BY_INPUT);
}

void wm_timer(wm_t *wm, int timer_id)
{
    if (!wm->suspended && wm->current->ops->timer != NULL) {
        react(wm, wm->current->ops->timer(wm->current, timer_id), WM_CHANGED_BY_DATA);
    }
}

void wm_scene_reacted(wm_t *wm, const scene_t *scene, scene_reaction_t reaction,
                      wm_change_t cause)
{
    if (!wm->suspended && scene == wm->current) {
        react(wm, reaction, cause);
    }
}

void wm_suspend(wm_t *wm)
{
    if (wm->suspended) {
        return;
    }
    if (wm->current->ops->leave != NULL) {
        wm->current->ops->leave(wm->current);
    }
    wm->suspended = true;
}

void wm_resume(wm_t *wm)
{
    if (!wm->suspended) {
        return;
    }
    wm->suspended = false;
    if (wm->current->ops->enter != NULL) {
        wm->current->ops->enter(wm->current);
    }
    /* The scene starts from what it stored: the frame may be another. */
    mark_changed(wm, WM_CHANGED_BY_DATA);
}

void wm_show(wm_t *wm, scene_t *scene)
{
    if (scene != wm->home && wm->current != scene) {
        wm->before_home = NULL;
    }
    give_screen_to(wm, scene);
}

void wm_invalidate(wm_t *wm, wm_change_t change)
{
    mark_changed(wm, change);
}

wm_change_t wm_pending_change(const wm_t *wm)
{
    return wm->change;
}

void wm_drop(wm_t *wm, const scene_t *scene)
{
    if (wm->before_home == scene) {
        wm->before_home = NULL;
    }
    if (wm->current == scene) {
        give_screen_to(wm, wm->home);
    }
}

void wm_header_share(int left_width, int right_width, int available, int *left_max,
                     int *right_max)
{
    int half = available / 2;
    if (left_width + right_width <= available) {
        *left_max = left_width;
        *right_max = right_width;
    } else if (right_width <= half) {
        *left_max = available - right_width;
        *right_max = right_width;
    } else if (left_width <= half) {
        *left_max = left_width;
        *right_max = available - left_width;
    } else {
        *left_max = half;
        *right_max = half;
    }
}

void wm_split(const gfx_t *screen, gfx_t *header, gfx_t *content)
{
    gfx_viewport(screen, header, 0, 0, screen->w, WM_HEADER_HEIGHT);
    gfx_viewport(screen, content, 0, WM_HEADER_HEIGHT, screen->w,
                 screen->h - WM_HEADER_HEIGHT - WM_BOTTOM_EDGE);
}

void wm_draw(wm_t *wm, gfx_t *screen)
{
    gfx_clear(screen, false);
    if (wm->current->fullscreen) {
        wm->current->ops->draw(wm->current, screen);
        return;
    }
    gfx_t header, content;
    wm_split(screen, &header, &content);
    wm->draw_header(&header, wm->current, wm->header_context);
    wm->current->ops->draw(wm->current, &content);
}

void wm_shown(wm_t *wm)
{
    wm->change = WM_CLEAN;
}
