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

static scene_t *top(const wm_t *wm)
{
    return wm->depth > 0 ? wm->stack[wm->depth - 1] : wm->home;
}

static int place_of(const wm_t *wm, const scene_t *scene)
{
    for (int i = 0; i < wm->depth; i++) {
        if (wm->stack[i] == scene) {
            return i;
        }
    }
    return -1;
}

static void remove_at(wm_t *wm, int place)
{
    for (int i = place; i + 1 < wm->depth; i++) {
        wm->stack[i] = wm->stack[i + 1];
    }
    wm->depth--;
}

/* The home is under everything already: at the bottom of the stack it
 * would only be itself twice. */
static void settle(wm_t *wm)
{
    while (wm->depth > 0 && wm->stack[0] == wm->home) {
        remove_at(wm, 0);
    }
}

/* Onto the top. A scene that is in the stack already is moved there from
 * where it waited: what was over it stays, and is what BACK returns to. */
static void push(wm_t *wm, scene_t *scene)
{
    int place = place_of(wm, scene);
    if (place >= 0) {
        remove_at(wm, place);
    } else if (wm->depth == WM_STACK_DEPTH) {
        remove_at(wm, 0); /* the oldest is the one to forget */
    }
    wm->stack[wm->depth++] = scene;
    settle(wm);
}

static void show_top(wm_t *wm)
{
    give_screen_to(wm, top(wm));
}

static void open_over_current(wm_t *wm, scene_t *scene)
{
    if (scene == NULL) {
        return;
    }
    if (scene == wm->home) {
        /* The menu is the beginning: whoever goes there is done with
         * everything that was open, and BACK there leads nowhere. */
        wm->depth = 0;
    } else {
        push(wm, scene);
    }
    show_top(wm);
}

/* Opened from the menu: a start anew, whatever the menu was opened over. */
static void open_from(wm_t *wm, const scene_t *opener, scene_t *scene)
{
    if (opener == wm->home) {
        wm->depth = 0;
    }
    open_over_current(wm, scene);
}

static void replace(wm_t *wm, const scene_t *done, scene_t *scene)
{
    int place = place_of(wm, done);
    if (place >= 0) {
        remove_at(wm, place);
    }
    open_over_current(wm, scene);
}

static void go_back(wm_t *wm)
{
    if (wm->depth > 0) {
        wm->depth--;
        settle(wm);
    }
    show_top(wm);
}

void wm_init(wm_t *wm, scene_t *home, scene_t *first, wm_draw_header_fn draw_header,
             void *header_context)
{
    assert(home != NULL && first != NULL && draw_header != NULL);
    wm->home = home;
    wm->current = NULL;
    wm->depth = 0;
    wm->change = WM_CLEAN;
    wm->suspended = false;
    wm->draw_header = draw_header;
    wm->header_context = header_context;
    open_over_current(wm, first);
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
        open_from(wm, wm->current, reaction.open);
        break;
    case SCENE_REPLACE:
        replace(wm, wm->current, reaction.open);
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
        open_over_current(wm, wm->home);
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

void wm_open(wm_t *wm, scene_t *scene)
{
    open_over_current(wm, scene);
}

void wm_open_anew(wm_t *wm, scene_t *scene)
{
    open_from(wm, wm->home, scene);
}

void wm_back(wm_t *wm)
{
    go_back(wm);
}

bool wm_has(const wm_t *wm, const scene_t *scene)
{
    return place_of(wm, scene) >= 0;
}

void wm_invalidate(wm_t *wm, wm_change_t change)
{
    mark_changed(wm, change);
}

wm_change_t wm_pending_change(const wm_t *wm)
{
    return wm->change;
}

void wm_close(wm_t *wm, const scene_t *scene)
{
    int place;
    while ((place = place_of(wm, scene)) >= 0) {
        remove_at(wm, place);
    }
    settle(wm);
    /* A scene that had the screen without being in the stack is closed the
     * same way: what the stack says is shown. */
    if (wm->current == scene || wm->current != top(wm)) {
        show_top(wm);
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
