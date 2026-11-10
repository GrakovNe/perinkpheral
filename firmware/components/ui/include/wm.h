/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The window manager: which scene has the screen, how the user moves
 * between scenes, and how a frame is put together from the host's header
 * and the scene's own picture. Pure, host-tested. */

#define WM_HEADER_HEIGHT 43

/* Why the frame has to be shown again; the display decides how. */
typedef enum {
    WM_CLEAN,
    WM_CHANGED_BY_DATA,   /* something arrived, the clock moved */
    WM_CHANGED_BY_INPUT,  /* the user pressed a button */
    WM_CHANGED_BY_SWITCH, /* another scene took the screen */
} wm_change_t;

typedef void (*wm_draw_header_fn)(gfx_t *surface, void *context);

typedef struct {
    scene_t *home;        /* the menu: a long press on BACK always leads here */
    scene_t *current;
    scene_t *before_home; /* the scene the menu was opened over, if it was */
    wm_change_t change;
    bool     suspended;   /* the current scene has left, see wm_suspend() */

    wm_draw_header_fn draw_header;
    void             *header_context;
} wm_t;

/* `first` takes the screen at once; it may be `home` itself. Nothing here
 * may be NULL but header_context. */
void wm_init(wm_t *wm, scene_t *home, scene_t *first, wm_draw_header_fn draw_header,
             void *header_context);

void wm_button(wm_t *wm, scene_button_t button, scene_press_t press);

/* A timer ran out; it goes to the scene on the screen, if that takes timers. */
void wm_timer(wm_t *wm, int timer_id);

/* The scene on the screen has to let go of what it holds for a moment -
 * there is room for one application in memory, and another one is about
 * to be loaded to take its data. The scene leaves, the frame stays as it
 * is; wm_resume() lets it enter again. Between the two nothing reaches
 * the scene. */
void wm_suspend(wm_t *wm);
void wm_resume(wm_t *wm);

/* Gives the screen to `scene` at once: an application asked for it and
 * the host agreed. Ends a suspension without bringing the old scene back. */
void wm_show(wm_t *wm, scene_t *scene);

/* Something outside the scene changed what the frame shows. */
void wm_invalidate(wm_t *wm, wm_change_t change);

/* WM_CLEAN: the last drawn frame still stands. */
wm_change_t wm_pending_change(const wm_t *wm);

/* Draws the whole frame. The change stays pending until wm_shown(). */
void wm_draw(wm_t *wm, gfx_t *screen);

/* The frame drawn last reached the panel. */
void wm_shown(wm_t *wm);

/* The two parts of a screen that has a header. */
void wm_split(const gfx_t *screen, gfx_t *header, gfx_t *content);

#ifdef __cplusplus
}
#endif
