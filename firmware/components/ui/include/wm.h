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

/* Window manager: scene stack, navigation, frame composition (host header
 * plus the picture of the current scene). Pure, host-tested.
 *
 * Stack rules:
 *   - The home scene (the menu) is below the stack and is shown when the
 *     stack is empty.
 *   - An opened scene is pushed on top and becomes current. The scene
 *     below receives leave() and receives enter() again when it is on top.
 *   - BACK pops one scene.
 *   - A scene opened from the home scene replaces the whole stack.
 *   - A long press of BACK empties the stack. BACK in the home scene has
 *     no effect.
 *   - A scene occupies one stack entry: opened again, it is moved to the top.
 *   - Only the top scene is entered (memory holds one application). A
 *     scene below must restore its state on enter(). */

#define WM_STACK_DEPTH 6

#define WM_HEADER_HEIGHT 39
/* Bottom row of the screen: unused, white. The area under the header is
 * 400 x 260. */
#define WM_BOTTOM_EDGE   1

/* Cause of a frame change. The display service selects the refresh by it. */
typedef enum {
    WM_CLEAN,
    WM_CHANGED_BY_DATA,   /* data, timer or clock */
    WM_CHANGED_URGENTLY,  /* SCENE_REDRAW_SOON */
    WM_CHANGED_IN_A_ROW,  /* SCENE_REDRAW_NO_BLINK */
    WM_CHANGED_BY_INPUT,  /* button press */
    WM_CHANGED_CLEANLY,   /* SCENE_REDRAW_CLEAN */
    WM_CHANGED_BY_SWITCH, /* current scene changed */
} wm_change_t;

/* Draws the header. `scene`: the scene under it. */
typedef void (*wm_draw_header_fn)(gfx_t *surface, const scene_t *scene, void *context);

typedef struct {
    scene_t *home;    /* the menu; target of a long press of BACK */
    scene_t *current; /* top of the stack, or `home` when the stack is empty */
    scene_t *stack[WM_STACK_DEPTH]; /* oldest first; never contains `home` */
    int      depth;
    wm_change_t change;
    bool     suspended;   /* see wm_suspend() */

    wm_draw_header_fn draw_header;
    void             *header_context;
} wm_t;

/* `first` becomes current; it may be `home`. Only `header_context` may be NULL. */
void wm_init(wm_t *wm, scene_t *home, scene_t *first, wm_draw_header_fn draw_header,
             void *header_context);

void wm_button(wm_t *wm, scene_button_t button, scene_press_t press);

/* Delivers an expired timer to the current scene. */
void wm_timer(wm_t *wm, int timer_id);

/* wm_suspend(): the current scene receives leave(); the frame is kept.
 * Used before another application is loaded for a background delivery.
 * No events reach the scene until wm_resume(), which calls enter(). */
void wm_suspend(wm_t *wm);
void wm_resume(wm_t *wm);

/* Pushes `scene` on top of the stack. Ends a suspension: the suspended
 * scene stays in the stack below `scene`. */
void wm_open(wm_t *wm, scene_t *scene);

/* Replaces the stack with `scene`, as opening from the menu does. */
void wm_open_from_home(wm_t *wm, scene_t *scene);

/* Pops the top scene, as BACK does. */
void wm_back(wm_t *wm);

/* True when `scene` is in the stack at any depth. */
bool wm_has(const wm_t *wm, const scene_t *scene);

/* Applies a reaction to an event delivered outside the window manager
 * (data). Ignored unless `scene` is current. */
void wm_scene_reacted(wm_t *wm, const scene_t *scene, scene_reaction_t reaction,
                      wm_change_t cause);

/* Marks the frame as changed for a cause outside the scene. */
void wm_invalidate(wm_t *wm, wm_change_t change);

/* Pending change. WM_CLEAN: the last drawn frame is valid. */
wm_change_t wm_pending_change(const wm_t *wm);

/* Draws the whole frame. The change remains pending until wm_shown(). */
void wm_draw(wm_t *wm, gfx_t *screen);

/* Reports that the last drawn frame is on the panel. */
void wm_shown(wm_t *wm);

/* Width limits of the two header texts when their sum exceeds `available`.
 * Each text is guaranteed half; the unused part of a half is given to the
 * other text. */
void wm_header_share(int left_width, int right_width, int available, int *left_max,
                     int *right_max);

/* Removes `scene` from the stack at any depth. When it was current, the
 * scene below becomes current. */
void wm_close(wm_t *wm, const scene_t *scene);

/* Splits the screen into the header area and the content area. */
void wm_split(const gfx_t *screen, gfx_t *header, gfx_t *content);

#ifdef __cplusplus
}
#endif
